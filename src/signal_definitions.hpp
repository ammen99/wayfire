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

/* same as both change_viewport_request and change_viewport_notify */
struct change_viewport_signal : public signal_data {
    int nvx, nvy;
};

#endif

