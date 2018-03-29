#include <cstdlib>
#include <cstring>

#include "debug-func.hpp"
#include "config.hpp"

extern "C"
{
#include <wlr/backend/multi.h>
#include <wlr/util/log.h>
}

#include <wayland-server.h>

#include "api/core.hpp"

std::ofstream wf_debug::logfile;

static wl_listener output_created;
void output_created_cb (wl_listener*, void *data)
{
    core->add_output((wlr_output*) data);
}

void compositor_wake_cb (wl_listener*, void*)
{
}

void compositor_sleep_cb (wl_listener*, void*)
{
}

int main(int argc, char *argv[])
{
    if (argc > 1) {
        wf_debug::logfile.open(argv[1]);
        wlr_log_init(L_DEBUG, NULL);
    } else {
        wf_debug::logfile.open("/dev/null");
        wlr_log_init(L_ERROR, NULL);
    }

    signal(SIGINT, signalHandle);
    signal(SIGSEGV, signalHandle);
    signal(SIGFPE, signalHandle);
    signal(SIGILL, signalHandle);
    signal(SIGABRT, signalHandle);

    core = new wayfire_core();
    core->display  = wl_display_create();
    core->ev_loop  = wl_display_get_event_loop(core->display);
    core->backend  = wlr_backend_autocreate(core->display);
    core->renderer = wlr_backend_get_renderer(core->backend);
    info << "first setup ready" << std::endl;

    /*
    auto ec = weston_compositor_create(display, NULL);

    crash_compositor = ec;
    ec->default_pointer_grab = NULL;
    ec->vt_switching = true;
    */

    std::string home_dir = secure_getenv("HOME");
    debug << "Using home directory: " << home_dir << std::endl;

    wayfire_config *config = new wayfire_config(home_dir + "/.config/wayfire.ini", -1);
    /*
    ec->repaint_msec = config->get_section("core")->get_int("repaint_msec", 16);
    ec->idle_time = config->get_section("core")->get_int("idle_time", 300);
    */
    config->set_refresh_rate(60);
    //device_config::load(config);

    core->init(config);

    auto server_name = wl_display_add_socket_auto(core->display);
    if (!server_name) {
        errio << "Failed to create listening server, bailing out" << std::endl;
        return -1;
    }

    setenv("_WAYLAND_DISPLAY", server_name, 1);

    core->wayland_display = server_name;

    output_created.notify = output_created_cb;
    wl_signal_add(&core->backend->events.new_output, &output_created);

    if (!wlr_backend_start(core->backend))
    {
        errio << "failed to start backend" << std::endl;
        wlr_backend_destroy(core->backend);
        wl_display_destroy(core->display);

        return -1;
    }

    debug << "running at server " << server_name << std::endl;
    setenv("WAYLAND_DISPLAY", server_name, 1);

//    load_xwayland(ec);

    /*
    auto desktop = weston_desktop_create(ec, &desktop_api, NULL);
    if (!desktop)
    {
        errio << "Failed to create weston_desktop" << std::endl;
        return -1;
    } */

    core->wake();

    //weston_compositor_wake(ec);

    wl_display_run(core->display);
    wl_display_destroy(core->display);

    return EXIT_SUCCESS;
}
