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

    void add_callback(bool swapped);

    std::vector<widget*> widgets;
    void init_widgets();

    public:
        wayfire_window *window;
        wayfire_panel(wayfire_config *config);
        void create_panel(uint32_t output, uint32_t width, uint32_t height);
        void render_frame(bool first_call = false);

        void resize(uint32_t width, uint32_t height);
};

#endif /* end of include guard: PANEL_HPP */
