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

void plugin_manager::deinit_plugins(bool unloadable, bool internal)
{
    for (auto& p : loaded_plugins)
    {
        if (!p.second) // already destroyed on the previous iteration
            continue;

        if (p.second->is_unloadable() == unloadable && p.second->is_internal() == internal)
            destroy_plugin(p.second);
    }
}

plugin_manager::~plugin_manager()
{
    deinit_plugins(true, false); // regular plugins - unloadable, not internal
    deinit_plugins(false, false); // regular plugins - not-unloadable, not internal
    deinit_plugins(true, true); // system plugins - unloadable, internal
    deinit_plugins(false, true); // system plugins - not-unloadable, internal

    loaded_plugins.clear();
    plugins_opt->updated.erase(
        std::remove(plugins_opt->updated.begin(), plugins_opt->updated.end(),
            &list_updated), plugins_opt->updated.end());
}

void plugin_manager::init_plugin(wayfire_plugin& p)
{
    p->grab_interface = new wayfire_grab_interface_t(output);
    p->output = output;

    p->init(config);
}

void plugin_manager::destroy_plugin(wayfire_plugin& p)
{
    p->grab_interface->ungrab();
    output->deactivate_plugin(p->grab_interface);

    p->fini();
    delete p->grab_interface;

    /* we load the same plugins for each output, so we must dlclose() the handle
     * only when we remove the last output */
    if (wf::get_core().output_layout->get_num_outputs() < 1)
    {
        if (p->dynamic)
            dlclose(p->handle);
    }

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


    auto initptr = dlsym(handle, "newInstance");
    if(initptr == NULL)
    {
        log_error("%s: missing newInstance(). %s", path.c_str(), dlerror());
        return nullptr;
    }

    log_debug("loading plugin %s", path.c_str());
    get_plugin_instance_t init = union_cast<void*, get_plugin_instance_t> (initptr);

    auto ptr = wayfire_plugin(init());

    ptr->handle = handle;
    ptr->dynamic = true;

    return wayfire_plugin(init());
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
    return std::unique_ptr<wayfire_plugin_t>(new T);
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
