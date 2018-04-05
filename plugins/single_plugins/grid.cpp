#include <output.hpp>
#include <core.hpp>
#include <view.hpp>
#include <workspace-manager.hpp>
#include <render-manager.hpp>
#include <algorithm>
#include <linux/input-event-codes.h>
#include "signal-definitions.hpp"
#include "../../shared/config.hpp"

#include "snap_signal.hpp"

/* TODO: add support for more than one window animation at a time */

struct sending_signal
{
    std::string name;
    view_maximized_signal *data;
};
void idle_send_signal(void *data)
{
    auto conv = (sending_signal*) data;
    conv->data->view->get_output()->emit_signal(conv->name, conv->data);
    delete conv->data;
    delete conv;
}

class wayfire_grid : public wayfire_plugin_t {

    std::unordered_map<wayfire_view, wf_geometry> saved_view_geometry;
    signal_callback_t output_resized_cb, view_destroyed_cb;

    std::vector<std::string> slots = {"unused", "bl", "b", "br", "l", "c", "r", "tl", "t", "tr"};
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

    signal_callback_t snap_cb, maximized_cb, fullscreen_cb;

    struct {
        wf_geometry original, target;
        wayfire_view view;
        bool maximizing = false, fullscreening = false;
    } current_view;

    int total_steps, current_step;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "grid";
        grab_interface->abilities_mask = WF_ABILITY_CHANGE_VIEW_GEOMETRY;

        auto section = config->get_section("grid");
        total_steps = section->get_duration("duration", 15);

        for (int i = 1; i < 10; i++) {
            keys[i] = section->get_key("slot_" + slots[i], default_keys[i]);

            bindings[i] = [=] (uint32_t key) {
                auto view = output->get_top_view();
                if (view && current_view.view == nullptr)
                    handle_key(view, i);
            };

            output->add_key(keys[i].mod, keys[i].keyval, &bindings[i]);
        }

        hook = std::bind(std::mem_fn(&wayfire_grid::update_pos_size), this);

        using namespace std::placeholders;
        snap_cb = std::bind(std::mem_fn(&wayfire_grid::snap_signal_cb), this, _1);
        output->connect_signal("view-snap", &snap_cb);

        maximized_cb = std::bind(std::mem_fn(&wayfire_grid::maximize_signal_cb), this, _1);
        output->connect_signal("view-maximized-request", &maximized_cb);

        fullscreen_cb = std::bind(std::mem_fn(&wayfire_grid::fullscreen_signal_cb), this, _1);
        output->connect_signal("view-fullscreen-request", &fullscreen_cb);

        output_resized_cb = [=] (signal_data*) {
            saved_view_geometry.clear();
        };
        output->connect_signal("output-resized", &output_resized_cb);

        view_destroyed_cb = [=] (signal_data *data)
        {
            if (get_signaled_view(data) == current_view.view)
                stop_animation();
        };

