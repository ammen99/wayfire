#ifndef SIGNAL_DEFINITIONS_HPP
#define SIGNAL_DEFINITIONS_HPP

#include "output.hpp"

struct output_created_signal : public signal_data {
    wayfire_output* output;
};

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

/* same as both change_viewport_request and change_viewport_notify */
struct change_viewport_signal : public signal_data {
    int old_vx, old_vy;
    int new_vx, new_vy;
};
using change_viewport_notify = change_viewport_signal;

struct move_request_signal : public signal_data {
    weston_pointer *ptr;
};

struct resize_request_signal : public signal_data {
    weston_pointer *ptr;
    uint32_t edges;
};
#endif

