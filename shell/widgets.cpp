#include "widgets.hpp"

#include <iostream>
#include <chrono>
#include <ctime>
#include <unistd.h>
#include <sys/wait.h>
#include <cmath>
#include <wayland-client.h>
#include <linux/input-event-codes.h>

#include <freetype2/ft2build.h>

wayfire_color widget::background_color;
int32_t widget::font_size;
std::string widget::font_face;

cairo_font_face_t *cairo_font_face = nullptr;

void load_default_font()
{
    if (cairo_font_face)
        return;

    FT_Library value;
    auto status = FT_Init_FreeType(&value);
    if (status != 0) {
        std::cerr << "failed to open freetype library" << std::endl;
        exit (EXIT_FAILURE);
    }

    FT_Face face;
    status = FT_New_Face (value, widget::font_face.c_str(), 0, &face);
    if (status != 0) {
        std::cerr << "Error opening font file " << widget::font_face << std::endl;
	    exit (EXIT_FAILURE);
    }

    cairo_font_face = cairo_ft_font_face_create_for_ft_face (face, 0);
}

void render_rounded_rectangle(cairo_t *cr, int x, int y, int width, int height,
        double radius, double r, double g, double b, double a)
{
    double degrees = M_PI / 180.0;

    cairo_new_sub_path (cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
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
    cairo_set_font_face(cr, cairo_font_face);
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
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    render_rounded_rectangle(cr, center_x - max_w / 2, 0, max_w, panel_h, 7,
            widget::background_color.r, widget::background_color.g,
            widget::background_color.b, widget::background_color.a);

    cairo_text_extents_t te;
    cairo_text_extents(cr, current_text.c_str(), &te);

    double x, y = font_size;
    cairo_set_source_rgb(cr, 0.91, 0.918, 0.965);

    x = center_x - te.width / 2;

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, current_text.c_str());
}

/* --------------- Battery widget ---------------------- */
char buf[256];
std::string find_battery()
{
    FILE *fin = popen("/bin/sh -c \"ls /sys/class/power_supply | grep BAT\"",
            "r");

    if (fin == NULL)
        return "";

    while(std::fgets(buf, 255, fin))
    {
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
    if (!fin)
        return -1;

    int percent = 0;

    std::fscanf(fin, "%d", &percent);
    fclose(fin);
    return percent;
}

void battery_widget::create()
{
    battery = find_battery();
    if (battery == "") {
        active = false;
        return;
    }

    /* battery ends with a newline, so erase the last symbol */
    battery = "/sys/class/power_supply/" + battery.substr(0, battery.size() - 1);
    percent_max = get_battery_energy(battery, "energy_full");

    load_default_font();

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); /* blank to white */
    cairo_set_font_size(cr, font_size);
    cairo_set_font_face(cr, cairo_font_face);

    active = true;
}

bool battery_widget::update()
{
    if (!active)
        return false;

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
    if (!active)
        return;

    int per = percent_current * 100LL / percent_max;
    std::string battery_string = std::to_string(per) + "%";

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    render_rounded_rectangle(cr, center_x - max_w / 2, 0, max_w, panel_h, 7,
            widget::background_color.r, widget::background_color.g,
            widget::background_color.b, widget::background_color.a);

    cairo_text_extents_t te;
    cairo_text_extents(cr, battery_string.c_str(), &te);

    double x = 0, y = font_size;
    cairo_set_source_rgb(cr, 0.91, 0.918, 0.965);

    x = center_x - te.width / 2;

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, battery_string.c_str());
}

/* --------------- Launchers widget ---------------------- */
struct launcher
{
    cairo_surface_t *img;
    std::string command;

    float scale;

    int x, y, size;
    bool active = false;
};

void execute(std::string cmd)
{
    pid_t pid = fork();

    /* The following is a "hack" for disowning the child processes,
     * otherwise they will simply stay as zombie processes */
    if (!pid)
    {
        if (!fork())
        {
            exit(execl("/bin/sh", "/bin/sh", "-c", cmd.c_str(), NULL));
        } else
        {
            exit(0);
        }
    } else
    {
        int status;
        waitpid(pid, &status, 0);
    }
}

/* format in config file:
 * launcher1_icon
 * launcher1_cmd
 */

constexpr float default_launcher_scale = 0.9;
constexpr float hover_launcher_scale = 1.1;

void launchers_widget::init_launchers(wayfire_config *config)
{
    auto section = config->get_section("shell_panel");

    for (int i = 0; i < 10; i++)
    {
        std::string icon = section->get_string("launcher" + std::to_string(i) + "_icon", "");
        std::string cmd =  section->get_string("launcher" + std::to_string(i) + "_cmd", "");

        if (icon == "" || cmd == "")
            continue;

        launcher *l = new launcher;
        l->scale = default_launcher_scale;
        l->img = cairo_image_surface_create_from_png(icon.c_str());
        l->command = cmd;

        launchers.push_back(l);
    }
}

#define pointer_in_launcher(l,x,y) (l->x <= x && l->y <= y && \
                l->x + l->size > x && l->y + l->size > y)

void launchers_widget::create()
{
    pointer_motion = [=] (int x, int y)
    {
        for (auto l : launchers)
        {
            bool was_active = l->active;
            if (pointer_in_launcher(l, x, y))
            {
                l->scale = hover_launcher_scale;
                l->active = true;
            } else {
                l->scale = default_launcher_scale;
                l->active = false;
            }

            if (was_active != l->active)
                need_repaint = true;
        }
    };

    pointer_button = [=] (uint32_t button, uint32_t state, int x, int y)
    {
        if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED)
            return;

        for (auto l : launchers)
        {
            if (pointer_in_launcher(l, x, y))
                execute(l->command);
        }
    };

}

void launchers_widget::repaint()
{
    const int icon_offset = font_size * 0.5;
    const int base_icon_size = font_size * 1.1;

    cairo_identity_matrix(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    render_rounded_rectangle(cr, center_x - max_w / 2, 0, max_w, panel_h, 7,
            widget::background_color.r, widget::background_color.g,
            widget::background_color.b, widget::background_color.a);

    cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);

    for (size_t i = 0; i < launchers.size(); i++)
    {
        int icon_size = base_icon_size * launchers[i]->scale;

        int y = (panel_h - icon_size) / 2;
        int x = icon_offset + i * (base_icon_size + icon_offset) - (icon_size - base_icon_size) / 2;

        launchers[i]->x = x;
        launchers[i]->y = y;
        launchers[i]->size = icon_size;

        double img_w = cairo_image_surface_get_width(launchers[i]->img);
        double img_h = cairo_image_surface_get_height(launchers[i]->img);

        cairo_identity_matrix(cr);
        cairo_new_path(cr);

        float scale_w = 1.0 * icon_size / img_w;
        float scale_h = 1.0 * icon_size / img_h;
        cairo_scale(cr, scale_w, scale_h);

        cairo_rectangle(cr, x / scale_w, y / scale_h, icon_size / scale_w, icon_size / scale_h);
        cairo_set_source_rgba(cr, 1, 1, 1, 1);
        cairo_set_source_surface(cr, launchers[i]->img, x / scale_w, y / scale_h);
        cairo_fill(cr);
    }

    need_repaint = false;
}

bool launchers_widget::update()
{
    return need_repaint;
}
