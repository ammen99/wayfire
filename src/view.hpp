#ifndef VIEW_HPP
#define VIEW_HPP
#include "commonincludes.hpp"
#include <libweston-3/libweston-desktop.h>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

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

/* TODO: we already have weston_geometry, it makes more sense to use it wherever possible,
 * we should remove this one */
struct wayfire_geometry {
    wayfire_point origin;
    wayfire_size size;
};

bool point_inside(wayfire_point point, wayfire_geometry rect);
bool rect_inside(wayfire_geometry screen, wayfire_geometry win);

class wayfire_view_t {
    public:
        weston_desktop_surface *desktop_surface;
        weston_surface *surface;
        weston_view *handle;

        wayfire_view_t(weston_desktop_surface *ds);
        ~wayfire_view_t();

        wayfire_output *output;

        wayfire_geometry geometry, saved_geometry;
        weston_geometry ds_geometry;

        struct {
            bool is_xorg = false;
            int x, y;
        } xwayland;

        void move(int x, int y);
        void resize(int w, int h);
        void set_geometry(wayfire_geometry g);
        /* convenience function */
        void set_geometry(int x, int y, int w, int h);

        bool maximized = false, fullscreen = false;
        /* TODO: implement set_fullscreen and add support for workarea in wayfire_output */
        void set_maximized(bool maxim);
        //void set_fullscreen(bool fullscreen);

        wayfire_view_transform transform;

        bool is_visible();

        bool is_mapped = false;
        void map(int sx, int sy);

        /* Used to specify that this view has been destroyed.
         * Useful when animating view close */
        bool destroyed = false;
        int keep_count = 0;

        /* Set if the current view should not be rendered by built-in renderer */
        bool is_hidden = false;

        std::vector<effect_hook> effects;

        void render(uint32_t bits = 0);
};

typedef std::shared_ptr<wayfire_view_t> wayfire_view;
#endif
