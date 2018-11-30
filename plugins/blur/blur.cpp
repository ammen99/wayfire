#include <view.hpp>
#include <output.hpp>
#include <view-transform.hpp>
#include <workspace-manager.hpp>
#include <signal-definitions.hpp>
#include <nonstd/make_unique.hpp>

#include "blur.hpp"

static std::string method;
static wayfire_box_blur box;
static wayfire_gaussian_blur gauss;
static wayfire_kawase_blur kawase;
static wayfire_bokeh_blur bokeh;
static struct blur_options options;

class wf_blur : public wf_view_transformer_t
{
    public:
        // the first two functions are for input transform, blur doesn't need it
        virtual wf_point local_to_transformed_point(wf_geometry view,
            wf_point point)
        {
            return point;
        }

        virtual wf_point transformed_to_local_point(wf_geometry view,
            wf_point point)
        {
            return point;
        }

        // again, nothing to transform when blurring
        virtual wlr_box get_bounding_box(wf_geometry view, wlr_box region)
        {
            return region;
        }

        uint32_t get_z_order() { return 1e9; }

        /* src_tex        the internal FBO texture,
         *
         * src_box        box of the view that has to be repainted, contains other transforms
         *
         * scissor_box    the subbox of the FB which the transform renderer must update,
         *                drawing outside of it will cause artifacts
         *
         * target_fb      the framebuffer the transform should render to.
         *                it can be part of the screen, so it's geometry is
         *                given in output-local coordinates */

        /* The above is copied from the docs
         * Now, the purpose of this function is to render src_tex
         *
         * The view has geometry src_box
         * It is in output-local coordinates
         * The target_fb.geometry is also in output-local coords
         *
         * You need to repaint ONLY inside scissor_box, just use it like view-3d.cpp#205
         *
         * target_fb contains some very useful values. First of all, you'd need
         * target_fb.tex (that is the texture of the screen as it has been rendered up to now)
         * You can copy this texture, or feed it to your shader, etc.
         * When you want to DRAW on it, use target_fb.bind(), that should be your last step
         *
         * as we both have 1920x1080 monitors without any transform or scaling (or I'm wrong?)
         * I suggest hardcoding it just to see if it works, and then we'll figure out what else
         * is there to be done. */

        virtual void pre_render(uint32_t src_tex,
                                        wlr_box src_box,
                                        pixman_region32_t *damage,
                                        const wf_framebuffer& target_fb)
        {
            if (!method.compare("box"))
                box.pre_render(src_tex, src_box, damage, target_fb);
            else if (!method.compare("gaussian"))
                gauss.pre_render(src_tex, src_box, damage, target_fb);
            else if (!method.compare("kawase"))
                kawase.pre_render(src_tex, src_box, damage, target_fb);
            else if (!method.compare("bokeh"))
                bokeh.pre_render(src_tex, src_box, damage, target_fb);
        }

        virtual void render_with_damage(uint32_t src_tex,
                                        wlr_box src_box,
                                        wlr_box scissor_box,
                                        const wf_framebuffer& target_fb)
        {
            if (!method.compare("box"))
                box.render(src_tex, src_box, scissor_box, target_fb);
            else if (!method.compare("gaussian"))
                gauss.render(src_tex, src_box, scissor_box, target_fb);
            else if (!method.compare("kawase"))
                kawase.render(src_tex, src_box, scissor_box, target_fb);
            else if (!method.compare("bokeh"))
                bokeh.render(src_tex, src_box, scissor_box, target_fb);
        }

        virtual ~wf_blur() {}
};

class wayfire_blur : public wayfire_plugin_t
{
    button_callback btn;

    signal_callback_t workspace_stream_pre, workspace_stream_post;
    wf_option_callback blur_option_changed, blur_method_changed;
    const std::string transformer_name = "blur";
    wayfire_config_section *section;
    pixman_region32_t padded_region;
    std::string last_method;
    wf_option method_opt;

    wf_framebuffer_base saved_pixels;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "blur";
        grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;

        section = config->get_section("blur");
        method_opt = section->get_option("method", "kawase");
        method = last_method = method_opt->as_string();

