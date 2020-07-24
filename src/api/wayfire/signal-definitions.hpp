#ifndef SIGNAL_DEFINITIONS_HPP
#define SIGNAL_DEFINITIONS_HPP

#include "wayfire/view.hpp"
#include "wayfire/output.hpp"

/**
 * Documentation of signals emitted from core components.
 * Each signal documentation follows the following scheme:
 *
 * name: The base name of the signal
 * on: Which components the plugin is emitted on. Prefixes are specified
 *     in (), i.e test(view-) means that the signal is emitted on component
 *     test with prefix 'view-'.
 * when: Description of when the signal is emitted.
 * argument: What the signal data represents when there is no dedicated
 *   signal data struct.
 */

namespace wf
{
/* ----------------------------------------------------------------------------/
 * Core signals
 * -------------------------------------------------------------------------- */

/**
 * name: shutdown
 * on: core
 * when: Right before the shutdown sequence starts.
 * argument: unused
 */

class input_device_t;
/**
 * name: input-device-added, input-device-removed
 * on: core
 * when: Whenever a new input device is added or removed.
 */
struct input_device_signal : public wf::signal_data_t
{
    nonstd::observer_ptr<input_device_t> device;
};

/**
 * name: tablet-mode, lid-state
 * on: core
 * when: When the corresponding switch device state changes.
 */
struct switch_signal : public wf::signal_data_t
{
    /** The switch device */
    nonstd::observer_ptr<input_device_t> device;
    /** On or off */
    bool state;
};

/**
 * name:
 *   pointer_motion, pointer_motion_abs, pointer_button, pointer_axis,
 *   pointer_swipe_begin, pointer_swipe_update, pointer_swipe_end,
 *   pointer_pinch_begin, pointer_pinch_update, pointer_pinch_end,
 *   keyboard_key,
 *   touch_down, touch_up, touch_motion,
 *   tablet_proximity, tablet_axis, tablet_button, tablet_tip
 *
 * on: core
 * when: The input event signals are sent from core whenever a new input from an
 *   input device arrives. The events are sent before any processing is done,
 *   and they are independent of plugin input grabs and other wayfire input
 *   mechanisms.
 *
 *   The event data can be modified by plugins, and then the modified event
 *   will be used instead. However plugins which modify the event must ensure
 *   that subsequent events are adjusted accordingly as well.
 *
 * example: The pointer_motion event is emitted with data of type
 *   input_event_signal<wlr_event_pointer_motion>
 */
template<class wlr_event_t>
struct input_event_signal : public wf::signal_data_t
{
    /* The event as it has arrived from wlroots */
    wlr_event_t *event;
};

/**
 * name: drag-started, drag-stopped
 * on: core
 * when: When a DnD action is started/stopped
 */
struct dnd_signal : public wf::signal_data_t
{
    /** The DnD icon */
    wf::surface_interface_t *icon;
};

/**
 * name: surface-mapped, surface-unmapped
 * on: core
 * when: Whenever a surface map state changes. This must be emitted for all
 *   surfaces regardless of their type (view, subsurface, etc).
 */
struct surface_map_state_changed_signal : public wf::signal_data_t
{
    wf::surface_interface_t *surface;
};

/**
 * name: reload-config
 * on: core
 * when: When the config file is reloaded
 * argument: unused
 */

/* ----------------------------------------------------------------------------/
 * Output signals
 * -------------------------------------------------------------------------- */

/** Base class for all output signals. */
struct _output_signal : public wf::signal_data_t
{
    wf::output_t *output;
};

/** @return The output in the signal. Must be an _output_signal. */
output_t *get_signaled_output(signal_data_t *data);

/**
 * name: output-added
 * on: output-layout
 * when: Each time a new output is added.
 */
using output_added_signal = _output_signal;

/**
 * name: pre-remove
 * on: output, output-layout(output-)
 * when: Emitted just before starting the destruction procedure for an output.
 */
using output_pre_remove_signal = _output_signal;

/**
 * name: output-removed
 * on: output-layout
 * when: Each time a new output is added.
 */
using output_removed_signal = _output_signal;

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
    /** Output position changed */
    OUTPUT_POSITION_CHANGE  = (1 << 4),
};

struct output_state_t;

/**
 * name: configuration-changed
 * on: output, output-layout(output-)
 * when: Each time the output's source, mode, scale, transform and/or position
 *   changes.
 */
