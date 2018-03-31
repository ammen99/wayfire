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
using focus_view_signal      = _view_signal;
using view_set_parent_signal = _view_signal;
using move_request_signal    = _view_signal;
using resize_request_signal  = _view_signal;

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

/* the view-fullscreen-request signal is sent on two ocassions:
 * 1. The app requests to be fullscreened
 * 2. Some plugin requests the view to be unfullscreened
 * callbacks for this signal can differentiate between the two cases
 * in the following way: when in case 1. then view->fullscreen != signal_data->state,
 * i.e the state hasn't been applied already. However, when some plugin etc.
 * wants to use this signal, then it should apply the state in advance */
using view_maximized_signal = _view_state_signal;
using view_fullscreen_signal = _view_state_signal;



/* same as both change_viewport_request and change_viewport_notify */
struct change_viewport_signal : public signal_data
{
    int old_vx, old_vy;
    int new_vx, new_vy;
};
using change_viewport_notify = change_viewport_signal;


/* sent when the workspace implementation actually reserves the workarea */
struct reserved_workarea_signal : public signal_data
{
    /* enum from wayfire-shell */
    uint32_t position;
    uint32_t width;
    uint32_t height;
};

#endif

