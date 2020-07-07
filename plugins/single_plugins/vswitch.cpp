#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/view-transform.hpp>
#include <cmath>

#include <wayfire/plugins/common/view-change-viewport-signal.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include <wayfire/plugins/common/workspace-wall.hpp>

#include <linux/input.h>
#include <wayfire/util/duration.hpp>

const std::string vswitch_view_transformer_name = "vswitch-transformer";

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
    wf::activator_callback callback_left, callback_right, callback_up, callback_down;
    wf::activator_callback callback_win_left, callback_win_right, callback_win_up,
        callback_win_down;

    wf::option_wrapper_t<int> gap{"vswitch/gap"};
    wf::option_wrapper_t<wf::color_t> background_color{"vswitch/background"};

    vswitch_animation_t animation;
    wayfire_view grabbed_view = nullptr;

    std::unique_ptr<wf::workspace_wall_t> wall;

  public:
    wayfire_view get_top_view()
    {
        auto ws    = output->workspace->get_current_workspace();
        auto views = output->workspace->get_views_on_workspace(ws,
            wf::LAYER_WORKSPACE);

        return views.empty() ? nullptr : views[0];
    }

    void init()
    {
        grab_interface->name = "vswitch";
        /* note: workspace_wall_t sets a custom renderer, so we
         * need the capability for that */
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP |
            wf::CAPABILITY_CUSTOM_RENDERER;
        grab_interface->callbacks.cancel = [=] () {stop_switch();};

        callback_left = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(-1, 0);
        };
        callback_right = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(1, 0);
        };
        callback_up = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(0, -1);
        };
        callback_down = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(0, 1);
        };

        callback_win_left = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(-1, 0, get_top_view());
        };
        callback_win_right = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(1, 0, get_top_view());
        };
        callback_win_up = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(0, -1, get_top_view());
        };
        callback_win_down = [=] (wf::activator_source_t, uint32_t)
        {
            return add_direction(0, 1, get_top_view());
        };

        wf::option_wrapper_t<wf::activatorbinding_t> binding_left{
            "vswitch/binding_left"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_right{
            "vswitch/binding_right"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_up{"vswitch/binding_up"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_down{
            "vswitch/binding_down"};

        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_left{
            "vswitch/binding_win_left"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_right{
            "vswitch/binding_win_right"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_up{
            "vswitch/binding_win_up"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_win_down{
            "vswitch/binding_win_down"};

        output->add_activator(binding_left, &callback_left);
        output->add_activator(binding_right, &callback_right);
        output->add_activator(binding_up, &callback_up);
        output->add_activator(binding_down, &callback_down);

        output->add_activator(binding_win_left, &callback_win_left);
        output->add_activator(binding_win_right, &callback_win_right);
        output->add_activator(binding_win_up, &callback_win_up);
        output->add_activator(binding_win_down, &callback_win_down);

        animation = vswitch_animation_t{
            wf::option_wrapper_t<int>{"vswitch/duration"}};
        output->connect_signal("set-workspace-request", &on_set_workspace_request);

        output->connect_signal("view-disappeared", &on_grabbed_view_disappear);
        output->connect_signal("view-detached", &on_grabbed_view_disappear);

        wall = std::make_unique<wf::workspace_wall_t>(output);
        wall->connect_signal("frame", &on_frame);
    }

    inline bool is_active()
    {
        return output->is_plugin_active(grab_interface->name);
    }

    bool add_direction(int x, int y, wayfire_view view = nullptr)
    {
        if (!x && !y)
        {
            return false;
        }

        if (!is_active() && !start_switch())
        {
            return false;
        }

        if (view && (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            view = nullptr;
        }

        if (view && !grabbed_view)
        {
            grabbed_view = view;
            view->add_transformer(std::make_unique<wf::view_2D>(view),
                vswitch_view_transformer_name);
            view->set_visible(false); // view is rendered as overlay
        }

        /* Make sure that when we add this direction, we won't go outside
         * of the workspace grid */
        auto cws   = output->workspace->get_current_workspace();
        auto wsize = output->workspace->get_workspace_grid_size();
        int tvx    = wf::clamp(cws.x + animation.dx.end + x, 0.0, wsize.width - 1.0);
        int tvy    =
            wf::clamp(cws.y + animation.dy.end + y, 0.0, wsize.height - 1.0);

        animation.dx.restart_with_end(1.0 * tvx - cws.x);
        animation.dy.restart_with_end(1.0 * tvy - cws.y);
        animation.start();

        return true;
    }

    void unset_grabbed_view()
    {
        if (!this->grabbed_view)
        {
            return;
        }

        this->grabbed_view->set_visible(true);
        this->grabbed_view->pop_transformer(vswitch_view_transformer_name);
        this->grabbed_view = nullptr;
    }

    wf::signal_connection_t on_grabbed_view_disappear =
    {[=] (wf::signal_data_t *data)
        {
            if (get_signaled_view(data) == this->grabbed_view)
            {
                unset_grabbed_view();
            }
        }
    };

    wf::signal_connection_t on_set_workspace_request = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<wf::workspace_change_request_signal*>(data);
        if (is_active())
        {
            auto cws = output->workspace->get_current_workspace();
            ev->carried_out =
                add_direction(ev->new_viewport.x - (cws.x + animation.dx.end),
                    ev->new_viewport.y - (cws.y + animation.dy.end));
        } else
        {
            ev->carried_out = add_direction(ev->new_viewport.x - ev->old_viewport.x,
                ev->new_viewport.y - ev->old_viewport.y);
        }
    };

    bool start_switch()
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        wall->set_gap_size(gap);
        wall->set_viewport(wall->get_workspace_rectangle(
            output->workspace->get_current_workspace()));
        wall->set_background_color(background_color);
        wall->start_output_renderer();

        animation.dx.set(0, 0);
        animation.dy.set(0, 0);
        animation.start();

        return true;
    }

    void render_overlay_view(const wf::framebuffer_t& fb)
    {
        if (!grabbed_view)
        {
            return;
        }

        double progress = animation.progress();
        auto tr = dynamic_cast<wf::view_2D*>(grabbed_view->get_transformer(
            vswitch_view_transformer_name).get());

        static constexpr double smoothing_in     = 0.4;
        static constexpr double smoothing_out    = 0.2;
        static constexpr double smoothing_amount = 0.5;

        if (progress <= smoothing_in)
        {
            tr->alpha = 1.0 - (smoothing_amount / smoothing_in) * progress;
        } else if (progress >= 1.0 - smoothing_out)
        {
            tr->alpha = 1.0 - (smoothing_amount / smoothing_out) * (1.0 - progress);
        } else
        {
            tr->alpha = smoothing_amount;
        }

        grabbed_view->render_transformed(fb, fb.geometry);
    }

    wf::wl_idle_call idle_update_grabbed_view;
    wf::signal_connection_t on_frame = {[=] (wf::signal_data_t *data)
        {
            auto start = wall->get_workspace_rectangle(
                output->workspace->get_current_workspace());
            auto size = output->get_screen_size();
            wf::geometry_t viewport = {
                (int)std::round(animation.dx * (size.width + gap) + start.x),
                (int)std::round(animation.dy * (size.height + gap) + start.y),
                start.width,
                start.height,
            };
            wall->set_viewport(viewport);

            render_overlay_view(static_cast<wf::wall_frame_event_t*>(data)->target);
            output->render->schedule_redraw();

            if (!animation.running())
            {
                return stop_switch();
            }
        }
    };

    void slide_done()
    {
        auto cws    = output->workspace->get_current_workspace();
        auto old_ws = cws;

        cws.x += animation.dx.end;
        cws.y += animation.dy.end;

        auto output_g = output->get_relative_geometry();

        if (grabbed_view)
        {
            grabbed_view->pop_transformer(vswitch_view_transformer_name);
            auto wm = grabbed_view->get_wm_geometry();
            grabbed_view->move(wm.x + animation.dx.end * output_g.width,
                wm.y + animation.dy.end * output_g.height);

            output->focus_view(grabbed_view);
            output->workspace->bring_to_front(grabbed_view);

            view_change_viewport_signal data;
            data.view = grabbed_view;
            data.from = old_ws;
            data.to   = output->workspace->get_current_workspace();
            output->emit_signal("view-change-viewport", &data);
        }
        output->workspace->set_workspace(cws);
    }

    void stop_switch()
    {
        slide_done();
        unset_grabbed_view();

        wall->stop_output_renderer(true);
        output->deactivate_plugin(grab_interface);
    }

    void fini()
    {
        if (!is_active())
        {
            stop_switch();
        }

        output->rem_binding(&callback_left);
        output->rem_binding(&callback_right);
        output->rem_binding(&callback_up);
        output->rem_binding(&callback_down);

        output->rem_binding(&callback_win_left);
        output->rem_binding(&callback_win_right);
        output->rem_binding(&callback_win_up);
        output->rem_binding(&callback_win_down);
    }
};

DECLARE_WAYFIRE_PLUGIN(vswitch);
