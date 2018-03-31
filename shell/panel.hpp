#ifndef PANEL_HPP
#define PANEL_HPP

#include "window.hpp"
#include <vector>

struct widget;
class wayfire_config;

class wayfire_panel {
    wl_callback *repaint_callback;
    cairo_t *cr;

    uint32_t output;
    uint32_t width, height;

    int hidden_height = 1;
    int autohide = 0;
    bool need_fullredraw = false;

    struct {
        int dy = 0;
        int y, target;
    } animation;

    timeval timer_target;
    enum animation_state
    { WAITING = (1 << 0),
      ANIMATING = (1 << 1),
      HIDDEN = (1 << 2),
      SHOWN = (1 << 3)
    };
    uint32_t state = HIDDEN;

    void show(int delay_ms);
    void hide(int delay_ms);

    int count_input = 0;
    void on_enter(uint32_t);
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
    void reinit_widgets_context();

    void setup_window();

    wayfire_config *config;

    public:
        wayfire_window *window;
        wayfire_panel(wayfire_config *config, uint32_t output, uint32_t width, uint32_t height);
        ~wayfire_panel();
        void create_panel();
        void render_frame(bool first_call = false);
        void set_autohide(bool ah);

        void resize(uint32_t width, uint32_t height);
};

#endif /* end of include guard: PANEL_HPP */
