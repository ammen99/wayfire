#ifndef SIGNAL_DEFINITIONS_HPP
#define SIGNAL_DEFINITIONS_HPP

#include "output.hpp"

/* signal definitions */
/* convenience functions are provided to get some basic info from the signal */
struct _view_signal : public signal_data
{
    wayfire_view view;
};
wayfire_view get_signaled_view(signal_data *data);

using create_view_signal     = _view_signal;
using destroy_view_signal    = _view_signal;
using map_view_signal        = _view_signal;
using unmap_view_signal      = _view_signal;
using pre_unmap_view_signal  = _view_signal;

/* Indicates the view is no longer available, for ex. it has been minimized
 * or unmapped */
using view_disappeared_signal = _view_signal;

using focus_view_signal      = _view_signal;
using view_set_parent_signal = _view_signal;
using move_request_signal    = _view_signal;
using title_changed_signal   = _view_signal;
using app_id_changed_signal  = _view_signal;

struct resize_request_signal : public _view_signal
{
    uint32_t edges;
};

/* sent when the view geometry changes */
struct view_geometry_changed_signal : public _view_signal
{
    wf_geometry old_geometry;
};

struct _view_state_signal : public _view_signal
{
    bool state;
};
bool get_signaled_state(signal_data *data);

using view_maximized_signal = _view_state_signal;
using view_fullscreen_signal = _view_state_signal;

struct view_minimize_request_signal : public _view_state_signal
{
    /* If some plugin wants to delay the (un)minimize of the view, it needs to
     * listen for the view-minimize-request and set carried_out to true.
     * It is a hint to core that the action will be (or already was) performed
     * by a plugin, and minimized state shouldn't be further changed by core */
    bool carried_out = false;
};

/* same as both change_viewport_request and change_viewport_notify */
struct change_viewport_signal : public signal_data
{
    bool carried_out;
    std::tuple<int, int> old_viewport, new_viewport;
};
using change_viewport_notify = change_viewport_signal;

/* sent when the workspace implementation actually reserves the workarea */
struct reserved_workarea_signal : public signal_data
{
    wf_geometry old_workarea;
    wf_geometry new_workarea;
};

// TODO: this is a private signal, maybe we should hide it? */
struct _surface_map_state_changed_signal : public signal_data
{
    wf::surface_interface_t *surface;
};

/* Part 2: Signals from wf::output_layout_t */
struct _output_signal : public signal_data
{
    wf::output_t *output;
};

wf::output_t *get_signaled_output(signal_data *data);

using output_added_signal = _output_signal;
using output_removed_signal = _output_signal;

namespace wf
{
    class input_device_t;
    /* Used in the tablet-mode and lid-state signals from core */
    struct switch_signal : public signal_data
    {
        nonstd::observer_ptr<input_device_t> device;
        bool state;
    };
}

#endif

