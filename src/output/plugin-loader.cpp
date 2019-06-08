#include <sstream>
#include <algorithm>
#include <set>
#include <memory>
#include <dlfcn.h>

#include "plugin-loader.hpp"
#include "output-layout.hpp"
#include "output.hpp"
#include "../core/wm.hpp"
#include "core.hpp"
#include "debug.hpp"

namespace
{
    template<class A, class B> B union_cast(A object)
    {
        union {
            A x;
            B y;
        } helper;
        helper.x = object;
        return helper.y;
    }
}

static const std::string default_plugins = "viewport_impl move resize animate \
                                            switcher vswitch cube expo command \
                                            grid";

plugin_manager::plugin_manager(wf::output_t *o, wayfire_config *config)
{
    this->config = config;
    this->output = o;

    auto section = config->get_section("core");
    plugins_opt = section->get_option("plugins", "none");

    reload_dynamic_plugins();
    load_static_plugins();

    list_updated = [=] ()
    {
        /* reload when config reload has finished */
        idle_reaload_plugins.run_once([&] () {reload_dynamic_plugins(); });
    };

    plugins_opt->updated.push_back(&list_updated);
}

void plugin_manager::deinit_plugins(bool unloadable)
{
    for (auto& p : loaded_plugins)
    {
        if (!p.second) // already destroyed on the previous iteration
            continue;

        if (p.second->is_unloadable() == unloadable)
            destroy_plugin(p.second);
    }
}

plugin_manager::~plugin_manager()
{
    /* First remove unloadable plugins, then others */
    deinit_plugins(true);
    deinit_plugins(false);

    loaded_plugins.clear();
    plugins_opt->updated.erase(
        std::remove(plugins_opt->updated.begin(), plugins_opt->updated.end(),
            &list_updated), plugins_opt->updated.end());
}

void plugin_manager::init_plugin(wayfire_plugin& p)
{
    p->grab_interface = std::make_unique<wf::plugin_grab_interface_t> (output);
    p->output = output;

    p->init(config);
}

void plugin_manager::destroy_plugin(wayfire_plugin& p)
{
    p->grab_interface->ungrab();
    output->deactivate_plugin(p->grab_interface);

    p->fini();

    /** dlopen()/dlclose() do reference counting */
    if (p->handle)
        dlclose(p->handle);

    p.reset();
}

wayfire_plugin plugin_manager::load_plugin_from_file(std::string path)
{
    // RTLD_GLOBAL is required for RTTI/dynamic_cast across plugins
    void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if(handle == NULL)
    {
        log_error("error loading plugin: %s", dlerror());
        return nullptr;
    }

    /* Check plugin version */
    auto version_func_ptr = dlsym(handle, "getWayfireVersion");
    if (version_func_ptr == NULL)
    {
        log_error("%s: missing getWayfireVersion()", path.c_str());
        dlclose(handle);
        return nullptr;
    }

    auto version_func =
        union_cast<void*, wayfire_plugin_version_func> (version_func_ptr);
    int32_t plugin_abi_version = version_func();

    if (version_func() != WAYFIRE_API_ABI_VERSION)
    {
        log_error("%s: API/ABI version mismatch: Wayfire is %d, plugin built "
            "with %d", path.c_str(), WAYFIRE_API_ABI_VERSION, plugin_abi_version);
        dlclose(handle);
        return nullptr;
    }

    auto new_instance_func_ptr = dlsym(handle, "newInstance");
    if(new_instance_func_ptr == NULL)
    {
        log_error("%s: missing newInstance(). %s", path.c_str(), dlerror());
        return nullptr;
    }

    log_debug("loading plugin %s", path.c_str());
    auto new_instance_func =
        union_cast<void*, wayfire_plugin_load_func> (new_instance_func_ptr);

    auto ptr = wayfire_plugin(new_instance_func());
    ptr->handle = handle;
    return ptr;
}

void plugin_manager::reload_dynamic_plugins()
{
    auto plugin_list = plugins_opt->as_string();
    if (plugin_list == "none")
    {
        log_error("No plugins specified in the config file, or config file is "
            "missing. In this state the compositor is nearly unusable, please "
            "ensure your configuration file is set up properly.");
        plugin_list = default_plugins;
    }

    std::stringstream stream(plugin_list);
    std::vector<std::string> next_plugins;

    auto plugin_prefix = std::string(INSTALL_PREFIX "/lib/wayfire/");

    std::string plugin_name;
    while(stream >> plugin_name)
    {
        if (plugin_name.size())
        {
            if (plugin_name.at(0) == '/')
                next_plugins.push_back(plugin_name);
            else
                next_plugins.push_back(plugin_prefix + "lib" + plugin_name + ".so");
        }
    }

    /* erase plugins that have been removed from the config */
    auto it = loaded_plugins.begin();
    while(it != loaded_plugins.end())
    {
        /* skip built-in(static) plugins */
        if (it->first.size() && it->first[0] == '_')
        {
            ++it;
            continue;
        }

        if (std::find(next_plugins.begin(), next_plugins.end(), it->first) == next_plugins.end() &&
            it->second->is_unloadable())
        {
            log_debug("unload plugin %s", it->first.c_str());
            destroy_plugin(it->second);
            it = loaded_plugins.erase(it);
        }
        else
        {
            ++it;
        }
    }


    /* load new plugins */
    for (auto plugin : next_plugins)
    {
        if (loaded_plugins.count(plugin))
            continue;

        auto ptr = load_plugin_from_file(plugin);
        if (ptr)
        {
            init_plugin(ptr);
            loaded_plugins[plugin] = std::move(ptr);
        }
    }
}

template<class T> static wayfire_plugin create_plugin()
{
    return std::unique_ptr<wf::plugin_interface_t>(new T);
}

void plugin_manager::load_static_plugins()
{
    loaded_plugins["_exit"]         = create_plugin<wayfire_exit>();
    loaded_plugins["_focus"]        = create_plugin<wayfire_focus>();
    loaded_plugins["_close"]        = create_plugin<wayfire_close>();
    loaded_plugins["_focus_parent"] = create_plugin<wayfire_handle_focus_parent>();

    init_plugin(loaded_plugins["_exit"]);
    init_plugin(loaded_plugins["_focus"]);
    init_plugin(loaded_plugins["_close"]);
    init_plugin(loaded_plugins["_focus_parent"]);
}
