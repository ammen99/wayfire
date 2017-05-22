#ifndef PANEL_HPP
#define PANEL_HPP

#include "common.hpp"
#include <vector>

struct widget;

class wayfire_panel {
    wl_callback *repaint_callback;

    uint32_t output;
    uint32_t width, height;

    int hidden_height = 1;

    struct {
        int dy;
        int start_y, current_y, target_y;
    } animation;

    void toggle_animation();
    void on_enter();
    void on_leave();

    void add_callback(bool swapped);

    std::vector<widget*> widgets;
    void init_widgets();

    public:
        wayfire_window *window;
        wayfire_panel();
        void create_panel(uint32_t output, uint32_t width, uint32_t height);
        void render_frame(bool first_call = false);
};

#endif /* end of include guard: PANEL_HPP */
