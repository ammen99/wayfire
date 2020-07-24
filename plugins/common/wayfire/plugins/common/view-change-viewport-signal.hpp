#pragma once

#include <wayfire/signal-definitions.hpp>

/**
 * Each plugin which changes the view's workspace should emit this signal.
 */
struct view_change_viewport_signal : public wf::_view_signal
{
    wf::point_t from, to;

    /**
     * Indicates whether the old viewport is known.
     * If false, then the `from` field should be ignored.
     */
    bool old_viewport_invalid = true;
};