        blur_option_changed = [=] ()
        {
            output->workspace->for_each_view([=] (wayfire_view view) {
                if (view->get_transformer(transformer_name))
                {
                     view->damage();
                }
            }, WF_ALL_LAYERS);
        };

        blur_method_changed = [=] ()
        {
	    method = method_opt->as_string();

            if (!last_method.compare("box"))
                box.fini();
	    else if (!last_method.compare("gaussian"))
                gauss.fini();
	    else if (!last_method.compare("kawase"))
                kawase.fini();
	    else if (!last_method.compare("bokeh"))
                bokeh.fini();

            if (!method.compare("box"))
                box.init(section, &blur_option_changed, &options);
	    else if (!method.compare("gaussian"))
                gauss.init(section, &blur_option_changed, &options);
	    else if (!method.compare("kawase"))
                kawase.init(section, &blur_option_changed, &options);
	    else if (!method.compare("bokeh"))
                bokeh.init(section, &blur_option_changed, &options);

            output->workspace->for_each_view([=] (wayfire_view view) {
                if (view->get_transformer(transformer_name))
                {
                     view->damage();
                }
            }, WF_ALL_LAYERS);

	    last_method = method;
        };
        method_opt->add_updated_handler(&blur_method_changed);

        btn = [=] (uint32_t, int, int)
        {

            auto focus = core->get_cursor_focus();

            if (!focus)
                return;

            auto view = core->find_view(focus->get_main_surface());
            view->add_transformer(nonstd::make_unique<wf_blur> (),
                transformer_name);
        };
        output->add_button(new_static_option("<super> <alt> BTN_LEFT"), &btn);

