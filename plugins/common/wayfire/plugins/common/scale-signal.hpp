/* definition of filters for scale plugin and activator */

#ifndef SCALE_SIGNALS_H
#define SCALE_SIGNALS_H

#include <wayfire/object.hpp>
#include <wayfire/view.hpp>
#include <vector>

/**
 * name: scale-filter
 * on: output
 * when: This signal is sent from the scale plugin whenever it is updating the
 *   list of views to display, for each view that is to be shown. A plugin can
 *   set hide to true to request this view not to be shown (hidden) while scale
 *   is active.
 *
 * TODO: possible alternative design: instead of emitting the signal for each
 * view separately, it could be emitted once, with a vector of views.
 */
struct scale_filter_signal : public wf::signal_data_t
{
    wayfire_view view;
    bool hide;
    scale_filter_signal(wayfire_view v, bool h) : view(v), hide(h)
    {}
};

/**
 * name: scale-end
 * on: output
 * when: When scale ended / is deactivated. A plugin performing filtering can
 *   connect to this signal to reset itself if filtering is not supposed to
 *   happen at the next activation of scale.
 * argument: unused
 */

/**
 * name: scale-activate
 * on: output
 * when: A plugin can emit this signal to request scale to be activated. If
 *   is already running, this causes it to refresh the list of views shown
 *   (this can be used by a filter plugin to update the filter).
 */
struct scale_activate_signal : public wf::signal_data_t
{
    /* should scale include views from all workspaces */
    bool all_workspaces = false;
    /* should change if scale if already running (if false, all_workspaces is
     * ignored if scale is already running) */
    bool change_all_workspaces = false;
};

#endif
