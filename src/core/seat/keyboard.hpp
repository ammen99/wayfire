#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP

#include "seat.hpp"
#include "util.hpp"
#include <option-wrapper.hpp>

struct wf_keyboard
{
    wf::wl_listener_wrapper on_key, on_modifier;
    void setup_listeners();

    wlr_keyboard *handle;
    wlr_input_device *device;

    wf::option_wrapper_t<std::string>
        model, variant, layout, options, rules;
    wf::option_wrapper_t<int>repeat_rate, repeat_delay;

    wf_keyboard(wlr_input_device *keyboard);
    void reload_input_options();
    ~wf_keyboard();
};

#endif /* end of include guard: KEYBOARD_HPP */
