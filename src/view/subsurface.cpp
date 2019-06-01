#include "subsurface.hpp"
#include <cassert>

wf::subsurface_implementation_t::subsurface_implementation_t(wlr_subsurface *_sub,
    wf::surface_interface_t *parent)
    : wlr_child_surface_base_t(parent, this)
{
    this->sub = _sub;
    on_map.set_callback([&] (void*) {
        this->map(this->sub->surface);
    });
    on_unmap.set_callback([&] (void*) { this->unmap(); });
    on_destroy.set_callback([&] (void*) {
        on_map.disconnect();
        on_unmap.disconnect();
        on_destroy.disconnect();
        unref();
    });

    on_map.connect(&sub->events.map);
    on_unmap.connect(&sub->events.unmap);
    on_destroy.connect(&sub->events.destroy);
}

wf_point wf::subsurface_implementation_t::get_offset()
{
    assert(is_mapped());

    return {
        sub->current.x,
        sub->current.y,
    };
}
