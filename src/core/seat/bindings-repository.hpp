#pragma once

#include "wayfire/geometry.hpp"
#include <memory>
#include <vector>
#include <wayfire/bindings.hpp>
#include <wayfire/config/option-wrapper.hpp>
#include <wayfire/config/types.hpp>

namespace wf
{
/**
 * Opaque binding handle for plugins.
 */
struct binding_t
{};

/**
 * Represents a binding with a plugin-provided callback and activation option.
 */
template<class Option, class Callback>
struct output_binding_t : public binding_t
{
    wf::option_sptr_t<Option> activated_by;
    Callback *callback;
};

/**
 * bindings_repository_t is responsible for managing a list of all bindings in
 * Wayfire, and for calling these bindings on the corresponding events.
 */
class bindings_repository_t
{
  public:
    /**
     * Handle a keybinding pressed by the user.
     *
     * @return true if any of the matching registered bindings consume the event.
     */
    bool handle_key(const wf::keybinding_t& pressed);

    /** Handle an axis event. */
    bool handle_axis(uint32_t modifiers, wlr_event_pointer_axis *ev);

    /**
     * Handle a buttonbinding pressed by the user.
     *
     * @return true if any of the matching registered bindings consume the event.
     */
    bool handle_button(const wf::buttonbinding_t& pressed,
        const wf::pointf_t& cursor);

    /** Handle a gesture from the user. */
    void handle_gesture(const wf::touchgesture_t& gesture);


    /** Erase binding of any type by callback */
    void rem_binding(void *callback);
    /** Erase binding of any type */
    void rem_binding(binding_t *binding);

    template<class Option, class Callback> using binding_container_t =
        std::vector<std::unique_ptr<output_binding_t<Option, Callback>>>;

  private:
    // output_t directly pushes in the binding containers to avoid having the
    // same wrapped functions as in the output public API.
    friend class output_impl_t;

    binding_container_t<wf::keybinding_t, key_callback> keys;
    binding_container_t<wf::keybinding_t, axis_callback> axes;
    binding_container_t<wf::buttonbinding_t, button_callback> buttons;
    binding_container_t<wf::activatorbinding_t, activator_callback> activators;
};
}
