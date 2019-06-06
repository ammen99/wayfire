#ifndef VIEW_HPP
#define VIEW_HPP

#include <vector>
#include <nonstd/observer_ptr.h>

#include "object.hpp"
#include "surface.hpp"
#include "geometry.hpp"

extern "C"
{
struct wlr_surface;
}

namespace wf
{
class view_interface_t;
}
using wayfire_view = nonstd::observer_ptr<wf::view_interface_t>;

class wf_decorator_frame_t;
class wf_view_transformer_t;

namespace wf
{
class output_t;

/* abstraction for desktop-apis, no real need for plugins
 * This is a base class to all "drawables" - desktop views, subsurfaces, popups */
enum view_role_t
{
    VIEW_ROLE_TOPLEVEL, // regular, "WM" views
    VIEW_ROLE_UNMANAGED, // xwayland override redirect or unmanaged views
    VIEW_ROLE_SHELL_VIEW // background, lockscreen, panel, notifications, etc
};

/**
 * view_interface_t is the base class for all "toplevel windows", i.e surfaces
 * which have no parent.
 */
class view_interface_t : public surface_interface_t, public wf::object_base_t
{
  public:
    /**
     * The toplevel parent of the view, for ex. the main view of a file chooser
     * dialogue.
     */
    wayfire_view parent = nullptr;

    /** A list of the children views.  */
    std::vector<wayfire_view> children;

    /**
     * Set the toplevel parent of the view, and adjust the children's list of
     * the parent.
     */
    void set_toplevel_parent(wayfire_view parent);

    /** The current view role.  */
    view_role_t role = VIEW_ROLE_TOPLEVEL;

    /** Set the view role */
    virtual void set_role(view_role_t new_role);

    /** Get a textual identifier for this view.  */
    std::string to_string() const;

    /** Wrap the view into a nonstd::observer_ptr<> */
    wayfire_view self();

    /**
     * Set the view's output.
     *
     * If the new output is different from the previous, the view will be
     * removed from the layer it was on the old output.
     */
    virtual void set_output(wf::output_t* new_output) override;

    /** Move the view to the given output-local coordinates.  */
    virtual void move(int x, int y) = 0;

    /**
     * Request that the view change its size to the given dimensions. The view
     * is not obliged to assume the given dimensions.
     *
     * Maximized and tiled views typically do obey the resize request.
     */
    virtual void resize(int w, int h);

    /**
     * A convenience function, has the same effect as calling move and resize
     * atomically.
     */
    virtual void set_geometry(wf_geometry g);

    /**
     * Start a resizing mode for this view. While a view is resizing, one edge
     * or corner of the view is made immobile (exactly the edge/corner opposite
     * to the edges which are set as resizing)
     *
     * @param resizing whether to enable or disable resizing mode
     * @param edges the edges which are being resized
     */
    virtual void set_resizing(bool resizing, uint32_t edges = 0);

    /**
     * Set the view in moving mode.
     *
     * @param moving whether to enable or disable moving mode
     */
    virtual void set_moving(bool moving);

    /**
     * Request that the view resizes to its native size.
     */
    virtual void request_native_size();

    /** Request that the view closes. */
    virtual void close();

    /**
     * The wm geometry of the view is the portion of the view surface that
     * contains the actual contents, for example, without the view shadows, etc.
     *
     * @return The wm geometry of the view.
     */
    virtual wf_geometry get_wm_geometry();

    /**
     * @return the geometry of the view. Coordinates are relative to the current
     * workspace of the view's output, or with undefined origin if the view is
     * not on any output. This doesn't take into account the view's transformers.
     */
    virtual wf_geometry get_output_geometry() = 0;

    /**
     * @return The bounding box of the view, which includes all (sub)surfaces,
     * menus, etc. after applying the view transformations.
     */
    virtual wlr_box get_bounding_box();

    /**
     * Find the surface in the view's tree which contains the given point.
     *
     * @param cursor_x The x coordinate
     * @param cursor_y The y coordinate
     * @param sx The x coordinate of the point relative to the returned surface
     * @param sy The y coordinate of the point relative to the returned surface
     *
     * @return The surface which is at the given point, or nullptr if no such
     *         surface was found (in which case sx and sy have no meaning)
     */
    virtual surface_interface_t *map_input_coordinates(
        int cursor_x, int cursor_y, int &sx, int &sy);

