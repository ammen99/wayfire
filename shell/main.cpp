#include "panel.hpp"
#include "gamma.hpp"
#include "../shared/config.hpp"
#include <vector>
#include <map>
#include <getopt.h>
#include <signal.h>

struct wayfire_shell_output {
    wayfire_panel* panel;
    gamma_adjust *gamma;
};

wayfire_config *config;

std::map<uint32_t, wayfire_shell_output> outputs;

std::string bg_path;

void output_created_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{
    outputs[output].panel = new wayfire_panel(config, output, width, height);
    //wayfire_shell_output_fade_in_start(wayfire_shell, output);
}

void output_resized_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{
    auto it = outputs.find(output);
    if (it == outputs.end())
        return;

    if (it->second.panel)
        it->second.panel->resize(width, height);
}

void output_destroyed_cb(void *data, wayfire_shell *wayfire_shell, uint32_t output)
{
    auto it = outputs.find(output);

    if (it == outputs.end())
        return;

    if (it->second.panel)
        delete it->second.panel;
    if (it->second.gamma)
        delete it->second.gamma;
}

void output_autohide_panels_cb(void *data, wayfire_shell *wayfire_shell, uint32_t output, uint32_t autohide)
{
    auto it = outputs.find(output);

    if (it == outputs.end())
        return;

    if (it->second.panel)
        it->second.panel->set_autohide(autohide);
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
    .output_destroyed = output_destroyed_cb,
    .output_autohide_panels = output_autohide_panels_cb,
    .gamma_size = output_gamma_size_cb,
};

int main(int argc, char *argv[])
{
    std::string home_dir = secure_getenv("HOME");
    std::string config_file = home_dir + "/.config/wayfire.ini";

    struct option opts[] = {
        { "config",   required_argument, NULL, 'c' },
        { 0,          0,                 NULL,  0  }
    };

    int c, i;
    while((c = getopt_long(argc, argv, "c:l:", opts, &i)) != -1)
    {
        switch(c)
        {
            case 'c':
                config_file = optarg;
                break;
            default:
                std::cerr << "failed to parse option " << optarg << std::endl;
        }
    }

    config = new wayfire_config(config_file);
    auto section = config->get_section("shell");

    bg_path = section->get_string("background", "none");

    gamma_adjust_enabled = section->get_int("color_temp_enabled", 0);
    if (!setup_wayland_connection())
        return -1;

outputs[0].panel = new wayfire_panel(config, 0, 600, 100);

    //wayfire_shell_add_listener(display.wfshell, &bg_shell_listener, 0);

    while(true) {
        if (wl_display_dispatch(display.wl_disp) < 0)
            break;
    }

    for (auto x : outputs) {
        if (x.second.panel)
            delete x.second.panel;
        if (x.second.gamma)
            delete x.second.gamma;
    }

    finish_wayland_connection();
}
