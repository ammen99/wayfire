#include <plugin.hpp>
#include <output.hpp>
#include <opengl.hpp>
#include <debug.hpp>
#include <render-manager.hpp>
#include <animation.hpp>

class wayfire_zoom_screen : public wf::plugin_interface_t
{

    wf::post_hook_t hook;
    axis_callback axis;

    wf_option speed, modifier, smoothing_duration;

    float target_zoom = 1.0;
    bool hook_set = false;
    wf_duration duration;

    public:
        void init(wayfire_config *config)
        {
            hook = [=] (const wf_framebuffer_base& source, const wf_framebuffer_base& dest) {
                render(source, dest);
            };

            axis = [=] (wlr_event_pointer_axis* ev)
            {
                if (ev->orientation == WLR_AXIS_ORIENTATION_VERTICAL)
                    update_zoom_target(ev->delta);
            };

            auto section = config->get_section("zoom");
            modifier = section->get_option("modifier", "<super>");
            output->add_axis(modifier, &axis);

            speed    = section->get_option("speed", "0.005");
            smoothing_duration = section->get_option("smoothing_duration", "300");

            duration = wf_duration(smoothing_duration);
            duration.start(1, 1); // so that the first value we get is correct
        }

        void update_zoom_target(float delta)
        {
            const float last_target = target_zoom;

            target_zoom -= target_zoom * delta * speed->as_cached_double();
            target_zoom = std::max(target_zoom, 1.0f);
            target_zoom = std::min(target_zoom, 50.0f);

            if (last_target != target_zoom)
            {
                auto current = duration.progress();
                duration.start(current, target_zoom);

                if (!hook_set)
                {
                    hook_set = true;
                    output->render->add_post(&hook);
                    output->render->set_redraw_always();
                }
            }
        }

        void render(const wf_framebuffer_base& source,
            const wf_framebuffer_base& destination)
        {
            auto w = destination.viewport_width;
            auto h = destination.viewport_height;
            GetTuple(_x, _y, output->get_cursor_position());
            double x, y;
            wlr_box b = output->get_relative_geometry();
            wlr_box_closest_point(&b, _x, _y, &x, &y);

            /* get rotation & scale */
            wlr_box box = {int(x), int(y), 1, 1};
            box = output->render->get_target_framebuffer().
                framebuffer_box_from_geometry_box(box);

            x = box.x;
            y = h - box.y;

            const float current_zoom = duration.progress();
            const float scale = (current_zoom - 1) / current_zoom;

            const float tw = w / current_zoom, th = h / current_zoom;
            const float x1 = x * scale;
            const float y1 = y * scale;

            OpenGL::render_begin(source);
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, source.fb));
            GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.fb));
            GL_CALL(glBlitFramebuffer(x1, y1, x1 + tw, y1 + th, 0, 0, w, h,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR));
            OpenGL::render_end();

            if (!duration.running() && current_zoom - 1 <= 0.01)
            {
                output->render->set_redraw_always(false);
                output->render->rem_post(&hook);
                hook_set = false;
            }
        }

        void fini()
        {
            if (hook_set)
                output->render->rem_post(&hook);

            output->rem_binding(&axis);
        }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_zoom_screen);
