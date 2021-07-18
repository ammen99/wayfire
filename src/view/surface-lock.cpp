#include "surface-lock.hpp"
#include <wayfire/nonstd/wlroots-full.hpp>
#include <assert.h>

#include <iostream>
#define _ << " " <<
#define debug(x) #x << " = " << x

wf::wlr_surface_manager_t::wlr_surface_manager_t(wlr_surface* surface)
{
    this->surface = surface;

    this->on_cache.set_callback([this] (void *data)
    {
        if (this->is_checkpoint && this->is_checkpoint())
        {
            next_checkpoint = wlr_surface_lock_pending(this->surface);
        }
    });

    this->on_cache.connect(&surface->events.cache);
}

uint64_t wf::wlr_surface_manager_t::lock_until(callback_t cb)
{
    if (this->is_checkpoint)
    {
        // Already had a lock, check whether to release next checkpoint
        // In any case, we just keep the current checkpoint
        if (this->next_checkpoint)
        {
            wlr_surface_unlock_cached(this->surface, next_checkpoint.value());
            next_checkpoint = {};
        }
    } else
    {
        // No locks so far, just lock current state.
        current_checkpoint = wlr_surface_lock_pending(surface);
    }

    this->is_checkpoint = cb;
    return ++last_id;
}

void wf::wlr_surface_manager_t::unlock(uint64_t id)
{
    if (id != last_id)
    {
        return;
    }

    this->is_checkpoint = {};

    assert(current_checkpoint.has_value());
    auto to_release = current_checkpoint.value();
    current_checkpoint = next_checkpoint;
    wlr_surface_unlock_cached(surface, to_release);
    next_checkpoint = {};
}

void wf::wlr_surface_manager_t::unlock_all(uint64_t id)
{
    if (id != last_id)
    {
        return;
    }

    std::vector<uint32_t> to_release;

    if (next_checkpoint.has_value())
    {
        to_release.push_back(next_checkpoint.value());
    }

    if (current_checkpoint.has_value())
    {
        to_release.push_back(current_checkpoint.value());
    }

    current_checkpoint = {};
    next_checkpoint = {};

    for (auto& pts : to_release)
    {
        wlr_surface_unlock_cached(surface, pts);
    }
}