struct output_configuration_changed_signal : public _output_signal
{
    output_configuration_changed_signal(const wf::output_state_t& st) :
        state(st)
    {}
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

/**
 * name: gain-focus
 * on: output, core(output-)
 * when: Immediately after the output becomes focused.
 */
using output_gain_focus_signal = _output_signal;

/* ----------------------------------------------------------------------------/
 * Output rendering signals (see also wayfire/workspace-stream.hpp)
 * -------------------------------------------------------------------------- */
/**
 * name: start-rendering
 * on: output
 * when: Whenever the output is ready to start rendering. This can happen
 *   either on output creation or whenever all inhibits in wayfire-shell have
 *   been removed.
 */
using output_start_rendering_signal = _output_signal;

/* ----------------------------------------------------------------------------/
 * Output workspace signals
 * -------------------------------------------------------------------------- */

/**
 * name: workspace-changed
 * on: output
 * when: Whenever the current workspace on the output has changed.
 */
struct workspace_changed_signal : public wf::signal_data_t
{
    /** For workspace-change-request, whether the request has already been
     * handled. */
    bool carried_out;

    /** Previously focused workspace */
    wf::point_t old_viewport;

    /** Workspace that is to be focused or became focused */
    wf::point_t new_viewport;

    /** The output this is happening on */
    wf::output_t *output;
};

/**
 * name: workspace-change-request
 * on: output
 * when: Whenever a workspace change is requested by core or by a plugin.
 *   This can be used by plugins who wish to handle workspace changing
 *   themselves, for ex. if animating the transition.
 */
using workspace_change_request_signal = workspace_changed_signal;

/**
 * name: workarea-changed
 * on: output
 * when: Whenever the available workarea changes.
 */
struct workarea_changed_signal : public wf::signal_data_t
{
    wf::geometry_t old_workarea;
    wf::geometry_t new_workarea;
};

/**
 * name: fullscreen-layer-focused
 * on: output
 * when: Whenever a fullscreen view is promoted on top of the other layers.
 * argument: The event data pointer is null if there are no promoted views,
 *   and not-null otherwise.
 */

/* ----------------------------------------------------------------------------/
 * View signals
 * -------------------------------------------------------------------------- */

/** Base class for all view signals. */
struct _view_signal : public wf::signal_data_t
{
    wayfire_view view;
};

/**
 * @return The view contained in the signal data, it must be
 * a _view_signal.
 */
wayfire_view get_signaled_view(wf::signal_data_t *data);

/**
 * name: mapped
 * on: view, output(view-)
 * when: After the view becomes mapped. This signal must also be emitted from
 *   all compositor views.
 */
struct view_mapped_signal : public _view_signal
{
    /* Indicates whether the position already has its initial position */
    bool is_positioned = false;
};

/**
 * name: pre-unmapped
 * on: view, output(view-)
 * when: Immediately before unmapping a mapped view. The signal may not be
 *   emitted from all views, but it is necessary for unmap animations to work.
 */
using view_pre_unmap_signal = _view_signal;

/**
 * name: unmapped
 * on: view, output(view-)
 * when: After a previously mapped view becomes unmapped. This must be emitted
 *   for all views.
 */
using view_unmapped_signal = _view_signal;

/**
 * name: set-output
 * on: view
 * when: Immediately after the view's output changes. Note that child views may
 *   still be on the old output.
 * argument: The old output of the view.
 */
using view_set_output_signal = _output_signal;

/* ----------------------------------------------------------------------------/
 * View state signals
 * -------------------------------------------------------------------------- */

/**
 * name: minimized
 * on: view, output(view-)
 * when: After the view's minimized state changes.
 */
struct view_minimized_signal : public _view_signal
{
    /** true is minimized, false is restored */
    bool state;
};

/**
 * name: view-minimize-request
 * on: output
 * when: Emitted whenever some entity requests that the view's minimized state
 *   changes. If no plugin is available to service the request, it is carried
 *   out by core. See view_interface_t::minimize_request()
 */
struct view_minimize_request_signal : public _view_signal
{
    /** true is minimized, false is restored */
    bool state;

    /**
     * Whether some plugin will service the minimization request, in which
     * case other plugins and core should ignore the request.
     */
    bool carried_out = false;
};

/**
 * name: tiled
 * on: view, output(view-)
 * when: After the view's tiled edges change.
 */
struct view_tiled_signal : public _view_signal
{
    /** Previously tiled edges */
    uint32_t old_edges;
    /** Currently tiled edges */
    uint32_t new_edges;
};

/**
 * name: view-tile-request
 * on: output
 * when: Emitted whenever some entity requests that the view's tiled edges
 *   change. If no plugin is available to service the request, it is carried
 *   out by core. See view_interface_t::tile_request()
 */
struct view_tile_request_signal : public _view_signal
{
    /** The desired edges */
    uint32_t edges;

    /**
     * The geometry the view should have. This is for example the last geometry
     * a view had before being tiled.  The given geometry is only a hint by core
     * and plugins may override it. It may also be undefined (0,0 0x0).
     */
    wf::geometry_t desired_size;

    /**
     * Whether some plugin will service the tile request, in which case other
     * plugins and core should ignore the request.
     */
    bool carried_out = false;
};


/**
 * name: fullscreen
 * on: view, output(view-)
 * when: After the view's fullscreen state changes.
 */
struct view_fullscreen_signal : public _view_signal
{
    /** The desired fullscreen state */
    bool state;

