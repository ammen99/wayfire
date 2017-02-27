#include "config.hpp"
#include <cstdlib>
#include <sstream>
#include <libweston-1/compositor.h>
#include <xkbcommon/xkbcommon.h>

using std::string;

string wayfire_config_section::get_string(string name, string default_value) {
    auto it = options.find(name);
    return (it == options.end() ? default_value : it->second);
}

int wayfire_config_section::get_int(string name, int df) {
    auto it = options.find(name);
    return (it == options.end() ? df : std::atoi(it->second.c_str()));
}

double wayfire_config_section::get_double(string name, double df) {
    auto it = options.find(name);
    return (it == options.end() ? df : std::atof(it->second.c_str()));
}

wayfire_key wayfire_config_section::get_key(string name, wayfire_key df) {
    auto it = options.find(name);
    if (it == options.end())
        return df;

    std::stringstream ss(it->second);
    string mod, key;
    ss >> mod >> key;

    wayfire_key ans;

    if (mod == "<alt>")
        ans.mod = MODIFIER_ALT;
    if (mod == "<ctrl>")
        ans.mod = MODIFIER_CTRL;
    if (mod == "<shift>")
        ans.mod = MODIFIER_SHIFT;
    if (mod == "<super>")
        ans.mod = MODIFIER_SUPER;

}

