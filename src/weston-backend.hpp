#ifndef WESTON_BACKEND_HPP
#define WESTON_BACKEND_HPP

#include "debug.hpp"
#include "core.hpp"
#include "../shared/config.hpp"
#include <climits>

#include <compositor-drm.h>
#include <compositor-x11.h>
#include <compositor-wayland.h>
#include <windowed-output-api.h>

#include <cstring>
#include <assert.h>
wl_listener output_pending_listener;
void set_output_pending_handler(weston_compositor *ec, wl_notify_func_t handler)
{
    output_pending_listener.notify = handler;
    wl_signal_add(&ec->output_pending_signal, &output_pending_listener);
}



bool backend_loaded = false;
std::vector<weston_output*> pending_outputs;

static wl_output_transform get_transfrom_from_string(std::string transform)
{
    if (transform == "normal")
        return WL_OUTPUT_TRANSFORM_NORMAL;
    else if (transform == "90")
        return WL_OUTPUT_TRANSFORM_90;
    else if (transform == "180")
        return WL_OUTPUT_TRANSFORM_180;
    else
        return WL_OUTPUT_TRANSFORM_270;
}

void configure_drm_backend_output (wl_listener *listener, void *data)
{
    weston_output *output = (weston_output*)data;
    if (!backend_loaded)
    {
        pending_outputs.push_back(output);
        return;
    }

    auto api = weston_drm_output_get_api(output->compositor);
    auto section = device_config::config->get_section(output->name);

    auto mode = section->get_string("mode", "current");

    if (mode == "current")
    {
        api->set_mode(output, WESTON_DRM_BACKEND_OUTPUT_CURRENT, NULL);
    } else if (mode == "preferred")
    {
        api->set_mode(output, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, NULL);
    } else if (mode == "off")
    {
        weston_output_disable(output);
    } else
    {
        api->set_mode(output, WESTON_DRM_BACKEND_OUTPUT_PREFERRED, mode.c_str());
    }

    auto transform = section->get_string("rotation", "normal");
    weston_output_set_transform(output, get_transfrom_from_string(transform));

    int scale = section->get_int("scale", 1);
    weston_output_set_scale(output, scale);

    api->set_gbm_format(output, NULL);
    api->set_seat(output, "");

    weston_output_enable(output);

    /* default is some *magic* number, we hope that no sane person would position
     * their output at INT_MIN */

    std::string default_string = std::to_string(INT_MIN) + " " + std::to_string(INT_MIN);
    auto pos = section->get_string("position", default_string);
    int x, y;
    std::sscanf(pos.c_str(), "%d %d", &x, &y);

    if (x != INT_MIN && y != INT_MIN)
        weston_output_move(output, x, y);
}

int load_drm_backend(weston_compositor *ec)
{
    weston_drm_backend_config config;
    std::memset(&config, 0, sizeof(config));

    config.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
    config.base.struct_size = sizeof(weston_drm_backend_config);
    config.configure_device = configure_input_device;

    config.gbm_format = 0;
    config.seat_id = 0;
    config.use_pixman = 0;
    config.tty = 0;

    if (device_config::drm_device != "default")
        config.specific_device = const_cast<char*> (device_config::drm_device.c_str());

    set_output_pending_handler(ec, configure_drm_backend_output);
    auto ret = weston_compositor_load_backend(ec, WESTON_BACKEND_DRM, &config.base);
    if (ret >= 0)
    {
        backend_loaded = true;
        for (auto output : pending_outputs)
            configure_drm_backend_output(&output_pending_listener, output);
        pending_outputs.clear();
    }

    core->backend = WESTON_BACKEND_DRM;
    return ret;
}

void configure_windowed_output (wl_listener *listener, void *data)
{
    weston_output *output = (weston_output*)data;
    auto api = weston_windowed_output_get_api(output->compositor);
    assert(api != NULL);

    auto section = device_config::config->get_section(output->name);
    auto transform = section->get_string("rotation", "normal");
    weston_output_set_transform(output, get_transfrom_from_string(transform));

    int scale = section->get_int("scale", 1);
    weston_output_set_scale(output, scale);

    auto resolution = section->get_string("mode", "1280x720");
    int width = 1280, height = 720;
    std::sscanf(resolution.c_str(), "%dx%d", &width, &height);

    if (api->output_set_size(output, width, height) < 0)
    {
        errio << "can't configure output " << output->name << std::endl;
        return;
    }

    weston_output_enable(output);
}


int load_wayland_backend(weston_compositor *ec)
{
    weston_wayland_backend_config config;
    std::memset(&config, 0, sizeof(config));

    config.base.struct_version = WESTON_WAYLAND_BACKEND_CONFIG_VERSION;
    config.base.struct_size = sizeof(weston_wayland_backend_config);

    config.cursor_size = 32;
    config.display_name = 0;
    config.use_pixman = 0;
    config.sprawl = 0;
    config.fullscreen = 0;
    config.cursor_theme = NULL;

    if (weston_compositor_load_backend(ec, WESTON_BACKEND_WAYLAND, &config.base) < 0)
        return -1;

    auto api = weston_windowed_output_get_api(ec);
    if (api == NULL)
        return -1;

    core->backend = WESTON_BACKEND_WAYLAND;
    set_output_pending_handler(ec, configure_windowed_output);

    if (api->output_create(ec, "wl1") < 0)
        return -1;

    return 0;
}

int load_x11_backend(weston_compositor *ec)
{
    weston_x11_backend_config config;

    config.base.struct_version = WESTON_X11_BACKEND_CONFIG_VERSION;
    config.base.struct_size = sizeof(weston_x11_backend_config);

    config.use_pixman = false;
    config.fullscreen = false;
    config.no_input = false;

    if (weston_compositor_load_backend(ec, WESTON_BACKEND_X11, &config.base) < 0)
        return -1;

    set_output_pending_handler(ec, configure_windowed_output);

    auto api = weston_windowed_output_get_api(ec);
    if (!api || api->output_create(ec, "x11") < 0)
        return -1;

    return 0;
}

#endif /* end of include guard: WESTON_BACKEND_HPP */
