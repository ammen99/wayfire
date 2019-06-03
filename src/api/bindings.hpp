#ifndef WF_BINDINGS_HPP
#define WF_BINDINGS_HPP

#include <functional>
extern "C"
{
    struct wlr_event_pointer_axis;
}

struct wf_touch_gesture;
struct wf_binding; // opaque handle to a binding, can be used to remove it
using key_callback = std::function<void(uint32_t)>;
using button_callback = std::function<void(uint32_t, int32_t, int32_t)>; // button, x, y
using axis_callback = std::function<void(wlr_event_pointer_axis*)>;
using touch_callback = std::function<void(int32_t, int32_t)>; // x, y
using gesture_callback = std::function<void(wf_touch_gesture*)>;

enum wf_activator_source
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
using activator_callback = std::function<void(wf_activator_source, uint32_t)>;


#endif /* end of include guard: WF_BINDINGS_HPP */
