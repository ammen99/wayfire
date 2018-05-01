#ifndef VIEW_HPP
#define VIEW_HPP
#include <plugin.hpp>
#include <vector>
#include <map>
#include <functional>
#include <pixman.h>

extern "C"
{
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
}

class wayfire_output;
struct wf_point
{
    int x, y;
};
using wf_geometry = wlr_box;

bool operator == (const wf_geometry& a, const wf_geometry& b);
bool operator != (const wf_geometry& a, const wf_geometry& b);

wf_point operator + (const wf_point& a, const wf_point& b);
wf_point operator + (const wf_point& a, const wf_geometry& b);
wf_geometry operator + (const wf_geometry &a, const wf_point& b);
wf_point operator - (const wf_point& a);

wf_geometry get_output_box_from_box(const wlr_box& box, float scale, wl_output_transform transform);

bool point_inside(wf_point point, wf_geometry rect);
bool rect_intersect(wf_geometry screen, wf_geometry win);

struct wf_custom_view_data
{
    virtual ~wf_custom_view_data() {}
};

/* General TODO: mark member functions const where appropriate */

class wayfire_view_t;
using wayfire_view = std::shared_ptr<wayfire_view_t>;

/* do not copy the surface, it is not reference counted and you will get crashes */
class wayfire_surface_t;
using wf_surface_iterator_callback = std::function<void(wayfire_surface_t*, int, int)>;

class wf_decorator_frame_t;
class wf_view_transformer_t;

/* abstraction for desktop-apis, no real need for plugins
 * This is a base class to all "drawables" - desktop views, subsurfaces, popups */
class wayfire_surface_t
{
    /* TODO: maybe some functions don't need to be virtual? */
    protected:

         wl_listener committed, destroy, new_sub;
         std::vector<wayfire_surface_t*> surface_children;

         virtual void for_each_surface_recursive(wf_surface_iterator_callback callback,
                                                 int x, int y, bool reverse = false);

        wayfire_surface_t *parent_surface;

        wayfire_output *output;

        /* position relative to parent */
        virtual void get_child_position(int &x, int &y);

        wf_geometry geometry = {0, 0, 0, 0};

        virtual bool is_subsurface();
        virtual void damage(const wlr_box& box);
        virtual void damage(pixman_region32_t *region);

    public:

        wayfire_surface_t(wayfire_surface_t *parent = nullptr);
        virtual ~wayfire_surface_t();

        /* if surface != nullptr, then the surface is mapped */
        wlr_surface *surface = nullptr;

        virtual void map(wlr_surface *surface);
        virtual void unmap();
        virtual void destruct() { delete this; }
        virtual bool is_mapped() { return surface; }

        int keep_count = 0;
        bool destroyed = false;

        void inc_keep_count();
        void dec_keep_count();

        virtual wayfire_surface_t *get_main_surface();

        virtual void damage();

        float alpha = 1.0;

        /* returns top-left corner in output coordinates */
        virtual wf_point get_output_position();

        /* return surface box in output coordinates */
        virtual wf_geometry get_output_geometry();

        virtual void commit();

        virtual wayfire_output *get_output() { return output; };
        virtual void set_output(wayfire_output*);

        virtual void render(int x, int y, wlr_box* damage);
        /* just wrapper for the render() */
        virtual void render_pixman(int x, int y, pixman_region32_t* damage);

        /* render to an offscreen buffer, without applying output transform/scale/etc.
         * Rendering is done in */
        virtual void render_fbo(int x, int y, int fb_width, int fb_height,
                                pixman_region32_t *damage);

        /* iterate all (sub) surfaces, popups, etc. in top-most order
         * for example, first popups, then subsurfaces, then main surface
         * When reverse=true, the order in which surfaces are visited is reversed */
        virtual void for_each_surface(wf_surface_iterator_callback callback, bool reverse = false);

        virtual void render_fb(int x, int y, pixman_region32_t* damage, int target_fb);
};

/* Represents a desktop window (not as X11 window, but like a xdg_toplevel surface) */
class wayfire_view_t : public wayfire_surface_t
{
    friend class wayfire_xdg6_decoration_view;
    friend void surface_destroyed_cb(wl_listener*, void *data);

    protected:
        wayfire_view decoration;
        int decor_x, decor_y;

        void force_update_xwayland_position();
        int in_continuous_move = 0, in_continuous_resize = 0;

