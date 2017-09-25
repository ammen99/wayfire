#ifndef SYSTEM_FADE_HPP
#define SYSTEM_FADE_HPP

#include <core.hpp>
#include <output.hpp>
#include "animate.hpp"

void destroy_system_fade(void *data);

/* animates wake from suspend/startup by fading in the whole output */
class wf_system_fade
{
    weston_surface *surface = nullptr;
    weston_view *view = nullptr;

    int frame_count, current_step = 0;
    wayfire_output *output;
    effect_hook_t hook;

    public:
        wf_system_fade(wayfire_output *out, int fr_count) :
            frame_count(fr_count), output(out)
        {
            surface = weston_surface_create(core->ec);
            view = weston_view_create(surface);

            if (!surface || !view)
            {
                current_step = frame_count;
                return;
            }

            weston_surface_set_color(surface, 0, 0, 0, 1.0);

            auto og = output->get_full_geometry();

            weston_surface_set_size(surface, og.width, og.height);
            weston_view_set_position(view, og.x, og.y);

            weston_layer_entry_insert(&core->ec->fade_layer.view_list, &view->layer_link);

            hook = [=] () { step(); };
            output->render->add_output_effect(&hook);
            output->render->auto_redraw(true);
        }

        void step()
        {
            float color = GetProgress(1, 0, current_step, frame_count);
            weston_surface_set_color(surface, 0, 0, 0, color);
            weston_view_geometry_dirty(view);
            weston_view_schedule_repaint(view);

            if (current_step++ >= frame_count)
            {
                auto loop = wl_display_get_event_loop(core->ec->wl_display);
                wl_event_loop_add_idle(loop, destroy_system_fade, this);

                output->render->rem_effect(&hook);
            }
        }

        ~wf_system_fade()
        {
            weston_surface_destroy(surface);
            output->render->auto_redraw(false);
        }
};

void destroy_system_fade(void *data)
{
    wf_system_fade *fade = (wf_system_fade*) data;
    delete fade;
}

#endif /* end of include guard: SYSTEM_FADE_HPP */
