#include "core-impl.hpp"
#include "wayfire/output.hpp"
#include "seat/input-manager.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/util/log.hpp>

wf::plugin_grab_interface_t::plugin_grab_interface_t(wf::output_t *wo) :
    output(wo)
{}

bool wf::plugin_grab_interface_t::grab()
{
    if (!(capabilities & CAPABILITY_GRAB_INPUT))
    {
        LOGE("attempt to grab iface ", name, " without input grabbing ability");

        return false;
    }

    if (grabbed)
    {
        return true;
    }

    if (!output->is_plugin_active(name))
    {
        return false;
    }

    grabbed = true;

    if (output == wf::get_core_impl().get_active_output())
    {
        return wf::get_core_impl().input->grab_input(this);
    } else
    {
        return true;
    }
}

void wf::plugin_grab_interface_t::ungrab()
{
    if (!grabbed)
    {
        return;
    }

    grabbed = false;
    if (output == wf::get_core_impl().get_active_output())
    {
        wf::get_core_impl().input->ungrab_input();
    }
}

bool wf::plugin_grab_interface_t::is_grabbed()
{
    return grabbed;
}

void wf::plugin_interface_t::fini()
{}
wf::plugin_interface_t::~plugin_interface_t()
{}

namespace wf
{
wayfire_view get_signaled_view(wf::signal_data_t *data)
{
    auto conv = static_cast<wf::_view_signal*>(data);
    if (!conv)
    {
        LOGE("Got a bad _view_signal");

        return nullptr;
    }

    return conv->view;
}

wf::output_t *get_signaled_output(wf::signal_data_t *data)
{
    auto result = static_cast<wf::_output_signal*>(data);

    return result ? result->output : nullptr;
}
}
