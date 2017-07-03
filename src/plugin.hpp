#ifndef PLUGIN_H
#define PLUGIN_H

#include <libweston-3/compositor.h>
#include <unordered_set>
#include <functional>
#include <memory>

using std::string;

struct wayfire_touch_gesture;
using key_callback = std::function<void(weston_keyboard*, uint32_t)>;
using button_callback = std::function<void(weston_pointer*, uint32_t)>;
using touch_callback = std::function<void(weston_touch*, wl_fixed_t, wl_fixed_t)>;
using touch_gesture_callback = std::function<void(wayfire_touch_gesture*)>;

class wayfire_output;
class wayfire_config;
using owner_t = string;

/* owners are used to acquire screen grab and to activate */
struct wayfire_grab_interface_t {
    private:
        bool grabbed = false;
        friend struct input_manager;

    public:
    owner_t name;
    std::unordered_set<owner_t> compat;
    bool compatAll = true;
    wayfire_output *output;

    wayfire_grab_interface_t(wayfire_output *_output) : output(_output) {}

    bool grab();
    void ungrab();

    struct {
        struct {
            std::function<void(weston_pointer*,weston_pointer_axis_event*)> axis;
            std::function<void(weston_pointer*,uint32_t, uint32_t)> button; // button, state
            std::function<void(weston_pointer*,weston_pointer_motion_event*)> motion;
        } pointer;

        struct {
            std::function<void(weston_keyboard*,uint32_t,uint32_t)> key; // button, state
            std::function<void(weston_keyboard*,uint32_t,uint32_t,uint32_t,uint32_t)> mod; // depressed, locks, latched, group
        } keyboard;

        struct {
            std::function<void(weston_touch*, int32_t, wl_fixed_t, wl_fixed_t)> down;
            std::function<void(weston_touch*, int32_t)> up;
            std::function<void(weston_touch*, int32_t, wl_fixed_t, wl_fixed_t)> motion;
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
