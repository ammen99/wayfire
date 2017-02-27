#ifndef WESTON_BACKEND_HPP
#define WESTON_BACKEND_HPP

#include "commonincludes.hpp"
#include <libweston-2/compositor-drm.h>
#include <libweston-2/compositor-wayland.h>
#include <libweston-2/windowed-output-api.h>
#include <cstring>
#include <assert.h>

//TODO: drm backend doesn't work since libweston 2.0
//
wl_listener output_pending_listener;
void set_output_pending_handler(weston_compositor *ec, wl_notify_func_t handler) {
    output_pending_listener.notify = handler;
    wl_signal_add(&ec->output_pending_signal, &output_pending_listener);
}

int load_drm_backend(weston_compositor *ec) {
    weston_drm_backend_config config;
    std::memset(&config, 0, sizeof(config));

    config.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
    config.base.struct_size = sizeof(weston_drm_backend_config);


    config.gbm_format = NULL;
    config.connector = 0;
    config.seat_id = const_cast<char*>("seat0");
    config.use_pixman = 0;
    config.tty = 0;

    error << "loading driver" << std::endl;
    return weston_compositor_load_backend(ec, WESTON_BACKEND_DRM, &config.base);
}

const int default_width = 1600, default_height = 900;
void configure_wayland_backend_output (wl_listener *listener, void *data) {
    weston_output *output = (weston_output*)data;
    auto api = weston_windowed_output_get_api(output->compositor);
    assert(api != NULL);

    weston_output_set_scale(output, 1);
    weston_output_set_transform(output, WL_OUTPUT_TRANSFORM_NORMAL);
    if (api->output_set_size(output, default_width, default_height) < 0) {
        error << "can't configure output " << output->id << std::endl;
        return;
    }

    weston_output_enable(output);
}


int load_wayland_backend(weston_compositor *ec) {
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

    if (weston_compositor_load_backend(ec, WESTON_BACKEND_WAYLAND, &config.base) < 0) {
        return -1;
    }
    auto api = weston_windowed_output_get_api(ec);
    if (api == NULL)
        return -1;

    set_output_pending_handler(ec, configure_wayland_backend_output);

    if (api->output_create(ec, "wl1") < 0)
        return -1;
    return 0;
}

#endif /* end of include guard: WESTON_BACKEND_HPP */
