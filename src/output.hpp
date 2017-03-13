#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include "plugin.hpp"
#include "opengl.hpp"
#include <vector>
#include <unordered_map>

/* TODO: add plugin->output so that plugins know which output they're running on */
/* Controls loading of plugins */
struct plugin_manager {
    std::vector<wayfire_plugin> plugins;

    template<class T> wayfire_plugin create_plugin();
    wayfire_plugin load_plugin_from_file(std::string path, void **handle);
    void load_dynamic_plugins();
    void init_default_plugins();
    plugin_manager(wayfire_output *o, wayfire_config *config);
    ~plugin_manager();
};

/* Manages input grabs */
/* TODO: make compatible with weston */
struct input_manager {
    private:
        std::unordered_set<wayfire_grab_interface> active_plugins;
        std::unordered_set<wayfire_grab_interface> active_grabs;

        weston_keyboard_grab kgrab;
        weston_pointer_grab pgrab;

    public:
        input_manager();
        bool activate_plugin  (wayfire_grab_interface owner);
        bool deactivate_plugin(wayfire_grab_interface owner);

        bool is_plugin_active (owner_t owner_name);

        void grab_input(wayfire_grab_interface);
        void ungrab_input(wayfire_grab_interface);

        void propagate_pointer_grab_axis  (weston_pointer *ptr, weston_pointer_axis_event *ev);
        void propagate_pointer_grab_motion(weston_pointer *ptr, weston_pointer_motion_event *ev);
        void propagate_pointer_grab_button(weston_pointer *ptr, uint32_t button, uint32_t state);

        void propagate_keyboard_grab_key(weston_keyboard *kdb, uint32_t key, uint32_t state);

        void end_grabs();

        /* TODO: support touch */
        weston_binding* add_key(uint32_t mod, uint32_t key, key_callback*);
        weston_binding* add_button(uint32_t mod, uint32_t button, button_callback*);
};
struct render_manager {
    private:
        wayfire_output *output;

        bool dirty_context = true;
        OpenGL::context_t *ctx;
        void load_context();
        void release_context();

        struct {
            GLuint tex = -1;
            GLuint fbuff;
            unsigned long w, h;
        } background;

        pixman_region32_t old_damage;
        void (*weston_renderer_repaint) (weston_output *output, pixman_region32_t *damage);
        render_hook_t renderer;

        void load_background();
        void update_damage (pixman_region32_t* damage, pixman_region32_t *total);
    public:
        render_manager(wayfire_output *o);

#ifdef USE_GLES3
        void blit_background(GLuint destination_fbuff, pixman_region32_t *damage);
#endif
        GLuint get_background() {return background.tex;}

        void set_renderer(render_hook_t rh = nullptr);
        void transformation_renderer();
        void reset_renderer();

        void paint(pixman_region32_t *damage);
        void post_paint();

        /* this function renders a viewport and
         * saves the image in texture which is returned */
#ifdef USE_GLES3
        void texture_from_viewport(std::tuple<int, int>, GLuint& fbuff, GLuint &tex);
#endif

        std::vector<effect_hook> output_effects;
        void add_output_effect(effect_hook&, wayfire_view v = nullptr);
        void rem_effect(const effect_hook&, wayfire_view v = nullptr);
};

// TODO: maybe it is better to merge with wayfire_output,
// as render manager is way too small to be separated
struct viewport_manager {
    private:
        int vwidth, vheight, vx, vy;
        wayfire_output *output;

    public:
        viewport_manager(wayfire_output *o);

        std::vector<wayfire_view> get_views_on_viewport(std::tuple<int, int>);
        void set_viewport(std::tuple<int, int>);

        std::tuple<int, int> get_current_viewport();
        std::tuple<int, int> get_viewport_grid_size();
};

/* when creating a signal there should be the definition of the derived class */
struct signal_data {
};
using signal_callback_t = std::function<void(signal_data*)>;

struct signal_manager {
    private:
        std::unordered_map<std::string, std::vector<signal_callback_t*>> sig;
    public:
        void connect_signal(std::string name, signal_callback_t* callback);
        void disconnect_signal(std::string name, signal_callback_t* callback);
        void emit_signal(std::string name, signal_data *data);
};

class wayfire_output {
    friend class core_t;
    private:
        plugin_manager *plugin;

    public:
    weston_output* handle;
    std::tuple<int, int> get_screen_size() {return std::make_tuple(handle->width, handle->height);}

    input_manager *input;
    render_manager *render;
    viewport_manager *viewport;
    signal_manager *signal;

    wayfire_output(weston_output*, wayfire_config *config);
    ~wayfire_output();

    void activate();
    void deactivate();


    wayfire_view active_view;
    wayfire_view get_view_at_point(int x, int y);

    /* normal layer is where we place weston_views for now
     * background layer contains the texture from the background */
    /* TODO: add other layers, draw cursor in cursor_layer */
    weston_layer normal_layer, background_layer;

    void for_each_view(view_callback_proc_t);
    void for_each_view_reverse(view_callback_proc_t);

    void attach_view(wayfire_view v);
    void detach_view(wayfire_view v);
    void focus_view(wayfire_view v, weston_seat *seat);
};

#endif /* end of include guard: OUTPUT_HPP */
