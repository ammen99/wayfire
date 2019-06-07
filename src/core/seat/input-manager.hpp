#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <map>
#include <vector>
#include <chrono>

#include "seat.hpp"
#include "cursor.hpp"
#include "plugin.hpp"
#include "view.hpp"

extern "C"
{
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_seat.h>
struct wlr_drag_icon;
}

struct wf_gesture_recognizer;
struct wlr_seat;

struct wf_touch;
struct wf_keyboard;

enum wf_binding_type
{
    WF_BINDING_KEY,
    WF_BINDING_BUTTON,
    WF_BINDING_AXIS,
    WF_BINDING_TOUCH,
    WF_BINDING_GESTURE,
    WF_BINDING_ACTIVATOR
};

struct wf_binding
{
    wf_option value;
    wf_binding_type type;
    wf::output_t *output;

    union {
        void *raw;
        key_callback *key;
        axis_callback *axis;
        touch_callback *touch;
        button_callback *button;
        gesture_callback *gesture;
        activator_callback *activator;
    } call;
};

using wf_binding_ptr = std::unique_ptr<wf_binding>;

/* TODO: most probably we want to split even more of input_manager's functionality into
 * wf_keyboard, wf_cursor and wf_touch */
class input_manager
{
    private:
      wf::plugin_grab_interface_t* active_grab = nullptr;
        bool session_active = true;

        wf::wl_listener_wrapper input_device_created, request_start_drag, start_drag,
                                request_set_cursor, request_set_selection,
                                request_set_primary_selection;
        wf::wl_idle_call idle_update_cursor;

        wf::signal_callback_t config_updated;

        int gesture_id;

        std::map<wf_binding_type, std::vector<std::unique_ptr<wf_binding>>> bindings;
        using binding_criteria = std::function<bool(wf_binding*)>;
        void rem_binding(binding_criteria criteria);

        bool is_touch_enabled();

        void create_seat();

        // returns the surface under the given global coordinates
        // if no such surface (return NULL), lx and ly are undefined
        wf::surface_interface_t* input_surface_at(int x, int y,
            int& lx, int& ly);

        void validate_drag_request(wlr_seat_request_start_drag_event *ev);
        void update_drag_icon();

        /* TODO: move this in a wf_keyboard struct,
         * This might not work with multiple keyboards */
        uint32_t mod_binding_key = 0; /* The keycode which triggered the modifier binding */
        std::chrono::steady_clock::time_point mod_binding_start;
        std::vector<std::function<void()>> match_keys(uint32_t mods, uint32_t key, uint32_t mod_binding_key = 0);

        wayfire_view keyboard_focus;
        wf::signal_callback_t surface_map_state_changed;

    public:

        input_manager();
        ~input_manager();

        void handle_new_input(wlr_input_device *dev);
        void handle_input_destroyed(wlr_input_device *dev);

        void update_cursor_position(uint32_t time_msec, bool real_update = true);
        void update_cursor_focus(wf::surface_interface_t *surface, int lx, int ly);
        void set_touch_focus(wf::surface_interface_t *surface, uint32_t time, int id, int lx, int ly);

        wl_client *exclusive_client = NULL;

        wlr_seat *seat = nullptr;
        std::unique_ptr<wf_cursor> cursor;

        wf::surface_interface_t* cursor_focus = nullptr, *touch_focus = nullptr;

        std::unique_ptr<wf_touch> our_touch;
        std::unique_ptr<wf_drag_icon> drag_icon;

        int pointer_count = 0, touch_count = 0;
        void update_capabilities();

        std::vector<std::unique_ptr<wf_keyboard>> keyboards;
        std::vector<std::unique_ptr<wf_input_device_internal>> input_devices;

        void set_keyboard_focus(wayfire_view view, wlr_seat *seat);

        bool grab_input(wf::plugin_grab_interface_t*);
        void ungrab_input();
        bool input_grabbed();

        bool can_focus_surface(wf::surface_interface_t *surface);
        void set_exclusive_focus(wl_client *client);

        void toggle_session();
        uint32_t get_modifiers();

        void free_output_bindings(wf::output_t *output);

        void handle_pointer_axis  (wlr_event_pointer_axis *ev);
        void handle_pointer_motion(wlr_event_pointer_motion *ev);
        void handle_pointer_motion_absolute(wlr_event_pointer_motion_absolute *ev);
        bool handle_pointer_button(wlr_event_pointer_button *ev);
        void handle_pointer_frame();

        void handle_pointer_swipe_begin(wlr_event_pointer_swipe_begin *ev);
        void handle_pointer_swipe_update(wlr_event_pointer_swipe_update *ev);
        void handle_pointer_swipe_end(wlr_event_pointer_swipe_end *ev);
        void handle_pointer_pinch_begin(wlr_event_pointer_pinch_begin *ev);
        void handle_pointer_pinch_update(wlr_event_pointer_pinch_update *ev);
        void handle_pointer_pinch_end(wlr_event_pointer_pinch_end *ev);

        bool handle_keyboard_key(uint32_t key, uint32_t state);
        void handle_keyboard_mod(uint32_t key, uint32_t state);

        void handle_touch_down  (uint32_t time, int32_t id, int32_t x, int32_t y);
        void handle_touch_motion(uint32_t time, int32_t id, int32_t x, int32_t y);
        void handle_touch_up    (uint32_t time, int32_t id);

        void handle_gesture(wf_touch_gesture g);

        void check_touch_bindings(int32_t x, int32_t y);

        wf_binding* new_binding(wf_binding_type type, wf_option value, wf::output_t *output, void *callback);
        void rem_binding(void *callback);
        void rem_binding(wf_binding *binding);
};

#endif /* end of include guard: INPUT_MANAGER_HPP */
