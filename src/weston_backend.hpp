#ifndef WESTON_BACKEND_HPP
#define WESTON_BACKEND_HPP

#include "commonincludes.hpp"
#include <libweston-1/compositor-drm.h>
#include <libweston-1/compositor-wayland.h>
#include <cstring>

weston_drm_backend_output_mode drm_configure_output (weston_compositor *ec, bool use_current_mode,
        const char *name, weston_drm_backend_output_config *config) {

    config->base.scale = 1;
    config->base.transform = WL_OUTPUT_TRANSFORM_NORMAL;
    config->gbm_format = NULL;
    config->seat = const_cast<char*>("");

    return WESTON_DRM_BACKEND_OUTPUT_PREFERRED;
}

void drm_configure_input_device(weston_compositor *ec, libinput_device *device) {
    /* TODO: enable tap to click, see weston/compositor/main.c */
}

int load_drm_backend(weston_compositor *ec) {
    weston_drm_backend_config config;
    std::memset(&config, 0, sizeof(config));

    config.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
    config.base.struct_size = sizeof(weston_drm_backend_config);

    config.configure_output = drm_configure_output;
    config.configure_device = drm_configure_input_device;

    config.gbm_format = NULL;
    config.connector = 0;
    config.seat_id = const_cast<char*>("seat0");
    config.use_pixman = 0;
    config.use_current_mode = 0;
    config.tty = 0;

    error << "loading driver" << std::endl;
    return weston_compositor_load_backend(ec, WESTON_BACKEND_DRM, &config.base);
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

    config.num_outputs = 1;
    config.outputs = (weston_wayland_backend_output_config*)
        std::malloc(sizeof(weston_wayland_backend_output_config));

    config.outputs[0].width = 800;
    config.outputs[0].height = 800;
    config.outputs[0].name = NULL;
    config.outputs[0].transform = WL_OUTPUT_TRANSFORM_NORMAL;
    config.outputs[0].scale = 1;

    return weston_compositor_load_backend(ec, WESTON_BACKEND_WAYLAND, &config.base);
}

#endif /* end of include guard: WESTON_BACKEND_HPP */
