#ifndef PANEL_HPP
#define PANEL_HPP

#include "common.hpp"

class wayfire_panel {
    cairo_t *cr;
    cairo_font_face_t *font;
    wl_callback *repaint_callback;

    uint32_t output;
    uint32_t width, height;

    double font_size = 20;
    int hidden_height = 1;

    std::string current_text;

    struct {
        int dy;
        int start_y, current_y, target_y;
    } animation;

    void toggle_animation();
    void on_enter();
    void on_leave();

    void add_callback(bool swapped);

    public:
        wayfire_window *window;
        wayfire_panel();
        void create_panel(uint32_t output, uint32_t width, uint32_t height);
        void render_frame();
};

#endif /* end of include guard: PANEL_HPP */
