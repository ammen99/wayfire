#include <plugin.hpp>
#include <output.hpp>
#include <core.hpp>
#include <debug.hpp>
#include <view.hpp>
#include <view-transform.hpp>
#include <render-manager.hpp>
#include <workspace-manager.hpp>

#include <queue>
#include <linux/input.h>
#include <utility>
#include <animation.hpp>
#include <set>
#include "view-change-viewport-signal.hpp"
#include "../wobbly/wobbly-signal.hpp"

class vswitch_view_transformer : public wf_2D_view
{
    public:
        static const std::string name;
        vswitch_view_transformer(wayfire_view view) : wf_2D_view(view) {}
        virtual uint32_t get_z_order() override { return WF_TRANSFORMER_BLUR - 1; }
};
const std::string vswitch_view_transformer::name = "vswitch-transformer";

class vswitch : public wf::plugin_interface_t
{
    private:
        activator_callback callback_left, callback_right, callback_up, callback_down;
        activator_callback callback_win_left, callback_win_right, callback_win_up, callback_win_down;

        gesture_callback gesture_cb;

        wf_duration duration;
        wf_transition dx, dy;
        wayfire_view grabbed_view = nullptr;

        wf_option animation_duration;

    public:
    wayfire_view get_top_view()
    {
        auto ws = output->workspace->get_current_workspace();
        auto views = output->workspace->get_views_on_workspace(ws, wf::LAYER_WORKSPACE, true);

        return views.empty() ? nullptr : views[0];
    }

    void init(wayfire_config *config)
    {
        grab_interface->name = "vswitch";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP;
        grab_interface->callbacks.cancel = [=] () {stop_switch();};

        callback_left  = [=] (wf_activator_source, uint32_t) { add_direction(-1,  0); };
        callback_right = [=] (wf_activator_source, uint32_t) { add_direction( 1,  0); };
        callback_up    = [=] (wf_activator_source, uint32_t) { add_direction( 0, -1); };
        callback_down  = [=] (wf_activator_source, uint32_t) { add_direction( 0,  1); };

        callback_win_left  = [=] (wf_activator_source, uint32_t) { add_direction(-1,  0, get_top_view()); };
        callback_win_right = [=] (wf_activator_source, uint32_t) { add_direction( 1,  0, get_top_view()); };
        callback_win_up    = [=] (wf_activator_source, uint32_t) { add_direction( 0, -1, get_top_view()); };
        callback_win_down  = [=] (wf_activator_source, uint32_t) { add_direction( 0,  1, get_top_view()); };

        auto section   = config->get_section("vswitch");

        auto binding_left  = section->get_option("binding_left",  "<super> KEY_LEFT  | swipe right 4");
        auto binding_right = section->get_option("binding_right", "<super> KEY_RIGHT | swipe left 4");
        auto binding_up    = section->get_option("binding_up",    "<super> KEY_UP    | swipe down 4");
        auto binding_down  = section->get_option("binding_down",  "<super> KEY_DOWN  | swipe up 4");

        auto binding_win_left  = section->get_option("binding_win_left",  "<super> <shift> KEY_LEFT");
        auto binding_win_right = section->get_option("binding_win_right", "<super> <shift> KEY_RIGHT");
        auto binding_win_up    = section->get_option("binding_win_up",    "<super> <shift> KEY_UP");
        auto binding_win_down  = section->get_option("binding_win_down",  "<super> <shift> KEY_DOWN");

        output->add_activator(binding_left,  &callback_left);
        output->add_activator(binding_right, &callback_right);
        output->add_activator(binding_up,    &callback_up);
        output->add_activator(binding_down,  &callback_down);

        output->add_activator(binding_win_left,  &callback_win_left);
        output->add_activator(binding_win_right, &callback_win_right);
        output->add_activator(binding_win_up,    &callback_win_up);
        output->add_activator(binding_win_down,  &callback_win_down);

        animation_duration = section->get_option("duration", "180");
        duration = wf_duration(animation_duration);

        output->connect_signal("set-workspace-request", &on_set_workspace_request);
    }

    inline bool is_active()
    {
        return output->is_plugin_active(grab_interface->name);
    }

