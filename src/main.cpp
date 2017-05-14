#include <cstdlib>
#include <cstring>

#include "output.hpp"
#include "debug.hpp"
#include "config.hpp"

#include "weston_backend.hpp"
#include "desktop_api.hpp"
#include "xwayland.hpp"

#include <wayland-server.h>

std::ofstream file_info, file_null, *file_debug;
weston_compositor *crash_compositor;

void output_created_cb (wl_listener*, void *data)
{
    auto output = (weston_output*) data;
    core->add_output(output);
}

void compositor_wake_cb (wl_listener*, void*)
{
    core->wake();
}

void compositor_sleep_cb (wl_listener*, void*)
{
    core->sleep();
}

weston_desktop_api desktop_api;
int main(int argc, char *argv[]) {
    char *debugging_enabled = secure_getenv("WAYFIRE_DEBUG");

    if (argc > 1) {
        file_info.open(argv[1]);
    } else {
        file_info.open("/dev/null");
    }

    if (debugging_enabled) {
        file_debug = &file_info;
    } else {
        file_null.open("/dev/null");
        file_debug = &file_null;
    }

    weston_log_set_handler(vlog, vlog_continue);
    wl_log_set_handler_server(wayland_log_handler);

    signal(SIGINT, signalHandle);
    signal(SIGSEGV, signalHandle);
    signal(SIGFPE, signalHandle);
    signal(SIGILL, signalHandle);
    signal(SIGABRT, signalHandle);

    auto display = wl_display_create();

    auto ec = weston_compositor_create(display, NULL);

    crash_compositor = ec;
    ec->idle_time = 300;
    ec->repaint_msec = 16;
    ec->default_pointer_grab = NULL;
    ec->vt_switching = true;

    /* TODO: load non-hardcoded config file, useful for debug */
    wayfire_config *config = new wayfire_config("/home/ilex/firerc");
    core = new wayfire_core();
    core->init(ec, config);

    wl_listener output_created_listener;
    output_created_listener.notify = output_created_cb;
    wl_signal_add(&ec->output_created_signal, &output_created_listener);

    wl_listener ec_wake_listener, ec_sleep_listener;
    ec_wake_listener.notify  = compositor_wake_cb;
    ec_sleep_listener.notify = compositor_sleep_cb;

    wl_signal_add(&ec->idle_signal, &ec_sleep_listener);
    wl_signal_add(&ec->wake_signal, &ec_wake_listener);

    int ret;
    if (getenv("WAYLAND_DISPLAY") || getenv("WAYLAND_SOCKET"))
        ret = load_wayland_backend(ec);
    else
        ret = load_drm_backend(ec);

    if (ret < 0) {
        debug << "failed to load weston backend, exiting" << std::endl;
        return 0;
    }

    core->hijack_renderer();

    auto server_name = wl_display_add_socket_auto(display);
    if (!server_name) {
        errio << "Failed to create listening server, bailing out" << std::endl;
        return -1;
    }

    debug << "running at server " << server_name << std::endl;
    setenv("WAYLAND_SERVER", server_name, 1);
    core->wayland_display = server_name;

    load_xwayland(ec);

    desktop_api.struct_size = sizeof(weston_desktop_api);
    desktop_api.surface_added = desktop_surface_added;
    desktop_api.surface_removed = desktop_surface_removed;
    desktop_api.committed = desktop_surface_commited;
    desktop_api.move = desktop_surface_move;
    desktop_api.resize = desktop_surface_resize;
    desktop_api.maximized_requested = desktop_surface_maximized_requested;
    desktop_api.fullscreen_requested = desktop_surface_fullscreen_requested;
    desktop_api.set_xwayland_position = desktop_surface_set_xwayland_position;

    auto desktop = weston_desktop_create(ec, &desktop_api, NULL);
    if (!desktop) {
        errio << "Failed to create weston_desktop" << std::endl;
        return -1;
    }

    /*
    weston_output *output;
    wl_list_for_each(output, &ec->output_list, link) {
        core->add_output(output);
    }
    */

    core->wake();
    weston_compositor_wake(ec);

    wl_display_run(display);

    return EXIT_SUCCESS;
}
