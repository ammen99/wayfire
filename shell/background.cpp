#include "background.hpp"
#include "../proto/wayfire-shell-client.h"

wayfire_background::wayfire_background(std::string image)
{
    this->image = image;
}

void wayfire_background::create_background(uint32_t output, uint32_t w, uint32_t h)
{
    this->output = output;
    window = create_window(w, h);
    wayfire_shell_add_background(display.wfshell, output, window->surface, 0, 0);

    using namespace std::placeholders;
    window->pointer_enter = std::bind(std::mem_fn(&wayfire_background::on_enter),
            this, _1, _2, _3, _4);

    cr = cairo_create(window->cairo_surface);
    img_surface = cairo_image_surface_create_from_png(image.c_str());

    set_active_window(window);

    double img_w = cairo_image_surface_get_width(img_surface);
    double img_h = cairo_image_surface_get_height(img_surface);

    cairo_rectangle(cr, 0, 0, w, h);
    cairo_scale(cr, 1.0 * w / img_w, 1.0 * h / img_h);
    cairo_set_source_surface(cr, img_surface, 0, 0);

    cairo_fill(cr);

    damage_commit_window(window);
}

void wayfire_background::resize(uint32_t w, uint32_t h)
{
    cairo_destroy(cr);
    cairo_surface_destroy(img_surface);
    delete_window(window);
    create_background(output, w, h);
}

void wayfire_background::on_enter(wl_pointer *ptr, uint32_t serial, int x, int y)
{
    show_default_cursor(serial);
}
