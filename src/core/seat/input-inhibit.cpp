#include <map>

extern "C"
{
#include <wlr/types/wlr_input_inhibitor.h>
}

#include "signal-definitions.hpp"
#include "input-inhibit.hpp"
#include "output.hpp"
#include "plugin.hpp"
#include "../core-impl.hpp"
#include "input-manager.hpp"
#include "../output/output-impl.hpp"

static const std::string iface_name = "_input_inhibitor";
static std::map<wf::output_t*, wayfire_grab_interface> iface_map;

/* output added/removed */
static wf::signal_callback_t on_output_changed = [] (wf::signal_data_t *data)
{
    auto wo = get_signaled_output(data);

    /* if the output was inhibited, then it is being removed */
    if (is_output_inhibited(wo))
    {
        uninhibit_output(wo);
    }
    else if (wf::get_core_impl().input->exclusive_client)
    {
        inhibit_output(wo);
    }
};

wlr_input_inhibit_manager* create_input_inhibit()
{
    wf::get_core().connect_signal("output-added", &on_output_changed);
    wf::get_core().connect_signal("output-removed", &on_output_changed);

    return wlr_input_inhibit_manager_create(wf::get_core().display);
}

void inhibit_output(wf::output_t *output)
{
    wayfire_grab_interface iface = new wayfire_grab_interface_t(output);
    iface->name = iface_name;
    iface->abilities_mask = WF_ABILITY_ALL;

    auto output_impl = dynamic_cast<wf::output_impl_t*> (output);
    output_impl->break_active_plugins();
    output_impl->activate_plugin(iface);

    iface_map[output] = iface;
}

bool is_output_inhibited(wf::output_t *output)
{
    return output->is_plugin_active(iface_name);
}

void uninhibit_output(wf::output_t *output)
{
    if (!output->is_plugin_active(iface_name))
        return;

    auto iface = iface_map[output];
    assert(iface);

    iface_map.erase(output);
    output->deactivate_plugin(iface);
}
