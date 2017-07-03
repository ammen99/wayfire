#include <wm.hpp>
#include <output.hpp>
#include <core.hpp>
#include <linux/input.h>
#include <libweston-desktop.h>
#include <signal_definitions.hpp>
#include "../../shared/config.hpp"


class wayfire_resize : public wayfire_plugin_t {
    signal_callback_t resize_request;

    button_callback activate_binding;
    touch_callback touch_activate_binding;
    wayfire_view view;

    int initial_x, initial_y;
    wayfire_geometry initial_geometry;

    uint32_t edges;
    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "resize";
        grab_interface->compatAll = true;

        auto button = config->get_section("resize")->get_button("initiate",
                {MODIFIER_SUPER, BTN_LEFT});
        if (button.button == 0)
            return;

        activate_binding = [=] (weston_pointer* ptr, uint32_t)
        {
            initiate(core->find_view(ptr->focus), ptr->x, ptr->y);
        };

        touch_activate_binding = [=] (weston_touch* touch,
                wl_fixed_t sx, wl_fixed_t sy)
        {
            initiate(core->find_view(touch->focus), sx, sy);
        };


        core->input->add_button(button.mod, button.button, &activate_binding, output);
        core->input->add_touch(button.mod, &touch_activate_binding, output);

        grab_interface->callbacks.pointer.button = [=] (weston_pointer*,
                uint32_t b, uint32_t s)
        {
            if (b != button.button)
                return;

            input_pressed(s);
        };
        grab_interface->callbacks.pointer.motion = [=] (weston_pointer *ptr,
                weston_pointer_motion_event*)
        {
            input_motion(ptr->x, ptr->y);
        };

        grab_interface->callbacks.touch.up = [=] (weston_touch *, int32_t id)
        {
            if (id == 0)
                input_pressed(WL_POINTER_BUTTON_STATE_RELEASED);
        };

        grab_interface->callbacks.touch.motion = [=] (weston_touch*, int32_t id,
                wl_fixed_t sx, wl_fixed_t sy)
        {
            if (id == 0)
                input_motion(sx, sy);
        };

        using namespace std::placeholders;
        resize_request = std::bind(std::mem_fn(&wayfire_resize::resize_requested),
                this, _1);
        output->signal->connect_signal("resize-request", &resize_request);
    }

    void resize_requested(signal_data *data)
    {
        auto converted = static_cast<resize_request_signal*> (data);
        if (converted) {
            auto seat = core->get_current_seat();

            auto ptr = weston_seat_get_pointer(seat);
            auto touch = weston_seat_get_touch(seat);

            if (ptr && ptr->grab_serial == converted->serial) {
                initiate(converted->view, ptr->grab_x, ptr->grab_y,
                        converted->edges);
            } else if (touch && touch->grab_serial == converted->serial) {
                initiate(converted->view, touch->grab_x, touch->grab_y,
                        converted->edges);
            }
        }
    }

    void initiate(wayfire_view view, wl_fixed_t sx, wl_fixed_t sy,
            uint32_t forced_edges = 0)
    {
        if (!view || view->is_special)
            return;
        if (!output->activate_plugin(grab_interface))
            return;
        if (!grab_interface->grab()) {
            output->deactivate_plugin(grab_interface);
            return;
        }

        initial_x = wl_fixed_to_int(sx);
        initial_y = wl_fixed_to_int(sy);
        initial_geometry = view->geometry;

        if (forced_edges == 0) {
            int view_x = initial_x - view->geometry.origin.x;
            int view_y = initial_y - view->geometry.origin.y;

            edges = 0;
            if (view_x < view->geometry.size.w / 2) {
                edges |= WL_SHELL_SURFACE_RESIZE_LEFT;
            } else {
                edges |= WL_SHELL_SURFACE_RESIZE_RIGHT;
            }

            if (view_y < view->geometry.size.h / 2) {
                edges |= WL_SHELL_SURFACE_RESIZE_TOP;
            } else {
                edges |= WL_SHELL_SURFACE_RESIZE_BOTTOM;
            }
        } else {
            edges = forced_edges;
        }

        weston_desktop_surface_set_resizing(view->desktop_surface, true);

        if (view->maximized)
            view->set_maximized(false);
        if (view->fullscreen)
            view->set_fullscreen(false);

        if (edges == 0) /* simply deactivate */
            input_pressed(WL_POINTER_BUTTON_STATE_RELEASED);
        this->view = view;
        view->output->render->auto_redraw(true);
    }

    void input_pressed(uint32_t state)
    {
        if (state != WL_POINTER_BUTTON_STATE_RELEASED)
            return;

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
        view->output->render->auto_redraw(false);
        weston_desktop_surface_set_resizing(view->desktop_surface, false);
    }

    void input_motion(wl_fixed_t sx, wl_fixed_t sy)
    {
        auto newg = initial_geometry;

        int current_x = wl_fixed_to_int(sx);
        int current_y = wl_fixed_to_int(sy);

        int dx = current_x - initial_x;
        int dy = current_y - initial_y;

        if (edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
            newg.origin.x += dx;
            newg.size.w -= dx;
        } else {
            newg.size.w += dx;
        }

        if (edges & WL_SHELL_SURFACE_RESIZE_TOP) {
            newg.origin.y += dy;
            newg.size.h -= dy;
        } else {
            newg.size.h += dy;
        }

        auto max_size = weston_desktop_surface_get_max_size(view->desktop_surface);
        auto min_size = weston_desktop_surface_get_min_size(view->desktop_surface);

        min_size.width = std::max(min_size.width, 10);
        min_size.height = std::max(min_size.height, 10);

        if (max_size.width > 0)
            newg.size.w = std::min(max_size.width, newg.size.w);
        newg.size.w = std::max(min_size.width, newg.size.w);

        if (max_size.height > 0)
            newg.size.h = std::min(max_size.height, newg.size.h);
        newg.size.h = std::max(min_size.height, newg.size.h);

        view->set_geometry(newg);
    }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_resize();
    }
}
