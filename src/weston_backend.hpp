#ifndef WESTON_BACKEND_HPP
#define WESTON_BACKEND_HPP

#include "commonincludes.hpp"
#include "core.hpp"
#include <compositor-drm.h>
#include <compositor-x11.h>
#include <compositor-wayland.h>
#include <windowed-output-api.h>
#include <cstring>
#include <assert.h>
#include <libinput.h>

wl_listener output_pending_listener;
void set_output_pending_handler(weston_compositor *ec, wl_notify_func_t handler)
{
    output_pending_listener.notify = handler;
    wl_signal_add(&ec->output_pending_signal, &output_pending_listener);
}

void configure_input_device(weston_compositor *ec, libinput_device *device)
{
    if (libinput_device_config_tap_get_finger_count(device) > 0)
    {
        /* TODO: read option from config */
        libinput_device_config_tap_set_enabled(device, LIBINPUT_CONFIG_TAP_ENABLED);
    }
}

bool backend_loaded = false;
std::vector<weston_output*> pending_outputs;

void configure_drm_backend_output (wl_listener *listener, void *data)
{
    weston_output *output = (weston_output*)data;
    if (!backend_loaded)
    {
        pending_outputs.push_back(output);
        return;
    }

    auto api = weston_drm_output_get_api(output->compositor);

    weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
    weston_output_set_scale(output, 1);

    api->set_gbm_format(output, NULL);
    api->set_mode(output, WESTON_DRM_BACKEND_OUTPUT_CURRENT, NULL);
    api->set_seat(output, "");

    weston_output_enable(output);
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

const int default_width = 800, default_height = 450;
void configure_windowed_output (wl_listener *listener, void *data)
{
    weston_output *output = (weston_output*)data;
    auto api = weston_windowed_output_get_api(output->compositor);
    assert(api != NULL);

    weston_output_set_scale(output, 1);
    weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
    if (api->output_set_size(output, default_width, default_height) < 0) {
        errio << "can't configure output " << output->id << std::endl;
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
    if (!api || api->output_create(ec, "wl1") < 0)
        return -1;

    return 0;
}

#endif /* end of include guard: WESTON_BACKEND_HPP */
