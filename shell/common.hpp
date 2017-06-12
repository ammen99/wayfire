#ifndef COMMON_HPP
#define COMMON_HPP

#include <wayland-client.h>
#include "../proto/wayfire-shell-client.h"
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <iostream>
#include <functional>

#include <cairo/cairo-gl.h>

extern struct wayfire_display {
    wl_compositor *compositor;
    wl_display *wl_disp;
    wl_pointer *pointer;
    wl_seat *seat;
    wl_shm *shm;
    wl_shell *shell;

    wayfire_shell *wfshell;

    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLConfig egl_config;

    cairo_device_t *rgb_device;
} display;

bool setup_wayland_connection();
void finish_wayland_connection();

struct wayfire_window {
	EGLSurface egl_surface;

	wl_surface *surface;
	wl_shell_surface *shell_surface;
	wl_egl_window *egl_window;

    std::function<void(wl_pointer*, uint32_t, int x, int y)> pointer_enter;
    std::function<void()> pointer_leave;
    std::function<void(int x, int y)> pointer_move;
    std::function<void(uint32_t button, uint32_t state)> pointer_button;

    cairo_surface_t *cairo_surface;

    bool configured = false;

    void resize(uint32_t new_w, uint32_t new_h);
};

wayfire_window* create_window(int32_t width, int32_t height);
void set_active_window(wayfire_window* window);
void delete_window(wayfire_window* window);

#endif /* end of include guard: COMMON_HPP */
