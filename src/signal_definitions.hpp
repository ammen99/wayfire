#ifndef SIGNAL_DEFINITIONS_HPP
#define SIGNAL_DEFINITIONS_HPP

#include "output.hpp"

struct create_view_signal : public signal_data {
    wayfire_view created_view;

    create_view_signal(wayfire_view view) {
        created_view = view;
    }
};

struct destroy_view_signal : public signal_data {
    wayfire_view destroyed_view;

    destroy_view_signal(wayfire_view view) {
        destroyed_view = view;
    }
};

struct view_maximized_signal : public signal_data {
    wayfire_view view;
    bool state;
};

/* the view-fullscreen-request signal is sent on two ocassions:
 * 1. The app requests to be fullscreened
 * 2. Some plugin requests the view to be unfullscreened
 * callbacks for this signal can differentiate between the two cases
 * in the following way: when in case 1. then view->fullscreen != signal_data->state,
 * i.e the state hasn't been applied already. However, when some plugin etc.
 * wants to use this signal, then it should apply the state in advance */
using view_fullscreen_signal = view_maximized_signal;

/* same as both change_viewport_request and change_viewport_notify */
struct change_viewport_signal : public signal_data {
    int old_vx, old_vy;
    int new_vx, new_vy;
};
using change_viewport_notify = change_viewport_signal;

struct move_request_signal : public signal_data {
    wayfire_view view;
    uint32_t serial;
};

struct resize_request_signal : public signal_data {
    wayfire_view view;
    uint32_t edges;
    uint32_t serial;
};
#endif

