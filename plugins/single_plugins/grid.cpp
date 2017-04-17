#include <output.hpp>
#include <core.hpp>
#include <algorithm>
#include <linux/input-event-codes.h>

#include "snap_signal.hpp"

/* TODO: implement "snap" signal */

class wayfire_grid : public wayfire_plugin_t {

    std::unordered_map<wayfire_view, wayfire_geometry> saved_view_geometry;

    std::vector<string> slots = {"unused", "bl", "b", "br", "l", "c", "r", "tl", "t", "tr"};
    std::vector<wayfire_key> default_keys = {
        {0, 0},
        {MODIFIER_ALT | MODIFIER_CTRL, KEY_KP1},
        {MODIFIER_ALT | MODIFIER_CTRL, KEY_KP2},
        {MODIFIER_ALT | MODIFIER_CTRL, KEY_KP3},
        {MODIFIER_ALT | MODIFIER_CTRL, KEY_KP4},
        {MODIFIER_ALT | MODIFIER_CTRL, KEY_KP5},
        {MODIFIER_ALT | MODIFIER_CTRL, KEY_KP6},
        {MODIFIER_ALT | MODIFIER_CTRL, KEY_KP7},
        {MODIFIER_ALT | MODIFIER_CTRL, KEY_KP8},
        {MODIFIER_ALT | MODIFIER_CTRL, KEY_KP9},
    };
    key_callback bindings[10];
    wayfire_key keys[10];

    effect_hook_t hook;

    signal_callback_t snap_cb;

    struct {
        wayfire_geometry original, target;
        wayfire_view view;
    } current_view;

    int total_steps, current_step;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "grid";
        grab_interface->compatAll = false;
        grab_interface->compat.insert("move");

        auto section = config->get_section("grid");
        total_steps = section->get_int("duration", 100);

        for (int i = 1; i < 10; i++) {
            keys[i] = section->get_key("slot_" + slots[i], default_keys[i]);

            bindings[i] = [=] (weston_keyboard *kbd, uint32_t key) {
                auto view = output->get_top_view();
                if (view)
                    handle_key(view, i);
            };

            core->input->add_key(keys[i].mod, keys[i].keyval, &bindings[i], output);
        }

        hook = std::bind(std::mem_fn(&wayfire_grid::update_pos_size), this);

        using namespace std::placeholders;
        snap_cb = std::bind(std::mem_fn(&wayfire_grid::snap_signal_cb), this, _1);
        output->signal->connect_signal("view-snap", &snap_cb);
    }

    void handle_key(wayfire_view view, int key)
    {

        if (!output->activate_plugin(grab_interface))
            return;
        core->input->grab_input(grab_interface);

        int tx, ty, tw, th; // target dimensions
        if (slots[key] == "c") {
            toggle_maximized(view, tx, ty, tw, th);
        } else {
            get_slot_dimensions(key, tx, ty, tw, th);
        }

        current_step = 0;
        current_view.view = view;
        current_view.original = view->geometry;
        current_view.target = {{tx, ty}, {tw, th}};

        weston_desktop_surface_set_resizing(view->desktop_surface, true);
        output->render->auto_redraw(true);

        output->render->add_output_effect(&hook);
    }

#define GetProgress(start,end,curstep,steps) ((float(end)*(curstep)+float(start) \
                                            *((steps)-(curstep)))/(steps))

    void update_pos_size()
    {
        int cx = GetProgress(current_view.original.origin.x,
                current_view.target.origin.x, current_step, total_steps);
        int cy = GetProgress(current_view.original.origin.y,
                current_view.target.origin.y, current_step, total_steps);
        int cw = GetProgress(current_view.original.size.w,
                current_view.target.size.w, current_step, total_steps);
        int ch = GetProgress(current_view.original.size.h,
                current_view.target.size.h, current_step, total_steps);

        current_view.view->set_geometry(cx, cy, cw, ch);

        current_step++;
        if (current_step == total_steps) {
            current_view.view->set_geometry(current_view.target);

            weston_desktop_surface_set_resizing(current_view.view->desktop_surface, false);
            output->render->auto_redraw(false);

            output->render->rem_effect(&hook);
            core->input->ungrab_input(grab_interface);
            output->deactivate_plugin(grab_interface);
        }
    }

    void toggle_maximized(wayfire_view v, int &x, int &y, int &w, int &h)
    {
        auto it = saved_view_geometry.find(v);
        auto g = output->workspace->get_workarea();

        if (it == saved_view_geometry.end()) {
            saved_view_geometry[v] = v->geometry;
            x = g.origin.x;
            y = g.origin.y;
            w = g.size.w;
            h = g.size.h;

            weston_desktop_surface_set_maximized(v->desktop_surface, true);
        } else {
            x = it->second.origin.x;
            y = it->second.origin.y;
            w = it->second.size.w;
            h = it->second.size.h;

            saved_view_geometry.erase(it);
            weston_desktop_surface_set_maximized(v->desktop_surface, false);
        }
    }

    void get_slot_dimensions(int n, int &x, int &y, int &w, int &h)
    {

        auto g = output->workspace->get_workarea();

        int w2 = g.size.w / 2;
        int h2 = g.size.h / 2;

        if(n == 7)
            x = g.origin.x, y = g.origin.y, w = w2, h = h2;
        if(n == 8)
            x = g.origin.x, y = g.origin.y, w = g.size.w, h = h2;
        if(n == 9)
            x = g.origin.x + w2, y = g.origin.y, w = w2, h = h2;
        if(n == 4)
            x = g.origin.x, y = g.origin.y, w = w2, h = g.size.h;
        if(n == 6)
            x = g.origin.x + w2, y = g.origin.y, w = w2, h = g.size.h;
        if(n == 1)
            x = g.origin.x, y = g.origin.y + h2, w = w2, h = h2;
        if(n == 2)
            x = g.origin.x, y = g.origin.y + h2, w = g.size.w, h = h2;
        if(n == 3)
            x = g.origin.x + w2, y = g.origin.y + h2, w = w2, h = h2;
    }

    void snap_signal_cb(signal_data *ddata)
    {
        snap_signal *data = static_cast<snap_signal*>(ddata);
        assert(data);
        handle_key(data->view, data->tslot);
    }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_grid;
    }
}
