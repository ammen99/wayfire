#ifndef WIDGETS_HPP
#define WIDGETS_HPP

#include <string>
#include <cairo/cairo-ft.h>

void render_rounded_rectangle(cairo_t *cr, int x, int y, int width, int height, double radius,
        double r, double g, double b, double a);

struct widget_bg_color {
    float r, g, b, a;
};
extern struct widget_bg_color widget_background;

struct widget
{
    /* those are initialized before calling create() */
    cairo_t *cr;
    /* position in panel, panel height, maximum width */
    int center_x, panel_h, max_w;


    virtual void create() = 0;

    /* return true if widget has painted on this iter */
    virtual bool update() = 0;
};

struct clock_widget : public widget
{

    std::string current_text;

    void create();
    bool update();
};

struct battery_widget : public widget
{
    std::string battery;
    bool active = false;
    int percent_current = -1, percent_max;
    std::string icon_current;

    void create();
    bool update();
};


#endif /* end of include guard: WIDGETS_HPP */
