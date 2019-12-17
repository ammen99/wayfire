#include <plugin.hpp>
#include <output.hpp>
#include <opengl.hpp>
#include <debug.hpp>
#include <render-manager.hpp>
#include <wayfire/util/duration.hpp>

class wayfire_zoom_screen : public wf::plugin_interface_t
{

    wf::post_hook_t hook;
    wf::option_wrapper_t<wf::keybinding_t> modifier{"zoom/modifier"};
    wf::option_wrapper_t<double> speed{"zoom/speed"};
    wf::option_wrapper_t<int> smoothing_duration{"zoom/smoothing_duration"};
    wf::animation::duration_t animation{smoothing_duration};
    wf::animation::timed_transition_t current_zoom{animation};
    bool hook_set = false;

    public:
        void init() override
        {
            grab_interface->name = "zoom";
            grab_interface->capabilities = 0;
            output->add_axis(modifier, &axis);
        }

        void update_zoom_target(float delta)
        {
            float target = current_zoom.end;
            target -= target * delta * speed;
            target = clamp(target, 1.0f, 50.0f);

            if (target != current_zoom.end)
            {
                current_zoom.restart_with_end(target);
                animation.start();

                if (!hook_set)
                {
                    hook_set = true;
                    output->render->add_post(&hook);
                    output->render->set_redraw_always();
                }
            }
        }

        axis_callback axis = [=] (wlr_event_pointer_axis* ev)
        {
            if (!output->can_activate_plugin(grab_interface))
                return false;
            if (ev->orientation != WLR_AXIS_ORIENTATION_VERTICAL)
                return false;

            update_zoom_target(ev->delta);
            return true;
        };

        wf::post_hook_t render_hook = [=] (const wf_framebuffer_base& source,
            const wf_framebuffer_base& destination)
        {
            auto w = destination.viewport_width;
            auto h = destination.viewport_height;
            auto oc = output->get_cursor_position();
            double x, y;
            wlr_box b = output->get_relative_geometry();
            wlr_box_closest_point(&b, oc.x, oc.y, &x, &y);

            /* get rotation & scale */
            wlr_box box = {int(x), int(y), 1, 1};
            box = output->render->get_target_framebuffer().
                framebuffer_box_from_geometry_box(box);

            x = box.x;
            y = h - box.y;

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

            if (!animation.running() && current_zoom - 1 <= 0.01)
                unset_hook();
        };

        void unset_hook()
        {
            output->render->set_redraw_always(false);
            output->render->rem_post(&hook);
            hook_set = false;
        }

        void fini() override
        {
            if (hook_set)
                output->render->rem_post(&hook);

            output->rem_binding(&axis);
        }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_zoom_screen);
