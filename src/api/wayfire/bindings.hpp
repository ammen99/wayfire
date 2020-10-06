#ifndef WF_BINDINGS_HPP
#define WF_BINDINGS_HPP

#include <functional>
#include <cstdint>
#include <wayfire/nonstd/wlroots.hpp>

namespace wf
{
struct touchgesture_t;
struct binding_t;

using key_callback    = std::function<bool (uint32_t)>;
using button_callback = std::function<bool (uint32_t, int32_t, int32_t)>; // button,
                                                                          // x, y
using axis_callback    = std::function<bool (wlr_event_pointer_axis*)>;
using touch_callback   = std::function<bool (int32_t, int32_t)>; // x, y
using gesture_callback = std::function<bool (wf::touchgesture_t*)>;

enum activator_source_t
{
    ACTIVATOR_SOURCE_KEYBINDING,
    ACTIVATOR_SOURCE_BUTTONBINDING,
    ACTIVATOR_SOURCE_GESTURE,
};

/* First argument is the source which was used to activate, the second one is
 * the key or button which triggered it, if applicable.
 *
 * Special case: modifier bindings. In that case, the source is a keybinding,
 * but the second argument is 0 */
using activator_callback = std::function<bool (wf::activator_source_t, uint32_t)>;
}

#endif /* end of include guard: WF_BINDINGS_HPP */
