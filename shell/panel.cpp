#include "common.hpp"
#include "panel.hpp"
#include <chrono>
#include <ctime>
#include <unistd.h>

#include <cairo/cairo-ft.h>
#include <freetype2/ft2build.h>

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
    /* TODO: should take font size into account */
    height = 1.3 * font_size;
    this->output = output;

    window = create_window(width, this->height);

    cr = cairo_create(window->cairo_surface);

    const char * filename =
	    "/usr/share/fonts/dejavu/DejaVuSerif.ttf";

    FT_Library value;
    auto status = FT_Init_FreeType(&value);
    if (status != 0) {
        std::cerr << "failed to open freetype library" << std::endl;
        exit (EXIT_FAILURE);
    }

    FT_Face face;
    status = FT_New_Face (value, filename, 0, &face);
    if (status != 0) {
        std::cerr << "Error opening font file " << filename << std::endl;
	    exit (EXIT_FAILURE);
    }

    font = cairo_ft_font_face_create_for_ft_face (face, 0);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); /* blank to white */
    cairo_set_font_size(cr, font_size);
    cairo_set_font_face(cr, font);

    animation.dy = -5;
    animation.target_y = hidden_height - height;
    animation.start_y = 0;
    animation.current_y = animation.start_y;

    repaint_callback = nullptr;
    render_frame();

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

void render_rounded_rectangle(cairo_t *cr, int x, int y, int width, int height, double radius,
        double r, double g, double b, double a)
{
    double degrees = M_PI / 180.0;

    cairo_new_sub_path (cr);
    cairo_arc (cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc (cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc (cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc (cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path (cr);

    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_fill_preserve(cr);
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

const std::string months[] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

void wayfire_panel::render_frame()
{
    set_active_window(window);

    using std::chrono::system_clock;

    time_t now = system_clock::to_time_t(system_clock::now());
    auto time = std::localtime(&now);

    std::string time_string = std::to_string(time->tm_mday) + " " +
        months[time->tm_mon] + " " + std::to_string(time->tm_hour) +
        ":" + std::to_string(time->tm_min);

    if (animation.current_y != animation.target_y) {
        animation.current_y += animation.dy;
        wayfire_shell_configure_panel(display.wfshell, output,
                window->surface, 0, animation.current_y);
    }

    bool should_swap = false;
    if (time_string != this->current_text && animation.current_y > hidden_height - (int)height) {
        render_rounded_rectangle(cr, 0, 0, width, height, 7, 0.117, 0.137, 0.152, 1);

        cairo_text_extents_t te;
        cairo_text_extents(cr, time_string.c_str(), &te);

        double x = 5, y = 20;
        cairo_set_source_rgb(cr, 0.91, 0.918, 0.965); /* blank to white */

        x = (width - te.width) / 2;

        cairo_move_to(cr, x, y);
        cairo_show_text(cr, time_string.c_str());
        should_swap = true;
    }

    current_text = time_string;

    //std::cout << animation.current_y << std::endl;
    if (animation.current_y != hidden_height - (int)height)
        add_callback(should_swap);

    if (should_swap)
        cairo_gl_surface_swapbuffers(window->cairo_surface);
}
