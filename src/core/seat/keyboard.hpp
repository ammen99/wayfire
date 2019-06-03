#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP

#include "config.hpp"
#include "seat.hpp"
#include "util.hpp"

struct wf_keyboard
{
    wf::wl_listener_wrapper on_key, on_modifier;
    void setup_listeners();

    wlr_keyboard *handle;
    wlr_input_device *device;

    wf_option model, variant, layout, options, rules;
    wf_option repeat_rate, repeat_delay;

    wf_keyboard(wlr_input_device *keyboard, wayfire_config *config);
    void reload_input_options();
    ~wf_keyboard();
};

#endif /* end of include guard: KEYBOARD_HPP */
