#ifndef BACKGROUND_HPP
#define BACKGROUND_HPP

#include "common.hpp"

class wayfire_background {
    void on_enter(wl_pointer*, uint32_t, int, int);

    std::string image;
    cairo_surface_t *img_surface;
    cairo_t *cr;

    public:
        wayfire_window *window;
        wayfire_background(std::string image);
        void create_background(uint32_t output, uint32_t w, uint32_t h);
        void resize(uint32_t w, uint32_t h);
};

#endif /* end of include guard: BACKGROUND_HPP */
