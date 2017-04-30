#include "core.hpp"
#include "output.hpp"
#include <cmath>

bool wayfire_grab_interface_t::grab()
{
    if (grabbed)
        return true;

    if (!output->is_plugin_active(name))
        return false;

    grabbed = true;
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
    assert(max_steps > 0);

    float prog = std::sin(current_step / max_steps * MPI);
    return prog * end + (1 - prog) * start;
}
