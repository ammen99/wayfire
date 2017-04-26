#include "background.hpp"
#include "../proto/wayfire-shell-client.h"

wayfire_background::wayfire_background(std::string image)
{
    this->image = image;
}

void wayfire_background::create_background(uint32_t output, uint32_t w, uint32_t h)
{
    auto cursor_theme = wl_cursor_theme_load(NULL, 16, display.shm);

    const char* alternatives[] = {
        "left_ptr", "default",
        "top_left_arrow", "left-arrow"
    };

    for (int i = 0; i < 4 && !cursor; i++)
        cursor = wl_cursor_theme_get_cursor(
            cursor_theme, alternatives[i]);

    if (!cursor)
        std::cout << "failed to load cursor" << std::endl;

    cursor_surface = wl_compositor_create_surface(display.compositor);

    window = create_window(w, h);
    wayfire_shell_add_background(display.wfshell, output, window->surface, 0, 0);

    std::cout << "create background" << std::endl;
    auto cr = cairo_create(window->cairo_surface);

    using namespace std::placeholders;
    window->pointer_enter = std::bind(std::mem_fn(&wayfire_background::on_enter),
            this, _1, _2, _3, _4);

    set_active_window(window);

    cairo_surface_t *img_surf = cairo_image_surface_create_from_png(image.c_str());
    double img_w = cairo_image_surface_get_width(img_surf);
    double img_h = cairo_image_surface_get_height(img_surf);

    cairo_rectangle(cr, 0, 0, w, h);
//    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_scale(cr, 1.0 * w / img_w, 1.0 * h / img_h);
    std::cout << img_surf << std::endl;
    cairo_set_source_surface(cr, img_surf, 0, 0);

    cairo_fill(cr);

    cairo_gl_surface_swapbuffers(window->cairo_surface);
}

void wayfire_background::on_enter(wl_pointer *ptr, uint32_t serial, int x, int y)
{
    auto image = cursor->images[0];
    auto buffer = wl_cursor_image_get_buffer(image);

    wl_surface_attach(cursor_surface, buffer, 0, 0);
    wl_surface_damage(cursor_surface, 0, 0, image->width, image->height);
    wl_surface_commit(cursor_surface);

    wl_pointer_set_cursor(ptr, serial, cursor_surface, image->hotspot_x, image->hotspot_y);
}
