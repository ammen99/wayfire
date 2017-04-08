#include "common.hpp"
#include <cstring>
#include <algorithm>

#include <GL/gl.h>

#define WIDTH 256
#define HEIGHT 256

static void pointer_enter(void *data,
    struct wl_pointer *wl_pointer,
    uint32_t serial, struct wl_surface *surface,
    wl_fixed_t surface_x, wl_fixed_t surface_y)
{
    /*
    struct pointer_data *pointer_data;

    pointer_data = wl_pointer_get_user_data(wl_pointer);
    pointer_data->target_surface = surface;
    wl_surface_attach(pointer_data->surface,
        pointer_data->buffer, 0, 0);
    wl_surface_commit(pointer_data->surface);
    wl_pointer_set_cursor(wl_pointer, serial,
        pointer_data->surface, pointer_data->hot_spot_x,
        pointer_data->hot_spot_y);
        */
}

static void pointer_leave(void *data,
    struct wl_pointer *wl_pointer, uint32_t serial,
    struct wl_surface *wl_surface) { }

static void pointer_motion(void *data,
    struct wl_pointer *wl_pointer, uint32_t time,
    wl_fixed_t surface_x, wl_fixed_t surface_y) { }

static void pointer_button(void *data,
    struct wl_pointer *wl_pointer, uint32_t serial,
    uint32_t time, uint32_t button, uint32_t state)
{
    /*
    struct pointer_data *pointer_data;
    void (*callback)(uint32_t);

    pointer_data = wl_pointer_get_user_data(wl_pointer);
    callback = wl_surface_get_user_data(
        pointer_data->target_surface);
    if (callback != NULL)
        callback(button);
        */
}

static void pointer_axis(void *data,
    struct wl_pointer *wl_pointer, uint32_t time,
    uint32_t axis, wl_fixed_t value) { }

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis
};

// listeners
void registry_add_object (void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version)
{
	if (!strcmp(interface,"wl_compositor")) {
		display.compositor = (wl_compositor*) wl_registry_bind (registry, name, &wl_compositor_interface, std::min(version, 2u));
	} else if (!strcmp(interface,"wl_shell")) {
		display.shell = (wl_shell*) wl_registry_bind (registry, name, &wl_shell_interface, std::min(version, 2u));
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
        display.seat = (wl_seat*) wl_registry_bind(registry, name,
            &wl_seat_interface, std::min(version, 2u));
        display.pointer = wl_seat_get_pointer(display.seat);
        wl_pointer_add_listener(display.pointer, &pointer_listener,
            NULL);
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
//	struct window *window = (struct window*) data;
//	wl_egl_window_resize (window->egl_window, width, height, 0, 0);
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


wayfire_window *create_window(int32_t width, int32_t height)
{

    wayfire_window *window = new wayfire_window;
	window->surface = wl_compositor_create_surface(display.compositor);
	window->shell_surface = wl_shell_get_shell_surface(display.shell, window->surface);
	wl_shell_surface_add_listener(window->shell_surface, &shell_surface_listener, window);
	wl_shell_surface_set_toplevel(window->shell_surface);

	window->egl_window = wl_egl_window_create(window->surface, width, height);
	window->egl_surface = eglCreateWindowSurface(display.egl_display, display.egl_config,
            window->egl_window, NULL);

	eglMakeCurrent(display.egl_display, window->egl_surface, window->egl_surface, display.egl_config);

    window->cairo_surface = cairo_gl_surface_create_for_egl(display.rgb_device,
            window->egl_surface, width, height);

    wl_egl_window_resize(window->egl_window, width, height, 0, 0);
    cairo_gl_surface_set_size(window->cairo_surface, width, height);

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

