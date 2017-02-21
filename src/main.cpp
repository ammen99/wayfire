#include <cstdlib>
#include <cstring>
#include "output.hpp"
#include "debug.hpp"
#include "weston_backend.hpp"
#include "desktop_api.hpp"

#include <wayland-server.h>
#include <libweston-1/timeline-object.h>

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

    auto display = wl_display_create();

    /* TODO: load non-hardcoded config file, useful for debug */
    weston_config *config = weston_config_parse("/home/ilex/firerc");
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

    core = new wayfire_core();
    ec->user_data = core;
    core->init(ec, config);

    load_wayland_backend(ec);

    auto socket_name = wl_display_add_socket_auto(display);
    if (!socket_name) {
        error << "Failed to create listening socket, bailing out" << std::endl;
        return -1;
    }

    debug << "running at socket " << socket_name << std::endl;
    setenv("WAYLAND_SERVER", socket_name, 1);
    signal(SIGINT, signalHandle);
    signal(SIGSEGV, signalHandle);
    signal(SIGFPE, signalHandle);
    signal(SIGILL, signalHandle);
    signal(SIGABRT, signalHandle);

    desktop_api.struct_size = sizeof(weston_desktop_api);
    desktop_api.surface_added = desktop_surface_added;
    desktop_api.surface_removed = desktop_surface_removed;
    desktop_api.committed = desktop_surface_commited;

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
