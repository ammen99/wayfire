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
#include <set>
#include "view-change-viewport-signal.hpp"
#include "../wobbly/wobbly-signal.hpp"
#include <wayfire/util/duration.hpp>

class vswitch_view_transformer : public wf_2D_view
{
    public:
        static const std::string name;
        vswitch_view_transformer(wayfire_view view) : wf_2D_view(view) {}
        virtual uint32_t get_z_order() override { return WF_TRANSFORMER_BLUR - 1; }
};
const std::string vswitch_view_transformer::name = "vswitch-transformer";

using namespace wf::animation;
class vswitch_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t dx{*this};
    timed_transition_t dy{*this};
};

class vswitch : public wf::plugin_interface_t
{
    private:
        activator_callback callback_left, callback_right, callback_up, callback_down;
        activator_callback callback_win_left, callback_win_right, callback_win_up, callback_win_down;

        gesture_callback gesture_cb;
        vswitch_animation_t animation;
        wayfire_view grabbed_view = nullptr;

    public:
    wayfire_view get_top_view()
    {
        auto ws = output->workspace->get_current_workspace();
        auto views = output->workspace->get_views_on_workspace(ws,
            wf::LAYER_WORKSPACE | wf::LAYER_FULLSCREEN, true);

        return views.empty() ? nullptr : views[0];
    }

    void init()
    {
        grab_interface->name = "vswitch";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP;
        grab_interface->callbacks.cancel = [=] () {stop_switch();};

        callback_left  = [=] (wf_activator_source, uint32_t) { return add_direction(-1,  0); };
        callback_right = [=] (wf_activator_source, uint32_t) { return add_direction( 1,  0); };
        callback_up    = [=] (wf_activator_source, uint32_t) { return add_direction( 0, -1); };
        callback_down  = [=] (wf_activator_source, uint32_t) { return add_direction( 0,  1); };

        callback_win_left  = [=] (wf_activator_source, uint32_t) { return add_direction(-1,  0, get_top_view()); };
        callback_win_right = [=] (wf_activator_source, uint32_t) { return add_direction( 1,  0, get_top_view()); };
        callback_win_up    = [=] (wf_activator_source, uint32_t) { return add_direction( 0, -1, get_top_view()); };
        callback_win_down  = [=] (wf_activator_source, uint32_t) { return add_direction( 0,  1, get_top_view()); };

        wf::option_wrapper_t<wf::activatorbinding_t> binding_left{"vswitch/binding_left"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_right{"vswitch/binding_right"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_up{"vswitch/binding_up"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_down{"vswitch/binding_down"};

        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_left{"vswitch/binding_win_left"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_right{"vswitch/binding_win_right"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_up{"vswitch/binding_win_up"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_down{"vswitch/binding_win_down"};

        output->add_activator(binding_left,  &callback_left);
        output->add_activator(binding_right, &callback_right);
        output->add_activator(binding_up,    &callback_up);
        output->add_activator(binding_down,  &callback_down);

        output->add_activator(binding_win_left,  &callback_win_left);
        output->add_activator(binding_win_right, &callback_win_right);
        output->add_activator(binding_win_up,    &callback_win_up);
        output->add_activator(binding_win_down,  &callback_win_down);

        animation = vswitch_animation_t{
            wf::option_wrapper_t<int> {"vswitch/duration"}.raw_option};
        output->connect_signal("set-workspace-request", &on_set_workspace_request);
    }

    inline bool is_active()
    {
        return output->is_plugin_active(grab_interface->name);
    }

    bool add_direction(int x, int y, wayfire_view view = nullptr)
    {
        if (!x && !y)
            return false;

        if (!is_active() && !start_switch())
            return false;

        if (view && view->role != wf::VIEW_ROLE_TOPLEVEL)
            view = nullptr;

        if (view && !grabbed_view)
            grabbed_view = view;

        /* Make sure that when we add this direction, we won't go outside
         * of the workspace grid */
        auto cws = output->workspace->get_current_workspace();
        auto wsize = output->workspace->get_workspace_grid_size();
        int tvx = clamp(cws.x + animation.dx.end + x, 0.0, wsize.width - 1.0);
        int tvy = clamp(cws.y + animation.dy.end + y, 0.0, wsize.height - 1.0);

        animation.dx.restart_with_end(1.0 * tvx - cws.x);
        animation.dy.restart_with_end(1.0 * tvy - cws.y);
        animation.start();
        return true;
    }

    wf::signal_callback_t on_set_workspace_request = [=] (wf::signal_data_t *data)
    {
        if (is_active())
            return;

        auto ev = static_cast<change_viewport_signal*> (data);
        ev->carried_out = true;
        add_direction(ev->new_viewport.x - ev->old_viewport.x,
            ev->new_viewport.y - ev->old_viewport.y);
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

        animation.dx.set(0, 0);
        animation.dy.set(0, 0);
        animation.start();
        return true;
    }

    wf::effect_hook_t update_animation = [=] ()
    {
        if (!animation.running())
            return stop_switch();

        auto screen_size = output->get_screen_size();
        for (auto view : get_ws_views())
        {
            ensure_transformer(view);
            auto tr = dynamic_cast<vswitch_view_transformer*> (
                view->get_transformer(vswitch_view_transformer::name).get());

            view->damage();
            tr->translation_x = -animation.dx * screen_size.width;
            tr->translation_y = -animation.dy * screen_size.height;
            view->damage();
        }
    };

    void slide_done()
    {
        auto cws = output->workspace->get_current_workspace();
        auto old_ws = cws;

        cws.x += animation.dx.end;
        cws.y += animation.dy.end;

        auto output_g = output->get_relative_geometry();
        output->workspace->set_workspace(cws);

        if (grabbed_view)
        {
            auto wm = grabbed_view->get_wm_geometry();
            grabbed_view->move(wm.x + animation.dx.end * output_g.width,
                wm.y + animation.dy.end * output_g.height);

            output->focus_view(grabbed_view);
            output->workspace->bring_to_front(grabbed_view);

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
