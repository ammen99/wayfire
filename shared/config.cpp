#include "config.hpp"
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <libevdev/libevdev.h>
#include <linux/input.h>

#include <libweston-3/compositor.h>

using std::string;
/* TODO: add checks to see if values are correct */

string wayfire_config_section::get_string(string name, string default_value)
{
    auto it = options.find(name);
    return (it == options.end() ? default_value : it->second);
}

int wayfire_config_section::get_int(string name, int df)
{
    auto it = options.find(name);
    return (it == options.end() ? df : std::atoi(it->second.c_str()));
}

int wayfire_config_section::get_duration(string name, int df)
{
    int result = get_int(name, df * (1000 / refresh_rate));
    return result / (1000 / refresh_rate);
}

double wayfire_config_section::get_double(string name, double df)
{
    auto it = options.find(name);
    return (it == options.end() ? df : std::atof(it->second.c_str()));
}

wayfire_key wayfire_config_section::get_key(string name, wayfire_key df)
{
    auto it = options.find(name);
    if (it == options.end())
        return df;

    std::stringstream ss(it->second);
    std::vector<std::string> items;
    std::string t;
    while(ss >> t)
        items.push_back(t);

    wayfire_key ans;

    ans.mod = 0;
    for (size_t i = 0; i < items.size() - 1; i++) {
        if (items[i] == "<alt>")
            ans.mod |= MODIFIER_ALT;
        if (items[i] == "<ctrl>")
            ans.mod |= MODIFIER_CTRL;
        if (items[i] == "<shift>")
            ans.mod |= MODIFIER_SHIFT;
        if (items[i] == "<super>")
            ans.mod |= MODIFIER_SUPER;
    }

    ans.keyval = libevdev_event_code_from_name(EV_KEY, items[items.size() - 1].c_str());
    return ans;
}

wayfire_button wayfire_config_section::get_button(string name, wayfire_button df)
{
    auto it = options.find(name);
    if (it == options.end())
        return df;

    std::stringstream ss(it->second);
    std::vector<std::string> items;
    std::string t;
    while(ss >> t)
        items.push_back(t);

    wayfire_button ans;

    ans.mod = 0;
    for (size_t i = 0; i < items.size() - 1; i++) {
        if (items[i] == "<alt>")
            ans.mod |= MODIFIER_ALT;
        if (items[i] == "<ctrl>")
            ans.mod |= MODIFIER_CTRL;
        if (items[i] == "<shift>")
            ans.mod |= MODIFIER_SHIFT;
        if (items[i] == "<super>")
            ans.mod |= MODIFIER_SUPER;
    }

    auto button = items[items.size() - 1];
    if (button == "left")
        ans.button = BTN_LEFT;
    else if (button == "right")
        ans.button = BTN_RIGHT;
    else if (button == "middle")
        ans.button = BTN_MIDDLE;

    return ans;
}

wayfire_color wayfire_config_section::get_color(string name, wayfire_color df)
{
    auto it = options.find(name);
    if (it == options.end())
        return df;

    wayfire_color ans;
    std::stringstream ss(it->second);
    ss >> ans.r >> ans.g >> ans.b >> ans.a;
    return ans;
}

namespace {
    string trim(string x)
    {
        int i = 0, j = x.length() - 1;
        while(i < (int)x.length() && x[i] == ' ') ++i;
        while(j >= 0 && x[j] == ' ') --j;

        if (i <= j)
            return x.substr(i, j - i + 1);
        else
            return "";
    }
}

wayfire_config::wayfire_config(string name, int rr)
{
    std::ifstream file(name);
    string line;

    refresh_rate = rr;
    wayfire_config_section *current_section;
    int line_id = -1;

    while(std::getline(file, line)) {
        ++line_id;
        if (line.size() == 0 || line[0] == '#')
            continue;

        if (line[0] == '[') {
            current_section = new wayfire_config_section();
            current_section->refresh_rate = rr;
            current_section->name = line.substr(1, line.size() - 2);
            sections.push_back(current_section);
            continue;
        }

        string name, value;
        int i = 0;
        while (i < (int)line.size() && line[i] != '=') i++;
        name = trim(line.substr(0, i));
        value = trim(line.substr(i + 1, line.size() - i - 1));

        current_section->options[name] = value;
    }
}

wayfire_config_section* wayfire_config::get_section(string name)
{
    for (auto section : sections)
        if (section->name == name)
            return section;

    auto nsect = new wayfire_config_section();
    nsect->name = name;
    nsect->refresh_rate = refresh_rate;
    sections.push_back(nsect);
    return nsect;
}
