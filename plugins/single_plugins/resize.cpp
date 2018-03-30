#include <output.hpp>
#include <view.hpp>
#include <core.hpp>
#include <workspace-manager.hpp>
#include <linux/input.h>
#include <signal-definitions.hpp>
#include "../../shared/config.hpp"


class wayfire_resize : public wayfire_plugin_t {
    signal_callback_t resize_request, view_destroyed;

    button_callback activate_binding;
    touch_callback touch_activate_binding;
    wayfire_view view;

    int initial_x, initial_y;
    wf_geometry initial_geometry;

    uint32_t edges;
    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "resize";
        grab_interface->abilities_mask = WF_ABILITY_CHANGE_VIEW_GEOMETRY;

        auto button = config->get_section("resize")->get_button("activate",
                {MODIFIER_SUPER, BTN_LEFT});
        if (button.button == 0)
            return;

        activate_binding = [=] (uint32_t, int x, int y)
        {
            auto view = output->get_view_at_point(x, y);
            if (view) initiate(view, x, y);
        };

        /*
        touch_activate_binding = [=] (weston_touch* touch,
                wl_fixed_t sx, wl_fixed_t sy)
        {
            initiate(core->find_view(touch->focus), sx, sy);
        };
        */


        output->add_button(button.mod, button.button, &activate_binding);
        output->add_touch(button.mod, &touch_activate_binding);

        grab_interface->callbacks.pointer.button = [=] (uint32_t b, uint32_t s)
        {
            if (b != button.button)
                return;

            input_pressed(s);
        };

        grab_interface->callbacks.pointer.motion = [=] (int x, int y)
        {
            input_motion(x, y);
        };

        /*
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
        }; */

        using namespace std::placeholders;
        resize_request = std::bind(std::mem_fn(&wayfire_resize::resize_requested),
                this, _1);
        output->connect_signal("resize-request", &resize_request);

        view_destroyed = [=] (signal_data* data)
        {
            if (get_signaled_view(data) == view)
            {
                view = nullptr;
                input_pressed(WL_POINTER_BUTTON_STATE_RELEASED);

            }
        };

        output->connect_signal("detach-view", &view_destroyed);
        output->connect_signal("destroy-view", &view_destroyed);
    }

    void resize_requested(signal_data *data)
    {
        auto view = get_signaled_view(data);
        if (view)
        {
            GetTuple(x, y, core->get_cursor_position());
            initiate(view, x, y);
        }
    }

    void initiate(wayfire_view view, int sx, int sy, uint32_t forced_edges = 0)
    {
        if (!view || view->is_special || view->destroyed)
            return;

        if (!output->workspace->
                get_implementation(output->workspace->get_current_workspace())->
                    view_resizable(view))
            return;

        if (!output->activate_plugin(grab_interface))
            return;
        if (!grab_interface->grab()) {
            output->deactivate_plugin(grab_interface);
            return;
        }

        initial_x = sx;
        initial_y = sy;
        initial_geometry = view->geometry;

        if (forced_edges == 0) {
            int view_x = initial_x - view->geometry.x;
            int view_y = initial_y - view->geometry.y;

            edges = 0;
            if (view_x < view->geometry.width / 2) {
                edges |= WL_SHELL_SURFACE_RESIZE_LEFT;
            } else {
                edges |= WL_SHELL_SURFACE_RESIZE_RIGHT;
            }

            if (view_y < view->geometry.height / 2) {
                edges |= WL_SHELL_SURFACE_RESIZE_TOP;
            } else {
                edges |= WL_SHELL_SURFACE_RESIZE_BOTTOM;
            }
        } else {
            edges = forced_edges;
        }

        view->set_moving(true);
        view->set_resizing(true);

        if (view->maximized)
            view->set_maximized(false);
        if (view->fullscreen)
            view->set_fullscreen(false);

        if (edges == 0) /* simply deactivate */
            input_pressed(WL_POINTER_BUTTON_STATE_RELEASED);
        this->view = view;
    }

    void input_pressed(uint32_t state)
    {
        if (state != WL_POINTER_BUTTON_STATE_RELEASED)
            return;

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);

        if (view)
        {
            view->set_moving(false);
            view->set_resizing(false);
        }
    }

    void input_motion(int sx, int sy)
    {
        auto newg = initial_geometry;

        int dx = sx - initial_x;
        int dy = sy - initial_y;

        if (edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
            newg.x += dx;
            newg.width -= dx;
        } else {
            newg.width += dx;
        }

        if (edges & WL_SHELL_SURFACE_RESIZE_TOP) {
            newg.y += dy;
            newg.height -= dy;
        } else {
            newg.height += dy;
        }

        /* TODO: add view::get_max/min size
        auto max_size = weston_desktop_surface_get_max_size(view->desktop_surface);
        auto min_size = weston_desktop_surface_get_min_size(view->desktop_surface);

        min_size.width = std::max(min_size.width, 10);
        min_size.height = std::max(min_size.height, 10);

        if (max_size.width > 0)
            newg.width = std::min(max_size.width, newg.width);
        newg.width = std::max(min_size.width, newg.width);

        if (max_size.height > 0)
            newg.height = std::min(max_size.height, newg.height);
        newg.height = std::max(min_size.height, newg.height);
        */

        newg.height = std::max(newg.height, 10);
        newg.width  = std::max(newg.width,  10);
        view->set_geometry(newg);
    }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_resize();
    }
}
