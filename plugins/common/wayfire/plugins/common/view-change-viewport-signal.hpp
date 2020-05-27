#pragma once

#include <wayfire/signal-definitions.hpp>

/**
 * Each plugin which changes the view's workspace should emit this signal.
 */
struct view_change_viewport_signal : public _view_signal
{
    wf::point_t from, to;

    /**
     * from and to maybe null
     * if old_viewport_invalid is false
     * as it should trigger a full re-read
     */
    bool old_viewport_invalid = true;
};
