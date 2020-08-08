#ifndef PLUGIN_LOADER_HPP
#define PLUGIN_LOADER_HPP

#include <vector>
#include <unordered_map>
#include "wayfire/plugin.hpp"
#include "config.h"
#include "wayfire/util.hpp"
#include <wayfire/option-wrapper.hpp>

namespace wf
{
class output_t;
}

class wayfire_config;

using wayfire_plugin = std::unique_ptr<wf::plugin_interface_t>;
struct plugin_manager
{
    plugin_manager(wf::output_t *o);
    ~plugin_manager();

    void reload_dynamic_plugins();
    wf::wl_idle_call idle_reaload_plugins;

  private:
    wf::output_t *output;
    wf::option_wrapper_t<std::string> plugins_opt;
    std::unordered_map<std::string, wayfire_plugin> loaded_plugins;

    void deinit_plugins(bool unloadable);

    wayfire_plugin load_plugin_from_file(std::string path);
    void load_static_plugins();

    void init_plugin(wayfire_plugin& plugin);
    void destroy_plugin(wayfire_plugin& plugin);
};

#endif /* end of include guard: PLUGIN_LOADER_HPP */
