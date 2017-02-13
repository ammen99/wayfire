#ifndef VIEW_HPP
#define VIEW_HPP
#include "commonincludes.hpp"
#include <libweston-1/libweston-desktop.h>
#include <vector>
#include <memory>

class wayfire_view_transform {
    public: // applied to all views
        static glm::mat4 global_rotation;
        static glm::mat4 global_scale;
        static glm::mat4 global_translate;

        static glm::mat4 global_view_projection;
    public:
        glm::mat4 rotation;
        glm::mat4 scale;
        glm::mat4 translation;

        glm::vec4 color;
    public:
        glm::mat4 calculate_total_transform();
};

/* effect hooks are called after main rendering */
using effect_hook_t = std::function<void()>;
struct effect_hook {
    effect_hook_t action;
    int id;
};

class wayfire_output;

struct wayfire_point {
    int x, y;
};

struct wayfire_size {
    int w, h;
};

struct wayfire_geometry {
    wayfire_point origin;
    wayfire_size size;
};

bool point_inside(wayfire_point point, wayfire_geometry rect);
bool rect_inside(wayfire_geometry screen, wayfire_geometry win);

class wayfire_view_t {
    public:
        weston_view *view;

        wayfire_view_t(weston_view *_view);
        ~wayfire_view_t();

        wayfire_output *output;

        wayfire_geometry geometry;

        void move(int x, int y);
        void resize(int w, int h);
        void set_geometry(wayfire_geometry g);
        /* convenience function */
        void set_geometry(int x, int y, int w, int h);


        wayfire_view_transform transform;

        bool is_visible();

        /* Used to specify that this view has been destroyed.
         * Useful when animating view close */
        bool destroyed = false;
        int keep_count = 0;

        /* default_mask is the mask of all viewports the current view is visible on */
        uint32_t default_mask;
        bool has_temporary_mask = false;

        /* Set if the current view should not be rendered by built-in renderer */
        bool is_hidden = false;

        std::vector<effect_hook> effects;

        /* vx and vy are the coords of the viewport where the top left corner is located */
        int vx, vy;

        void set_mask(uint32_t mask);
        void restore_mask();
        void set_temporary_mask(uint32_t tmask);
        void render(uint32_t bits = 0);
};

typedef std::shared_ptr<wayfire_view_t> wayfire_view;
#endif
