#include "panel.hpp"
#include "background.hpp"
#include "gamma.hpp"
#include "../shared/config.hpp"
#include <vector>
#include <map>

wayfire_display display;

struct wayfire_shell_output {
    wayfire_panel* panel;
    wayfire_background *background;
    gamma_adjust *gamma;
};

wayfire_config *config;

std::map<uint32_t, wayfire_shell_output> outputs;

std::string bg_path;
void output_created_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{
    if (bg_path != "none") {
        auto bg = (outputs[output].background = new wayfire_background(bg_path));
        bg->create_background(output, width, height);
    }

    auto panel = (outputs[output].panel = new wayfire_panel(config));
    panel->create_panel(output, width, height);
}

void output_resized_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{
    auto it = outputs.find(output);
    if (it == outputs.end())
        return;

    if (it->second.background)
        it->second.background->resize(width, height);
    if (it->second.panel)
        it->second.panel->resize(width, height);
}

bool gamma_adjust_enabled;

void output_gamma_size_cb(void *data, wayfire_shell *shell, uint32_t output,
        uint32_t size)
{
    if (size > 0 && gamma_adjust_enabled)
        outputs[output].gamma = new gamma_adjust(output, size, config);
}

static const struct wayfire_shell_listener bg_shell_listener = {
    .output_created = output_created_cb,
    .output_resized = output_resized_cb,
    .gamma_size = output_gamma_size_cb
};

int main()
{
    std::string home_dir = secure_getenv("HOME");
    config = new wayfire_config(home_dir + "/.config/wayfire.ini");
    auto section = config->get_section("shell");

    bg_path = section->get_string("background", "none");

    gamma_adjust_enabled = section->get_int("color_temp_enabled", 0);
    if (!setup_wayland_connection())
        return -1;

    /* TODO: parse background src from command line */
    wayfire_shell_add_listener(display.wfshell, &bg_shell_listener, 0);

    while(true) {
        if (wl_display_dispatch(display.wl_disp) < 0)
            break;
    }

    for (auto x : outputs) {
        if (x.second.panel)
            delete x.second.panel;
        if (x.second.background)
            delete x.second.background;
        if (x.second.gamma)
            delete x.second.gamma;
    }

    wl_display_disconnect(display.wl_disp);
}
