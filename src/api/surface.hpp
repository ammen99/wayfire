#ifndef WF_SURFACE_HPP
#define WF_SURFACE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

#include "geometry.hpp"

struct wf_region;
struct wf_framebuffer;
namespace wf
{
class output_t;
class surface_interface_t;

/**
 * A surface and its position on the screen.
 */
struct surface_iterator_t
{
    /** The surface */
    surface_interface_t *surface;
    /**
     * The position of the surface relative to the topmost surface in the
     * surface tree
     */
    wf_point position;
};

/**
 * surface_interface_t is the base class for everything that can be displayed
 * on the screen. It is the closest thing there is in Wayfire to a Window in X11.
 */
class surface_interface_t
{
  public:
    virtual ~surface_interface_t();

    /**
     * Surface lifetime is managed by reference counting. To take a reference,
     * use take_ref(). Note that one reference is automatically made when the
     * surface is created.
     */
    void take_ref();

    /**
     * Drop a reference to the surface. When the reference count reaches 0, the
     * destruct() method is called.
     */
    void unref();

    /**
     * Check whether the surface is mapped. Mapped surfaces are "alive", i.e
     * they are rendered, can receive input, etc.
     *
     * Note an unmapped surface may be temporarily rendered, for ex. when
     * using a close animation.
     *
     * @return true if the surface is mapped and false otherwise.
     */
    virtual bool is_mapped() const = 0;

    /**
     * Related surfaces usually form hierarchies, where the topmost surface
     * is a View, except for drag icons or surfaces managed by plugins.
     *
     * @return The topmost surface in the hierarchy of the surface.
     */
    virtual surface_interface_t *get_main_surface();

    /**
     * @param surface_origin The coordinates of the top-left corner of the
     * surface.
     *
     * @return a list of each mapped surface in the surface tree, including the
     * surface itself.
     *
     * The surfaces should be ordered from the topmost to the bottom-most one.
     */
    virtual std::vector<surface_iterator_t> enumerate_surfaces(
        wf_point surface_origin = {0, 0});

    /**
     * @return The output the surface is currently attached to. Note this
     * doesn't necessarily mean that it is visible.
     */
    virtual wf::output_t *get_output();

    /**
     * Set the current output of the surface and all surfaces in its surface
     * tree. Note that calling this for a surface with a parent is an invalid
     * operation and may have undefined consequences.
     *
     * @param output The new output, may be null
     */
    virtual void set_output(wf::output_t* output);

    /** @return The offset of this surface relative to its parent surface.  */
    virtual wf_point get_offset() = 0;

    /** @return The surface dimensions */
    virtual wf_surface_size_t get_size() const = 0;

    /**
     * Test whether the surface accepts touch or pointer input at the given
     * surface-local position. By default, the surface doesn't accept any
     * input.
     *
     * @return true if the point lies inside the input region of the surface,
     * false otherwise.
     */
    virtual bool accepts_input(int32_t sx, int32_t sy);

    /**
     * Send wl_surface.frame event. Surfaces which aren't backed by a
     * wlr_surface don't need to do anything here.
     *
     * @param frame_end The time when the frame ended.
     */
    virtual void send_frame_done(const timespec& frame_end);

    /**
     * Subtract the opaque region of the surface from region.
     *
     * This is just a hint, so surface implementations don't have to implement
     * this function.
     *
     * @param region The region to subtract from
     * @param x The x position of the surface in the coordinate system of region
     * @param y The y position of the surface in the coordinate system of region
     */
    virtual void subtract_opaque(wf_region& region, int x, int y);

    /**
     * Request that the opaque region is shrunk by a certain amount of pixels
     * from the edge. Surface implementations that implement subtract_opaque
     * typically also need to implement this function.
     *
     * @param constraint_name The unique name of the component that makes this
     *        request. The request with the biggest shrink_by will be used.
     * @param shrink_by The amount of pixels to shrink by.
     */
    static void set_opaque_shrink_constraint(
        std::string constraint_name, int value);

    /**
     * @return the wl_client associated with this surface, or null if the
     *         surface doesn't have a backing wlr_surface.
     */
    virtual wl_client *get_client();

    /**
     * Render the surface, without applying any transformations.
     *
     * @param fb The target framebuffer
     * @param x The x coordinate of the surface relative to fb
     * @param y The y coordinate of the surface relative to fb
     * @param damage The damaged region of the surface. Nothing should be drawn
     * outside of it.
     */
    virtual void simple_render(const wf_framebuffer& fb, int x, int y,
        const wf_region& damage) = 0;

    /** Private data for surface_interface_t, used for the implementation of
     * provided functions */
    class impl;
    std::unique_ptr<impl> priv;
  protected:
    /**
     * Construct a new surface with the given parent surface.
     *
     * @param parent_surface The parent surface. May be null.
     */
    surface_interface_t(surface_interface_t* parent_surface);

    /** @return the active shrink constraint */
    static int get_active_shrink_constraint();

    /**
     * Called when the reference count reaches 0.
     * It destructs the object and deletes it, so "this" may not be
     * accessed after calling destruct().
     */
    virtual void destruct();
};
void emit_map_state_change(wf::surface_interface_t *surface);
}

#endif /* end of include guard: WF_SURFACE_HPP */
