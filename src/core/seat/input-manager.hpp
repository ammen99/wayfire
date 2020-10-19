#ifndef INPUT_MANAGER_HPP
#define INPUT_MANAGER_HPP

#include <map>
#include <vector>
#include <chrono>

#include "bindings-repository.hpp"
#include "seat.hpp"
#include "cursor.hpp"
#include "pointer.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/view.hpp"
#include "wayfire/core.hpp"
#include "wayfire/signal-definitions.hpp"
#include <wayfire/option-wrapper.hpp>

namespace wf
{
class touch_interface_t;
namespace touch
{
class gesture_action_t;
}
}

struct wf_keyboard;

enum wf_locked_mods
{
    WF_KB_NUM  = 1 << 0,
    WF_KB_CAPS = 1 << 1,
};

/* TODO: most probably we want to split even more of input_manager's functionality
 * into
 * wf_keyboard, wf_cursor and wf_touch */
class input_manager
{
  private:
    wf::wl_listener_wrapper input_device_created, request_start_drag, start_drag,
        request_set_cursor, request_set_selection,
        request_set_primary_selection;
    wf::wl_idle_call idle_update_cursor;

    wf::signal_callback_t config_updated;

    void create_seat();

    void validate_drag_request(wlr_seat_request_start_drag_event *ev);
    std::chrono::steady_clock::time_point mod_binding_start;

    wf::signal_callback_t output_added;

  public:
    void refresh_device_mappings();

    /* TODO: move this in a wf_keyboard struct,
     * This might not work with multiple keyboards */
    uint32_t mod_binding_key = 0; /* The keycode which triggered the modifier binding
                                   * */

    input_manager();
    ~input_manager();

    void handle_new_input(wlr_input_device *dev);
    void handle_input_destroyed(wlr_input_device *dev);

    void update_drag_icon();

    void set_touch_focus(wf::surface_interface_t *surface, uint32_t time, int id,
        wf::pointf_t local);

    wf::plugin_grab_interface_t *active_grab = nullptr;
    wl_client *exclusive_client = NULL;

    wlr_seat *seat = nullptr;
    std::unique_ptr<wf_cursor> cursor;
    std::unique_ptr<wf::pointer_t> lpointer;
    std::unique_ptr<wf::touch_interface_t> touch;

    wayfire_view keyboard_focus;

    std::unique_ptr<wf_drag_icon> drag_icon;
    bool drag_active = false;
    wf::wl_listener_wrapper on_drag_end;

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
    // returns the surface under the given global coordinates
    // if no such surface (return NULL), lx and ly are undefined
    wf::surface_interface_t *input_surface_at(wf::pointf_t global,
        wf::pointf_t& local);

    uint32_t get_modifiers();
    uint32_t locked_mods = 0;

    bool handle_keyboard_key(uint32_t key, uint32_t state);
    void handle_keyboard_mod(uint32_t key, uint32_t state);

    /** @return the bindings for the active output */
    wf::bindings_repository_t& get_active_bindings();
};

template<class EventType>
void emit_device_event_signal(std::string event_name, EventType *event)
{
    wf::input_event_signal<EventType> data;
    data.event = event;
    wf::get_core().emit_signal(event_name, &data);
}

#endif /* end of include guard: INPUT_MANAGER_HPP */
