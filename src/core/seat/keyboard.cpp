#include <cstring>
#include <linux/input-event-codes.h>

extern "C"
{
#include <xkbcommon/xkbcommon.h>
#include <wlr/backend/session.h>
}

#include <wayfire/util/log.hpp>
#include "keyboard.hpp"
#include "../core-impl.hpp"
#include "../../output/output-impl.hpp"
#include "cursor.hpp"
#include "touch.hpp"
#include "input-manager.hpp"
#include "wayfire/compositor-view.hpp"
#include "wayfire/signal-definitions.hpp"

void wf_keyboard::setup_listeners()
{
    on_key.set_callback([&] (void *data)
    {
        auto ev = static_cast<wlr_event_keyboard_key*> (data);
        emit_device_event_signal("keyboard_key", ev);

        auto seat = wf::get_core().get_current_seat();
        wlr_seat_set_keyboard(seat, this->device);

        if (!wf::get_core_impl().input->handle_keyboard_key(ev->keycode, ev->state))
        {
            wlr_seat_keyboard_notify_key(wf::get_core_impl().input->seat,
                ev->time_msec, ev->keycode, ev->state);
        }

        wlr_idle_notify_activity(wf::get_core().protocols.idle, seat);
    });

    on_modifier.set_callback([&] (void *data)
    {
        auto kbd = static_cast<wlr_keyboard*> (data);
        auto seat = wf::get_core().get_current_seat();

        wlr_seat_set_keyboard(seat, this->device);
        wlr_seat_keyboard_send_modifiers(seat, &kbd->modifiers);
        wlr_idle_notify_activity(wf::get_core().protocols.idle, seat);
    });

    on_key.connect(&handle->events.key);
    on_modifier.connect(&handle->events.modifiers);
}

wf_keyboard::wf_keyboard(wlr_input_device *dev)
    : handle(dev->keyboard), device(dev)
{
    model.load_option("input/xkb_model");
    variant.load_option("input/xkb_variant");
    layout.load_option("input/xkb_layout");
    options.load_option("input/xkb_option");
    rules.load_option("input/xkb_rule");

    repeat_rate.load_option("input/kb_repeat_rate");
    repeat_delay.load_option("input/kb_repeat_delay");

    setup_listeners();
    reload_input_options();
    wlr_seat_set_keyboard(wf::get_core().get_current_seat(), dev);
}