    /**
     * Transform the given point's coordinates into the local coordinate system
     * of the given surface in the view's surface tree, after applying all
     * transforms of the view.
     *
     * @param arg The point in global (output-local) coordinates
     * @param surface The reference surface, or null for view-local coordinates
     * @return The point in surface-local coordinates
     */
    virtual wf_point global_to_local_point(const wf_point& arg,
        surface_interface_t* surface);

    /**
     * @return the wlr_surface which should receive focus when focusing this
     * view. Views which aren't backed by a wlr_surface should implement the
     * compositor_view interface.
     *
     * In case no focus surface is available, or the view should not be focused,
     * nullptr should be returned.
     */
    virtual wlr_surface *get_keyboard_focus_surface() = 0;

    /**
     * Check whether the surface is focuseable. Note the actual ability to give
     * keyboard focus while the surface is mapped is determined by the keyboard
     * focus surface or the compositor_view implementation.
     *
     * This is meant for plugins like matcher, which need to check whether the
     * view is focuseable at any point of the view life-cycle.
     */
    virtual bool is_focuseable() const;

    /** Whether the view is in maximized state, usually you want to use either
     * set_maximized() or maximize_request() */
    bool maximized = false;
    /** Whether the view is in fullscreen state, usually you want to use either
     * set_fullscreen() or fullscreen_request() */
    bool fullscreen = false;
    /** Whether the view is in activated state, usually you want to use either
     * set_activated() or focus_request() */
    bool activated = false;
    /** Whether the view is in minimized state, usually you want to use either
     * set_minizied() or minimize_request() */
    bool minimized = false;
    /** The tiled edges of the view, usually you want to use set_tiled() */
    uint32_t tiled_edges = 0;

    /** Set the minimized state of the view. */
    virtual void set_minimized(bool minimized);
    /** Set the tiled edges of the view */
    virtual void set_tiled(uint32_t edges);
    /** Set the maximized state of the view */
    virtual void set_maximized(bool maxim);
    /** Set the fullscreen state of the view */
    virtual void set_fullscreen(bool fullscreen);
    /** Set the view's activated state.  */
    virtual void set_activated(bool active);

    /** Request that an interactive move starts for this view */
    virtual void move_request();
    /** Request that the view is focused on its output */
    virtual void focus_request();
    /** Request that an interactive resize starts for this view */
    virtual void resize_request(uint32_t edges = 0);
    /** Request that the view is (un)minimized */
    virtual void minimize_request(bool minimized);
    /** Request that the view is (un)maximized */
    virtual void maximize_request(bool maximized);
    /** Request that the view is (un)fullscreened on the given output */
    virtual void fullscreen_request(wf::output_t *output, bool state);

    /** @return true if the view is visible */
    virtual bool is_visible();

    /** Damage the whole view and add the damage to its output */
    virtual void damage();

    /** @return the app-id of the view */
    virtual std::string get_app_id() { return ""; }
    /** @return the title of the view */
    virtual std::string get_title() { return ""; }

    /**
     * Get the minimize target for this view, i.e when displaying a minimize
     * animation, where the animation's target should be. Defaults to {0,0,0,0}.
     *
     * @return the minimize target
     */
    virtual wlr_box get_minimize_hint();

    /** @return true if the view needs decorations */
    virtual bool should_be_decorated();

    /**
     * Set the decoration surface for the view.
     *
     * @param frame The surface to be set as a decoration. It must be subclass
     * of both surface_interface_t and of wf_decorator_frame_t, and its parent
     * surface must be this view.
     *
     * The life-time of the decoration is managed by the view itself, so after
     * calling this function you probably want to drop any references that you
     * hold (excluding the default one)
     */
    virtual void set_decoration(surface_interface_t *frame);

    /*
     *                        View transforms
     * A view transform can be any kind of transformation, for example 3D
     * rotation, wobbly effect or similar. When we speak of transforms, a
     * "view" is defined as a toplevel window (including decoration) and also
     * all of its subsurfaces/popups. The transformation then is applied to
     * this group of surfaces together.
     *
     * When a view has a custom transform, then internally all these surfaces
     * are rendered to a FBO, and then the custom transformation renders the
     * resulting texture as it sees fit. In case of multiple transforms, we do
     * multiple render passes where each transform is fed the result of the
     * previous transforms.
     *
     * Damage tracking for transformed views is done on the boundingbox of the
     * damaged region after applying the transformation, but all damaged parts
     * of the internal FBO are updated.
     * */

