#include "core.hpp"
#include "output.hpp"

namespace {
    int grabCount = 0;
    std::vector<wayfire_grab_interface> active_grabs;
}

void wayfire_grab_interface_t::grab() {
    if (grabbed)
        return;
    weston_pointer_axis_event *ev;

    if (!output->input->is_plugin_active(name))
        return;

    grabbed = true;
    grabCount++;

    if (grabCount == 1) {
        output->input->grab_pointer();
        output->input->grab_keyboard();
    }
}

void wayfire_grab_interface_t::ungrab() {
    if (!grabbed)
        return;

    grabbed = false;
    grabCount--;

    if (grabCount == 0) {
        output->input->ungrab_pointer();
        output->input->ungrab_keyboard();
    }

    assert(grabCount >= 0); // alarm if anything isn't okay
}

void wayfire_plugin_t::fini() {}

void weston_config_section_get_cppstring(weston_config_section* section, std::string name,
        std::string &value, std::string default_value) {

    char *buf;
    weston_config_section_get_string(section, name.c_str(), &buf, default_value.c_str());
    value = buf;
    free(buf);
}


