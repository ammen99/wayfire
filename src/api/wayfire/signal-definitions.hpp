#ifndef SIGNAL_DEFINITIONS_HPP
#define SIGNAL_DEFINITIONS_HPP

#include "wayfire/view.hpp"
#include "wayfire/output.hpp"

/* signal definitions */
/* convenience functions are provided to get some basic info from the signal */
struct _view_signal : public wf::signal_data_t
{
    wayfire_view view;
};
wayfire_view get_signaled_view(wf::signal_data_t *data);

/**
 * attach-view is a signal emitted by an output. It is emitted whenever a view's
 * output is set to the given output.
 *
 * layer-attach-view is a signal emitted by an output. It is emitted whenever a
 * view is added to a layer on the output, where it was not in any layer on the
 * output previously.
 */
using attach_view_signal     = _view_signal;

/**
 * detach-view is a signal emitted by an output. It is emitted whenever a view's
 * output is no longer the given output, or the view is about to be destroyed.
 *
 * layer-detach-view is a signal emitted by an output. It is emitted whenever a
 * view is no longer in a layer of the given output.
 */
using detach_view_signal    = _view_signal;
using unmap_view_signal      = _view_signal;
using pre_unmap_view_signal  = _view_signal;

struct map_view_signal : public _view_signal
{
    /* Indicates whether the position already has its initial posittion */
    bool is_positioned = false;
};

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
    wf::geometry_t old_geometry;
};

struct view_tiled_signal : public _view_signal
{
    uint32_t edges;
    bool carried_out = false;
    wf::geometry_t desired_size;
};

/**
 * The view-fullscreen-request and view-fullscreen signals are emitted on the view's output when the view's fullscreen state changes.
 * view-fullscreen-request is emitted when the view needs to be fullscreened, but has not been fullscreened yet.
 * view-fullscreen is emitted whenever the view's fullscreen state actually changes.
 * state is true if the view is fullscreen and false otherwise.
 * carried_out should be set by the listener if it handles the signal,
 * by setting the view fullscreen.
 * desired_size is the intended size for the fullscreen view
 * but may be undefined (0,0 0x0).
 */
struct view_fullscreen_signal : public _view_signal
{
    bool state;
    bool carried_out = false;
    wf::geometry_t desired_size;
};

struct view_minimize_request_signal : public _view_signal
{
    bool state;
    /* If some plugin wants to delay the (un)minimize of the view, it needs to
     * listen for the view-minimize-request and set carried_out to true.
     * It is a hint to core that the action will be (or already was) performed
     * by a plugin, and minimized state shouldn't be further changed by core */
    bool carried_out = false;
};

/* same as both change_viewport_request and change_viewport_notify */
struct change_viewport_signal : public wf::signal_data_t
{
    bool carried_out;
    wf::point_t old_viewport, new_viewport;
    wf::output_t *output;
};
using change_viewport_notify = change_viewport_signal;

/* sent when the workspace implementation actually reserves the workarea */
struct reserved_workarea_signal : public wf::signal_data_t
{
    wf::geometry_t old_workarea;
    wf::geometry_t new_workarea;
};

// TODO: this is a private signal, maybe we should hide it? */
struct _surface_map_state_changed_signal : public wf::signal_data_t
{
    wf::surface_interface_t *surface;
};

/* Part 2: Signals from wf::output_layout_t */
struct _output_signal : public wf::signal_data_t
{
    wf::output_t *output;
};

wf::output_t *get_signaled_output(wf::signal_data_t *data);

using output_added_signal = _output_signal;
using output_removed_signal = _output_signal;

namespace wf
{
/**
 * output-configuration-changed is a signal emitted on an output whenever the
 * output's source, mode, scale or transform changes.
 */
enum output_config_field_t
{
    /** Output source changed */
    OUTPUT_SOURCE_CHANGE    = (1 << 0),
    /** Output mode changed */
    OUTPUT_MODE_CHANGE      = (1 << 1),
    /** Output scale changed */
    OUTPUT_SCALE_CHANGE     = (1 << 2),
    /** Output transform changed */
    OUTPUT_TRANSFORM_CHANGE = (1 << 3),
};

struct output_state_t;
struct output_configuration_changed_signal : public _output_signal
{
    output_configuration_changed_signal(const wf::output_state_t& st)
        : state(st) { }
    /**
     * Which output attributes actually changed.
     * A bitwise OR of output_config_field_t.
     */
    uint32_t changed_fields;

    /**
     * The new state of the output.
     */
    const wf::output_state_t& state;
};

class input_device_t;
/* Used in the tablet-mode and lid-state signals from core */
struct switch_signal : public wf::signal_data_t
{
    nonstd::observer_ptr<input_device_t> device;
    bool state;
};

/* in input-device-added and input-device-removed signals from core */
struct input_device_signal : public signal_data_t
{
    nonstd::observer_ptr<input_device_t> device;
};

/**
 * Used for the following events:
 *
 * pointer_motion, pointer_motion_abs, pointer_button, pointer_axis,
 * pointer_swipe_begin, pointer_swipe_update, pointer_swipe_end,
 * pointer_pinch_begin, pointer_pinch_update, pointer_pinch_end,
 *
 * keyboard_key,
 *
 * touch_down, touch_up, touch_motion,
 *
 * tablet_proximity, tablet_axis, tablet_button, tablet_tip
 *
 * The template parameter is the corresponding type of wlr events.
 *
 * The input event signals are sent from core whenever a new input from an
 * input device arrives. The events are sent before any processing is done,
 * and they are independent of plugin input grabs and other wayfire input
 * mechanisms.
 *
 * The event data can be modified by plugins, and then the modified event
 * will be used instead. However plugins which modify the event must ensure
 * that subsequent events are adjusted accordingly as well.
 */
template<class wlr_event_t>
    struct input_event_signal : public wf::signal_data_t
{
    /* The event as it has arrived from wlroots */
    wlr_event_t *event;
};

/**
 * decoration-state-updated signal is emitted when the value of
 * view::should_be_decorated() changes.
 *
 * decoration-state-updated-view is emitted on the output of the view.
 */
using decoration_state_updated_signal = _view_signal;

/**
 * view-move-to-output signal is emitted by core just before a view is moved
 * from one output to another.
 */
struct view_move_to_output_signal : public signal_data_t
{
    /* The view being moved */
    wayfire_view view;
    /* The output the view was on */
    wf::output_t *old_output;
    /* The output the view is being moved to. */
    wf::output_t *new_output;
};

/**
 * stack-order-changed is emitted whenever the stacking order changes in the
 * workspace-manager of an output.
 */
struct stack_order_changed_signal : public signal_data_t
{ /* Empty */ };
}

#endif
