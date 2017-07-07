#include <wm.hpp>
#include <output.hpp>
#include <core.hpp>
#include <linux/input.h>
#include <libweston-desktop.h>
#include <signal_definitions.hpp>

class wayfire_resize : public wayfire_plugin_t {
    signal_callback_t resize_request;

    button_callback activate_binding;
    wayfire_view view;

    int initial_x, initial_y;
    wayfire_geometry initial_geometry;

    uint32_t edges;
    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "resize";
        grab_interface->compatAll = true;

        activate_binding = [=] (weston_pointer* ptr, uint32_t) {
            this->initiate(ptr);
        };

        using namespace std::placeholders;
        core->input->add_button(MODIFIER_SUPER, BTN_LEFT, &activate_binding, output);
        grab_interface->callbacks.pointer.button =
            std::bind(std::mem_fn(&wayfire_resize::button_pressed), this, _1, _2, _3);
        grab_interface->callbacks.pointer.motion =
            std::bind(std::mem_fn(&wayfire_resize::pointer_motion), this, _1, _2);

        resize_request = std::bind(std::mem_fn(&wayfire_resize::resize_requested), this, _1);
        output->signal->connect_signal("resize-request", &resize_request);
        /* TODO: read binding from config */
    }

    void resize_requested(signal_data *data)
    {
        auto converted = static_cast<resize_request_signal*> (data);
        if (converted)
            initiate(converted->ptr, converted->edges);
    }

    void initiate(weston_pointer *ptr, uint32_t forced_edges = 0)
    {
        if (!ptr->focus)
            return;

        view = core->find_view(weston_surface_get_main_surface(ptr->focus->surface));
        if (!view || view->is_special)
            return;

        if (!output->activate_plugin(grab_interface))
            return;
        if (!grab_interface->grab())
            return;

        initial_x = wl_fixed_to_int(ptr->x);
        initial_y = wl_fixed_to_int(ptr->y);
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
            button_pressed(ptr, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
    }

    void button_pressed(weston_pointer *ptr, uint32_t button, uint32_t state)
    {
        if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_RELEASED)
            return;

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
        weston_desktop_surface_set_resizing(view->desktop_surface, false);
    }

    void pointer_motion(weston_pointer *ptr, weston_pointer_motion_event *ev)
    {
        auto newg = initial_geometry;

        int current_x = wl_fixed_to_int(ptr->x);
        int current_y = wl_fixed_to_int(ptr->y);

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
