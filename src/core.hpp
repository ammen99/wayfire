#ifndef FIRE_H
#define FIRE_H

#include <libweston-3/compositor.h>
#include "view.hpp"
#include "plugin.hpp"
#include <vector>
#include <map>

using output_callback_proc = std::function<void(wayfire_output*)>;

struct input_manager {
    private:
        std::unordered_set<wayfire_grab_interface> active_grabs;

        weston_keyboard_grab kgrab;
        weston_pointer_grab pgrab;

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

        void end_grabs();

        /* TODO: support touch */
        weston_binding* add_key(uint32_t mod, uint32_t key, key_callback*, wayfire_output *output);
        weston_binding* add_button(uint32_t mod, uint32_t button, button_callback*, wayfire_output *output);
};

class wayfire_core {
    friend struct plugin_manager;

    wayfire_config *config;

    wayfire_output *active_output;
    std::map<uint32_t, wayfire_output*> outputs;
    std::map<weston_view*, wayfire_view> views;

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

    void add_view(weston_desktop_surface*);
    wayfire_view find_view(weston_view*);
    wayfire_view find_view(weston_desktop_surface*);
    wayfire_view find_view(weston_surface*);
    /* Only removes the view from the "database".
     * Use only when view is already destroyed and detached from output */
    void erase_view(wayfire_view view);

    /* brings the view to the top
     * and also focuses its output */
    void focus_view(wayfire_view win, weston_seat* seat);
    void close_view(wayfire_view win);
    void move_view_to_output(wayfire_view v, wayfire_output *old, wayfire_output *new_output);

    void add_output(weston_output *output);
    wayfire_output* get_output(weston_output *output);

    void focus_output(wayfire_output* o);
    void remove_output(wayfire_output* o);

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
