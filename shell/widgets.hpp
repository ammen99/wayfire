#ifndef WIDGETS_HPP
#define WIDGETS_HPP

#include <string>
#include <cairo-ft.h>
#include <functional>
#include <thread>
#include "../shared/config.hpp"

void render_rounded_rectangle(cairo_t *cr, int x, int y, int width, int height, double radius,
        double r, double g, double b, double a);

extern cairo_font_face_t *cairo_font_face;

struct widget
{
    static wayfire_color background_color;
    static int32_t font_size;
    static std::string font_face;

    /* those are initialized before calling create() */
    cairo_t *cr;
    /* position in panel, panel height, maximum width */
    int center_x, panel_h, max_w;


    virtual void create() = 0;

    /* return true if widget has to be painted on this iter */
    virtual bool update() = 0;

    virtual void repaint() = 0;

    std::function<void(int x, int y)> pointer_motion = nullptr;
    std::function<void(uint32_t, uint32_t, int, int)> pointer_button = nullptr;
};

struct clock_widget : public widget
{
    std::string current_text;

    void create();
    bool update();
    void repaint();
};

struct battery_info;
struct upower_backend;

struct battery_options
{
    static std::string icon_path_prefix;
    static bool invert_icons;
    static float text_scale;
};

struct battery_widget : public widget
{
    bool active = false;

    cairo_surface_t *icon_surface = nullptr;

    battery_info *info;
    upower_backend *backend;
    std::thread backend_thread;

    void create();
    bool update();
    void repaint();
};

struct launcher;
struct launchers_widget : public widget
{
    bool need_repaint = true;

    std::vector<launcher*> launchers;
    void init_launchers(wayfire_config *config);

    void create();
    bool update();
    void repaint();

    void on_pointer_button(uint32_t state, int x, int y);
    void on_pointer_motion(int x, int y);
};

#endif /* end of include guard: WIDGETS_HPP */