        /* workspace_stream_pre is called before rendering each frame
         * when rendering a workspace. It gives us a chance to pad
         * damage and take a snapshot of the padded area. The padded
         * damage will be used to render the scene as normal. Then
         * workspace_stream_post is called so we can copy the padded
         * pixels back. */
        pixman_region32_init(&padded_region);
        workspace_stream_pre = [=] (signal_data *data)
        {
            auto damage = static_cast<wf_stream_signal*>(data)->raw_damage;
            const auto& target_fb = static_cast<wf_stream_signal*>(data)->fb;
            pixman_region32_t expanded_damage;
            wlr_box fb_geom;
            int i, n_rect;

            pixman_region32_init(&expanded_damage);

            fb_geom = target_fb.framebuffer_box_from_geometry_box(target_fb.geometry);

            /* As long as the padding is big enough to cover the
             * furthest sampled pixel by the shader, there should
             * be no visual artifacts. */
            int padding = options.iterations *
                          options.offset *
                          options.degrade *
                          ((method.compare("kawase") &&
                          method.compare("bokeh")) ? 4.0 : 1.0);

            wayfire_surface_t::set_opaque_shrink_constraint("blur",
                padding);

            auto rects = pixman_region32_rectangles(damage, &n_rect);
            /* Pad the raw damage and store result in expanded_damage. */
            for (i = 0; i < n_rect; i++)
            {
                pixman_region32_union_rect(&expanded_damage, &expanded_damage,
                    rects[i].x1 - padding, rects[i].y1 - padding,
                    (rects[i].x2 - rects[i].x1) + 2 * padding,
                    (rects[i].y2 - rects[i].y1) + 2 * padding);
            }
            auto fb_g = target_fb.damage_box_from_geometry_box(target_fb.geometry);

            /* Keep rects on screen */
            pixman_region32_intersect_rect(&expanded_damage, &expanded_damage,
                fb_g.x, fb_g.y,
                fb_g.width, fb_g.height);

            /* Compute padded region and store result in padded_region. */
            pixman_region32_subtract(&padded_region, &expanded_damage, damage);

            OpenGL::render_begin(target_fb);

            /* Initialize a place to store padded region pixels. */
            saved_pixels.allocate(fb_geom.width, fb_geom.height);
            saved_pixels.bind();

            /* Setup framebuffer I/O. target_fb contains the pixels
             * from last frame at this point. fbo is a scratch buffer. */
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fb.fb));

            /* Copy pixels in padded_region from target_fb to saved_pixels. */
            rects = pixman_region32_rectangles(&padded_region, &n_rect);
            for (i = 0; i < n_rect; i++)
            {
                pixman_box32_t box;
                pixman_box_from_damage_box(target_fb, box,
                                           rects[i].x1,
                                           rects[i].y1,
                                           rects[i].x2,
                                           rects[i].y2);

                GL_CALL(glBlitFramebuffer(
                        box.x1, fb_geom.height - box.y2,
                        box.x2, fb_geom.height - box.y1,
                        box.x1, box.y1, box.x2, box.y2,
                        GL_COLOR_BUFFER_BIT, GL_LINEAR));
            }

            /* This effectively makes damage the same as expanded_damage. */
            pixman_region32_union(damage, damage, &expanded_damage);

            /* Reset stuff */
            pixman_region32_fini(&expanded_damage);
            OpenGL::render_end();
        };

        output->render->connect_signal("workspace-stream-pre", &workspace_stream_pre);

        /* workspace_stream_post is called after rendering each frame
         * when rendering a workspace. It gives us a chance to copy
         * the pixels back to the framebuffer that we saved in
         * workspace_stream_pre. */
        workspace_stream_post = [=] (signal_data *data)
        {
            const auto& target_fb = static_cast<wf_stream_signal*>(data)->fb;
            wlr_box fb_geom;
            int i, n_rect;

            fb_geom = target_fb.framebuffer_box_from_geometry_box(target_fb.geometry);

            OpenGL::render_begin(target_fb);
            /* Setup framebuffer I/O. target_fb contains the frame
             * rendered with expanded damage and artifacts on the edges.
             * fbo has the the padded region of pixels to overwrite the
             * artifacts that blurring has left behind. */
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, saved_pixels.fb));
            GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_fb.fb));

            /* Copy pixels back in padded_region from saved_pixels to target_fb. */
            auto rects = pixman_region32_rectangles(&padded_region, &n_rect);
            for (i = 0; i < n_rect; i++)
            {
                pixman_box32_t box;
                pixman_box_from_damage_box(target_fb, box,
                                           rects[i].x1,
                                           rects[i].y1,
                                           rects[i].x2,
                                           rects[i].y2);

                GL_CALL(glBlitFramebuffer(box.x1, box.y1, box.x2, box.y2,
                        box.x1, fb_geom.height - box.y2,
                        box.x2, fb_geom.height - box.y1,
                        GL_COLOR_BUFFER_BIT, GL_LINEAR));
            }

            /* Reset stuff */
            pixman_region32_clear(&padded_region);
            OpenGL::render_end();
        };

        output->render->connect_signal("workspace-stream-post", &workspace_stream_post);

        if (!method.compare("box"))
            box.init(section, &blur_option_changed, &options);
	else if (!method.compare("gaussian"))
            gauss.init(section, &blur_option_changed, &options);
	else if (!method.compare("kawase"))
            kawase.init(section, &blur_option_changed, &options);
	else if (!method.compare("bokeh"))
            bokeh.init(section, &blur_option_changed, &options);
    }

    void pixman_box_from_damage_box(const wf_framebuffer& target_fb,
                                    pixman_box32_t &pbox,
                                    uint32_t x1,
                                    uint32_t y1,
                                    uint32_t x2,
                                    uint32_t y2)
    {
        wlr_box box;

        box.x = x1;
        box.y = y1;
        box.width = x2 - x1;
        box.height = y2 - y1;

        box = target_fb.framebuffer_box_from_damage_box(box);

        pbox.x1 = box.x;
        pbox.y1 = box.y;
        pbox.x2 = box.x + box.width;
        pbox.y2 = box.y + box.height;
    }

    void fini()
    {
        if (!last_method.compare("box"))
            box.fini();
        else if (!last_method.compare("gaussian"))
            gauss.fini();
        else if (!last_method.compare("kawase"))
            kawase.fini();
        else if (!last_method.compare("bokeh"))
            bokeh.fini();

        OpenGL::render_begin();
        saved_pixels.release();
        OpenGL::render_end();

        pixman_region32_fini(&padded_region);
    }
};

extern "C"
{
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_blur();
    }
}
