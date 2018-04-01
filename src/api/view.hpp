#ifndef VIEW_HPP
#define VIEW_HPP
#include <plugin.hpp>
#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <functional>
#include <pixman.h>

extern "C"
{
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
}

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
class wayfire_output;

struct wf_point
{
    int x, y;
};
using wf_geometry = wlr_box;

bool operator == (const wf_geometry& a, const wf_geometry& b);
bool operator != (const wf_geometry& a, const wf_geometry& b);

bool point_inside(wf_point point, wf_geometry rect);
bool rect_intersect(wf_geometry screen, wf_geometry win);

struct wf_custom_view_data
{
    virtual ~wf_custom_view_data() {}
};

class wayfire_view_t;
using wayfire_view = std::shared_ptr<wayfire_view_t>;
using wf_surface_iterator_callback = std::function<void(wlr_surface*, int, int)>;

class wayfire_view_t
{
    protected:

        void force_update_xwayland_position();
        int in_continuous_move = 0, in_continuous_resize = 0;

        wl_listener committed, destroy;

        inline wayfire_view self();
        virtual void update_size();

        /* iterate all (sub) surfaces, popups, etc. in top-most order
         * for example, first popups, then subsurfaces, then main surface
         * When reverse=true, the order in which surfaces are visited is reversed */
        virtual void for_each_surface(wf_surface_iterator_callback callback, bool reverse = false);

    public:

        wayfire_view parent = nullptr;
        std::vector<wayfire_view> children;

        wlr_surface *surface;

        /* plugins can subclass wf_custom_view_data and use it to store view-specific information
         * it must provide a virtual destructor to free its data. Custom data is deleted when the view
         * is destroyed if not removed earlier */
        std::map<std::string, wf_custom_view_data*> custom_data;

        wayfire_view_t(wlr_surface *surface);
        virtual ~wayfire_view_t();

        wayfire_output *output;
        wf_geometry geometry;

        virtual void move(int x, int y, bool send_signal = true);
        virtual void resize(int w, int h, bool send_signal = true);
        virtual void activate(bool active) {};
        virtual void close() {};

        virtual void set_parent(wayfire_view parent);
        virtual bool is_toplevel() { return true; }

        /* return geometry together with shadows, etc.
         * view->geometry contains "WM" geometry */
        virtual wf_geometry get_output_geometry() { return geometry; };

        /* map from global to surface local coordinates
         * returns the (sub)surface under the cursor or NULL iff the cursor is outside of the view
         * TODO: it should be overwritable by plugins which deform the view */
        virtual wlr_surface *map_input_coordinates(int cursor_x, int cursor_y, int &sx, int &sy);

        virtual void set_geometry(wf_geometry g);
        virtual void set_resizing(bool resizing);
        virtual void set_moving(bool moving);

        bool maximized = false, fullscreen = false;

        virtual void set_maximized(bool maxim);
        virtual void set_fullscreen(bool fullscreen);

        wayfire_view_transform transform;

        bool is_visible();

        bool is_mapped = false;

        virtual void commit();
        virtual void map();
        virtual void damage();

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
        virtual void simple_render(uint32_t bits = 0, pixman_region32_t *damage = nullptr);
        void render(uint32_t bits = 0, pixman_region32_t *damage = nullptr);
};
#endif
