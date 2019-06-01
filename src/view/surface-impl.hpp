#ifndef SURFACE_IMPL_HPP
#define SURFACE_IMPL_HPP

#include <surface.hpp>
#include <util.hpp>

extern "C"
{
#include <wlr/types/wlr_surface.h>
}

namespace wf
{

class surface_interface_t::impl
{
  public:
    surface_interface_t* parent_surface;
    std::vector<surface_interface_t*> surface_children;

    wf::output_t *output;
    int ref_cnt = 0;

    static int active_shrink_constraint;

    /**
     * Most surfaces don't have a wlr_surface. However, internal surface
     * implementations can set the underlying surface so that functions like
     *
     * subtract_opaque(), send_frame_done(), etc. work for the surface
     */
    wlr_surface *wsurface = nullptr;
};

/**
 * A base class for views and surfaces which are based on a wlr_surface
 * Any class that derives from wlr_surface_base_t must also derive from
 * surface_interface_t!
 */
class wlr_surface_base_t
{
  protected:
    wf::wl_listener_wrapper::callback_t handle_new_subsurface;
    wf::wl_listener_wrapper on_commit, on_destroy, on_new_subsurface;

    virtual void damage_surface_box(const wlr_box& box);
    virtual void damage_surface_region(const wf_region& region);

    void apply_surface_damage();
    virtual void _wlr_render_box(const wf_framebuffer& fb, int x, int y,
        const wlr_box& scissor);

    wlr_surface_base_t(wf::surface_interface_t *self);
    /* Pointer to this as surface_interface, see requirement above */
    wf::surface_interface_t *_as_si = nullptr;

  public:
    /* if surface != nullptr, then the surface is mapped */
    wlr_surface *surface = nullptr;

    virtual ~wlr_surface_base_t();

    /** @return The offset from the surface coordinates to the actual geometry */
    virtual wf_point get_window_offset();

    /** Update the surface output */
    virtual void update_output(wf::output_t *old_output,
        wf::output_t* new_output);

    /*
     * Functions that need to be implemented/overridden from the
     * surface_implementation_t
     */
    virtual bool _is_mapped() const;
    virtual wf_surface_size_t _get_size() const;
    virtual void _simple_render(const wf_framebuffer& fb, int x, int y,
        const wf_region& damage);

  protected:
    virtual void map(wlr_surface *surface);
    virtual void unmap();
    virtual void commit();

    virtual wlr_buffer *get_buffer();
};

/**
 * wlr_child_surface_base_t is a base class for wlr-surface based child surfaces,
 * i.e popups, subsurfaces, etc.
 *
 * However, they can still exist without a parent, for ex. drag icons.
 */
class wlr_child_surface_base_t :
    public surface_interface_t, public wlr_surface_base_t
{
  public:
    wlr_child_surface_base_t(surface_interface_t *parent,
        surface_interface_t *self);
    virtual ~wlr_child_surface_base_t();

    /* Just pass to the default wlr surface implementation */
    virtual bool is_mapped() const override { return _is_mapped(); }
    virtual wf_surface_size_t get_size() const override { return _get_size(); }
    virtual void simple_render(const wf_framebuffer& fb, int x, int y,
        const wf_region& damage) override {
        _simple_render(fb, x, y, damage);
    }
    virtual void set_output(wf::output_t *output) override {
        update_output(get_output(), output);
        surface_interface_t::set_output(output);
    }
};
}

#endif /* end of include guard: SURFACE_IMPL_HPP */
