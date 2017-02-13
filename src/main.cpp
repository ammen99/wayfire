#include <cstdlib>
#include <cstring>
#include "output.hpp"
#include "debug.hpp"

#include <wayland-server.h>
#include <libweston-1/compositor.h>
#include <libweston-1/compositor-drm.h>
#include <libweston-1/compositor-wayland.h>
#include <libweston-1/libweston-desktop.h>
#include <libweston-1/timeline-object.h>

std::ofstream file_debug;

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

    return weston_compositor_load_backend(ec,WESTON_BACKEND_WAYLAND, &config.base);
}


void surface_added(struct weston_desktop_surface *surface,
        void *user_data) {
}
void surface_removed(struct weston_desktop_surface *surface,
        void *user_data) {}


weston_compositor *crash_compositor;

weston_desktop_api desktop_api;
int main(int argc, char *argv[]) {
    std::streambuf *save = std::cout.rdbuf();
    /* setup logging */
    /* first argument is a log file */
    if (argc > 1) {
        file_debug.open(argv[1]);
        std::cout.rdbuf(file_debug.rdbuf());
    } else {
        file_debug.open("/dev/null");
    }

    weston_log_set_handler(vlog, vlog_continue);
    wl_log_set_handler_server(wayland_log_handler);

    auto display = wl_display_create();

//    weston_config *config = weston_config_parse("wayfire.ini");
    auto ec = weston_compositor_create(display, NULL);
    crash_compositor = ec;
    ec->idle_time = 300;
    ec->kb_repeat_rate = 40;
    ec->kb_repeat_delay = 400;
    ec->default_pointer_grab = NULL;
    ec->vt_switching = true;

    xkb_rule_names names;
    names.rules = NULL;
    names.model = NULL;
    names.variant = NULL;
    names.layout = NULL;
    names.options = NULL;

    weston_compositor_set_xkb_rule_names(ec, &names);
    //load_wayland_backend(ec);
    error << "pre-loading" << std::endl;
    /*
    if (load_drm_backend(ec) < 0) {
        error << "could not load drm backend, exiting" << std::endl;
        return -1;
    }
    */
    error << "drm loaded" << std::endl;

 //   core = new Core();
    ec->user_data = core;

    load_wayland_backend(ec);

    auto socket_name = wl_display_add_socket_auto(display);
    if (!socket_name) {
        error << "Failed to create listening socket, bailing out" << std::endl;
        return -1;
    }

    error << "socket name is" << socket_name << std::endl;
    setenv("WAYLAND_SERVER", socket_name, 1);
    signal(SIGINT, signalHandle);
    signal(SIGSEGV, signalHandle);
    signal(SIGFPE, signalHandle);
    signal(SIGILL, signalHandle);
    signal(SIGABRT, signalHandle);

    desktop_api.struct_size = sizeof(weston_desktop_api);
    desktop_api.surface_added = surface_added;
    desktop_api.surface_removed = surface_removed;

    auto desktop = weston_desktop_create(ec, &desktop_api, NULL);
    if (!desktop) {
        error << "Failed to create weston_desktop" << std::endl;
        return -1;
    }

    weston_compositor_wake(ec);

    wl_display_run(display);

    std::cout.rdbuf(save);
    return EXIT_SUCCESS;
}
