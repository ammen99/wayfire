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
        void propagate_keyboard_grab_mod(weston_keyboard *kbd, uint32_t depressed,
                uint32_t locked, uint32_t latched, uint32_t group);

        void end_grabs();

        /* TODO: support touch */
        weston_binding* add_key(uint32_t mod, uint32_t key, key_callback*);
        weston_binding* add_button(uint32_t mod, uint32_t button, button_callback*);
};
struct render_manager {
    private:
        wayfire_output *output;
        bool constant_redraw = false;

        bool dirty_context = true;
                void load_context();
        void release_context();

        struct {
            GLuint tex = -1;
            GLuint fbuff;
            unsigned long w, h;
            long long times_blitted = 0;
        } background;

        pixman_region32_t old_damage;
        void (*weston_renderer_repaint) (weston_output *output, pixman_region32_t *damage);
        render_hook_t renderer;

        void load_background();
        void update_damage (pixman_region32_t* damage, pixman_region32_t *total);
    public:
        OpenGL::context_t *ctx;
        render_manager(wayfire_output *o);

        void blit_background(GLuint destination_fbuff, pixman_region32_t *damage);
        GLuint get_background() {return background.tex;}

        void set_renderer(render_hook_t rh = nullptr);

        void auto_redraw(bool redraw); /* schedule repaint immediately after finishing the last */
        void transformation_renderer();
        void reset_renderer();

        void paint(pixman_region32_t *damage);
        void pre_paint();

        /* this function renders a viewport and
         * saves the image in texture which is returned */
#ifdef USE_GLES3
        void texture_from_viewport(std::tuple<int, int>, GLuint& fbuff, GLuint &tex);
#endif

        std::vector<effect_hook_t*> output_effects;
        void add_output_effect(effect_hook_t*, wayfire_view v = nullptr);
        void rem_effect(const effect_hook_t*, wayfire_view v = nullptr);
};

class workspace_manager {
    public:
        virtual void init(wayfire_output *output) = 0;
        virtual std::vector<wayfire_view> get_views_on_workspace(std::tuple<int, int>) = 0;

        virtual void set_workspace(std::tuple<int, int>) = 0;
        virtual std::tuple<int, int> get_current_workspace() = 0;
        virtual std::tuple<int, int> get_workspace_grid_size() = 0;

        /* this function renders a viewport and
         * saves the image in texture which is returned */
        virtual void texture_from_workspace(std::tuple<int, int>, GLuint& fbuff, GLuint &tex) = 0;
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
    signal_manager *signal;
    workspace_manager *workspace;

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
    void bring_to_front(wayfire_view v);
};

#endif /* end of include guard: OUTPUT_HPP */
