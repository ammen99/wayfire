#ifndef COMPOSITOR_SURFACE_HPP
#define COMPOSITOR_SURFACE_HPP

#include "wayfire/surface.hpp"

namespace wf
{
/**
 * compositor_surface_t is a base class for all surfaces which have
 * compositor-generated content, i.e for all surfaces not created by client
 * programs. Such surfaces can receive pointer/touch events via the provided
 * callbacks.
 *
 * Note that a compositor surface must additionally inherit surface_interface_t
 */
class compositor_surface_t
{
  public:
    virtual ~compositor_surface_t()
    {}

    virtual void on_pointer_enter(int x, int y)
    {}
    virtual void on_pointer_leave()
    {}
    virtual void on_pointer_motion(int x, int y)
    {}
    virtual void on_pointer_button(uint32_t button, uint32_t state)
    {}

    virtual void on_touch_down(int x, int y)
    {}
    virtual void on_touch_up()
    {}
    virtual void on_touch_motion(int x, int y)
    {}
};

compositor_surface_t *compositor_surface_from_surface(
    wf::surface_interface_t *surface);
}

#endif /* end of include guard: COMPOSITOR_SURFACE_HPP */