    void add_transformer(std::unique_ptr<wf_view_transformer_t> transformer);

    /**
     * Add a transformer with the given name. Note that you can add multiple
     * transforms with the same name.
     */
    void add_transformer(std::unique_ptr<wf_view_transformer_t> transformer,
        std::string name);

    /** @return a transformer with the give name, or null */
    nonstd::observer_ptr<wf_view_transformer_t> get_transformer(
        std::string name);

    /** remove a transformer */
    void pop_transformer(
        nonstd::observer_ptr<wf_view_transformer_t> transformer);
    /** remove all transformers with the given name */
    void pop_transformer(std::string name);
    /** @return true if the view has active transformers */
    bool has_transformer();

    /** @return the bounding box of the view up to the given transformer */
    wlr_box get_bounding_box(std::string transformer);
    /** @return the bounding box of the view up to the given transformer */
    wlr_box get_bounding_box(
        nonstd::observer_ptr<wf_view_transformer_t> transformer);

    /** @return a bounding box of the given box after applying the
     * transformers of the view */
    wlr_box transform_region(const wlr_box &box);

    /** @return a bounding box of the given box after applying the transformers
     * of the view up to the given transformer */
    wlr_box transform_region(const wlr_box& box, std::string transformer);
    /** @return a bounding box of the given box after applying the transformers
     * of the view up to the given transformer */
    wlr_box transform_region(const wlr_box& box,
        nonstd::observer_ptr<wf_view_transformer_t> transformer);

    /**
     * @return true if the region intersects any of the surfaces in the view's
     * surface tree.
     */
    bool intersects_region(const wlr_box& region);

    /**
     * Render all the surfaces of the view using the view's transforms.
     * If the view is unmapped, this operation will try to read from any
     * snapshot created by take_snapshot() or when transformers were applied,
     * and use that buffer.
     *
     * @param framebuffer the framebuffer to render to.
     * @param damage the damaged region of the view, in the same (but scaled)
     *        coordinate system as the framebuffer.
     *
     * @return true if the render operation was successful, and false if the
                    view is both unmapped and has no snapshot.
     */
    bool render_transformed(const wf_framebuffer& framebuffer,
        const wf_region& damage);

    /**
     * A snapshot of the view is a copy of the view's contents into a
     * framebuffer. It is used to get an image of the view while it is mapped,
     * and continue displaying it afterwards.
     */
    virtual void take_snapshot();

    virtual ~view_interface_t();

    class view_priv_impl;
    std::unique_ptr<view_priv_impl> view_impl;
  protected:
    view_interface_t();

    /** get_offset() is not valid for views */
    virtual wf_point get_offset() override { return {0, 0}; }

    /**
     * Damage the given box. It is assumed that the box is already transformed
     * by the view's transformers.
     *
     * The main difference with directly damaging the output is that this will
     * add the damage to all workspaces the view is visible on, in case of shell
     * views.
     */
    virtual void damage_raw(const wlr_box& box);

    /**
     * Damage the given box after transforming it with the transformers.
     */
    virtual void damage_box(const wlr_box& box);

    /**
     * @return the bounding box of the view before transformers
     */
    virtual wf_geometry get_untransformed_bounding_box();

    virtual void destruct() override;

    /**
     * Called whenever the minimized, maximized, fullscreened
     * or activated state changes */
    virtual void desktop_state_updated();
};

/**
 * Emit the view map signal. It indicates that a view has been mapped, i.e.
 * plugins can now "work" with it. Note that not all views will emit the map
 * event.
 */
void emit_view_map(wayfire_view view);

/**
 * Emit the view unmap signal. It indicates that the view is in the process of
 * being destroyed. Most plugins should stop any actions they have on the view.
 */
void emit_view_unmap(wayfire_view view);

/**
 * Emit the view pre-unmap signal. It is emitted right before the view
 * destruction start. At this moment a plugin can still take a snapshot of the
 * view. Note that not all views emit the pre-unmap signal, however the unmap
 * signal is mandatory for all views.
 */
void emit_view_pre_unmap(wayfire_view view);
wayfire_view wl_surface_to_wayfire_view(wl_resource *surface);
}

#endif
