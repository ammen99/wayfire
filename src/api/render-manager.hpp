#ifndef RENDER_MANAGER_HPP
#define RENDER_MANAGER_HPP

#include "plugin.hpp"
#include "opengl.hpp"
#include "object.hpp"
#include "util.hpp"
#include <vector>

/* Emitted whenever a workspace stream is being started or stopped */
struct wf_stream_signal : public signal_data
{
    wf_stream_signal(wf_region& damage, const wf_framebuffer& _fb)
        : raw_damage(damage), fb(_fb) { }

    /* Raw damage can be adjusted by the signal handlers */
    wf_region& raw_damage;
    const wf_framebuffer& fb;
};

/* Workspace streams are used if you need to continuously render a workspace
 * to a texture, for example if you call texture_from_viewport at every frame */
struct wf_workspace_stream
{
    std::tuple<int, int> ws;
    wf_framebuffer_base buffer;
    bool running = false;

    float scale_x, scale_y;
};

enum wf_output_effect_type
{
    WF_OUTPUT_EFFECT_PRE = 0,
    WF_OUTPUT_EFFECT_OVERLAY = 1,
    WF_OUTPUT_EFFECT_POST = 2,
    WF_OUTPUT_EFFECT_TOTAL = 3
};

/* effect hooks are called after main rendering */
using effect_hook_t = std::function<void()>;

/* post hooks are used for postprocessing effects.
 * They can take the output image and modify it as they want */
using post_hook_t = std::function<void(const wf_framebuffer_base& source,
    const wf_framebuffer_base& destination)>;

/* render hooks are used when a plugin requests to draw the whole desktop on their own
 * example plugin is cube. Rendering must happen to the indicated framebuffer */
using render_hook_t = std::function<void(const wf_framebuffer& fb)>;

struct wf_output_damage;
class render_manager : public wf_signal_provider_t
{
    friend void redraw_idle_cb(void *data);
    friend void damage_idle_cb(void *data);
    friend void frame_cb (wl_listener*, void *data);

    private:
        wayfire_output *output;
        wl_event_source *idle_redraw_source = NULL, *idle_damage_source = NULL;

        wl_listener frame_listener;

        signal_callback_t output_resized;

        wf_region frame_damage;
        std::unique_ptr<wf_output_damage> output_damage;

        std::vector<std::vector<wf_workspace_stream>> output_streams;
        wf_workspace_stream *current_ws_stream = nullptr;

        wf_region get_ws_damage(std::tuple<int, int> ws);

        using effect_container_t = std::vector<effect_hook_t*>;
        effect_container_t effects[WF_OUTPUT_EFFECT_TOTAL];

        struct wf_post_effect;
        /* TODO: use unique_ptr */
        using post_container_t = std::vector<wf_post_effect*>;
        post_container_t post_effects;

        wf_framebuffer_base default_buffer;

        int constant_redraw = 0;
        int output_inhibit = 0;
        render_hook_t renderer;

        void paint();
        void post_paint();

        void run_effects(effect_container_t&);

        void _rem_post(wf_post_effect *hook);
        void cleanup_post_hooks();

        void init_default_streams();

    public:
        render_manager(wayfire_output *o);
        ~render_manager();

        void set_renderer(render_hook_t rh = nullptr);
        void reset_renderer();

        /* schedule repaint immediately after finishing the last one
         * to undo, call auto_redraw(false) as much times as auto_redraw(true) was called */
        void auto_redraw(bool redraw);
        void schedule_redraw();

        void add_inhibit(bool add);

        void add_effect(effect_hook_t*, wf_output_effect_type type);
        void rem_effect(const effect_hook_t*, wf_output_effect_type type);

        /* add a new postprocessing effect */
        void add_post(post_hook_t*);
        /* Calling rem_post will remove the postprocessing effect as soon as
         * possible.
         *
         * NOTE: this doesn't guarantee that the hook won't be executed anymore,
         * but it is guaranteed that the hook will be removed by the beginning
         * of the next frame.
         */
        void rem_post(post_hook_t*);

        /* Returns the damage scheduled for the next frame, if not in a frame
         * Otherwise, undefined result */
        wf_region get_scheduled_damage();

        void damage_whole();
        void damage(const wlr_box& box);
        void damage(const wf_region& region);

        /* Returns the box representing the output in damage coordinate system */
        wlr_box get_damage_box() const;
        /* Returns the box representing the given workspace in damage coordinate system */
        wlr_box get_ws_box(std::tuple<int, int> ws) const;

        wf_framebuffer get_target_framebuffer() const;

        void workspace_stream_start(wf_workspace_stream *stream);
        void workspace_stream_update(wf_workspace_stream *stream,
                float scale_x = 1, float scale_y = 1);
        void workspace_stream_stop(wf_workspace_stream *stream);
};

#endif
