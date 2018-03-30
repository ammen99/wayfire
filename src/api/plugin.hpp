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
#include <memory>

/* when creating a signal there should be the definition of the derived class */
struct signal_data { };
using signal_callback_t = std::function<void(signal_data*)>;

/* effect hooks are called after main rendering */
using effect_hook_t = std::function<void()>;

#define MODIFIER_CTRL  (1 << 0)
#define MODIFIER_ALT   (1 << 1)
#define MODIFIER_SUPER (1 << 2)
#define MODIFIER_SHIFT (1 << 3)

struct wayfire_touch_gesture;
using key_callback = std::function<void(uint32_t)>;
using button_callback = std::function<void(uint32_t)>;
using touch_callback = std::function<void(wlr_touch*, wl_fixed_t, wl_fixed_t)>;
using touch_gesture_callback = std::function<void(wayfire_touch_gesture*)>;

enum wayfire_gesture_type {
    GESTURE_NONE,
    GESTURE_SWIPE,
    GESTURE_EDGE_SWIPE,
    GESTURE_PINCH
};

#define GESTURE_DIRECTION_LEFT (1 << 0)
#define GESTURE_DIRECTION_RIGHT (1 << 1)
#define GESTURE_DIRECTION_UP (1 << 2)
#define GESTURE_DIRECTION_DOWN (1 << 3)
#define GESTURE_DIRECTION_IN (1 << 4)
#define GESTURE_DIRECTION_OUT (1 << 5)

struct wayfire_touch_gesture
{
    wayfire_gesture_type type;
    uint32_t direction;
    int finger_count;
};

class wayfire_output;
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
    wayfire_output *output;

    wayfire_grab_interface_t(wayfire_output *_output) : output(_output) {}

    bool grab();
    bool is_grabbed();
    void ungrab();

    struct {
        struct {
            std::function<void(wlr_event_pointer_axis*)> axis;
            std::function<void(uint32_t, uint32_t)> button; // button, state
            std::function<void()> motion;
        } pointer;

        struct {
            std::function<void(uint32_t,uint32_t)> key; // button, state
            std::function<void(uint32_t,uint32_t)> mod; // modifier, state
        } keyboard;

        struct {
            std::function<void(wlr_touch*, int32_t, wl_fixed_t, wl_fixed_t)> down;
            std::function<void(wlr_touch*, int32_t)> up;
            std::function<void(wlr_touch*, int32_t, wl_fixed_t, wl_fixed_t)> motion;
        } touch;

        std::function<void()> cancel; // called when we must stop current grab
    } callbacks;
};

using wayfire_grab_interface = wayfire_grab_interface_t*;
class wayfire_plugin_t {
    public:
        /* the output this plugin is running on
         * each output has an instance of each plugin, so that the two monitors act independently
         * in the future there should be an option for mirroring displays */
        wayfire_output *output;

        wayfire_grab_interface grab_interface;

        /* should read configuration data, attach hooks / keybindings, etc */
        virtual void init(wayfire_config *config) = 0;

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

/* TODO: move elsewhere */
/* render hooks are used when a plugin requests to draw the whole desktop on their own
 * example plugin is cube */
using render_hook_t = std::function<void()>;

#define GetTuple(x,y,t) auto x = std::get<0>(t); \
                        auto y = std::get<1>(t)

float GetProgress(float start, float end, float current_step, float max_steps);
#endif
