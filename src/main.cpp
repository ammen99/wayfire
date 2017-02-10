#include <cstdlib>
#include <cstring>
#include "output.hpp"
#include "debug.hpp"

#include <wayland-server.h>
#include <libweston-1/compositor.h>
#include <libweston-1/compositor-drm.h>
#include <libweston-1/libweston-desktop.h>
#include <libweston-1/timeline-object.h>

std::ofstream file_debug;

/* for now use drm backend only -> should support also x11 and wayland backends for testing */
void load_drm_backend(weston_compositor *ec) {
    weston_drm_backend_config config;
    std::memset(&config, 0, sizeof(config));

    config.base.struct_version = WESTON_DRM_BACKEND_CONFIG_VERSION;
    config.base.struct_size = sizeof(weston_drm_backend_config);

    //TODO: use config.configure_output and configure_input_device
    weston_compositor_load_backend(ec, WESTON_BACKEND_DRM, &config.base);
}
void surface_added(struct weston_desktop_surface *surface,
        void *user_data) {
}
void surface_removed(struct weston_desktop_surface *surface,
        void *user_data) {}



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

    auto display = wl_display_create();

//    weston_config *config = weston_config_parse("wayfire.ini");
    auto ec = weston_compositor_create(display, NULL);
    ec->idle_time = 300;
    ec->default_pointer_grab = NULL;

 //   core = new Core();
    ec->user_data = core;

    auto socket_name = wl_display_add_socket_auto(display);
    setenv("WAYLAND_SERVER", socket_name, 1);

    signal(SIGINT, signalHandle);
    signal(SIGSEGV, signalHandle);
    signal(SIGFPE, signalHandle);
    signal(SIGILL, signalHandle);
    signal(SIGABRT, signalHandle);
    signal(SIGTRAP, signalHandle);

    desktop_api.struct_size = sizeof(weston_desktop_api);
    desktop_api.surface_added = surface_added;
    desktop_api.surface_removed = surface_removed;

    weston_desktop_create(ec, &desktop_api, NULL);

    weston_compositor_wake(ec);

    wl_display_run(display);

    std::cout.rdbuf(save);
    return EXIT_SUCCESS;
}
