#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include "wayfire/geometry.hpp"
#include "wayfire/object.hpp"
#include "wayfire/bindings.hpp"

#include <wayfire/option-wrapper.hpp>
#include <wayfire/config/types.hpp>

extern "C"
{
    struct wlr_output;
}

namespace wf
{
class view_interface_t;
}

using wayfire_view = nonstd::observer_ptr<wf::view_interface_t>;

namespace wf
{
class render_manager;
class workspace_manager;

struct plugin_grab_interface_t;
using plugin_grab_interface_uptr = std::unique_ptr<plugin_grab_interface_t>;

/**
 * Flags which can be passed to wf::output_t::activate_plugin() and
 * wf::output_t::can_activate_plugin().
 */
enum plugin_activation_flags_t
{
    /**
     * Activate the plugin even if input is inhibited, for ex. even when a
     * lockscreen is active.
     */
    PLUGIN_ACTIVATION_IGNORE_INHIBIT = (1 << 0),
    /*
     * Allow the same plugin to be activated multiple times.
     * The plugin will also have to be deactivated as many times as it has been
     * activated.
     */
    PLUGIN_ACTIVATE_ALLOW_MULTIPLE   = (1 << 1),
};

class output_t : public wf::object_base_t
{
  public:
    /**
     * The wlr_output that this output represents
     */
    wlr_output *handle;

    /**
     * The render manager of this output
     */
    std::unique_ptr<render_manager> render;

    /**
     * The workspace manager of this output
     */
    std::unique_ptr<workspace_manager> workspace;

    /**
     * Get a textual representation of the output
     */
    std::string to_string() const;

    /**
     * Get the logical resolution of the output, i.e if an output has mode
     * 3860x2160, scale 2 and transform 90, then get_screen_size will report
     * that it has logical resolution of 1080x1920
     */
    virtual wf::dimensions_t get_screen_size() const = 0;

    /**
     * Same as get_screen_size() but returns a wf::geometry_t with x,y = 0
     */
    wf::geometry_t get_relative_geometry() const;

    /**
     * Returns the output geometry as the output layout sees it. This is
     * typically the same as get_relative_geometry() but with meaningful x and y
     */
    wf::geometry_t get_layout_geometry() const;

    /**
     * Moves the pointer so that it is inside the output
     *
     * @param center If set to true, the pointer will be centered on the
     *   output, regardless of whether it was inside before.
     */
    void ensure_pointer(bool center = false) const;

    /**
     * Gets the cursor position relative to the output
     */
    wf::pointf_t get_cursor_position() const;

    /**
     * Checks if a plugin can activate. This may not succeed if a plugin
     * with the same abilities is already active or if input is inhibited.
     *
     * @param flags A bitwise OR of plugin_activation_flags_t.
     *
     * @return true if the plugin is able to be activated, false otherwise.
     */
    virtual bool can_activate_plugin(const plugin_grab_interface_uptr& owner,
        uint32_t flags = 0) = 0;

    /**
     * Activates a plugin. Note that this may not succeed, if a plugin with the
     * same abilities is already active. However the same plugin might be
     * activated twice.
     *
     * @param flags A bitwise OR of plugin_activation_flags_t.
     *
     * @return true if the plugin was successfully activated, false otherwise.
     */
    virtual bool activate_plugin(const plugin_grab_interface_uptr& owner,
        uint32_t flags = 0) = 0;

    /**
     * Deactivates a plugin once, i.e if the plugin was activated more than
     * once, only one activation is removed.
     *
     * @return true if the plugin remains activated, false otherwise.
     */
    virtual bool deactivate_plugin(const plugin_grab_interface_uptr& owner) = 0;

    /**
     * @return true if a grab interface with the given name is activated, false
     *              otherwise.
     */
    virtual bool is_plugin_active(std::string owner_name) const = 0;

    /**
     * @return The topmost view in the workspace layer
     */
    wayfire_view get_top_view() const;

    /**
     * @return The currently focused view for the given output. The might not,
     * however, be actually focused, if the output isn't focused itself.
     */
    virtual wayfire_view get_active_view() const = 0;

    /**
     * Attempt to give keyboard focus to the given view and set it as the
     * output's active view.
     *
     * @param raise If set to true, the view will additionally be raised to the
     * top of its layer.
     */
    virtual void focus_view(wayfire_view v, bool raise = false) = 0;

    /**
     * Switch the workspace so that view becomes visible.
     * @return true if workspace switch really occured
     */
    bool ensure_visible(wayfire_view view);

    /**
     * Force refocus the topmost view in one of the layers marked in layers
     * and which isn't skip_view.
     *
     * The stacking order is not changed.
     */
    virtual void refocus(wayfire_view skip_view, uint32_t layers) = 0;

    /**
     * Refocus the topmost focuseable view != skip_view, preferring regular
     * views. The stacking order is not changed.
     */
    void refocus(wayfire_view skip_view = nullptr);

    /**
     * the add_* functions are used by plugins to register bindings. They pass
     * a wf::option_t, which means that core will always use the latest binding
     * which is in the option.
     *
     * Adding a binding happens on a per-output basis. If a plugin registers
     * bindings on each output, it will receive for ex. a keybinding only on
     * the currently focused one.
     *
     * @return The wf::binding_t which can be used to unregister the binding.
     */
    wf::binding_t *add_key(option_sptr_t<keybinding_t> key, wf::key_callback*);
    wf::binding_t *add_axis(option_sptr_t<keybinding_t> axis,
        wf::axis_callback*);
    wf::binding_t *add_touch(option_sptr_t<keybinding_t> mod,
        wf::touch_callback*);
    wf::binding_t *add_button(option_sptr_t<buttonbinding_t> button,
        wf::button_callback*);
    wf::binding_t *add_gesture(option_sptr_t<touchgesture_t> gesture,
        wf::gesture_callback*);
    wf::binding_t *add_activator(option_sptr_t<activatorbinding_t> activator,
        wf::activator_callback*);

    /**
     * Remove the given binding, regardless of its type.
     */
    void rem_binding(wf::binding_t *binding);

    /**
     * Remove all bindings which have the given callback, regardless of the type.
     */
    void rem_binding(void *callback);

    virtual ~output_t();

  protected:
    /* outputs are instantiated internally by core */
    output_t();
};
}

#endif /* end of include guard: OUTPUT_HPP */
