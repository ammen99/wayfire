#include "panel.hpp"
#include "background.hpp"
#include <vector>

wayfire_display display;

std::vector<wayfire_panel*> panels;
std::vector<wayfire_background*> bgs;

std::string bg_path = "/home/ilex/Downloads/phoenix6.png";
void output_created_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{

    auto bg = new wayfire_background(bg_path);
    bg->create_background(output, width, height);
    bgs.push_back(bg);

    auto panel = new wayfire_panel();
    panel->create_panel(output, width, height);
    panels.push_back(panel);
}

static const struct wayfire_shell_listener bg_shell_listener = {
    .output_created = output_created_cb
};

int main()
{
    if (!setup_wayland_connection())
        return -1;

    /* TODO: parse background src from command line */
    wayfire_shell_add_listener(display.wfshell, &bg_shell_listener, 0);

    while(true) {
        if (wl_display_dispatch(display.wl_disp) < 0)
            break;
    }

    for (auto x : bgs)
        delete x;
    for (auto x : panels)
        delete x;

    wl_display_disconnect(display.wl_disp);
}
