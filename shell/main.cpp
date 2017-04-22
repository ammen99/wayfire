#include "panel.hpp"
#include "background.hpp"

wayfire_display display;

wayfire_panel *panel;
wayfire_background *background;

void output_created_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{

    background->create_background(output, width, height);
    panel->create_panel(output, width, height);
}

static const struct wayfire_shell_listener bg_shell_listener = {
    .output_created = output_created_cb
};

int main()
{
    if (!setup_wayland_connection())
        return -1;

    /* TODO: parse background src from command line */
    background = new wayfire_background("/home/ilex/Downloads/phoenix6.png");
    panel = new wayfire_panel();

    wayfire_shell_add_listener(display.wfshell, &bg_shell_listener, 0);

    while(true) {
        wl_display_dispatch(display.wl_disp);
    }
}
