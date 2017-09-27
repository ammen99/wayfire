#ifndef PANEL_HPP
#define PANEL_HPP

#include "common.hpp"
#include <vector>

struct widget;
class wayfire_config;

class wayfire_panel {
    wl_callback *repaint_callback;
    cairo_t *cr;

    uint32_t output;
    uint32_t width, height;

    int hidden_height = 1;
    bool need_fullredraw = false;

    struct {
        int dy;
        int start_y, current_y, target_y;
    } animation;

    void toggle_animation();

    void on_enter(wl_pointer*, uint32_t);
    void on_leave();
    void on_button(uint32_t, uint32_t, int, int);
    void on_motion(int, int);

    void add_callback(bool swapped);

    std::vector<widget*> widgets[3];
#define for_each_widget(w) for(int i = 0; i < 3; i++) for (auto w : widgets[i])

    widget *create_widget_from_name(std::string name);
    enum position_policy
    {
        PART_LEFT      = 0, /* widgets are placed on the left */
        PART_SYMMETRIC = 1, /* widgets are ordered symmetrically around the center */
        PART_RIGHT     = 2  /* widgets are placed on the right */
    };

    /* only reposition widgets, when for ex. output has been resized */
    void position_widgets(position_policy policy);

    void init_widgets(std::string str, position_policy policy);

    /* load widget list and position them */
    void init_widgets();


    wayfire_config *config;

    public:
        wayfire_window *window;
        wayfire_panel(wayfire_config *config);
        void create_panel(uint32_t output, uint32_t width, uint32_t height);
        void render_frame(bool first_call = false);

        void resize(uint32_t width, uint32_t height);
};

#endif /* end of include guard: PANEL_HPP */
