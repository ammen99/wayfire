#ifndef WF_SUBSURFACE_HPP
#define WF_SUBSURFACE_HPP

#include "surface-impl.hpp"

namespace wf
{
class subsurface_implementation_t : public wlr_child_surface_base_t
{
    wl_listener_wrapper on_map, on_unmap, on_destroy;
    wlr_subsurface *sub;

  public:
    // set sub data
    subsurface_implementation_t(wlr_subsurface *s,
        surface_interface_t *parent);

    virtual wf_point get_offset() override;
};
}


#endif /* end of include guard: WF_SUBSURFACE_HPP */
