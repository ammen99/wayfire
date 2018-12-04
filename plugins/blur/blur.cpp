#include <view.hpp>
#include <output.hpp>
#include <view-transform.hpp>
#include <workspace-manager.hpp>
#include <signal-definitions.hpp>
#include <nonstd/make_unique.hpp>

#include "blur.hpp"

using blur_algorithm_provider = std::function<nonstd::observer_ptr<wf_blur_base>()>;
class wf_blur_transformer : public wf_view_transformer_t
{
    blur_algorithm_provider provider;
    public:

        wf_blur_transformer(blur_algorithm_provider blur_algorithm_provider)
        {
            provider = blur_algorithm_provider;
        }

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

        virtual wlr_box get_bounding_box(wf_geometry view, wlr_box region)
        {
            return region;
        }

        uint32_t get_z_order() { return 1e9; }

        virtual void render_with_damage(uint32_t src_tex, wlr_box src_box, const wf_region& damage,
            const wf_framebuffer& target_fb)
        {
            provider()->pre_render(src_tex, src_box, damage, target_fb);
            wf_view_transformer_t::render_with_damage(src_tex, src_box, damage, target_fb);
        }

        virtual void render_box(uint32_t src_tex, wlr_box src_box, wlr_box scissor_box,
            const wf_framebuffer& target_fb)
        {
            provider()->render(src_tex, src_box, scissor_box, target_fb);
        }
};

class wayfire_blur : public wayfire_plugin_t
{
    button_callback btn;

    effect_hook_t frame_pre_paint;
    signal_callback_t workspace_stream_pre, workspace_stream_post;

    wf_option method_opt;
    wf_option_callback blur_method_changed;
    std::unique_ptr<wf_blur_base> blur_algorithm;

    const std::string transformer_name = "blur";

    /* the pixels from padded_region */
    wf_framebuffer_base saved_pixels;
    wf_region padded_region;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "blur";
        grab_interface->abilities_mask = WF_ABILITY_NONE;

        auto section = config->get_section("blur");

        method_opt = section->get_option("method", "kawase");
        blur_method_changed = [=] () {
            blur_algorithm = create_blur_from_name(output, method_opt->as_string());
            blur_algorithm->damage_all_workspaces();
        };

        /* Create initial blur algorithm */
        blur_method_changed();
        method_opt->add_updated_handler(&blur_method_changed);

        btn = [=] (uint32_t, int, int)
        {

            auto focus = core->get_cursor_focus();

            if (!focus)
                return;

            auto view = core->find_view(focus->get_main_surface());
            view->add_transformer(nonstd::make_unique<wf_blur_transformer> (
                    [=] () {return nonstd::make_observer(blur_algorithm.get()); }),
                transformer_name);
        };
        output->add_button(new_static_option("<super> <alt> BTN_LEFT"), &btn);

        frame_pre_paint = [=] ()
        {
            int padding = blur_algorithm->calculate_blur_radius();
            wayfire_surface_t::set_opaque_shrink_constraint("blur",
                padding);

            auto damage = output->render->get_scheduled_damage();
            for (const auto& rect : damage)
            {
                output->render->damage(wlr_box{
                        rect.x1 - padding,
                        rect.y1 - padding,
                        (rect.x2 - rect.x1) + 2 * padding,
                        (rect.y2 - rect.y1) + 2 * padding
                });
            }
        };
        output->render->add_effect(&frame_pre_paint, WF_OUTPUT_EFFECT_PRE);

        /* workspace_stream_pre is called before rendering each frame
         * when rendering a workspace. It gives us a chance to pad
         * damage and take a snapshot of the padded area. The padded
         * damage will be used to render the scene as normal. Then
         * workspace_stream_post is called so we can copy the padded
         * pixels back. */
        workspace_stream_pre = [=] (signal_data *data)
        {
            auto& damage = static_cast<wf_stream_signal*>(data)->raw_damage;
            const auto& target_fb = static_cast<wf_stream_signal*>(data)->fb;

            /* As long as the padding is big enough to cover the
             * furthest sampled pixel by the shader, there should
             * be no visual artifacts. */
            int padding = blur_algorithm->calculate_blur_radius();

            wf_region expanded_damage;
            for (const auto& rect : damage)
            {
                expanded_damage |= {
                    rect.x1 - padding,
                    rect.y1 - padding,
                    (rect.x2 - rect.x1) + 2 * padding,
                    (rect.y2 - rect.y1) + 2 * padding
                };
            }

            /* Keep rects on screen */
            expanded_damage &= output->render->get_damage_box();

            /* Compute padded region and store result in padded_region. */
            padded_region = expanded_damage ^ damage;

            OpenGL::render_begin(target_fb);
            /* Initialize a place to store padded region pixels. */
            saved_pixels.allocate(target_fb.viewport_width,
                target_fb.viewport_height);

            /* Setup framebuffer I/O. target_fb contains the pixels
             * from last frame at this point. We are writing them
             * to saved_pixels, bound as GL_DRAW_FRAMEBUFFER */
            saved_pixels.bind();
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fb.fb));

            /* Copy pixels in padded_region from target_fb to saved_pixels. */
            for (const auto& rect : padded_region)
            {
                pixman_box32_t box = pixman_box_from_wlr_box(
                    target_fb.framebuffer_box_from_damage_box(
                        wlr_box_from_pixman_box(rect)));

                GL_CALL(glBlitFramebuffer(
                        box.x1, target_fb.viewport_height - box.y2,
                        box.x2, target_fb.viewport_height - box.y1,
                        box.x1, box.y1, box.x2, box.y2,
                        GL_COLOR_BUFFER_BIT, GL_LINEAR));
            }

            /* This effectively makes damage the same as expanded_damage. */
            damage |= expanded_damage;
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
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
            OpenGL::render_begin(target_fb);
            /* Setup framebuffer I/O. target_fb contains the frame
             * rendered with expanded damage and artifacts on the edges.
             * saved_pixels has the the padded region of pixels to overwrite the
             * artifacts that blurring has left behind. */
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, saved_pixels.fb));

            /* Copy pixels back from saved_pixels to target_fb. */
            for (const auto& rect : padded_region)
            {
                pixman_box32_t box = pixman_box_from_wlr_box(
                    target_fb.framebuffer_box_from_damage_box(
                        wlr_box_from_pixman_box(rect)));

                GL_CALL(glBlitFramebuffer(box.x1, box.y1, box.x2, box.y2,
                        box.x1, target_fb.viewport_height - box.y2,
                        box.x2, target_fb.viewport_height - box.y1,
                        GL_COLOR_BUFFER_BIT, GL_LINEAR));
            }

            /* Reset stuff */
            padded_region.clear();
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            OpenGL::render_end();
        };

        output->render->connect_signal("workspace-stream-post", &workspace_stream_post);
    }

    void fini()
    {
        output->workspace->for_each_view([=] (wayfire_view view) {
            if (view->get_transformer(transformer_name))
                view->pop_transformer(transformer_name);
        }, WF_ALL_LAYERS);

        output->rem_binding(&btn);
        method_opt->rem_updated_handler(&blur_method_changed);
        output->render->rem_effect(&frame_pre_paint, WF_OUTPUT_EFFECT_PRE);
        output->render->disconnect_signal("workspace-stream-pre", &workspace_stream_pre);
        output->render->disconnect_signal("workspace-stream-post", &workspace_stream_post);

        /* Call blur algorithm destructor */
        blur_algorithm = nullptr;

        OpenGL::render_begin();
        saved_pixels.release();
        OpenGL::render_end();
    }
};

extern "C"
{
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_blur();
    }
}
