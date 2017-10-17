#ifndef VIEW_HPP
#define VIEW_HPP
#include "commonincludes.hpp"
#include <vector>
#include <map>
#include <memory>
#include <glm/glm.hpp>
#include <functional>
#include <libweston-desktop.h>
#include <pixman.h>

struct weston_view;
struct weston_surface;

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
class wayfire_output;

struct wayfire_point {
    int x, y;
};

bool operator == (const weston_geometry& a, const weston_geometry& b);
bool operator != (const weston_geometry& a, const weston_geometry& b);

bool point_inside(wayfire_point point, weston_geometry rect);
bool rect_intersect(weston_geometry screen, weston_geometry win);

struct wf_custom_view_data
{
    virtual ~wf_custom_view_data();
};

class wayfire_view_t {
    public:
        weston_desktop_surface *desktop_surface;
        weston_surface *surface;
        weston_view *handle;

        /* plugins can subclass wf_custom_view_data and use it to store view-specific information
         * it must provide a virtual destructor to free its data. Custom data is deleted when the view
         * is destroyed if not removed earlier */
        std::map<std::string, wf_custom_view_data*> custom_data;

        wayfire_view_t(weston_desktop_surface *ds);
        ~wayfire_view_t();

        wayfire_output *output;

        weston_geometry geometry, saved_geometry;
        weston_geometry ds_geometry;

        struct {
            bool is_xorg = false;
            int x, y;
        } xwayland;

        void move(int x, int y);
        void resize(int w, int h);
        void set_geometry(weston_geometry g);
        /* convenience function */
        void set_geometry(int x, int y, int w, int h);

        bool maximized = false, fullscreen = false;

        void set_maximized(bool maxim);
        void set_fullscreen(bool fullscreen);

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

        /* backgrounds, panels, lock surfaces -> they shouldn't be touched
         * by plugins like move, animate, etc. */
        bool is_special = false;

        std::vector<effect_hook_t*> effects;

        /* simple_render is used to just render the view without running the attached effects */
        void simple_render(uint32_t bits = 0, pixman_region32_t *damage = nullptr);
        void render(uint32_t bits = 0, pixman_region32_t *damage = nullptr);
};

typedef std::shared_ptr<wayfire_view_t> wayfire_view;
using view_callback_proc_t = std::function<void(wayfire_view)>;

#endif
