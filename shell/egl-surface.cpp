#include <cairo-gl.h>
#include <EGL/egl.h>
#include <wayland-egl.h>
#include "window.hpp"

namespace
{
    EGLDisplay egl_display;
    EGLContext egl_context;
    EGLConfig egl_config;

    cairo_device_t *argb_device;
}

struct egl_window : public wayfire_window
{
	EGLSurface egl_surface;
	wl_egl_window *egl_window;
};

bool setup_backend()
{
    egl_display = eglGetDisplay((EGLNativeDisplayType)display.wl_disp);

	if (!eglInitialize(egl_display, NULL, NULL))
    {
        std::cerr << "Failed to initialize EGL" << std::endl;
        return false;
    }

	if (!eglBindAPI(EGL_OPENGL_API))
    {
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
	if (!eglChooseConfig(egl_display, attributes, &egl_config, 1, &num_config))
    {
        std::cerr << "Failed to choose EGL config" << std::endl;
        return false;
    }

	egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, NULL);
    if (egl_context == NULL)
    {
        std::cerr << "Failed to create EGL context" << std::endl;
        return false;
    }

    argb_device = cairo_egl_device_create(egl_display, egl_context);
    if (argb_device == NULL)
    {
        std::cerr << "Failed to create cairo device" << std::endl;
        return false;
    }

    return true;
}

void finish_backend()
{
    eglDestroyContext (egl_display, egl_context);
}

wayfire_window *create_window(uint32_t width, uint32_t height)
{
    egl_window *window = new egl_window;
	window->surface = wl_compositor_create_surface(display.compositor);
    wl_surface_set_user_data(window->surface, window);

	window->shell_surface = wl_shell_get_shell_surface(display.shell, window->surface);
	wl_shell_surface_add_listener(window->shell_surface, &shell_surface_listener, window);
	wl_shell_surface_set_toplevel(window->shell_surface);

	window->egl_window = wl_egl_window_create(window->surface, width, height);
	window->egl_surface = eglCreateWindowSurface(egl_display, egl_config,
            (EGLNativeWindowType)window->egl_window, NULL);

	eglMakeCurrent(egl_display, window->egl_surface, window->egl_surface, egl_config);

    window->cairo_surface = cairo_gl_surface_create_for_egl(argb_device,
            window->egl_surface, width, height);

    wl_egl_window_resize(window->egl_window, width, height, 0, 0);
    cairo_gl_surface_set_size(window->cairo_surface, width, height);

    window->cairo_surface = cairo_surface_reference(window->cairo_surface);
    return window;
}

void set_active_window(wayfire_window *w)
{
    auto window = static_cast<egl_window*> (w);

    cairo_device_flush(argb_device);
    cairo_device_acquire(argb_device);
	eglMakeCurrent(egl_display, window->egl_surface, window->egl_surface, egl_config);
}

void backend_delete_window(wayfire_window *w)
{
    auto window = static_cast<egl_window*> (w);

	eglDestroySurface (egl_display, window->egl_surface);
	wl_egl_window_destroy (window->egl_window);
    delete window;
}

void damage_commit_window(wayfire_window *w)
{
    auto window = static_cast<egl_window*> (w);
    cairo_gl_surface_swapbuffers(window->cairo_surface);
}
