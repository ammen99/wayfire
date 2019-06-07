#ifndef PLUGIN_LOADER_HPP
#define PLUGIN_LOADER_HPP

#include <vector>
#include <unordered_map>
#include "plugin.hpp"
#include "config.h"
#include "util.hpp"

namespace wf
{
class output_t;
}
class wayfire_config;

using wayfire_plugin = std::unique_ptr<wf::plugin_interface_t>;
struct plugin_manager
{
    plugin_manager(wf::output_t *o, wayfire_config *config);
    ~plugin_manager();

    void reload_dynamic_plugins();
    wf::wl_idle_call idle_reaload_plugins;

private:
    wf::output_t *output;
    wayfire_config *config;
    wf_option plugins_opt;

    std::unordered_map<std::string, wayfire_plugin> loaded_plugins;
    wf_option_callback list_updated;

    void deinit_plugins(bool unloadable);

    wayfire_plugin load_plugin_from_file(std::string path);
    void load_static_plugins();

    void init_plugin(wayfire_plugin& plugin);
    void destroy_plugin(wayfire_plugin& plugin);
};

#endif /* end of include guard: PLUGIN_LOADER_HPP */
