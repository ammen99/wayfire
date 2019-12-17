#include <plugin.hpp>
#include <output.hpp>
#include <core.hpp>
#include <view.hpp>
#include <output-layout.hpp>
#include <linux/input-event-codes.h>

class wayfire_output_manager : public wf::plugin_interface_t
{
    wf::wl_idle_call idle_next_output;

    activator_callback switch_output = [=] (wf_activator_source, uint32_t)
    {
        if (!output->activate_plugin(grab_interface))
            return false;

        /* when we switch the output, the oswitch keybinding
         * may be activated for the next output, which we don't want,
         * so we postpone the switch */
        auto next =
            wf::get_core().output_layout->get_next_output(output);
        idle_next_output.run_once([=] () {
            wf::get_core().focus_output(next);
        });

        return true;
    };

    activator_callback switch_output_with_window = [=] (wf_activator_source, uint32_t)
    {
        if (!output->can_activate_plugin(grab_interface))
            return false;

        auto next =
            wf::get_core().output_layout->get_next_output(output);
        auto view = output->get_active_view();

        if (!view)
        {
            switch_output(ACTIVATOR_SOURCE_KEYBINDING, 0);
            return true;
        }

        wf::get_core().move_view_to_output(view, next);
        idle_next_output.run_once([=] () {
            wf::get_core().focus_output(next);
        });

        return true;
    };
  public:
    void init()
    {
        grab_interface->name = "oswitch";
        grab_interface->capabilities = 0;

        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/next_output"},
            &switch_output);
        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"oswitch/next_output_with_win"},
            &switch_output_with_window);
    }

    void fini()
    {
        output->rem_binding(&switch_output);
        output->rem_binding(&switch_output_with_window);
        idle_next_output.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_output_manager);
