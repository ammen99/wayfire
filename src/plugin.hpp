#ifndef PLUGIN_H
#define PLUGIN_H

#include "view.hpp"
#include <unordered_set>

using std::string;

using key_callback = std::function<void(weston_keyboard*, uint32_t)>;
using button_callback = std::function<void(weston_pointer*, uint32_t)>;

/*
 * Documentation for writing a plugin
 *
 * Plugins are just objects which are created during init of
 * core and destroyed when core is destroyed
 *
 * Plugin's main functionality is achieved through their init() function
 * where they initialize themselves and register hooks for various events or connect to signals
 *
 * They should not atempt to access the global core variable
 * in their constructors since during that time that global
 * variable is still uninitialized
 *
 * A typical example of a plugin is when
 * in the init() function it registers a hook(disabled)
 * and then a key/button binding to activate it
 *
 * For example see expo.cpp, move.cpp, resize.cpp
 */

class wayfire_output;
using owner_t = string;

/* owners are used to acquire screen grab and to activate */
struct wayfire_grab_interface_t {
    owner_t name;
    std::unordered_set<owner_t> compat;
    bool compatAll = true;
    const wayfire_output *output;

    wayfire_grab_interface_t(wayfire_output *_output) : output(_output) {}

    void grab();
    void ungrab();
    bool grabbed;
};

using wayfire_grab_interface = std::shared_ptr<wayfire_grab_interface_t>;
class wayfire_plugin_t {
    public:
        /* the output this plugin is running on
         * each output has an instance of each plugin, so that the two monitors act independently
         * in the future there should be an option for mirroring displays */
        wayfire_output *output;

        wayfire_grab_interface grab_interface;

        /* should read configuration data, attach hooks / keybindings, etc */
        virtual void init(weston_config *config) = 0;

        /* the fini() method should remove all hooks/buttons/keys
         * and of course prepare the plugin for deletion, i.e
         * fini() must act like destuctor */
        virtual void fini();

        /* used by the plugin loader, shouldn't be modified by plugins */
        bool dynamic = false;
        void *handle;
};

using wayfire_plugin = std::shared_ptr<wayfire_plugin_t>;
/* each dynamic plugin should have the symbol get_plugin_instance() which returns
 * an instance of the plugin */
typedef wayfire_plugin_t *(*get_plugin_instance_t)();

/* render hooks are used when a plugin requests to draw the whole desktop on their own
 * example plugin is cube */
using render_hook_t = std::function<void()>;

#define GetTuple(x,y,t) auto x = std::get<0>(t); \
                        auto y = std::get<1>(t)

using view_callback_proc_t = std::function<void(wayfire_view)>;

#endif

void weston_config_section_get_cppstring(weston_config_section *section,
        std::string name, std::string& val, std::string default_value);
