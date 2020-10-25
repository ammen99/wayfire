#pragma once
#include <wayfire/nonstd/noncopyable.hpp>

namespace wf
{
/**
 * Dummy non-copyable type that increments the global inhibitor count when created,
 * and decrements when destroyed. These changes influence wlroots idle enablement.
 */
class idle_inhibitor_t : public noncopyable_t
{
  public:
    idle_inhibitor_t();
    ~idle_inhibitor_t();

  private:
    static unsigned int inhibitors;
    void notify_wlroots();
};
}
