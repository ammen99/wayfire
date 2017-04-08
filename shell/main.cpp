#include "panel.hpp"

wayfire_display display;

int main()
{
    if (!setup_wayland_connection())
        return -1;

    auto panel = new wayfire_panel();

    while(true) {
        wl_display_dispatch(display.wl_disp);
    }
}
