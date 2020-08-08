#ifndef CURSOR_HPP
#define CURSOR_HPP

#include "seat.hpp"
#include "wayfire/plugin.hpp"

extern "C"
{
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
}


struct wf_cursor
{
    wf_cursor();
    ~wf_cursor();

    void attach_device(wlr_input_device *device);
    void detach_device(wlr_input_device *device);

    /**
     * Set the cursor image from a wlroots event.
     * @param validate_request Whether to validate the request against the
     * currently focused pointer surface, or not.
     */
    void set_cursor(wlr_seat_pointer_request_set_cursor_event *ev,
        bool validate_request);
    void set_cursor(std::string name);

    void hide_cursor();

    /* Move the cursor to the given point */
    void warp_cursor(wf::pointf_t point);
    wf::pointf_t get_cursor_position();

    void init_xcursor();
    void setup_listeners();
    void load_xcursor_scale(float scale);

    wf::wl_listener_wrapper on_button, on_motion, on_motion_absolute, on_axis,

        on_swipe_begin, on_swipe_update, on_swipe_end,
        on_pinch_begin, on_pinch_update, on_pinch_end,

        on_tablet_tip, on_tablet_axis,
        on_tablet_button, on_tablet_proximity,
        on_frame;

    wf::signal_callback_t config_reloaded;

    wlr_cursor *cursor = NULL;
    wlr_xcursor_manager *xcursor = NULL;
};

#endif /* end of include guard: CURSOR_HPP */
