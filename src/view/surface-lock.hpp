#pragma once

#include <wayfire/nonstd/wlroots.hpp>
#include <wayfire/util.hpp>
#include <optional>

namespace wf
{
/**
 * A class for managing wlr_surface locks.
 */
class wlr_surface_manager_t
{
  public:
    wlr_surface_manager_t(wlr_surface* surface);

    using callback_t = std::function<bool ()>;
    /**
     * Lock the current surface state, but do not lock a second time if the
     * state has been already locked. On each surface cache event, the is_ready
     * callback will be called.  When it returns true, a checkpoint is created.
     * The next unlock() call will unlock events up to that checkpoint.
     *
     * @return The serial ID of this request.
     */
    uint64_t lock_until(callback_t is_ready);

    /**
     * Unlock all commits up to the checkpoint created by lock_until().
     * If no checkpoint has been reached, the behavior is the same as unlock_all().
     *
     * @param id The ID returned by the corresponding lock_until request.
     */
    void unlock(uint64_t id);

    /**
     * Remove all locks on the surface, thus allowing subsequent commits.
     *
     * @param id The ID returned by the corresponding lock_until request.
     *   If the ID is not the newest ID given by lock_until, then no locks are
     *   removed.
     */
    void unlock_all(uint64_t id);

  private:
    wlr_surface *surface;
    callback_t is_checkpoint;

    wf::wl_listener_wrapper on_cache;
    uint64_t last_id = 0;

    std::optional<uint32_t> current_checkpoint;
    std::optional<uint32_t> next_checkpoint;

  public:
    wlr_surface_manager_t(const wlr_surface_manager_t&) = delete;
    wlr_surface_manager_t& operator =(const wlr_surface_manager_t&) = delete;
    wlr_surface_manager_t(wlr_surface_manager_t&&) = delete;
    wlr_surface_manager_t& operator =(wlr_surface_manager_t&&) = delete;
};
}
