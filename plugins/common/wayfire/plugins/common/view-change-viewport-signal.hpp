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
     * if recheck_all is true
     */
    bool old_viewport_invalid = true;
};
