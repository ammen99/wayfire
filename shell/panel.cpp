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
    std::cout << "frame cb" << std::endl;
    wayfire_panel *panel = (wayfire_panel*) data;
    panel->render_frame();
}

void output_created_cb(void *data, wayfire_shell *wayfire_shell, wl_output *output,
        uint32_t width, uint32_t height)
{
    wayfire_panel *panel = (wayfire_panel*) data;
    panel->create_panel(output, width, height);
}

static const struct wl_callback_listener frame_listener = {
    panel_redraw
};

static const struct wayfire_shell_listener shell_listener = {
};

wayfire_panel::wayfire_panel()
{
    wayfire_shell_add_listener(display.wfshell, &shell_listener, this);
}

void wayfire_panel::create_panel(wl_output *output, uint32_t width, uint32_t height)
{
    this->width = width;
    this->height = 60;
    this->output = output;

    window = create_window(width, height);

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

    repaint_callback = nullptr;
    render_frame();

    wayfire_shell_reserve(display.wfshell, output, WAYFIRE_SHELL_PANEL_POSITION_DOWN, width, height);
    wayfire_shell_add_panel(display.wfshell, output, window->surface, 0, 0);
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

void wayfire_panel::render_frame()
{
    set_active_window(window);

    double font_size = 20;
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); /* blank to white */
    cairo_set_font_size(cr, font_size);
    cairo_set_font_face(cr, font);
    //cairo_paint(cr);

    using std::chrono::system_clock;
    time_t now = system_clock::to_time_t(system_clock::now());

    const char *time_string = std::ctime(&now);

    render_rounded_rectangle(cr, 0, 0, width, font_size * 1.3, 7, 0.102, 0.137, 0.494, 1);

    double x = 5, y = 20;
    cairo_set_source_rgb(cr, 0.91, 0.918, 0.965); /* blank to white */

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, time_string);

    //cairo_stroke(cr);
    //cairo_destroy(cr);

    usleep(1e6*59./60.);

    if (repaint_callback)
        wl_callback_destroy(repaint_callback);

    repaint_callback = wl_surface_frame(window->surface);
    wl_callback_add_listener(repaint_callback, &frame_listener, this);

    cairo_gl_surface_swapbuffers(window->cairo_surface);
}
