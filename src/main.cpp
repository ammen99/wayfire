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

    /* TODO: load non-hardcoded config file, useful for debug */
    wayfire_config *config = new wayfire_config("/home/ilex/firerc");
    core = new wayfire_core();
    core->init(ec, config);

    int ret;
    if (getenv("WAYLAND_DISPLAY") || getenv("WAYLAND_SOCKET"))
        ret = load_wayland_backend(ec);
    else
        ret = load_drm_backend(ec);

    if (ret < 0) {
        debug << "failed to load weston backend, exiting" << std::endl;
        return 0;
    }

    auto server_name = wl_display_add_socket_auto(display);
    if (!server_name) {
        error << "Failed to create listening server, bailing out" << std::endl;
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

    auto desktop = weston_desktop_create(ec, &desktop_api, NULL);
    if (!desktop) {
        error << "Failed to create weston_desktop" << std::endl;
        return -1;
    }

    weston_output *output;
    wl_list_for_each(output, &ec->output_list, link) {
        core->add_output(output);
    }

    weston_compositor_wake(ec);

    wl_display_run(display);

    return EXIT_SUCCESS;
}
