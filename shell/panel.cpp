#include <sstream>
#include "panel.hpp"
#include "widgets.hpp"
#include "net.hpp"
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

static void load_misc_config(wayfire_config *config)
{
    auto section = config->get_section("shell_panel");

    widget::background_color = section->get_color("background_color",
            {0.033, 0.041, 0.047, 0.9});
    widget::font_size = section->get_int("font_size", 25);
    widget::font_face = section->get_string("font_face",
            "/usr/share/fonts/gnu-free/FreeSerif.ttf");

    battery_options::icon_path_prefix = section->get_string("battery_icon_path_prefix",
            "/usr/share/icons/Adwaita/64x64/status");
    battery_options::invert_icons = section->get_int("battery_invert_icons", 1);
    battery_options::text_scale = section->get_double("battery_text_scale", 0.6);
}

wayfire_panel::wayfire_panel(wayfire_config *config)
{
    this->config = config;
    load_misc_config(config);

    autohide = config->get_section("shell_panel")->get_int("autohide", 1);
}

void wayfire_panel::create_panel(uint32_t output, uint32_t _width, uint32_t _height)
{
    width = _width;
    height = 1.3 * widget::font_size;

    this->output = output;

    setup_window();
    init_widgets();
    render_frame(true);
}

void wayfire_panel::setup_window()
{
    window = create_window(width, height);
    cr = cairo_create(window->cairo_surface);


    animation.dy = -5;
    animation.target_y = hidden_height - height;
    animation.start_y = 0;
    animation.current_y = animation.start_y;

    repaint_callback = nullptr;

    using namespace std::placeholders;
    window->pointer_enter = std::bind(std::mem_fn(&wayfire_panel::on_enter), this, _1, _2);
    window->pointer_leave = std::bind(std::mem_fn(&wayfire_panel::on_leave), this);
    window->pointer_move  = std::bind(std::mem_fn(&wayfire_panel::on_motion), this, _1, _2);
    window->pointer_button= std::bind(std::mem_fn(&wayfire_panel::on_button), this, _1, _2, _3, _4);

    wayfire_shell_add_panel(display.wfshell, output, window->surface);
    if (!autohide)
    {
        wayfire_shell_reserve(display.wfshell, output, WAYFIRE_SHELL_PANEL_POSITION_UP, width, height);
        wayfire_shell_configure_panel(display.wfshell, output, window->surface, 0, 0);
    }

}

void wayfire_panel::resize(uint32_t w, uint32_t h)
{
    width = w;

    for_each_widget(w)
        cairo_destroy(w->cr);

    cairo_destroy(cr);

    delete_window(window);
    setup_window();

    for_each_widget(w)
        w->cr = cairo_create(window->cairo_surface);

    cr = cairo_create(window->cairo_surface);
    render_frame(true);
}

void wayfire_panel::toggle_animation()
{
    std::swap(animation.target_y, animation.start_y);
    animation.dy *= -1;
}

void wayfire_panel::on_enter(wl_pointer *ptr, uint32_t serial)
{
    show_default_cursor(serial);
    if (autohide)
        toggle_animation();
    add_callback(false);
}

void wayfire_panel::on_leave()
{
    if (autohide)
        toggle_animation();
}

void wayfire_panel::on_button(uint32_t button, uint32_t state, int x, int y)
{
    for_each_widget(w)
    {
        if (w->pointer_button)
            w->pointer_button(button, state, x, y);
    }
}

void wayfire_panel::on_motion(int x, int y)
{
    for_each_widget(w)
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

void wayfire_panel::position_widgets(position_policy policy)
{
    int widget_spacing = widget::font_size * 0.5;
    int total_width = widget_spacing;

    for (size_t i = 0; i < widgets[policy].size(); i++)
    {
        widgets[policy][i]->x = total_width;
        total_width += widgets[policy][i]->get_width();
        total_width += widget_spacing;
    }

    int delta = 0;
    if (policy == PART_RIGHT)
        delta += width - total_width;
    if (policy == PART_SYMMETRIC)
        delta += width / 2 - total_width / 2;

    for (size_t i = 0; i < widgets[policy].size(); i++)
        widgets[policy][i]->x += delta;
}

widget* wayfire_panel::create_widget_from_name(std::string name)
{
    widget *w = nullptr;
    if (name == "clock")
    {
        w = new clock_widget();
    } else if (name == "battery")
    {
        w = new battery_widget();
    } else if (name == "launchers")
    {
        auto l = new launchers_widget();
        l->init_launchers(config);
        w = l;
    } else if (name == "network")
    {
        w = new network_widget();
    }

    if (w)
    {
        w->cr = cairo_create(window->cairo_surface);
        w->panel_h = height;
        w->create();
    }

    return w;
}

void wayfire_panel::init_widgets(std::string str, position_policy policy)
{
    std::istringstream stream(str);
    std::string name;
    while(stream >> name)
        widgets[policy].push_back(create_widget_from_name(name));

    position_widgets(policy);
}

void wayfire_panel::init_widgets()
{
    cr = cairo_create(window->cairo_surface);

    auto section = config->get_section("shell_panel");
    std::string left = section->get_string("widgets_left", "");
    std::string center = section->get_string("widgets_center", "clock");
    std::string right = section->get_string("widgets_right", "");

    init_widgets(left, PART_LEFT);
    init_widgets(center, PART_SYMMETRIC);
    init_widgets(right, PART_RIGHT);
}

void wayfire_panel::render_frame(bool first_call)
{
    set_active_window(window);
    if (autohide && animation.current_y != animation.target_y) {
        animation.current_y += animation.dy;

        if (animation.current_y * animation.dy > animation.target_y * animation.dy)
            animation.current_y = animation.target_y;

        wayfire_shell_configure_panel(display.wfshell, output,
                window->surface, 0, animation.current_y);
    }

    bool should_swap = first_call;
    if (animation.current_y > hidden_height - (int)height || !autohide)
    {
        for_each_widget(w)
            should_swap |= w->update();
    }

    should_swap = should_swap || need_fullredraw;

    if (should_swap)
    {
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        render_rounded_rectangle(cr, 0, 0, width, height,
                4, widget::background_color.r, widget::background_color.g,
                widget::background_color.b, widget::background_color.a);
        need_fullredraw = false;

        for (int i = 0; i < 3; i++)
            position_widgets((position_policy) i);

        for_each_widget(w)
            w->repaint();
    }

    if (animation.current_y != hidden_height - (int)height || !autohide)
        add_callback(should_swap);

    if (should_swap)
        damage_commit_window(window);
}
