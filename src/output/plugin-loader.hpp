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

namespace wf
{
/** Helper functions */
template<class A, class B>
B union_cast(A object)
{
    union
    {
        A x;
        B y;
    } helper;
    helper.x = object;
    return helper.y;
}

/**
 * Open a plugin file and check the file for version errors.
 *
 * On success, return the handle from dlopen() and the pointer to the
 * newInstance of the plugin.
 *
 * @return (dlopen() handle, newInstance pointer)
 */
std::pair<void*, void*> get_new_instance_handle(const std::string& path);
}

#endif /* end of include guard: PLUGIN_LOADER_HPP */
