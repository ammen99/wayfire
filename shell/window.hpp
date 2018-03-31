#ifndef COMMON_HPP
#define COMMON_HPP

#include <wayland-client.h>
#include "xdg-shell-unstable-v6-client-protocol.h"
#include "wayfire-shell-client-protocol.h"
#include <iostream>
#include <functional>
#include <cairo.h>
#include "config.h"

extern struct wayfire_display
{
    wl_compositor *compositor;
    wl_display *wl_disp;
    wl_pointer *pointer;
    wl_seat *seat = NULL;
    wl_shm *shm;

    zxdg_shell_v6 *zxdg_shell;

    wayfire_shell *wfshell;
    wayfire_virtual_keyboard *vkbd;

    int32_t scale;
} display;

bool setup_wayland_connection();
void finish_wayland_connection();

struct wayfire_window
{
	wl_surface *surface;
    zxdg_surface_v6 *xdg_surface;
    zxdg_toplevel_v6 *toplevel;

    std::function<void(wl_pointer*, uint32_t, int x, int y)> pointer_enter;
    std::function<void()> pointer_leave;
    std::function<void(int x, int y)> pointer_move;
    std::function<void(uint32_t button, uint32_t state, int x, int y)> pointer_button;

    std::function<void(uint32_t time, int32_t id, uint32_t x, uint32_t y)> touch_down;
    std::function<void(int32_t id, uint32_t x, uint32_t y)> touch_motion;
    std::function<void(int32_t id)> touch_up;

    int scale = 1;
    void set_scale(int scale);

    cairo_surface_t *cairo_surface;

    bool configured = false;
    std::function<void()> first_configure;

    bool has_pointer_focus = false;
};

void show_default_cursor(uint32_t serial);

void delete_window(wayfire_window *window);
void render_rounded_rectangle(cairo_t *cr, int x, int y, int width, int height, double radius,
        double r, double g, double b, double a);
cairo_surface_t *cairo_try_load_png(const char *path);


extern const struct wl_shell_surface_listener shell_surface_listener;

/* the following functions are implemented by the specific backend which is enabled at
 * build time(shm-surface or egl-surface) */

bool setup_backend();
void finish_backend();

/* Once we create the window, we should wait until we get a zxdg_configure event, afterwards we can draw/whatever */
wayfire_window* create_window(uint32_t width, uint32_t height, std::function<void()> configured);

void set_active_window(wayfire_window* window);
void backend_delete_window(wayfire_window* window);
void damage_commit_window(wayfire_window *window);

#endif /* end of include guard: COMMON_HPP */