        bool wait_decoration = false;

        inline wayfire_view self();
        virtual bool update_size();

        uint32_t id;
        virtual void get_child_position(int &x, int &y);
        virtual void damage(const wlr_box& box);

        struct offscreen_buffer_t
        {
            uint32_t fbo = -1, tex = -1;
            /* used to store output_geometry when the view has been destroyed */
            int32_t output_x = 0, output_y = 0;
            int32_t fb_width = 0, fb_height = 0;
            pixman_region32_t cached_damage;

            void init(int w, int h);
            void fini();
            bool valid();

        } offscreen_buffer;

        std::unique_ptr<wf_view_transformer_t> transform;

    public:

        /* these represent toplevel relations, children here are transient windows,
         * such as the close file dialogue */
        wayfire_view parent = nullptr;
        std::vector<wayfire_view> children;

        /* plugins can subclass wf_custom_view_data and use it to store view-specific information
         * it must provide a virtual destructor to free its data. Custom data is deleted when the view
         * is destroyed if not removed earlier */
        std::map<std::string, wf_custom_view_data*> custom_data;

        wayfire_view_t();
        virtual ~wayfire_view_t();
        uint32_t get_id() { return id; }

        virtual void move(int x, int y, bool send_signal = true);
        virtual void resize(int w, int h, bool send_signal = true);
        virtual void activate(bool active);
        virtual void close() {};

        virtual void set_parent(wayfire_view parent);
        virtual wayfire_surface_t* get_main_surface();

        /* return geometry as should be used for all WM purposes */
        virtual wf_geometry get_wm_geometry() { return decoration ? decoration->get_wm_geometry() : geometry; }
        virtual wf_geometry get_output_geometry();

        virtual wlr_box get_bounding_box();
        virtual wf_point get_output_position();

        /* map from global to surface local coordinates
         * returns the (sub)surface under the cursor or NULL iff the cursor is outside of the view
         * TODO: it should be overwritable by plugins which deform the view */
        virtual wayfire_surface_t *map_input_coordinates(int cursor_x, int cursor_y, int &sx, int &sy);
        virtual wlr_surface *get_keyboard_focus_surface() { return surface; };

        virtual void set_geometry(wf_geometry g);
        virtual void set_resizing(bool resizing);
        virtual void set_moving(bool moving);

        bool maximized = false, fullscreen = false;

        virtual void set_maximized(bool maxim);
        virtual void set_fullscreen(bool fullscreen);

        bool is_visible();
        virtual void commit();
        virtual void map(wlr_surface *surface);
        virtual void unmap();
        virtual void destruct();

        virtual void damage();

        virtual std::string get_app_id() { return ""; }
        virtual std::string get_title() { return ""; }


        /* Set if the current view should not be rendered by built-in renderer */
        bool is_hidden = false;

        /* backgrounds, panels, lock surfaces -> they shouldn't be touched
         * by plugins like move, animate, etc. */
        bool is_special = false;

        virtual void move_request();
        virtual void resize_request();
        virtual void maximize_request(bool state);
        virtual void fullscreen_request(wayfire_output *output, bool state);

        virtual void set_decoration(wayfire_view view,
                                    std::unique_ptr<wf_decorator_frame_t> frame);

        /*
         *                              View transforms
         * A view transform can be any kind of transformation, for example 3D rotation,
         * wobbly effect or similar. When we speak of transforms, a "view" is defined
         * as a toplevel window (including decoration) and also all of its subsurfaces/popups.
         * The transformation then is applied to this group of surfaces together.
         *
         * When a view has a custom transform, then internally all these surfaces are
         * rendered to a FBO, and then the custom transformation renders the resulting
         * texture as it sees fit. In this way we could have composable transformations
         * in the future(e.g several FBO passes).
         *
         * Damage tracking for transformed views is done on the boundingbox of the
         * damaged region after applying the transformation, but all damaged parts
         * of the internal FBO are updated.
         * */

        virtual void set_transformer(std::unique_ptr<wf_view_transformer_t> transformer);

        /* the returned value is just a temporary object */
        virtual wf_view_transformer_t* get_transformer() { return transform.get(); }
        virtual void render_fb(int x, int y, pixman_region32_t* damage, int target_fb);

        bool has_snapshot = false;
        virtual void take_snapshot();
};

wayfire_view wl_surface_to_wayfire_view(wl_resource *surface);
#endif
