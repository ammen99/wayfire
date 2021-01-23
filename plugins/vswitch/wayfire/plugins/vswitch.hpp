#pragma once

#include <wayfire/plugins/common/view-change-viewport-signal.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include <wayfire/plugins/common/workspace-wall.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/nonstd/reverse.hpp>
#include <cmath>

namespace wf
{
namespace vswitch
{
using namespace animation;
class workspace_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t dx{*this};
    timed_transition_t dy{*this};
};

/**
 * Represents the action of switching workspaces with the vswitch algorithm.
 *
 * The workspace is actually switched at the end of the animation
 */
class workspace_switch_t
{
  public:
    /**
     * Initialize the workspace switch process.
     *
     * @param output The output the workspace switch happens on.
     */
    workspace_switch_t(output_t *output)
    {
        this->output = output;
        wall = std::make_unique<workspace_wall_t>(output);
        wall->connect_signal("frame", &on_frame);

        animation = workspace_animation_t{
            wf::option_wrapper_t<int>{"vswitch/duration"}
        };
    }

    /**
     * Initialize switching animation.
     * At this point, the calling plugin needs to have the custom renderer
     * ability set.
     */
    virtual void start_switch()
    {
        /* Setup wall */
        wall->set_gap_size(gap);
        wall->set_viewport(wall->get_workspace_rectangle(
            output->workspace->get_current_workspace()));
        wall->set_background_color(background_color);
        wall->start_output_renderer();
        running = true;

        /* Setup animation */
        animation.dx.set(0, 0);
        animation.dy.set(0, 0);
        animation.start();
    }

    /**
     * Start workspace switch animation towards the given workspace,
     * and set that workspace as current.
     *
     * @param workspace The new target workspace.
     */
    virtual void set_target_workspace(point_t workspace)
    {
        point_t cws = output->workspace->get_current_workspace();

        animation.dx.set(animation.dx + cws.x - workspace.x, 0);
        animation.dy.set(animation.dy + cws.y - workspace.y, 0);
        animation.start();

        std::vector<wayfire_view> fixed_views;
        if (overlay_view)
        {
            fixed_views.push_back(overlay_view);
        }

        output->workspace->set_workspace(workspace, fixed_views);
    }

    /**
     * Set the overlay view. It will be hidden from the normal workspace layers
     * and shown on top of the workspace wall. The overlay view's position is
     * not animated together with the workspace transition, but its alpha is.
     *
     * Note: if the view disappears, the caller is responsible for resetting the
     * overlay view.
     *
     * @param view The desired overlay view, or NULL if the overlay view needs
     *   to be unset.
     */
    virtual void set_overlay_view(wayfire_view view)
    {
        if (this->overlay_view == view)
        {
            /* Nothing to do */
            return;
        }

        /* Reset old view */
        if (this->overlay_view)
        {
            overlay_view->set_visible(true);
            overlay_view->pop_transformer(vswitch_view_transformer_name);
        }

        /* Set new view */
        this->overlay_view = view;
        if (view)
        {
            view->add_transformer(std::make_unique<wf::view_2D>(view),
                vswitch_view_transformer_name);
            view->set_visible(false); // view is rendered as overlay
        }
    }

    /** @return the current overlay view, might be NULL. */
    virtual wayfire_view get_overlay_view()
    {
        return this->overlay_view;
    }

    /**
     * Called automatically when the workspace switch animation is done.
     * By default, this stops the animation.
     *
     * @param normal_exit Whether the operation has ended because of animation
     *   running out, in which case the workspace and the overlay view are
     *   adjusted, and otherwise not.
     */
    virtual void stop_switch(bool normal_exit)
    {
        if (normal_exit)
        {
            auto old_ws = output->workspace->get_current_workspace();
            adjust_overlay_view_switch_done(old_ws);
        }

        wall->stop_output_renderer(true);
        running = false;
    }

    virtual bool is_running() const
    {
        return running;
    }

    virtual ~workspace_switch_t()
    {}

  protected:
    option_wrapper_t<int> gap{"vswitch/gap"};
    option_wrapper_t<color_t> background_color{"vswitch/background"};
    workspace_animation_t animation;

    output_t *output;
    std::unique_ptr<workspace_wall_t> wall;

    const std::string vswitch_view_transformer_name = "vswitch-transformer";
    wayfire_view overlay_view;

    bool running = false;
    wf::signal_connection_t on_frame = [=] (wf::signal_data_t *data)
    {
        render_frame(static_cast<wall_frame_event_t*>(data)->target);
    };

