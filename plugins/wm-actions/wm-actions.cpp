#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/util/log.hpp>
#include "wm-actions-signals.hpp"

class wayfire_wm_actions_t : public wf::plugin_interface_t
{
    nonstd::observer_ptr<wf::sublayer_t> always_above;

    wf::option_wrapper_t<wf::activatorbinding_t> toggle_above{
        "wm-actions/toggle_always_on_top"};
    wf::option_wrapper_t<wf::activatorbinding_t> toggle_fullscreen{
        "wm-actions/toggle_fullscreen"};

    bool toggle_keep_above(wayfire_view view)
    {
        if (!output->can_activate_plugin(this->grab_interface))
        {
            return false;
        }

        if (!view || (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            return false;
        }

        if (view->has_data("wm-actions-above"))
        {
            output->workspace->add_view(view,
                (wf::layer_t)output->workspace->get_view_layer(view));
            view->erase_data("wm-actions-above");
        } else
        {
            output->workspace->add_view_to_sublayer(view, always_above);
            view->store_data(std::make_unique<wf::custom_data_t>(),
                "wm-actions-above");
        }

        wf::wm_actions_above_changed data;
        data.view = view;
        output->emit_signal("wm-actions-above-changed", &data);

        return true;
    }

    wayfire_view choose_view(wf::activator_source_t source)
    {
        if (source == wf::ACTIVATOR_SOURCE_BUTTONBINDING)
        {
            return wf::get_core().get_cursor_focus_view();
        }

        return output->get_active_view();
    }

    /**
     * Calling a specific view / specific keep_above action via signal
     */
    wf::signal_connection_t on_toggle_above_signal =
    {[=] (wf::signal_data_t *data)
        {
            auto signal = static_cast<wf::wm_actions_toggle_above*>(data);

            if (!toggle_keep_above(signal->view))
            {
                LOG(wf::log::LOG_LEVEL_DEBUG,
                    "view above action failed via signal.");
            }
        }
    };

    /**
     * Ensures views marked as above are still above
     * if their output changes.
     */
    wf::signal_connection_t on_view_output_changed
    {[=] (wf::signal_data_t *data)
        {
            auto signal = static_cast<wf::view_moved_to_output_signal*>(data);
            if (signal->new_output != output)
            {
                return;
            }

            auto view = signal->view;

            if (!view)
            {
                return;
            }

            if (view->has_data("wm-actions-above"))
            {
                output->workspace->add_view_to_sublayer(view, always_above);
            }
        }
    };

    /**
     * Ensures views marked as above are still above
     * if they are minimized and unminimized.
     */
    wf::signal_connection_t on_view_minimized
    {[=] (wf::signal_data_t *data)
        {
            auto signal = static_cast<wf::view_minimize_request_signal*>(data);
            auto view   = signal->view;

            if (!view)
            {
                return;
            }

            if (view->get_output() != output)
            {
                return;
            }

            if (view->has_data("wm-actions-above") && (signal->state == false))
            {
                output->workspace->add_view_to_sublayer(view, always_above);
            }
        }
    };

    /**
     * The default activator bindings.
     */
    wf::activator_callback on_toggle_above =
        [=] (wf::activator_source_t source, uint32_t) -> bool
    {
        auto view = choose_view(source);

        return toggle_keep_above(view);
    };

    wf::activator_callback on_toggle_fullscreen =
        [=] (wf::activator_source_t source, uint32_t) -> bool
    {
        if (!output->can_activate_plugin(this->grab_interface))
        {
            return false;
        }

        auto view = choose_view(source);
        if (!view || (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            return false;
        }

        view->fullscreen_request(view->get_output(), !view->fullscreen);

        return true;
    };

  public:
    void init() override
    {
        always_above = output->workspace->create_sublayer(
            wf::LAYER_WORKSPACE, wf::SUBLAYER_DOCKED_ABOVE);
        output->add_activator(toggle_above, &on_toggle_above);
        output->add_activator(toggle_fullscreen, &on_toggle_fullscreen);
        output->connect_signal("wm-actions-toggle-above", &on_toggle_above_signal);
        output->connect_signal("view-minimized", &on_view_minimized);
        wf::get_core().connect_signal("view-moved-to-output",
            &on_view_output_changed);
    }

    void fini() override
    {
        auto always_on_top_views =
            output->workspace->get_views_in_sublayer(always_above);

        for (auto view : always_on_top_views)
        {
            view->erase_data("wm-actions-above");
        }

        output->workspace->destroy_sublayer(always_above);
        output->rem_binding(&on_toggle_above);
        output->rem_binding(&on_toggle_fullscreen);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_wm_actions_t);
