#ifndef WF_SEAT_SURFACE_MAP_STATE_HPP
#define WF_SEAT_SURFACE_MAP_STATE_HPP

#include <functional>
#include <wayfire/surface.hpp>
#include <wayfire/object.hpp>

namespace wf
{
/**
 * Convenience class to listen for map state changes of all surfaces.
 */
struct SurfaceMapStateListener
{
  public:
    SurfaceMapStateListener();
    ~SurfaceMapStateListener();

    using Callback = std::function<void (wf::surface_interface_t*)>;
    void set_callback(Callback call);

  private:
    wf::signal_callback_t on_surface_map_state_change;
    Callback callback;
};
}

#endif /* end of include guard: WF_SEAT_SURFACE_MAP_STATE_HPP */
