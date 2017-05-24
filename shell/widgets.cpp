#include "widgets.hpp"

#include <iostream>
#include <chrono>
#include <ctime>
#include <unistd.h>
#include <cmath>

#include <freetype2/ft2build.h>

widget_bg_color widget_background =  {
    .r = 0.117, .g = 0.137, .b = 0.152, .a = 1
};

cairo_font_face_t *font_face = nullptr;
double font_size = 20;

void load_default_font()
{
    if (font_face)
        return;

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

    font_face = cairo_ft_font_face_create_for_ft_face (face, 0);
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

/* -------------------- Clock widget ----------------- */
void clock_widget::create()
{
    load_default_font();
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); /* blank to white */
    cairo_set_font_size(cr, font_size);
    cairo_set_font_face(cr, font_face);
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

std::string format(int x)
{
    if (x < 10) {
        return "0" + std::to_string(x);
    } else {
        return std::to_string(x);
    }
}

bool clock_widget::update()
{
    using std::chrono::system_clock;

    time_t now = system_clock::to_time_t(system_clock::now());
    auto time = std::localtime(&now);

    std::string time_string = std::to_string(time->tm_mday) + " " +
        months[time->tm_mon] + " " + format(time->tm_hour) +
        ":" + format(time->tm_min);

    if (time_string != this->current_text) {
        current_text = time_string;
        return true;
    } else {
        return false;
    }
}

void clock_widget::repaint() {
    render_rounded_rectangle(cr, center_x - max_w / 2, 0, max_w, panel_h,
            7, widget_background.r, widget_background.g, widget_background.b, widget_background.a);

    cairo_text_extents_t te;
    cairo_text_extents(cr, current_text.c_str(), &te);

    double x = 5, y = 20;
    cairo_set_source_rgb(cr, 0.91, 0.918, 0.965);

    x = center_x - te.width / 2;

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, current_text.c_str());
}

/* --------------- Battery widget ---------------------- */
char buf[256];
std::string find_battery()
{
    FILE *fin = popen("/bin/sh -c \"ls /sys/class/power_supply | grep BAT\"", "r");

    while(std::fscanf(fin, "%s", buf)) {
        int len = strlen(buf);
        if (len > 0) {
            pclose(fin);
            return buf;
        }
    }
    pclose(fin);
    return "";
}

int get_battery_energy(std::string path, std::string suffix)
{
    std::string cmd = path + "/" + suffix;
    FILE *fin = fopen(cmd.c_str() , "r");
    int percent = 0;

    std::fscanf(fin, "%d", &percent);
    fclose(fin);
    return percent;
}

void battery_widget::create()
{
    battery = "/sys/class/power_supply/" + find_battery();
    if (battery == "")
        return;

    percent_max = get_battery_energy(battery, "energy_full");

    load_default_font();

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); /* blank to white */
    cairo_set_font_size(cr, font_size);
    cairo_set_font_face(cr, font_face);

    active = true;
}

bool battery_widget::update()
{
    int percent = get_battery_energy(battery, "energy_now");

    if (percent_current != percent) {
        percent_current = percent;
        return true;
    } else {
        return false;
    }
}

void battery_widget::repaint()
{
    int per = percent_current * 100LL / percent_max;
    std::string battery_string = std::to_string(per) + "%";

    render_rounded_rectangle(cr, center_x - max_w / 2, 0, max_w, panel_h,
            7, widget_background.r, widget_background.g, widget_background.b, widget_background.a);

    cairo_text_extents_t te;
    cairo_text_extents(cr, battery_string.c_str(), &te);

    double x = 5, y = 20;
    cairo_set_source_rgb(cr, 0.91, 0.918, 0.965);

    x = center_x - te.width / 2;

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, battery_string.c_str());
}
