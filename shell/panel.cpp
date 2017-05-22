#include "common.hpp"
#include "panel.hpp"
#include "widgets.hpp"
#include "../proto/wayfire-shell-client.h"

void panel_redraw(void *data, wl_callback*, uint32_t)
{
    wayfire_panel *panel = (wayfire_panel*) data;
    panel->render_frame();
}

static const struct wl_callback_listener frame_listener = {
    panel_redraw
};

wayfire_panel::wayfire_panel()
{
}

void wayfire_panel::create_panel(uint32_t output, uint32_t _width, uint32_t _height)
{
    width = _width;
    height = 1.3 * 20;

    this->output = output;

    window = create_window(width, height);

    animation.dy = -5;
    animation.target_y = hidden_height - height;
    animation.start_y = 0;
    animation.current_y = animation.start_y;

    repaint_callback = nullptr;
    init_widgets();
    render_frame(true);

    //wayfire_shell_reserve(display.wfshell, output, WAYFIRE_SHELL_PANEL_POSITION_UP, width, hidden_height);
    wayfire_shell_add_panel(display.wfshell, output, window->surface);

    window->pointer_enter = std::bind(std::mem_fn(&wayfire_panel::on_enter), this);
    window->pointer_leave = std::bind(std::mem_fn(&wayfire_panel::on_leave), this);
}

void wayfire_panel::toggle_animation()
{
    std::swap(animation.target_y, animation.start_y);
    animation.dy *= -1;
}

void wayfire_panel::on_enter()
{
    toggle_animation();
    add_callback(false);
}

void wayfire_panel::on_leave()
{
    toggle_animation();
}

void wayfire_panel::add_callback(bool swapped)
{
    if (repaint_callback)
        wl_callback_destroy(repaint_callback);

    repaint_callback = wl_surface_frame(window->surface);
    wl_callback_add_listener(repaint_callback, &frame_listener, this);

    if (!swapped)
        wl_surface_commit(window->surface);
}

void wayfire_panel::init_widgets()
{
    cairo_t *cr = cairo_create(window->cairo_surface);
    render_rounded_rectangle(cr, 0, 0, width, height,
            4, widget_background.r, widget_background.g,
            widget_background.b, widget_background.a);

    clock_widget *clock = new clock_widget();
    clock->cr = cairo_create(window->cairo_surface);
    clock->panel_h = height;

    /* FIXME: this won't work with all possible fonts and sizes */
    clock->max_w = 100;
    clock->center_x = width / 2;

    widgets.push_back(clock);

    for (auto w : widgets)
        w->create();
}

void wayfire_panel::render_frame(bool first_call)
{
    set_active_window(window);
    if (animation.current_y != animation.target_y) {
        animation.current_y += animation.dy;

        if (animation.current_y * animation.dy > animation.target_y * animation.dy)
            animation.current_y = animation.target_y;

        wayfire_shell_configure_panel(display.wfshell, output,
                window->surface, 0, animation.current_y);
    }

    bool should_swap = first_call;
    if (animation.current_y > hidden_height - (int)height) {
        for (auto w : widgets) {
            if (w->update())
                should_swap = true;
        }
    }

    if (animation.current_y != hidden_height - (int)height)
        add_callback(should_swap);

    if (should_swap)
        cairo_gl_surface_swapbuffers(window->cairo_surface);
}
