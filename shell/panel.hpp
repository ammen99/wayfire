#ifndef PANEL_HPP
#define PANEL_HPP

#include "common.hpp"

class wayfire_panel {
    cairo_t *cr;
    cairo_font_face_t *font;
    wl_callback *repaint_callback;
    wl_output *output;

    uint32_t width, height;

    public:
        wayfire_window *window;
        wayfire_panel();
        void create_panel(wl_output *output, uint32_t width, uint32_t height);
        void render_frame();
};

#endif /* end of include guard: PANEL_HPP */