    virtual void render_overlay_view(const framebuffer_t& fb)
    {
        if (!overlay_view)
        {
            return;
        }

        double progress = animation.progress();
        auto tr = dynamic_cast<wf::view_2D*>(overlay_view->get_transformer(
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

        auto all_views = overlay_view->enumerate_views();
        for (auto v : wf::reverse(all_views))
        {
            v->render_transformed(fb, fb.geometry);
        }
    }

    virtual void render_frame(const framebuffer_t& fb)
    {
        auto start = wall->get_workspace_rectangle(
            output->workspace->get_current_workspace());
        auto size = output->get_screen_size();
        geometry_t viewport = {
            (int)std::round(animation.dx * (size.width + gap) + start.x),
            (int)std::round(animation.dy * (size.height + gap) + start.y),
            start.width,
            start.height,
        };
        wall->set_viewport(viewport);

        render_overlay_view(fb);
        output->render->schedule_redraw();

        if (!animation.running())
        {
            stop_switch(true);
        }
    }

    /**
     * Emit the view-change-viewport signal from the old workspace to the current
     * workspace and unset the view.
     */
    virtual void adjust_overlay_view_switch_done(wf::point_t old_workspace)
    {
        if (!overlay_view)
        {
            return;
        }

        view_change_viewport_signal data;
        data.view = overlay_view;
        data.from = old_workspace;
        data.to   = output->workspace->get_current_workspace();
        output->emit_signal("view-change-viewport", &data);

        set_overlay_view(nullptr);
    }
};

/**
 * A simple class to register the vswitch bindings and get a custom callback called.
 */
class control_bindings_t
{
  public:
    /**
     * Create a vswitch binding instance for the given output.
     *
     * The bindings will not be automatically connected.
     */
    control_bindings_t(wf::output_t *output)
    {
        this->output = output;
    }

    virtual ~control_bindings_t() = default;

    /**
     * A binding callback for vswitch.
     *
     * @param delta The difference between current and target workspace.
     * @param view The view to be moved together with the switch, or nullptr.
     */
    using binding_callback_t =
        std::function<bool (wf::point_t delta, wayfire_view view)>;

    /**
     * Connect bindings on the output.
     *
     * @param callback The callback to execute on each binding
     */
    void setup(binding_callback_t callback)
    {
        callback_left = [=] (const wf::activator_data_t&)
        {
            return handle_dir({-1, 0}, nullptr, callback);
        };
        callback_right = [=] (const wf::activator_data_t&)
        {
            return handle_dir({1, 0}, nullptr, callback);
        };
        callback_up = [=] (const wf::activator_data_t&)
        {
            return handle_dir({0, -1}, nullptr, callback);
        };
        callback_down = [=] (const wf::activator_data_t&)
        {
            return handle_dir({0, 1}, nullptr, callback);
        };

        callback_win_left = [=] (const wf::activator_data_t&)
        {
            return handle_dir({-1, 0}, get_target_view(), callback);
        };
        callback_win_right = [=] (const wf::activator_data_t&)
        {
            return handle_dir({1, 0}, get_target_view(), callback);
        };
        callback_win_up = [=] (const wf::activator_data_t&)
        {
            return handle_dir({0, -1}, get_target_view(), callback);
        };
        callback_win_down = [=] (const wf::activator_data_t&)
        {
            return handle_dir({0, 1}, get_target_view(), callback);
        };

        wf::option_wrapper_t<wf::activatorbinding_t> binding_left{
            "vswitch/binding_left"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_right{
            "vswitch/binding_right"};
        wf::option_wrapper_t<wf::activatorbinding_t> binding_up{
            "vswitch/binding_up"};
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
    }

    /**
     * Disconnect the bindings.
     */
    void tear_down()
    {
        output->rem_binding(&callback_left);
        output->rem_binding(&callback_right);
        output->rem_binding(&callback_up);
        output->rem_binding(&callback_down);

        output->rem_binding(&callback_win_left);
        output->rem_binding(&callback_win_right);
        output->rem_binding(&callback_win_up);
        output->rem_binding(&callback_win_down);
    }

  protected:
    wf::activator_callback callback_left, callback_right, callback_up, callback_down;
    wf::activator_callback callback_win_left, callback_win_right, callback_win_up,
        callback_win_down;

    wf::option_wrapper_t<bool> wraparound{"vswitch/wraparound"};

    wf::output_t *output;

    /** Find the view to switch workspace with */
    virtual wayfire_view get_target_view()
    {
        auto view = output->get_active_view();
        if (!view || (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            return nullptr;
        }

        return view;
    }

    /**
     * Handle binding in the given direction. The next workspace will be
     * determined by the current workspace, target direction and wraparound
     * mode.
     */
    virtual bool handle_dir(wf::point_t dir, wayfire_view view,
        binding_callback_t callback)
    {
        auto ws = output->workspace->get_current_workspace();
        auto target_ws = ws + dir;
        if (!output->workspace->is_workspace_valid(target_ws))
        {
            if (wraparound)
            {
                auto grid_size = output->workspace->get_workspace_grid_size();
                target_ws.x = (target_ws.x + grid_size.width) % grid_size.width;
                target_ws.y = (target_ws.y + grid_size.height) % grid_size.height;
            } else
            {
                target_ws = ws;
            }
        }

        return callback(target_ws - ws, view);
    }
};
}
}
