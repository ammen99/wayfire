#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include "plugin.hpp"
#include "opengl.hpp"
#include <vector>

/* TODO: add plugin->output so that plugins know which output they're running on */
/* Controls loading of plugins */
struct plugin_manager {
    std::vector<wayfire_plugin> plugins;

    template<class T> wayfire_plugin create_plugin();
    wayfire_plugin load_plugin_from_file(std::string path, void **handle);
    void load_dynamic_plugins();
    void init_default_plugins();
    plugin_manager(wayfire_output *o, weston_config *config);
    ~plugin_manager();
};

/* Manages input grabs */
/* TODO: make compatible with weston */
struct input_manager {
    private:
        std::unordered_set<wayfire_grab_interface> active_plugins;

        int keyboard_grab_count = 0;
        int pointer_grab_count = 0;
    public:
        bool activate_plugin  (wayfire_grab_interface owner);
        bool deactivate_plugin(wayfire_grab_interface owner);
        bool is_plugin_active (owner_t owner_name);

        void grab_keyboard();
        void ungrab_keyboard();
        void grab_pointer();
        void ungrab_pointer();

        /* TODO: support touch */
        weston_binding* add_key(weston_keyboard_modifier mod, uint32_t key, key_callback*);
        weston_binding* add_button(weston_keyboard_modifier mod, uint32_t button, button_callback*);
};
struct render_manager {
    private:
        wayfire_output *output;

        int redraw_timer = 0;
        struct {
            GLuint tex = -1;
            GLuint fbuff;
            unsigned long w, h;
        } background;

        render_hook_t renderer;
#define ALL_VISIBLE 4294967295 // All 32 bits are on
        uint32_t visibility_mask = ALL_VISIBLE;

        void load_background();
    public:
        render_manager(wayfire_output *o);

        bool dirty_context = true;
        OpenGL::context_t *ctx;
        void load_context();
        void release_context();

#ifdef USE_GLES3
        void blit_background(GLuint destination_fbuff);
#endif
        GLuint get_background() {return background.tex;}

        void set_renderer(uint32_t visibility_mask = ALL_VISIBLE, render_hook_t rh = nullptr);
        void transformation_renderer();
        void reset_renderer();
        bool renderer_running() { return renderer != nullptr || redraw_timer; }
        void paint();
        void post_paint();

        bool should_repaint_everything() { return redraw_timer > 0; }

        void force_full_redraw(bool state) {
            if(state) ++redraw_timer;
            else if(redraw_timer) --redraw_timer;
        }

        /* this function renders a viewport and
         * saves the image in texture which is returned */
#ifdef USE_GLES3
        void texture_from_viewport(std::tuple<int, int>, GLuint& fbuff, GLuint &tex);
#endif

        std::vector<effect_hook> output_effects;
        void add_output_effect(effect_hook&, wayfire_view v = nullptr);
        void rem_effect(const effect_hook&, wayfire_view v = nullptr);
};

struct viewport_manager {
    private:
        int vwidth, vheight, vx, vy;
        wayfire_output *output;

    public:
        viewport_manager(wayfire_output *o);
        /* returns viewport mask for a View, assuming it is on current viewport */
        uint32_t get_mask_for_view(wayfire_view);

        /* returns the coords of the viewport where top left corner of a view is,
         * assuming the view coords are on current viewport */
        void get_viewport_for_view(wayfire_view, int&, int&);

        uint32_t get_mask_for_viewport(int x, int y) {
            return (1 << (x + y * vwidth));
        }

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
        std::map<std::string, std::vector<signal_callback_t*>> sig;
    public:
        signal_manager();
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

    bool should_redraw() {
        return render->renderer_running();
    }

    wayfire_output(weston_output*, weston_config *config);
    ~wayfire_output();

    void activate();
    void deactivate();

    wayfire_view get_active_view();
    wayfire_view get_view_at_point(int x, int y);

    void for_each_view(view_callback_proc_t);
    void for_each_view_reverse(view_callback_proc_t);

    void attach_view(wayfire_view v);
    void detach_view(wayfire_view v);
    void focus_view(wayfire_view v);
};

#endif /* end of include guard: OUTPUT_HPP */
