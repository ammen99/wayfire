#ifndef RENDER_MANAGER_HPP
#define RENDER_MANAGER_HPP

#include "plugin.hpp"

extern "C"
{
#include <wlr/types/wlr_output_damage.h>
}

#include <vector>
#include <pixman.h>


namespace OpenGL { struct context_t; }

class wayfire_view_t;
using wayfire_view = std::shared_ptr<wayfire_view_t>;

/* Workspace streams are used if you need to continuously render a workspace
 * to a texture, for example if you call texture_from_viewport at every frame */
struct wf_workspace_stream
{
    std::tuple<int, int> ws;
    uint fbuff, tex;
    bool running = false;

    float scale_x, scale_y;
};

class render_manager
{

    friend void redraw_idle_cb(void *data);
    friend void frame_cb (wl_listener*, void *data);

    private:
        wayfire_output *output;
        wl_event_source *idle_redraw_source = NULL;

        wlr_output_damage *damage_manager;
        wl_listener frame_listener;

        bool dirty_context = true;
        void load_context();
        void release_context();

        bool draw_overlay_panel = true;

        pixman_region32_t frame_damage;
        void get_ws_damage(std::tuple<int, int> ws, pixman_region32_t *out_damage);
        std::vector<effect_hook_t*> output_effects;

        int constant_redraw = 0;
        render_hook_t renderer;

        void paint();
        void post_paint();

        void transformation_renderer();
        void run_effects();
        void render_panels();

    public:
        OpenGL::context_t *ctx;

        render_manager(wayfire_output *o);
        ~render_manager();

        void set_renderer(render_hook_t rh = nullptr);
        void reset_renderer();

        /* schedule repaint immediately after finishing the last one
         * to undo, call auto_redraw(false) as much times as auto_redraw(true) was called */
        void auto_redraw(bool redraw);
        void schedule_redraw();
        void set_hide_overlay_panels(bool set);

        void add_output_effect(effect_hook_t*, wayfire_view v = nullptr);
        void rem_effect(const effect_hook_t*, wayfire_view v = nullptr);

        void damage(wlr_box box);

        /* this function renders a viewport and
         * saves the image in texture which is returned */
        void texture_from_workspace(std::tuple<int, int>, uint& fbuff, uint &tex);

        void workspace_stream_start(wf_workspace_stream *stream);
        void workspace_stream_update(wf_workspace_stream *stream,
                float scale_x = 1, float scale_y = 1);
        void workspace_stream_stop(wf_workspace_stream *stream);
};

#endif