void wf_keyboard::reload_input_options()
{
    auto ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    /* Copy memory to stack, so that .c_str() is valid */
    std::string rules   = this->rules;
    std::string model   = this->model;
    std::string layout  = this->layout;
    std::string variant = this->variant;
    std::string options = this->options;

    xkb_rule_names names;
    names.rules   = rules.c_str();
    names.model   = model.c_str();
    names.layout  = layout.c_str();
    names.variant = variant.c_str();
    names.options = options.c_str();
    auto keymap = xkb_map_new_from_names(ctx, &names,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (!keymap)
    {
        LOGE("Could not create keymap with given configuration:",
            " rules=\"", rules, "\" model=\"", model, "\" layout=\"",layout,
            "\" variant=\"", variant, "\" options=\"", options, "\"");

        // reset to NULL
        std::memset(&names, 0, sizeof(names));
        keymap = xkb_map_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }

    wlr_keyboard_set_keymap(handle, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    wlr_keyboard_set_repeat_info(handle, repeat_rate, repeat_delay);
}

wf_keyboard::~wf_keyboard() { }

/* input manager things */

void input_manager::set_keyboard_focus(wayfire_view view, wlr_seat *seat)
{
    auto surface = view ? view->get_keyboard_focus_surface() : NULL;
    auto iv = interactive_view_from_view(view.get());
    auto oiv = interactive_view_from_view(keyboard_focus.get());

    if (oiv)
        oiv->handle_keyboard_leave();
    if (iv)
        iv->handle_keyboard_enter();

    /* Don't focus if we have an active grab */
    if (!active_grab)
    {
        auto kbd = wlr_seat_get_keyboard(seat);
        if (surface)
        {
            wlr_seat_keyboard_notify_enter(seat, surface,
                kbd ? kbd->keycodes : NULL,
                kbd ? kbd->num_keycodes : 0,
                kbd ? &kbd->modifiers : NULL);
        } else {
            wlr_seat_keyboard_notify_clear_focus(seat);
        }
        keyboard_focus = view;
    }
    else
    {
        wlr_seat_keyboard_notify_clear_focus(seat);
        keyboard_focus = nullptr;
    }
}

static bool check_vt_switch(wlr_session *session, uint32_t key, uint32_t mods)
{
    if (!session)
        return false;
    if (mods ^ (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL))
        return false;

    if (key < KEY_F1 || key > KEY_F10)
        return false;

    /* Somebody inhibited the output, most probably a lockscreen */
    auto output_impl =
        static_cast<wf::output_impl_t*> (wf::get_core().get_active_output());
    if (output_impl->is_inhibited())
        return false;

    int target_vt = key - KEY_F1 + 1;
    wlr_session_change_vt(session, target_vt);
    return true;
}

static uint32_t mod_from_key(wlr_seat *seat, uint32_t key)
{
    xkb_keycode_t keycode = key + 8;
    auto keyboard = wlr_seat_get_keyboard(seat);
    if (!keyboard)
        return 0; // potentially a bug?

    const xkb_keysym_t *keysyms;
    auto keysyms_len = xkb_state_key_get_syms(keyboard->xkb_state, keycode, &keysyms);

    for (int i = 0; i < keysyms_len; i++)
    {
        auto key = keysyms[i];
        if (key == XKB_KEY_Alt_L || key == XKB_KEY_Alt_R)
            return WLR_MODIFIER_ALT;
        if (key == XKB_KEY_Control_L || key == XKB_KEY_Control_R)
            return WLR_MODIFIER_CTRL;
        if (key == XKB_KEY_Shift_L || key == XKB_KEY_Shift_R)
            return WLR_MODIFIER_SHIFT;
        if (key == XKB_KEY_Super_L || key == XKB_KEY_Super_R)
            return WLR_MODIFIER_LOGO;
    }

    return 0;
}

std::vector<std::function<bool()>> input_manager::match_keys(uint32_t mod_state,
    uint32_t key, uint32_t mod_binding_key)
{
    std::vector<std::function<bool()>> callbacks;

    uint32_t actual_key = key == 0 ? mod_binding_key : key;

    for (auto& binding : bindings[WF_BINDING_KEY])
    {
        auto as_key = std::dynamic_pointer_cast<
            wf::config::option_t<wf::keybinding_t>> (binding->value);
        assert(as_key);

        if (as_key->get_value() == wf::keybinding_t{mod_state, key} &&
            binding->output == wf::get_core().get_active_output())
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->call.key;
            callbacks.push_back([actual_key, callback] () {
                return (*callback) (actual_key);
            });
        }
    }

    for (auto& binding : bindings[WF_BINDING_ACTIVATOR])
    {
        auto as_activator = std::dynamic_pointer_cast<
            wf::config::option_t<wf::activatorbinding_t>> (binding->value);
        assert(as_activator);

        if (as_activator->get_value().has_match(wf::keybinding_t{mod_state, key}) &&
            binding->output == wf::get_core().get_active_output())
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda
             *
             * Also, do not send keys for modifier bindings */
            auto callback = binding->call.activator;
            callbacks.push_back([=] () {
                return (*callback) (wf::ACTIVATOR_SOURCE_KEYBINDING,
                    mod_from_key(seat, actual_key) ? 0 : actual_key);
            });
        }
    }

    return callbacks;
}

bool input_manager::handle_keyboard_key(uint32_t key, uint32_t state)
{
    using namespace std::chrono;

    if (active_grab && active_grab->callbacks.keyboard.key)
        active_grab->callbacks.keyboard.key(key, state);

    auto mod = mod_from_key(seat, key);
    if (mod)
        handle_keyboard_mod(mod, state);

    std::vector<std::function<bool()>> callbacks;
    auto kbd = wlr_seat_get_keyboard(seat);

    if (state == WLR_KEY_PRESSED)
    {
        auto session = wlr_backend_get_session(wf::get_core().backend);
        if (check_vt_switch(session, key, get_modifiers()))
            return true;

        /* as long as we have pressed only modifiers, we should check for modifier bindings on release */
        if (mod)
        {
            bool modifiers_only = !lpointer->has_pressed_buttons()
                && (!our_touch || our_touch->gesture_recognizer.current.empty());

            for (size_t i = 0; kbd && i < kbd->num_keycodes; i++)
                if (!mod_from_key(seat, kbd->keycodes[i]))
                    modifiers_only = false;

            if (modifiers_only)
            {
                mod_binding_start = steady_clock::now();
                mod_binding_key = key;
            }
        } else
        {
            mod_binding_key = 0;
        }

        callbacks = match_keys(get_modifiers(), key);
    } else
    {
        if (mod_binding_key != 0)
        {
            int timeout = wf::option_wrapper_t<int> (
                "input/modifier_binding_timeout");
            if (timeout <= 0 ||
                duration_cast<milliseconds>(steady_clock::now() - mod_binding_start)
                    <= milliseconds(timeout))
            {
                callbacks = match_keys(get_modifiers() | mod, 0, mod_binding_key);
            }
        }

        mod_binding_key = 0;
    }

    bool keybinding_handled = false;
    for (auto call : callbacks)
        keybinding_handled |= call();

    auto iv = interactive_view_from_view(keyboard_focus.get());
    if (iv) iv->handle_key(key, state);

    return active_grab || keybinding_handled;
}

void input_manager::handle_keyboard_mod(uint32_t modifier, uint32_t state)
{
    if (active_grab && active_grab->callbacks.keyboard.mod)
        active_grab->callbacks.keyboard.mod(modifier, state);
}

