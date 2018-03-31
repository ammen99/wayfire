#include "config.hpp"
#include <cstdlib>
#include <sstream>
#include <fstream>
#include <libevdev/libevdev.h>
#include <linux/input.h>

#include <config.h>

std::ofstream out;

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

    if (it->second == "none")
        return {0, 0};

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

    if (it->second == "none")
        return {0, 0};

    std::stringstream ss(it->second);
    std::vector<std::string> items;
    std::string t;
    while(ss >> t)
        items.push_back(t);

    if (items.empty())
        return df;

    wayfire_button ans;
    ans.mod = 0;

    for (size_t i = 0; i < items.size() - 1; i++)
    {
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
    else
        ans.button = 0;

    return ans;
}

wayfire_color wayfire_config_section::get_color(string name, wayfire_color df)
{
    auto it = options.find(name);
    if (it == options.end())
        return df;

    wayfire_color ans = {0, 0, 0, 0};
    std::stringstream ss(it->second);
    ss >> ans.r >> ans.g >> ans.b >> ans.a;
    return ans;
}

static string trim(const string& x)
{
    int i = 0, j = x.length() - 1;
    while(i < (int)x.length() && std::iswspace(x[i])) ++i;
    while(j >= 0 && std::iswspace(x[j])) --j;

    if (i <= j)
        return x.substr(i, j - i + 1);
    else
        return "";
}

using lines_t = std::vector<string>;
static void prune_comments(lines_t& file)
{
    for (auto& line : file)
    {
        size_t i = line.find_first_of('#');
        if (i != string::npos)
            line = line.substr(0, i);
    }
}

static lines_t filter_empty_lines(const lines_t& file)
{
    lines_t pruned;
    for (const auto& line : file)
    {
        string cleaned_line = trim(line);
        if (cleaned_line.size())
            pruned.push_back(cleaned_line);
    }

    return pruned;
}

static lines_t merge_lines(const lines_t& file)
{
    lines_t merged;
    int i = 0;
    for (; i < (int)file.size(); i++)
    {
        string result = file[i]; ++i;
        while(file[i - 1].back() == '\\' && i < (int)file.size())
        {
            result.pop_back();
            result += file[i++];
        }
        --i;

        merged.push_back(result);
    }

    return merged;
}

wayfire_config::wayfire_config(string name, int rr)
{
    std::ifstream file(name);
    string line;

#ifdef WAYFIRE_DEBUG_ENABLED
    out.open("/tmp/.wayfire_config_debug");
    out << "use config: " << name << std::endl;
#endif

    refresh_rate = rr;
    wayfire_config_section *current_section;

    lines_t lines;
    while(std::getline(file, line))
    {
        lines.push_back(line);
    }

    prune_comments(lines);
    lines = filter_empty_lines(lines);
    lines = merge_lines(lines);

    for (auto line : lines)
    {
        if (line[0] == '[')
        {
            current_section = new wayfire_config_section();
            current_section->refresh_rate = rr;
            current_section->name = line.substr(1, line.size() - 2);
            sections.push_back(current_section);
            continue;
        }

        string name, value;
        size_t i = line.find_first_of('=');
        if (i != string::npos)
        {
            name = trim(line.substr(0, i));
            value = trim(line.substr(i + 1, line.size() - i - 1));
            current_section->options[name] = value;

#ifdef WAYFIRE_DEBUG_ENABLED
            out << current_section->name << ": " << name << " = " << value << std::endl;
#endif
        }
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

void wayfire_config::set_refresh_rate(int rr)
{
    for (auto section : sections)
        section->refresh_rate = rr;
}
