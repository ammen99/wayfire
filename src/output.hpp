#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include "view.hpp"
#include "plugin.hpp"
#include <vector>
#include <unordered_map>
#include <pixman-1/pixman.h>
#include "../proto/wayfire-shell-server.h"

namespace OpenGL {
    struct context_t;
}
struct weston_seat;
struct weston_output;

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

/* Workspace streams are used if you need to continuously render a workspace
 * to a texture, for example if you call texture_from_viewport at every frame */
struct wf_workspace_stream {
    std::tuple<int, int> ws;
    uint fbuff, tex;
    bool running = false;
};

struct render_manager {
    private:
        wayfire_output *output;
        bool constant_redraw = false;

        bool dirty_context = true;

        void load_context();
        void release_context();

        render_hook_t renderer;

        pixman_region32_t frame_damage, prev_damage;
        int streams_running = 0;

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

        /* this function renders a viewport and
         * saves the image in texture which is returned */
        void texture_from_workspace(std::tuple<int, int>, uint& fbuff, uint &tex);

        void workspace_stream_start(wf_workspace_stream *stream);
        void workspace_stream_update(wf_workspace_stream *stream);
        void workspace_stream_stop(wf_workspace_stream *stream);
};

class workspace_manager {
    public:
        virtual void init(wayfire_output *output) = 0;

        /* we could actually attach signal listeners, but this is easier */
        virtual void view_bring_to_front(wayfire_view view) = 0;
        virtual void view_removed(wayfire_view view) = 0;

        virtual void for_each_view(view_callback_proc_t call) = 0;
        virtual void for_each_view_reverse(view_callback_proc_t call) = 0;

        /* toplevel views (i.e windows) on the given workspace */
        virtual std::vector<wayfire_view>
            get_views_on_workspace(std::tuple<int, int>) = 0;

        virtual void set_workspace(std::tuple<int, int>) = 0;
        virtual std::tuple<int, int> get_current_workspace() = 0;
        virtual std::tuple<int, int> get_workspace_grid_size() = 0;

        virtual wayfire_view get_background_view() = 0;

        /* returns a list of all views on workspace that are visible on the current
         * workspace except panels(but should include background)
         * The list must be returned from top to bottom(i.e the last is background) */
        virtual std::vector<wayfire_view>
            get_renderable_views_on_workspace(std::tuple<int, int> ws) = 0;

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
    std::tuple<int, int> get_screen_size();

    render_manager *render;
    signal_manager *signal;
    workspace_manager *workspace;

    wayfire_output(weston_output*, wayfire_config *config);
    ~wayfire_output();
    wayfire_geometry get_full_geometry();

    void set_transform(wl_output_transform new_transform);
    wl_output_transform get_transform();
    /* makes sure that the pointer is inside the output's geometry */
    void ensure_pointer();

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
