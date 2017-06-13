#include "panel.hpp"
#include "background.hpp"
#include "gamma.hpp"
#include "../shared/config.hpp"
#include <vector>

wayfire_display display;

std::vector<wayfire_panel*> panels;
std::vector<wayfire_background*> bgs;
std::vector<gamma_adjust*> gammas;

std::string bg_path;
void output_created_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{
    if (bg_path != "none") {
        auto bg = new wayfire_background(bg_path);
        bg->create_background(output, width, height);
        bgs.push_back(bg);
    }

    auto panel = new wayfire_panel();
    panel->create_panel(output, width, height);
    panels.push_back(panel);
}

struct {
    bool enabled;
    int day_start, day_end;
    int day_t, night_t;
} gamma_ops;

void output_gamma_size_cb(void *data, wayfire_shell *shell, uint32_t output,
        uint32_t size)
{
    if (size > 0 && gamma_ops.enabled) {
        auto gamma = new gamma_adjust(output, size, gamma_ops.day_t,
                gamma_ops.night_t, gamma_ops.day_start, gamma_ops.day_end);
        gammas.push_back(gamma);
    }
}

static const struct wayfire_shell_listener bg_shell_listener = {
    .output_created = output_created_cb,
    .gamma_size = output_gamma_size_cb
};

int main()
{
    std::string home_dir = secure_getenv("HOME");
    auto config = new wayfire_config(home_dir + "/.config/wayfire.ini");
    auto section = config->get_section("shell");

    bg_path = section->get_string("background", "none");

    gamma_ops.enabled = section->get_int("color_temp_enabled", 0);
    if (gamma_ops.enabled) {
        std::string s = section->get_string("day_start", "08:00");
        int h, m;
        sscanf(s.c_str(), "%d:%d", &h, &m);

        gamma_ops.day_start = h * 60 + m;

        s = section->get_string("day_end", "20:00");
        sscanf(s.c_str(), "%d:%d", &h, &m);
        gamma_ops.day_end = h * 60 + m;

        gamma_ops.day_t = section->get_int("day_temperature", 6500);
        gamma_ops.night_t = section->get_int("night_temperature", 4500);
    }

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
    for (auto x : gammas)
        delete x;

    wl_display_disconnect(display.wl_disp);
}
