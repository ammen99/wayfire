#ifndef PLUGIN_H
#define PLUGIN_H

extern "C"
{
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_cursor.h>
}

#include <functional>
#include "config.hpp"

struct wf_binding; // opaque handle to a binding, can be used to remove it
using key_callback = std::function<void(uint32_t)>;
using button_callback = std::function<void(uint32_t, int32_t, int32_t)>; // button, x, y
using axis_callback = std::function<void(wlr_event_pointer_axis*)>;
using touch_callback = std::function<void(int32_t, int32_t)>; // x, y
using gesture_callback = std::function<void(wf_touch_gesture*)>;

enum wf_activator_source
{
    ACTIVATOR_SOURCE_KEYBINDING,
    ACTIVATOR_SOURCE_BUTTONBINDING,
    ACTIVATOR_SOURCE_GESTURE,
};

/* First argument is the source which was used to activate, the second one is
 * the key or button which triggered it, if applicable.
 *
 * Special case: modifier bindings. In that case, the source is a keybinding,
 * but the second argument is 0 */
using activator_callback = std::function<void(wf_activator_source, uint32_t)>;

namespace wf
{
class output_t;
}

class wayfire_config;
using owner_t = std::string;

/* In the current model, plugins can add as much keybindings as they want
 * and receive events for them. However, some plugins cannot be active at
 * the same * time - consider expo and cube as examples. As plugins are
 * basically isolated except for signals, there must be a way for them
 * to signal which other plugins might run. This is the role of "abilities".
 * Each plugin specifies what it needs(to have a custom renderer, or to
 * record screen). If there is already a running plugin which has overridden
 * these, * the output->activate_plugin() function will fail and thus
 * the plugin will know that it shouldn't run */

/* base abilities */
enum wayfire_grab_abilities
{
    WF_ABILITY_CHANGE_VIEW_GEOMETRY = 1 << 0,
    WF_ABILITY_RECORD_SCREEN        = 1 << 1,
    WF_ABILITY_CUSTOM_RENDERING     = 1 << 2,
    WF_ABILITY_GRAB_INPUT           = 1 << 3
};

/* some more "high-level" ability names */
#define WF_ABILITY_CONTROL_WM (WF_ABILITY_CHANGE_VIEW_GEOMETRY | \
        WF_ABILITY_CUSTOM_RENDERING | WF_ABILITY_GRAB_INPUT)

#define WF_ABILITY_ALL (WF_ABILITY_RECORD_SCREEN | WF_ABILITY_CUSTOM_RENDERING | \
        WF_ABILITY_CHANGE_VIEW_GEOMETRY | WF_ABILITY_GRAB_INPUT)

#define WF_ABILITY_NONE (0)

/* owners are used to acquire screen grab and to activate */
struct wayfire_grab_interface_t {
    private:
        bool grabbed = false;
        friend class input_manager;

    public:
    owner_t name;
    uint32_t abilities_mask = 0;
    wf::output_t *output;

    wayfire_grab_interface_t(wf::output_t *_output) : output(_output) {}

    bool grab();
    bool is_grabbed();
    void ungrab();

    struct {
        struct {
            std::function<void(wlr_event_pointer_axis*)> axis;
            std::function<void(uint32_t, uint32_t)> button; // button, state
            std::function<void(int32_t, int32_t)> motion;
            std::function<void(wlr_event_pointer_motion*)> relative_motion;
        } pointer;

        struct {
            std::function<void(uint32_t,uint32_t)> key; // button, state
            std::function<void(uint32_t,uint32_t)> mod; // modifier, state
        } keyboard;

        struct {
            std::function<void(int32_t, int32_t, int32_t)> down; // id, x, y
            std::function<void(int32_t)> up; // id
            std::function<void(int32_t, int32_t, int32_t)> motion; // id, x, y
        } touch;

        /* each plugin might be deactivated forcefully, for example when the desktop is locked.
         * Plugins MUST honor this signal and exit their grabs/renderers immediately
         *
         * Note this is emitted for all active plugins, not only those that have grabs */
        std::function<void()> cancel;
    } callbacks;
};

using wayfire_grab_interface = wayfire_grab_interface_t*;
class wayfire_plugin_t {
    public:
        /* the output this plugin is running on
         * each output has an instance of each plugin, so that the two monitors act independently
         * in the future there should be an option for mirroring displays */
        wf::output_t *output;

        wayfire_grab_interface grab_interface;

        /* should read configuration data, attach hooks / keybindings, etc */
        virtual void init(wayfire_config *config) = 0;
        virtual void fini();

        /* override this function if the plugin cannot be safely unloaded at runtime -
         * it will remain active even if it is removed from the config file. Please note
         * however that they should still provide fini() and free their data - they will
         * be destroyed once their output has been destroyed. However, non-unloadable plugins
         * are generally destroyed after all unloadable ones */
        virtual bool is_unloadable() { return true; }

        /* used to determine if the plugin provides some special features like workspace implementations */
        virtual bool is_internal() { return false; }

        /* grab_interface is already freed in destructor, so you might want to use fini() */
        virtual ~wayfire_plugin_t();

        /* used by the plugin loader, shouldn't be modified by plugins */
        bool dynamic = false;
        void *handle;
};

/* each dynamic plugin should have the symbol get_plugin_instance() which returns
 * an instance of the plugin */
typedef wayfire_plugin_t *(*get_plugin_instance_t)();
#define GetTuple(x,y,t) auto x = std::get<0>(t); \
                        auto y = std::get<1>(t)
#endif