        output->connect_signal("destroy-view", &view_destroyed_cb);
        output->connect_signal("detach-view", &view_destroyed_cb);
    }

    void handle_key(wayfire_view view, int key)
    {
        int tx = 0, ty = 0, tw = 0, th = 0; // target dimensions
        if (slots[key] == "c") {
            toggle_maximized(view, tx, ty, tw, th);
        } else {
            get_slot_dimensions(key, tx, ty, tw, th);
        }

        start_animation(view, tx, ty, tw, th);
        if (slots[key] == "c")
            current_view.maximizing = view->maximized;
    }

    bool start_animation(wayfire_view view, int tx, int ty, int tw, int th)
    {
        auto impl = output->workspace->
            get_implementation(output->workspace->get_current_workspace());

        if (!impl->view_movable(view) || !impl->view_resizable(view))
            return true;
        if (!output->activate_plugin(grab_interface) || !grab_interface->grab())
        {
            output->deactivate_plugin(grab_interface);
            return false;
        }
        output->focus_view(nullptr);

        current_step = 0;
        current_view.view = view;
        current_view.original = view->get_wm_geometry();
        current_view.target = {tx, ty, tw, th};
        current_view.maximizing = current_view.fullscreening = false;

        view->set_moving(true);
        view->set_resizing(true);
        view->set_geometry(view->get_wm_geometry());

        output->render->auto_redraw(true);
        output->render->add_output_effect(&hook);

        return true;
    }

    void update_pos_size()
    {
        int cx = GetProgress(current_view.original.x,
                current_view.target.x, current_step, total_steps);
        int cy = GetProgress(current_view.original.y,
                current_view.target.y, current_step, total_steps);
        int cw = GetProgress(current_view.original.width,
                current_view.target.width, current_step, total_steps);
        int ch = GetProgress(current_view.original.height,
                current_view.target.height, current_step, total_steps);
        current_view.view->set_geometry({cx, cy, cw, ch});

        current_step++;
        if (current_step == total_steps)
        {
            current_view.view->set_geometry(current_view.target);
            current_view.view->set_moving(false);
            current_view.view->set_resizing(false);

            stop_animation();
        }
    }

    void stop_animation()
    {
        output->render->auto_redraw(false);
        output->render->rem_effect(&hook);

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
        output->focus_view(current_view.view);

        check_send_signal(current_view.view, current_view.maximizing, current_view.fullscreening);
        current_view.view = nullptr;
    }

    void check_send_signal(wayfire_view view, bool maximizing, bool fullscreening)
    {
        if (!fullscreening && !maximizing)
            return;

        auto sig = new sending_signal;
        sig->data = new view_maximized_signal;

        sig->data->view = view;
        sig->data->state = true;

        if (fullscreening)
            sig->name = "view-fullscreen";
        else
            sig->name = "view-maximized";
        wl_event_loop_add_idle(core->ev_loop, idle_send_signal, sig);
    }

    void toggle_maximized(wayfire_view v, int &x, int &y, int &w, int &h,
            bool force_maximize = false, bool use_full_area = false)
    {
        auto it = saved_view_geometry.find(v);
        auto g = output->workspace->get_workarea();
        if (use_full_area)
            g = output->get_full_geometry();

        if (it == saved_view_geometry.end() || v->get_wm_geometry() != g ||
                force_maximize)
        {
            saved_view_geometry[v] = v->get_wm_geometry();
            x = g.x;
            y = g.y;
            w = g.width;
            h = g.height;

            if (!use_full_area)
                v->set_maximized(true);
        } else {
            x = it->second.x;
            y = it->second.y;
            w = it->second.width;
            h = it->second.height;

            saved_view_geometry.erase(it);

            if (!use_full_area)
                v->set_maximized(false);
        }
    }

    void get_slot_dimensions(int n, int &x, int &y, int &w, int &h)
    {
        auto g = output->workspace->get_workarea();

        int w2 = g.width / 2;
        int h2 = g.height / 2;

        if(n == 7)
            x = g.x, y = g.y, w = w2, h = h2;
        if(n == 8)
            x = g.x, y = g.y, w = g.width, h = h2;
        if(n == 9)
            x = g.x + w2, y = g.y, w = w2, h = h2;
        if(n == 4)
            x = g.x, y = g.y, w = w2, h = g.height;
        if(n == 6)
            x = g.x + w2, y = g.y, w = w2, h = g.height;
        if(n == 1)
            x = g.x, y = g.y + h2, w = w2, h = h2;
        if(n == 2)
            x = g.x, y = g.y + h2, w = g.width, h = h2;
        if(n == 3)
            x = g.x + w2, y = g.y + h2, w = w2, h = h2;
    }

    void snap_signal_cb(signal_data *ddata)
    {
        snap_signal *data = static_cast<snap_signal*>(ddata);
        assert(data);
        handle_key(data->view, data->tslot);
    }

    void maximize_signal_cb(signal_data *ddata)
    {
        auto data = static_cast<view_maximized_signal*> (ddata);
        assert(data);

        int x, y, w, h;
        toggle_maximized(data->view, x, y, w, h, data->state);

        if (current_view.view || !start_animation(data->view, x, y, w, h))
        {
            data->view->set_geometry({x, y, w, h});
            check_send_signal(data->view, data->state, false);
            return;
        }

        current_view.maximizing = data->state;
    }

    void fullscreen_signal_cb(signal_data *ddata)
    {
        auto data = static_cast<view_fullscreen_signal*> (ddata);
        assert(data);

        int x, y, w, h;
        toggle_maximized(data->view, x, y, w, h, data->state, true);

        if (current_view.view || data->view->fullscreen == data->state ||
            !start_animation(data->view, x, y, w, h))
        {
            data->view->set_geometry({x, y, w, h});
            check_send_signal(data->view, false, data->state);
            return;
        }

        current_view.fullscreening = data->state;
    }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_grid;
    }
}
