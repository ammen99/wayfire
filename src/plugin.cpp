#include "core.hpp"
#include "output.hpp"

bool wayfire_grab_interface_t::grab() {
    if (grabbed)
        return true;
    if (!output->input->is_plugin_active(name))
        return false;

    grabbed = true;
    output->input->grab_input(this);
    return true;
}

void wayfire_grab_interface_t::ungrab() {
    if (!grabbed)
        return;

    grabbed = false;
    output->input->ungrab_input(this);
}

void wayfire_plugin_t::fini() {}
