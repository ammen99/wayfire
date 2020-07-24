#ifndef WOBBLY_SIGNAL_HPP
#define WOBBLY_SIGNAL_HPP

#include <wayfire/signal-definitions.hpp>

enum wobbly_event
{
    WOBBLY_EVENT_GRAB     = (1 << 0),
    WOBBLY_EVENT_MOVE     = (1 << 1),
    WOBBLY_EVENT_END      = (1 << 2),
    WOBBLY_EVENT_ACTIVATE = (1 << 3),
};

enum wobbly_corner
{
    WOBBLY_CORNER_TL = 0,
    WOBBLY_CORNER_TR = 1,
    WOBBLY_CORNER_BL = 2,
    WOBBLY_CORNER_BR = 3,
};

struct wobbly_signal : public wf::_view_signal
{
    wobbly_event events;
    int grab_x, grab_y; // set only with events & WOBBLY_EVENT_GRAB
};

/**
 * Start wobblying when the view is being grabbed, for ex. when moving it
 */
inline void start_wobbly(wayfire_view view, int grab_x, int grab_y)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = WOBBLY_EVENT_GRAB;
    sig.grab_x = grab_x;
    sig.grab_y = grab_y;

    view->get_output()->emit_signal("wobbly-event", &sig);
}

/**
 * Release the wobbly grab
 */
inline void end_wobbly(wayfire_view view)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = WOBBLY_EVENT_END;
    view->get_output()->emit_signal("wobbly-event", &sig);
}

/**
 * Indicate that the grab has moved (i.e cursor moved, touch moved, etc.)
 */
inline void move_wobbly(wayfire_view view, int grab_x, int grab_y)
{
    wobbly_signal sig;
    sig.view   = view;
    sig.events = WOBBLY_EVENT_MOVE;
    sig.grab_x = grab_x;
    sig.grab_y = grab_y;
    view->get_output()->emit_signal("wobbly-event", &sig);
}

/**
 * Temporarily activate wobbly on the view.
 * This is useful when animating some transition like fullscreening, tiling, etc.
 */
inline void activate_wobbly(wayfire_view view)
{
    if (!view->get_transformer("wobbly"))
    {
        wobbly_signal sig;
        sig.view   = view;
        sig.events = WOBBLY_EVENT_ACTIVATE;
        view->get_output()->emit_signal("wobbly-event", &sig);
    }
}

#endif /* end of include guard: WOBBLY_SIGNAL_HPP */
