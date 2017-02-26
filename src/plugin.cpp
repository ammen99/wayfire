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

void weston_config_section_get_cppstring(weston_config_section* section, std::string name,
        std::string &value, std::string default_value) {

    char *buf;
    weston_config_section_get_string(section, name.c_str(), &buf, default_value.c_str());
    value = buf;
    free(buf);
}


