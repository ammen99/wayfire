#ifndef FIRE_H
#define FIRE_H

#include <compositor.h>
#include "view.hpp"
#include "plugin.hpp"
#include <vector>
#include <map>

using output_callback_proc = std::function<void(wayfire_output *)>;

enum wayfire_gesture_type {
    GESTURE_NONE,
    GESTURE_SWIPE,
    GESTURE_PINCH
};

#define GESTURE_DIRECTION_LEFT (1 << 0)
#define GESTURE_DIRECTION_RIGHT (1 << 1)
#define GESTURE_DIRECTION_UP (1 << 2)
#define GESTURE_DIRECTION_DOWN (1 << 3)
#define GESTURE_DIRECTION_IN (1 << 4)
#define GESTURE_DIRECTION_OUT (1 << 5)

struct wayfire_touch_gesture {
    wayfire_gesture_type type;
    uint32_t direction;
    int finger_count;
};

struct wf_gesture_recognizer;

struct input_manager {
    private:
        std::unordered_set<wayfire_grab_interface> active_grabs;

        weston_keyboard_grab kgrab;
        weston_pointer_grab pgrab;
        weston_touch_grab tgrab;

        wf_gesture_recognizer *gr;
        bool touch_grabbed;

        void handle_gesture(wayfire_touch_gesture g);

        int gesture_id;
        struct wf_gesture_listener {
            wayfire_touch_gesture gesture;
            touch_callback* call;
        };
        std::map<int, wf_gesture_listener> gesture_listeners;
    public:
        input_manager();
        void grab_input(wayfire_grab_interface);
        void ungrab_input(wayfire_grab_interface);

        void propagate_pointer_grab_axis  (weston_pointer *ptr, weston_pointer_axis_event *ev);
        void propagate_pointer_grab_motion(weston_pointer *ptr, weston_pointer_motion_event *ev);
        void propagate_pointer_grab_button(weston_pointer *ptr, uint32_t button, uint32_t state);

        void propagate_keyboard_grab_key(weston_keyboard *kdb, uint32_t key, uint32_t state);
        void propagate_keyboard_grab_mod(weston_keyboard *kbd, uint32_t depressed,
                                         uint32_t locked, uint32_t latched, uint32_t group);

        void propagate_touch_down(weston_touch*, uint32_t, int32_t, wl_fixed_t, wl_fixed_t);
        void propagate_touch_up(weston_touch*, uint32_t, int32_t);
        void propagate_touch_motion(weston_touch*, uint32_t, int32_t, wl_fixed_t, wl_fixed_t);

        void end_grabs();

        weston_binding *add_key(uint32_t mod, uint32_t key, key_callback *, wayfire_output *output);
        weston_binding *add_button(uint32_t mod, uint32_t button, button_callback *, wayfire_output *output);

        /* we take only gesture type and finger count into account,
         * we send for all possible directions */
        int add_gesture(const wayfire_touch_gesture& gesture, touch_callback* callback);
        void rem_gesture(int id);
};

class wayfire_core {
        friend struct plugin_manager;

        wayfire_config *config;

        wayfire_output *active_output;
        std::map<uint32_t, wayfire_output *> outputs;
        std::map<weston_view *, wayfire_view> views;

        void configure(wayfire_config *config);
        void (*weston_renderer_repaint) (weston_output *output, pixman_region32_t *damage);

        int times_wake = 0;
    public:
        std::string wayland_display, xwayland_display;

        input_manager *input;

        struct {
            wl_client *client;
            wl_resource *resource;
        } wf_shell;

        weston_compositor *ec;
        void init(weston_compositor *ec, wayfire_config *config);
        void wake();
        void sleep();
        void refocus_active_output_active_view();

        void hijack_renderer();
        void weston_repaint(weston_output *output, pixman_region32_t *damage);

        weston_seat *get_current_seat();

        void add_view(weston_desktop_surface *);
        wayfire_view find_view(weston_view *);
        wayfire_view find_view(weston_desktop_surface *);
        wayfire_view find_view(weston_surface *);
        /* Only removes the view from the "database".
         * Use only when view is already destroyed and detached from output */
        void erase_view(wayfire_view view);

        /* brings the view to the top
         * and also focuses its output */
        void focus_view(wayfire_view win, weston_seat *seat);
        void close_view(wayfire_view win);
        void move_view_to_output(wayfire_view v, wayfire_output *old, wayfire_output *new_output);

        void add_output(weston_output *output);
        wayfire_output *get_output(weston_output *output);

        void focus_output(wayfire_output *o);
        void remove_output(wayfire_output *o);

        wayfire_output *get_active_output();
        wayfire_output *get_next_output(wayfire_output *output);
        size_t          get_num_outputs();

        void for_each_output(output_callback_proc);

        void run(const char *command);

        int vwidth, vheight;

        std::string shadersrc, plugin_path, plugins;
        bool run_panel;

        weston_compositor_backend backend;
};

extern wayfire_core *core;
#endif
