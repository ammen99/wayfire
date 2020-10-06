#ifndef WF_SEAT_TABLET_HPP
#define WF_SEAT_TABLET_HPP

#include <wayfire/util.hpp>
#include "seat.hpp"

namespace wf
{
struct tablet_tool_t
{
    /**
     * Create a new tablet tool.
     * It will automatically free its memory once the wlr object is destroyed.
     */
    tablet_tool_t(wlr_tablet_tool *tool, wlr_tablet_v2_tablet *tablet);
    ~tablet_tool_t();

    wlr_tablet_tool *tool;
    wlr_tablet_v2_tablet_tool *tool_v2;

    /**
     * Called whenever a refocus of the tool is necessary
     */
    void update_tool_position();

    /** Set the proximity surface */
    void set_focus(wf::surface_interface_t *surface);

    /**
     * Send the axis updates directly.
     * Only the position is handled separately.
     */
    void passthrough_axis(wlr_event_tablet_tool_axis *ev);

    /**
     * Called whenever a tip occurs for this tool
     */
    void handle_tip(wlr_event_tablet_tool_tip *ev);

    /** Handle a button event */
    void handle_button(wlr_event_tablet_tool_button *ev);

    /** Set proximity state */
    void handle_proximity(wlr_event_tablet_tool_proximity *ev);

  private:
    wf::wl_listener_wrapper on_destroy, on_set_cursor;
    wf::wl_listener_wrapper on_tool_v2_destroy;
    signal_callback_t on_surface_map_state_changed;
    wf::signal_callback_t on_views_updated;

    /** Tablet that this tool belongs to */
    wlr_tablet_v2_tablet *tablet_v2;

    /** Surface where the tool is in */
    wf::surface_interface_t *proximity_surface = nullptr;
    /** Surface where the tool was grabbed */
    wf::surface_interface_t *grabbed_surface = nullptr;

    double tilt_x = 0.0;
    double tilt_y = 0.0;

    /* A tablet tool is active if it has a proximity_in
     * event but no proximity_out */
    bool is_active = false;
};

struct tablet_t : public wf_input_device_internal
{
    /**
     * Create a new tablet tool for the given cursor.
     */
    tablet_t(wlr_cursor *cursor, wlr_input_device *tool);
    virtual ~tablet_t();

    /** Handle a tool tip event */
    void handle_tip(wlr_event_tablet_tool_tip *ev);
    /** Handle an axis event */
    void handle_axis(wlr_event_tablet_tool_axis *ev);
    /** Handle a button event */
    void handle_button(wlr_event_tablet_tool_button *ev);
    /** Handle a proximity event */
    void handle_proximity(wlr_event_tablet_tool_proximity *ev);

  private:
    wlr_tablet *handle;
    wlr_tablet_v2_tablet *tablet_v2;
    wlr_cursor *cursor;

    /**
     * Get the wayfire tool associated with the wlr tool.
     * The wayfire tool will be created if it doesn't exist yet.
     */
    tablet_tool_t *ensure_tool(wlr_tablet_tool *tool);
};
}


#endif /* end of include guard: WF_SEAT_TABLET_HPP */
