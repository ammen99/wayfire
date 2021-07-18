#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <doctest/doctest.h>

#include "../stub.h"
#include <wayfire/nonstd/wlroots-full.hpp>
#include "../../src/view/surface-lock.hpp"


std::map<wlr_surface*, int> nr_locks;
static uint32_t mock_lock_pending(wlr_surface *surface)
{
    ++nr_locks[surface];
    return nr_locks[surface];
}

std::map<wlr_surface*, std::set<uint32_t>> unlocked;
static void mock_unlock_pending(wlr_surface *surface, uint32_t id)
{
    unlocked[surface].insert(id);
}

TEST_CASE("wlr_surface_manager_t")
{
    wlr_surface surface;
    nr_locks.clear();
    unlocked.clear();

    STUB(wlr_surface_lock_pending, mock_lock_pending);
    STUB(wlr_surface_unlock_cached, mock_unlock_pending);

    wl_signal_init(&surface.events.commit);
    wl_signal_init(&surface.events.cache);

    int nr_commit = 0;
    wf::wl_listener_wrapper on_commit;
    on_commit.set_callback([&] (void*)
    {
        ++nr_commit;
    });
    on_commit.connect(&surface.events.commit);

    wf::wlr_surface_manager_t lockmgr(&surface);

    SUBCASE("No locks - commit passes through")
    {
        wl_signal_emit(&surface.events.commit, NULL);
        REQUIRE(nr_locks[&surface] == 0);
        REQUIRE(nr_commit == 1);
    }

    SUBCASE("Create a checkpoint")
    {
        int x = 0;
        const auto& wait_2 = [&x] ()
        {
            ++x;
            return x == 2;
        };

        const auto& wait_0 = [] ()
        {
            return true;
        };

        REQUIRE(nr_locks[&surface] == 0);
        int lock_id = lockmgr.lock_until(wait_2);
        REQUIRE(nr_locks[&surface] == 1);

        // Halfway to checkpoint
        wl_signal_emit(&surface.events.cache, NULL);
        REQUIRE(nr_locks[&surface] == 1);

        SUBCASE("Reach checkpoint")
        {
            wl_signal_emit(&surface.events.cache, NULL);
            REQUIRE(nr_locks[&surface] == 2);
            REQUIRE(unlocked[&surface].empty());

            SUBCASE("Unlock commits up to checkpoint")
            {
                lockmgr.unlock(lock_id);
                REQUIRE(unlocked[&surface] == std::set<uint32_t>{1});

                SUBCASE("Unlock all commits")
                {
                    lockmgr.unlock_all(lock_id);
                    REQUIRE(unlocked[&surface] == std::set<uint32_t>{1, 2});
                }

                SUBCASE("Transfer lock")
                {
                    lockmgr.lock_until(wait_0);
                    lockmgr.unlock_all(lock_id);
                    REQUIRE(unlocked[&surface] == std::set<uint32_t>{1});
                }
            }

            SUBCASE("Stay at first checkpoint")
            {
                lockmgr.lock_until(wait_0);
                REQUIRE(unlocked[&surface] == std::set<uint32_t>{2});

                // Unlock the first lock.
                // Only the second checkpoint should be removed!
                lockmgr.unlock_all(lock_id);
                REQUIRE(unlocked[&surface] == std::set<uint32_t>{2});
            }
        }

        SUBCASE("Stay at first checkpoint")
        {
            lockmgr.lock_until(wait_0);
            REQUIRE(unlocked[&surface].empty());

            // Unlock the first lock.
            // Only the second checkpoint should be removed!
            lockmgr.unlock_all(lock_id);
            REQUIRE(unlocked[&surface].empty());
        }
    }
}
