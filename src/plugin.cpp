#include "core.hpp"
#include "output.hpp"
#include "input-manager.hpp"
#include "signal-definitions.hpp"
#include "debug.hpp"
#include <cmath>

bool wayfire_grab_interface_t::grab()
{
    if (grabbed)
        return true;

    if (!output->is_plugin_active(name))
        return false;

    grabbed = true;

    /* unset modifiers, otherwise clients may not receive
     * the release event for them, as the release usually happens in a grab */
//    auto kbd = weston_seat_get_keyboard(core->get_current_seat());
 //   if (kbd)
    {
        // TODO: check how wlr handles grabs
        /*
        weston_keyboard_send_modifiers(kbd,
                                       wl_display_get_serial(core->ec->wl_display),
                                       0, 0, 0, 0);
                                       */
    }

    return core->input->grab_input(this);
}

void wayfire_grab_interface_t::ungrab()
{
    if (!grabbed)
        return;

    grabbed = false;
    core->input->ungrab_input();
}

bool wayfire_grab_interface_t::is_grabbed()
{
    return grabbed;
}

void wayfire_plugin_t::fini() {}
wayfire_plugin_t::~wayfire_plugin_t() {}

wayfire_view get_signaled_view(signal_data *data)
{
    auto conv = static_cast<_view_signal*> (data);
    if (!conv)
    {
        log_error("Got a bad _view_signal");
        return nullptr;
    }

    return conv->view;
}

bool get_signaled_state(signal_data *data)
{
    auto conv = static_cast<_view_state_signal*> (data);

    if (!conv || !conv->view)
    {
        log_error ("Got a bad _view_state_signal");
        return false;
    }

    return conv->state;
}



const float MPI = 3.1415926535;

float GetProgress(float start, float end, float current_step, float max_steps)
{
    if (max_steps <= 1e-4)
        return end;

    float c = current_step / max_steps;
    float prog = std::sqrt(2 * c - c * c);
    return prog * end + (1 - prog) * start;
}