    void add_direction(int x, int y, wayfire_view view = nullptr)
    {
        if (!x && !y)
            return;

        if (!is_active())
            start_switch();

        if (view && view->role != wf::VIEW_ROLE_TOPLEVEL)
            view = nullptr;

        if (view && !grabbed_view)
            grabbed_view = view;

        /* Make sure that when we add this direction, we won't go outside
         * of the workspace grid */
        GetTuple(vx, vy, output->workspace->get_current_workspace());
        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        int tvx = clamp(vx + dx.end + x, 0.0, vw - 1.0);
        int tvy = clamp(vy + dy.end + y, 0.0, vh - 1.0);

        dx = {duration.progress(dx), 1.0 * tvx - vx};
        dy = {duration.progress(dy), 1.0 * tvy - vy};

        duration.start();
    }

    wf::signal_callback_t on_set_workspace_request = [=] (wf::signal_data_t *data)
    {
        if (is_active())
            return;

        auto ev = static_cast<change_viewport_signal*> (data);

        GetTuple(ox, oy, ev->old_viewport);
        GetTuple(vx, vy, ev->new_viewport);

        ev->carried_out = true;
        add_direction(vx - ox, vy - oy);
    };

    std::vector<wayfire_view> get_ws_views()
    {
        std::vector<wayfire_view> views;
        for (auto& view : output->workspace->get_views_in_layer(wf::MIDDLE_LAYERS))
        {
            if (view != grabbed_view)
                views.push_back(view);
        }

        return views;
    }

    void ensure_transformer(wayfire_view view)
    {
        if (!view->get_transformer(vswitch_view_transformer::name))
        {
            view->add_transformer(
                std::make_unique<vswitch_view_transformer>(view),
                vswitch_view_transformer::name);
        }
    }

    bool start_switch()
    {
        if (!output->activate_plugin(grab_interface))
            return false;

        output->render->add_effect(&update_animation, wf::OUTPUT_EFFECT_PRE);
        output->render->set_redraw_always();

        duration.start();
        dx = dy = {0, 0};

        return true;
    }

    wf::effect_hook_t update_animation = [=] ()
    {
        if (!duration.running())
            return stop_switch();

        GetTuple(sw, sh, output->get_screen_size());
        for (auto view : get_ws_views())
        {
            ensure_transformer(view);
            auto tr = dynamic_cast<vswitch_view_transformer*> (
                view->get_transformer(vswitch_view_transformer::name).get());

            view->damage();
            tr->translation_x = -duration.progress(dx) * sw;
            tr->translation_y = -duration.progress(dy) * sh;
            view->damage();
        }
    };

    void slide_done()
    {
        GetTuple(vx, vy, output->workspace->get_current_workspace());
        auto old_ws = output->workspace->get_current_workspace();

        vx += dx.end;
        vy += dy.end;

        auto output_g = output->get_relative_geometry();
        output->workspace->set_workspace(std::make_tuple(vx, vy));

        if (grabbed_view)
        {
            auto wm = grabbed_view->get_wm_geometry();
            grabbed_view->move(wm.x + dx.end * output_g.width,
                wm.y + dy.end * output_g.height);

            output->focus_view(grabbed_view);

            view_change_viewport_signal data;
            data.view = grabbed_view;
            data.from = old_ws;
            data.to = output->workspace->get_current_workspace();
            output->emit_signal("view-change-viewport", &data);
        }
    }

    void stop_switch()
    {
        slide_done();
        grabbed_view = nullptr;

        for (auto view : get_ws_views())
            view->pop_transformer(vswitch_view_transformer::name);

        output->deactivate_plugin(grab_interface);
        output->render->rem_effect(&update_animation);
        output->render->set_redraw_always(false);
    }

    void fini()
    {
        if (!is_active())
            stop_switch();

        output->rem_binding(&callback_left);
        output->rem_binding(&callback_right);
        output->rem_binding(&callback_up);
        output->rem_binding(&callback_down);

        output->rem_binding(&callback_win_left);
        output->rem_binding(&callback_win_right);
        output->rem_binding(&callback_win_up);
        output->rem_binding(&callback_win_down);

        output->rem_binding(&gesture_cb);
        output->disconnect_signal("set-workspace-request",
            &on_set_workspace_request);
    }
};

DECLARE_WAYFIRE_PLUGIN(vswitch);
