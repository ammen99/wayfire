#include "common.hpp"
#include <cstring>
#include <algorithm>

#include <GL/gl.h>

wayfire_window *current_window = nullptr;

void pointer_enter(void *data, struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    auto window = (wayfire_window*) wl_surface_get_user_data(surface);
    if (window && window->pointer_enter)
        window->pointer_enter(wl_pointer, serial,
                wl_fixed_to_int(surface_x), wl_fixed_to_int(surface_y));
    else if (window)
        current_window = window;
}

void pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
    struct wl_surface *surface)
{
    auto window = (wayfire_window*) wl_surface_get_user_data(surface);
    if (window && window->pointer_leave)
        window->pointer_leave();
    current_window = nullptr;
}

void pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
    wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    if (current_window && current_window->pointer_move)
        current_window->pointer_move(wl_fixed_to_int(surface_x), wl_fixed_to_int(surface_y));
}

void pointer_button(void *data, struct wl_pointer *wl_pointer, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state)
{
    if (current_window && current_window->pointer_button)
        current_window->pointer_button(button, state);
}

void pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
    uint32_t axis, wl_fixed_t value)
{
}
void pointer_frame(void *data, struct wl_pointer *wl_pointer) {}
void pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
        uint32_t axis_source) {}
void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time,
        uint32_t axis) {}
void pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis,
        int32_t discrete) {}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete
};

// listeners
void registry_add_object(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version)
{
	if (!strcmp(interface,"wl_compositor")) {
		display.compositor = (wl_compositor*) wl_registry_bind (registry, name,
                &wl_compositor_interface, std::min(version, 2u));
	} else if (!strcmp(interface,"wl_shell")) {
		display.shell = (wl_shell*) wl_registry_bind(registry, name,
                &wl_shell_interface, std::min(version, 2u));
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
        display.seat = (wl_seat*) wl_registry_bind(registry, name,
            &wl_seat_interface, std::min(version, 2u));
        display.pointer = wl_seat_get_pointer(display.seat);
        wl_pointer_add_listener(display.pointer, &pointer_listener,
            NULL);
    } else if (strcmp(interface, "wl_shm") == 0) {
        display.shm = (wl_shm*) wl_registry_bind(registry, name, &wl_shm_interface,
                std::min(version, 1u));
    } else if (strcmp(interface, wayfire_shell_interface.name) == 0) {
        display.wfshell = (wayfire_shell*) wl_registry_bind(registry, name, &wayfire_shell_interface,
                std::min(version, 1u));
    }
}

void registry_remove_object(void *data, struct wl_registry *registry, uint32_t name)
{
}
static struct wl_registry_listener registry_listener =
{
    &registry_add_object,
    &registry_remove_object
};

void shell_surface_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial)
{
	wl_shell_surface_pong(shell_surface, serial);
}

static void shell_surface_configure(void *data, struct wl_shell_surface *shell_surface, uint32_t edges,
        int32_t width, int32_t height)
{
    ((wayfire_window*) data)->configured = true;
}

static void shell_surface_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static struct wl_shell_surface_listener shell_surface_listener =
{
    &shell_surface_ping,
    &shell_surface_configure,
    &shell_surface_popup_done
};

bool setup_egl()
{
    display.egl_display = eglGetDisplay (display.wl_disp);
	if (!eglInitialize(display.egl_display, NULL, NULL)) {
        std::cerr << "Failed to initialize EGL" << std::endl;
        return false;
    }

	if (!eglBindAPI(EGL_OPENGL_API)) {
        std::cerr << "Failed to bind EGL API" << std::endl;
        return false;
    }

	EGLint attributes[] =
    {
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
	    EGL_NONE
    };

	EGLint num_config;
	if (!eglChooseConfig(display.egl_display, attributes, &display.egl_config, 1, &num_config)) {
        std::cerr << "Failed to choose EGL config" << std::endl;
        return false;
    }

	display.egl_context = eglCreateContext(display.egl_display, display.egl_config, EGL_NO_CONTEXT, NULL);
    if (display.egl_context == NULL) {
        std::cerr << "Failed to create EGL context" << std::endl;
        return false;
    }

    display.rgb_device = cairo_egl_device_create(display.egl_display, display.egl_context);
    if (display.rgb_device == NULL) {
        std::cerr << "Failed to create cairo device" << std::endl;
        return false;
    }

    return true;
}

void finish_egl()
{
    eglDestroyContext (display.egl_display, display.egl_context);
}

void wayfire_window::resize(uint32_t width, uint32_t height)
{
    wl_egl_window_resize(egl_window, width, height, 0, 0);
    cairo_gl_surface_set_size(cairo_surface, width, height);
}

wayfire_window *create_window(int32_t width, int32_t height)
{
    wayfire_window *window = new wayfire_window;
	window->surface = wl_compositor_create_surface(display.compositor);
    wl_surface_set_user_data(window->surface, window);

	window->shell_surface = wl_shell_get_shell_surface(display.shell, window->surface);
	wl_shell_surface_add_listener(window->shell_surface, &shell_surface_listener, window);
	wl_shell_surface_set_toplevel(window->shell_surface);

	window->egl_window = wl_egl_window_create(window->surface, width, height);
	window->egl_surface = eglCreateWindowSurface(display.egl_display, display.egl_config,
            window->egl_window, NULL);

	eglMakeCurrent(display.egl_display, window->egl_surface, window->egl_surface, display.egl_config);

    window->cairo_surface = cairo_gl_surface_create_for_egl(display.rgb_device,
            window->egl_surface, width, height);

    window->resize(width, height);
    window->cairo_surface = cairo_surface_reference(window->cairo_surface);
    return window;
}

void set_active_window(wayfire_window *window)
{
    cairo_device_flush(display.rgb_device);
    cairo_device_acquire(display.rgb_device);
	eglMakeCurrent(display.egl_display, window->egl_surface, window->egl_surface, display.egl_config);
}

void delete_window(wayfire_window *window)
{
	eglDestroySurface (display.egl_display, window->egl_surface);
	wl_egl_window_destroy (window->egl_window);
	wl_shell_surface_destroy (window->shell_surface);
	wl_surface_destroy (window->surface);
}

bool setup_wayland_connection()
{
    display.wl_disp = wl_display_connect(NULL);

    if (!display.wl_disp) {
        std::cerr << "Failed to connect to display!" << std::endl;
        std::exit(-1);
    }

    wl_registry *registry = wl_display_get_registry(display.wl_disp);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display.wl_disp);
    wl_registry_destroy(registry);

    if (!setup_egl())
        return false;

    return true;
}

