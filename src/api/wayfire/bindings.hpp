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
using axis_callback  = std::function<bool (wlr_event_pointer_axis*)>;
using touch_callback = std::function<bool (int32_t, int32_t)>; // x, y

enum activator_source_t
{
    ACTIVATOR_SOURCE_KEYBINDING,
    ACTIVATOR_SOURCE_BUTTONBINDING,
    ACTIVATOR_SOURCE_GESTURE,
    ACTIVATOR_SOURCE_HOTSPOT,
};

/*
 * The first argument shows the activator source.
 * The second argument has a different meaning:
 * - For keybindings, it is the key which triggered the binding.
 *   Zero for modifier bindings.
 * - For buttonbindings, the button that triggered the binding.
 * - Unused for gestures.
 * - The edges of the activating hotspot.
 */
using activator_callback = std::function<bool (wf::activator_source_t, uint32_t)>;
}

#endif /* end of include guard: WF_BINDINGS_HPP */
