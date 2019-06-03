#include <string.h>
#include <linux/input-event-codes.h>

extern "C"
{
#include <xkbcommon/xkbcommon.h>
#include <wlr/backend/session.h>
}

#include "keyboard.hpp"
#include "../core-impl.hpp"
#include "cursor.hpp"
#include "touch.hpp"
#include "input-manager.hpp"
#include "compositor-view.hpp"
#include "input-inhibit.hpp"

void wf_keyboard::setup_listeners()
{
    on_key.set_callback([&] (void *data)
    {
        auto ev = static_cast<wlr_event_keyboard_key*> (data);

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

wf_keyboard::wf_keyboard(wlr_input_device *dev, wayfire_config *config)
    : handle(dev->keyboard), device(dev)
{
    auto section = config->get_section("input");

    model   = section->get_option("xkb_model", "");
    variant = section->get_option("xkb_variant", "");
    layout  = section->get_option("xkb_layout", "");
    options = section->get_option("xkb_option", "");
    rules   = section->get_option("xkb_rule", "");

    repeat_rate  = section->get_option("kb_repeat_rate", "40");
    repeat_delay = section->get_option("kb_repeat_delay", "400");

    setup_listeners();
    reload_input_options();
    wlr_seat_set_keyboard(wf::get_core().get_current_seat(), dev);
}

void wf_keyboard::reload_input_options()
{
    auto ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    auto rules = this->rules->as_string();
    auto model = this->model->as_string();
    auto layout = this->layout->as_string();
    auto variant = this->variant->as_string();
    auto options = this->options->as_string();

    xkb_rule_names names;
    names.rules   = rules.c_str();
    names.model   = model.c_str();
    names.layout  = layout.c_str();
    names.variant = variant.c_str();
    names.options = options.c_str();

    auto keymap = xkb_map_new_from_names(ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(handle, keymap);

    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    wlr_keyboard_set_repeat_info(handle, repeat_rate->as_int(),
                                 repeat_delay->as_int());
}

wf_keyboard::~wf_keyboard() { }

/* input manager things */

void input_manager::set_keyboard_focus(wayfire_view view, wlr_seat *seat)
{
    auto kbd = wlr_seat_get_keyboard(seat);

    auto surface = view ? view->get_keyboard_focus_surface() : NULL;
    auto iv = interactive_view_from_view(view.get());
    auto oiv = interactive_view_from_view(keyboard_focus.get());

    if (oiv)
        oiv->handle_keyboard_leave();
    if (iv)
        iv->handle_keyboard_enter();

    /* Don't focus if we have an active grab */
    if (kbd != NULL && !active_grab)
    {
        wlr_seat_keyboard_notify_enter(seat, surface, kbd->keycodes,
            kbd->num_keycodes, &kbd->modifiers);
    } else
    {
        wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
    }

    keyboard_focus = view;
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
    if (is_output_inhibited(wf::get_core().get_active_output()))
        return false;

    int target_vt = key - KEY_F1 + 1;
    wlr_session_change_vt(session, target_vt);
    return true;
}

static uint32_t mod_from_key(wlr_seat *seat, uint32_t key)
{
    xkb_keycode_t keycode = key + 8;
    auto keyboard = wlr_seat_get_keyboard(seat);
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

std::vector<std::function<void()>> input_manager::match_keys(uint32_t mod_state, uint32_t key, uint32_t mod_binding_key)
{
    std::vector<std::function<void()>> callbacks;

    uint32_t actual_key = key == 0 ? mod_binding_key : key;

    for (auto& binding : bindings[WF_BINDING_KEY])
    {
        if (binding->value->as_cached_key().matches({mod_state, key}) &&
            binding->output == wf::get_core().get_active_output())
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->call.key;
            callbacks.push_back([actual_key, callback] () {
                (*callback) (actual_key);
            });
        }
    }

    for (auto& binding : bindings[WF_BINDING_ACTIVATOR])
    {
        if (binding->value->matches_key({mod_state, key}) &&
            binding->output == wf::get_core().get_active_output())
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda
             *
             * Also, do not send keys for modifier bindings */
            auto callback = binding->call.activator;
            callbacks.push_back([=] () {
                (*callback) (ACTIVATOR_SOURCE_KEYBINDING,
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

    std::vector<std::function<void()>> callbacks;
    auto kbd = wlr_seat_get_keyboard(seat);

    if (state == WLR_KEY_PRESSED)
    {
        auto session = wlr_backend_get_session(wf::get_core().backend);
        if (check_vt_switch(session, key, get_modifiers()))
            return true;

        /* as long as we have pressed only modifiers, we should check for modifier bindings on release */
        if (mod)
        {
            bool modifiers_only = !cursor->count_pressed_buttons
                && (!our_touch || our_touch->gesture_recognizer.current.empty());

            for (size_t i = 0; i < kbd->num_keycodes; i++)
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
            auto section = wf::get_core().config->get_section("input");
            auto timeout = section->get_option("modifier_binding_timeout", "0")->as_int();
            if (timeout <= 0 ||
                duration_cast<milliseconds>(steady_clock::now() - mod_binding_start)
                    <= milliseconds(timeout))
            {
                callbacks = match_keys(get_modifiers() | mod, 0, mod_binding_key);
            }
        }

        mod_binding_key = 0;
    }

    for (auto call : callbacks)
        call();

    auto iv = interactive_view_from_view(keyboard_focus.get());
    if (iv) iv->handle_key(key, state);

    return active_grab || !callbacks.empty();
}

void input_manager::handle_keyboard_mod(uint32_t modifier, uint32_t state)
{
    if (active_grab && active_grab->callbacks.keyboard.mod)
        active_grab->callbacks.keyboard.mod(modifier, state);
}

