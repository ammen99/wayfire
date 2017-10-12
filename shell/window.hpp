#ifndef COMMON_HPP
#define COMMON_HPP

#include <wayland-client.h>
#include "../proto/wayfire-shell-client.h"
#include <iostream>
#include <functional>
#include <cairo.h>
#include "config.h"

extern struct wayfire_display
{
    wl_compositor *compositor;
    wl_display *wl_disp;
    wl_pointer *pointer;
    wl_seat *seat;
    wl_shm *shm;
    wl_shell *shell;

    wayfire_shell *wfshell;
} display;

bool setup_wayland_connection();
void finish_wayland_connection();

struct wayfire_window
{
	wl_surface *surface;
	wl_shell_surface *shell_surface;

    std::function<void(wl_pointer*, uint32_t, int x, int y)> pointer_enter;
    std::function<void()> pointer_leave;
    std::function<void(int x, int y)> pointer_move;
    std::function<void(uint32_t button, uint32_t state, int x, int y)> pointer_button;

    cairo_surface_t *cairo_surface;

    bool configured = false;
};

void show_default_cursor(uint32_t serial);

void delete_window(wayfire_window *window);

extern const struct wl_shell_surface_listener shell_surface_listener;

/* the following functions are implemented by the specific backend which is enabled at
 * build time(shm-surface or egl-surface) */

bool setup_backend();
void finish_backend();

wayfire_window* create_window(uint32_t width, uint32_t height);
void set_active_window(wayfire_window* window);
void backend_delete_window(wayfire_window* window);
void damage_commit_window(wayfire_window *window);

#endif /* end of include guard: COMMON_HPP */
