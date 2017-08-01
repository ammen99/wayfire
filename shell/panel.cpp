#include "common.hpp"
#include "panel.hpp"
#include "widgets.hpp"
#include "../proto/wayfire-shell-client.h"
#include "../shared/config.hpp"

void panel_redraw(void *data, wl_callback*, uint32_t)
{
    wayfire_panel *panel = (wayfire_panel*) data;
    panel->render_frame();
}

static const struct wl_callback_listener frame_listener = {
    panel_redraw
};

wayfire_panel::wayfire_panel(wayfire_config *config)
{
    this->config = config;
    auto section = config->get_section("shell_panel");
    widget::background_color = section->get_color("background_color",
            {0.033, 0.041, 0.047, 0.9});
    widget::font_size = section->get_int("font_size", 25);
    widget::font_face = section->get_string("font_face",
            "/usr/share/fonts/dejavu/DejaVuSerif.ttf");
}

void wayfire_panel::create_panel(uint32_t output, uint32_t _width, uint32_t _height)
{
    width = _width;
    height = 1.3 * widget::font_size;

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

    using namespace std::placeholders;
    window->pointer_enter = std::bind(std::mem_fn(&wayfire_panel::on_enter), this, _1, _2);
    window->pointer_leave = std::bind(std::mem_fn(&wayfire_panel::on_leave), this);
    window->pointer_move  = std::bind(std::mem_fn(&wayfire_panel::on_motion), this, _1, _2);
    window->pointer_button= std::bind(std::mem_fn(&wayfire_panel::on_button), this, _1, _2, _3, _4);
}

void wayfire_panel::resize(uint32_t w, uint32_t h)
{
    width = w;
    window->resize(width, height);

    widgets[0]->center_x = width / 2; // first widget is the clock
    widgets[1]->center_x = width - widgets[1]->max_w / 2; // second is the battery

    need_fullredraw = true;
}

void wayfire_panel::toggle_animation()
{
    std::swap(animation.target_y, animation.start_y);
    animation.dy *= -1;
}

void wayfire_panel::on_enter(wl_pointer *ptr, uint32_t serial)
{
    show_default_cursor(serial);
    toggle_animation();
    add_callback(false);
}

void wayfire_panel::on_leave()
{
    toggle_animation();
}

void wayfire_panel::on_button(uint32_t button, uint32_t state, int x, int y)
{
    for (auto w : widgets)
    {
        if (w->pointer_button)
            w->pointer_button(button, state, x, y);
    }
}

void wayfire_panel::on_motion(int x, int y)
{
    for (auto w : widgets)
    {
        if (w->pointer_motion)
            w->pointer_motion(x, y);
    }
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
    cr = cairo_create(window->cairo_surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

    clock_widget *clock = new clock_widget();
    clock->cr = cairo_create(window->cairo_surface);
    clock->panel_h = height;

    /* FIXME: this won't work with all possible fonts and sizes */
    clock->max_w = 0.2 * width;
    clock->center_x = width / 2;

    widgets.push_back(clock);

    battery_widget *bat = new battery_widget();
    bat->cr = cairo_create(window->cairo_surface);
    bat->panel_h = height;
    bat->max_w = widget::font_size * 3;
    bat->center_x = width - bat->max_w / 2;
    widgets.push_back(bat);

    launchers_widget *launch = new launchers_widget();
    launch->cr = cairo_create(window->cairo_surface);
    launch->panel_h = height;
    launch->max_w = width / 2 - width / 5;
    launch->center_x = launch->max_w / 2;
    launch->init_launchers(config);

    widgets.push_back(launch);

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

    static int frame_count = 0;

    should_swap = should_swap || need_fullredraw;

    if (should_swap) {
        frame_count++;
        if (frame_count <= 3 || need_fullredraw) {
            render_rounded_rectangle(cr, 0, 0, width, height,
                    4, widget::background_color.r, widget::background_color.g,
                    widget::background_color.b, widget::background_color.a);
            need_fullredraw = false;
        }

        for (auto w : widgets)
            w->repaint();
    }

    if (animation.current_y != hidden_height - (int)height)
        add_callback(should_swap);

    if (should_swap)
        cairo_gl_surface_swapbuffers(window->cairo_surface);
}