    /**
     * For view-fullscreen-request:
     *
     * Whether some plugin will service the fullscreen request, in which case
     * other plugins and core should ignore the request.
     */
    bool carried_out = false;

    /**
     * For view-fullscreen-request:
     *
     * The geometry the view should have. This is for example the last geometry
     * a view had before being fullscreened. The given geometry is only a hint
     * by core and plugins may override it. It may also be undefined (0,0 0x0).
     */
    wf::geometry_t desired_size;
};

/**
 * name: view-fullscreen-request
 * on: output
 * when: Emitted whenever some entity requests that the view's fullscreen state
 *   change. If no plugin is available to service the request, it is carried
 *   out by core. See view_interface_t::fullscreen_request()
 */
using view_fullscreen_request_signal = view_fullscreen_signal;

/**
 * name: title-changed
 * on: view
 * when: After the view's title has changed.
 */
using title_changed_signal = _view_signal;

/**
 * name: app-id-changed
 * on: view
 * when: After the view's app-id has changed.
 */
using app_id_changed_signal = _view_signal;

/**
 * name: geometry-changed
 * on: view
 * when: Whenever the view's wm geometry changes.
 */
struct view_geometry_changed_signal : public _view_signal
{
    /** The old wm geometry */
    wf::geometry_t old_geometry;
};

/**
 * name: region-damaged
 * on: view
 * when: Whenever a region of the view becomes damaged, for ex. when the client
 *   updates its contents.
 * argument: Unused.
 */

/**
 * name: decoration-state-updated
 * on: view, output(view-)
 * when: Whenever the value of view::should_be_decorated() changes.
 */
using view_decoration_state_updated_signal = _view_signal;

/**
 * name: decoration-changed
 * on: view
 * when: Whenever the view's decoration changes.
 * argument: unused.
 */

/* ----------------------------------------------------------------------------/
 * View <-> output signals
 * -------------------------------------------------------------------------- */

/**
 * name: view-attached
 * on: output
 * when: As soon as the view's output is set to the given output. This is the
 *   first point where the view is considered to be part of that output.
 */
using view_attached_signal = _view_signal;

/**
 * name: view-layer-attached
 * on: output
 * when: Emitted when the view is added to a layer in the output's workspace
 *   manager and it was in no layer previously.
 */
using view_layer_attached_signal = _view_signal;

/**
 * name: view-detached
 * on: output
 * when: Emitted when the view's output is about to be changed to another one.
 *   This is the last point where the view is considered to be part of the given
 *   output.
 */
using view_detached_signal = _view_signal;

/**
 * name: view-layer-detached
 * on: output
 * when: Emitted when the view is removed from a layer but is not added to
 *   another.
 */
using view_layer_detached_signal = _view_signal;

/**
 * name: view-pre-moved-to-output
 * on: core
 * when: Immediately before the view is moved to another output. The usual
 *   sequence when moving views to another output is:
 *
 *   pre-moved-to-output -> layer-detach -> detach ->
 *      attach -> layer-attach -> moved-to-output
 */
struct view_pre_moved_to_output_signal : public signal_data_t
{
    /* The view being moved */
    wayfire_view view;
    /* The output the view was on, may be NULL. */
    wf::output_t *old_output;
    /* The output the view is being moved to. */
    wf::output_t *new_output;
};

/**
 * name: view-moved-to-output
 * on: core
 * when: After the view has been moved to a new output.
 */
using view_moved_to_output_signal = view_pre_moved_to_output_signal;

/**
 * name: view-disappeared
 * on: output
 * when: This is a signal which combines view-unmapped, view-detached and
 *   view-minimized, and is emitted together with each of these three. Semantic
 *   meaning is that the view is no longer available for focus, interaction with
 *   the user, etc.
 */
using view_disappeared_signal = _view_signal;

/**
 * name: view-focused
 * on: output
 * when: As soon as the output focus changes.
 * argument: The newly focused view.
 */
using focus_view_signal = _view_signal;

/**
 * name: view-move-request
 * on: output
 * when: Whenever an interactive move is requested on the view. See also
 *   view_interface_t::move_request()
 */
using view_move_request_signal = _view_signal;

/**
 * name: view-resize-request
 * on: output
 * when: Whenever an interactive resize is requested on the view. See also
 *   view_interface_t::resize_request()
 */
struct view_resize_request_signal : public _view_signal
{
    /** The requested resize edges */
    uint32_t edges;
};

/**
 * name: view-self-request-focus
 * on: output
 * when: Whenever the client requests that a view be focused.
 */
using view_self_request_focus_signal = _view_signal;

/**
 * name: view-system-bell
 * on: core
 * when: Whenever a client wants to invoke the system bell if such is available.
 *   Note the system bell may or may not be tied to a particular view, so the
 *   signal may be emitted with a nullptr view.
 */
using view_system_bell_signal = _view_signal;
}

#endif
