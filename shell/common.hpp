#ifndef COMMON_HPP
#define COMMON_HPP

#include <wayland-client.h>
#include "../proto/wayfire-shell-client.h"
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <iostream>

#include <cairo/cairo-gl.h>

extern struct wayfire_display {
    wl_compositor *compositor;
    wl_display *wl_disp;
    wl_pointer *pointer;
    wl_seat *seat;
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



    cairo_surface_t *cairo_surface;

    bool configured = false;
};

wayfire_window* create_window(int32_t width, int32_t height);
void set_active_window(wayfire_window* window);
void delete_window(wayfire_window* window);

#endif /* end of include guard: COMMON_HPP */
