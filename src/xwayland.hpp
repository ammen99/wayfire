#ifndef XWAYLAND_HPP
#define XWAYLAND_HPP

#include <compositor.h>
#include <xwayland-api.h>
#include "debug.hpp"
#include <sys/socket.h>
#include <string>

struct xwayland_t {
    const weston_xwayland_api *api;
    weston_xwayland* handle;

    wl_event_source *sigusr1;
    wl_client *client;
    int fd;
} xwayland;

int handle_sigusr1(int ignore, void *data) {
    xwayland.api->xserver_loaded(xwayland.handle, xwayland.client, xwayland.fd);
    wl_event_source_remove(xwayland.sigusr1);
    return 0;
}

pid_t spawn_callback(void *data, const char *display, int abstract_fd, int unix_fd) {
    info << "Xwayland display: " << display << std::endl;

    core->xwayland_display = display;
    setenv("DISPLAY", display, 1);
    int sv[2], wm[2];

    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        errio << "Can't create first socket pair" << std::endl;
        return 1;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wm) < 0) {
        errio << "Can't create second socket pair" << std::endl;
        return 1;
    }

    int pid = fork();
    if (pid == 0) {
        int fd = dup(sv[1]);
        setenv("WAYLAND_SOCKET", std::to_string(fd).c_str(), 1);

        std::string abstractstr, unixstr, wmstr;

        fd = dup(abstract_fd);
        abstractstr = std::to_string(fd);
        fd = dup(unix_fd);
        unixstr = std::to_string(fd);
        fd = dup(wm[1]);
        wmstr = std::to_string(fd);

        signal(SIGUSR1, SIG_IGN);

        auto path = "/usr/bin/Xwayland";
        if (execl(path, path, display, "-rootless", "-listen", abstractstr.c_str(),
                    "-listen", unixstr.c_str(), "-wm", wmstr.c_str(), "-terminate", NULL) < 0) {
            errio << "failed to execute server" << std::endl;
        }

        _exit(EXIT_FAILURE);
    } else if (pid == -1) {
        errio << "failed to fork Xwayland" << std::endl;
    } else {
        close(sv[1]);
        xwayland.client = wl_client_create(core->ec->wl_display, sv[0]);
        close(wm[1]);
        xwayland.fd = wm[0];
    }

    return pid;
}

int load_xwayland(weston_compositor *ec) {
    if (weston_compositor_load_xwayland(ec) < 0)
        return -1;

    xwayland.api = weston_xwayland_get_api(ec);
    if (!xwayland.api)
        return -1;

    xwayland.handle = xwayland.api->get(ec);
    if (!xwayland.handle)
        return -1;

    if (xwayland.api->listen(xwayland.handle, &xwayland, spawn_callback) < 0) {
        errio << "Can't listen for xwayland" << std::endl;
        return -1;
    }

    auto loop = wl_display_get_event_loop(core->ec->wl_display);
    xwayland.sigusr1 = wl_event_loop_add_signal(loop, SIGUSR1, handle_sigusr1, NULL);
    return 0;
}

#endif /* end of include guard: XWAYLAND_HPP */
