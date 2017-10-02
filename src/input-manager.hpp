#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <compositor.h>
#include <unordered_set>
#include <map>

#include "plugin.hpp"


struct wf_gesture_recognizer;

class input_manager
{
    private:
        std::unordered_set<wayfire_grab_interface> active_grabs;

        weston_keyboard_grab kgrab;
        weston_pointer_grab pgrab;
        weston_touch_grab tgrab;

        wf_gesture_recognizer *gr;

        void handle_gesture(wayfire_touch_gesture g);

        int gesture_id;
        struct wf_gesture_listener {
            wayfire_touch_gesture gesture;
            touch_gesture_callback* call;
            wayfire_output *output;
        };

        std::map<int, wf_gesture_listener> gesture_listeners;
        struct touch_listener {
            uint32_t mod;
            touch_callback* call;
            wayfire_output *output;
        };
        std::map<int, touch_listener> touch_listeners;

        bool is_touch_enabled();

    public:
        input_manager();
        void grab_input(wayfire_grab_interface);
        void ungrab_input(wayfire_grab_interface);
        bool input_grabbed();

        void propagate_pointer_grab_axis  (weston_pointer *ptr, weston_pointer_axis_event *ev);
        void propagate_pointer_grab_motion(weston_pointer *ptr, weston_pointer_motion_event *ev);
        void propagate_pointer_grab_button(weston_pointer *ptr, uint32_t button, uint32_t state);

        void propagate_keyboard_grab_key(weston_keyboard *kdb, uint32_t key, uint32_t state);
        void propagate_keyboard_grab_mod(weston_keyboard *kbd, uint32_t depressed,
                                         uint32_t locked, uint32_t latched, uint32_t group);

        void propagate_touch_down(weston_touch*, uint32_t, int32_t, wl_fixed_t, wl_fixed_t);
        void grab_send_touch_down(weston_touch*, int32_t, wl_fixed_t, wl_fixed_t);
        void propagate_touch_up(weston_touch*, uint32_t, int32_t);
        void grab_send_touch_up(weston_touch*, int32_t);
        void propagate_touch_motion(weston_touch*, uint32_t, int32_t, wl_fixed_t, wl_fixed_t);
        void grab_send_touch_motion(weston_touch*, int32_t, wl_fixed_t, wl_fixed_t);

        void check_touch_bindings(weston_touch*, wl_fixed_t sx, wl_fixed_t sy);

        void end_grabs();

        weston_binding *add_key(uint32_t mod, uint32_t key, key_callback *, wayfire_output *output);
        weston_binding *add_button(uint32_t mod, uint32_t button,
                button_callback *, wayfire_output *output);

        int add_touch(uint32_t mod, touch_callback*, wayfire_output *output);
        void rem_touch(int32_t id);

        int add_gesture(const wayfire_touch_gesture& gesture,
                touch_gesture_callback* callback, wayfire_output *output);
        void rem_gesture(int id);
};

#endif /* end of include guard: INPUT_MANAGER_HPP */
