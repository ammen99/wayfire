#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include "plugin.hpp"
#include "opengl.hpp"
#include <vector>
#include <unordered_map>
#include "../proto/wayfire-shell-server.h"

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

struct render_manager {
    private:
        wayfire_output *output;
        bool constant_redraw = false;

        bool dirty_context = true;

        void load_context();
        void release_context();

        render_hook_t renderer;

    public:
        OpenGL::context_t *ctx;
        render_manager(wayfire_output *o);

        void set_renderer(render_hook_t rh = nullptr);

        void auto_redraw(bool redraw); /* schedule repaint immediately after finishing the last */
        void transformation_renderer();
        void reset_renderer();

        void paint(pixman_region32_t *damage);
        void pre_paint();

        std::vector<effect_hook_t*> output_effects;
        void add_output_effect(effect_hook_t*, wayfire_view v = nullptr);
        void rem_effect(const effect_hook_t*, wayfire_view v = nullptr);
};

class workspace_manager {
    public:
        virtual void init(wayfire_output *output) = 0;

        /* we could actually attach signal listeners, but this is easier */
        virtual void view_bring_to_front(wayfire_view view) = 0;
        virtual void view_removed(wayfire_view view) = 0;

        virtual void for_each_view(view_callback_proc_t call) = 0;
        virtual void for_each_view_reverse(view_callback_proc_t call) = 0;

        virtual std::vector<wayfire_view>
            get_views_on_workspace(std::tuple<int, int>) = 0;

        virtual void set_workspace(std::tuple<int, int>) = 0;
        virtual std::tuple<int, int> get_current_workspace() = 0;
        virtual std::tuple<int, int> get_workspace_grid_size() = 0;

        /* this function renders a viewport and
         * saves the image in texture which is returned */
        virtual void texture_from_workspace(std::tuple<int, int>, GLuint& fbuff,
                GLuint &tex) = 0;

        virtual wayfire_view get_background_view() = 0;

        /* wayfire_shell implementation */
        virtual void add_background(wayfire_view background, int x, int y) = 0;
        virtual void add_panel(wayfire_view panel) = 0;
        virtual void reserve_workarea(wayfire_shell_panel_position position,
                uint32_t width, uint32_t height) = 0;
        virtual void configure_panel(wayfire_view view, int x, int y) = 0;

        /* returns the available area for views, it is basically
         * the output geometry minus the area reserved for panels */
        virtual wayfire_geometry get_workarea() = 0;
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
       std::unordered_set<wayfire_grab_interface> active_plugins;
       plugin_manager *plugin;

       wayfire_view active_view;
    public:
    weston_output* handle;

    /* used for differences between backends */
    int output_dx, output_dy;
    std::tuple<int, int> get_screen_size() {return std::make_tuple(handle->width, handle->height);}

    render_manager *render;
    signal_manager *signal;
    workspace_manager *workspace;

    wayfire_output(weston_output*, wayfire_config *config);
    ~wayfire_output();
    wayfire_geometry get_full_geometry();

    bool activate_plugin  (wayfire_grab_interface owner);
    bool deactivate_plugin(wayfire_grab_interface owner);
    bool is_plugin_active (owner_t owner_name);

    void activate();
    void deactivate();

    wayfire_view get_top_view();
    wayfire_view get_view_at_point(int x, int y);

    void attach_view(wayfire_view v);
    void detach_view(wayfire_view v);

    void focus_view(wayfire_view v, weston_seat *seat);
    void set_active_view(wayfire_view v);
    void bring_to_front(wayfire_view v);
};
extern const struct wayfire_shell_interface shell_interface_impl;
#endif /* end of include guard: OUTPUT_HPP */
