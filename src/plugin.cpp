#include "core.hpp"
#include "output.hpp"
#include <cmath>
#include "input-manager.hpp"

bool wayfire_grab_interface_t::grab()
{
    if (grabbed)
        return true;

    if (!output->is_plugin_active(name))
        return false;

    grabbed = true;

    /* unset modifiers, otherwise clients may not receive
     * the release event for them, as the release usually happens in a grab */
    auto kbd = weston_seat_get_keyboard(core->get_current_seat());
    weston_keyboard_send_modifiers(kbd,
            wl_display_get_serial(core->ec->wl_display),
            0, 0, 0, 0);

    core->input->grab_input(this);
    return true;
}

void wayfire_grab_interface_t::ungrab()
{
    if (!grabbed)
        return;

    grabbed = false;
    core->input->ungrab_input(this);
}

void wayfire_plugin_t::fini() {}

const float MPI = 3.1415926535 / 2;

float GetProgress(float start, float end, float current_step, float max_steps)
{
    if (max_steps <= 1e-4)
        return end;

    float c = current_step / max_steps;
    float prog = std::sin(c * MPI);
    return prog * end + (1 - prog) * start;
}
