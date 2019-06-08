#ifndef RENDER_MANAGER_HPP
#define RENDER_MANAGER_HPP

#include "output.hpp"
#include "object.hpp"

struct wf_framebuffer_base;
struct wf_framebuffer;
struct wf_region;

namespace wf
{
struct workspace_stream_t;
/** Render hooks can be used to override Wayfire's built-in rendering. The
 * plugin which sets the hook gains full control over what and how is drawn
 * to the screen. Workspace streams however are not affected.
 *
 * @param fb Indicates the framebuffer that the custom renderer should draw to */
using render_hook_t = std::function<void(const wf_framebuffer& fb)>;

/* Effect hooks provide the plugins with a way to execute custom code
 * at certain parts of the repaint cycle */
using effect_hook_t = std::function<void()>;

enum output_effect_type_t
{
    /* Pre hooks are called immediately before repainting the output */
    OUTPUT_EFFECT_PRE = 0,
    /* Overlay hooks are called right after repainting the output, but
     * before post hooks and before swapping buffers */
    OUTPUT_EFFECT_OVERLAY = 1,
    /* Post hooks are called after the buffers have been swapped */
    OUTPUT_EFFECT_POST = 2,

    /* Invalid type for a hook, used internally */
    OUTPUT_EFFECT_TOTAL = 3,
};

/** Post hooks are called just before swapping buffers. In contrast to
 * render hooks, post hooks operate on the whole output image, i.e they
 * are suitable for different postprocessing effects.
 *
 * When using post hooks, the output first gets rendered to a framebuffer,
 * which can then pass through multiple post hooks. The last hook then will
 * draw to the output's framebuffer.
 *
 * @param source Indicates the source buffer of the hook, which contains
 *        the output image up to this moment.
 *
 * @param destination Indicates where the processed image should be stored.
 */
using post_hook_t = std::function<void(const wf_framebuffer_base& source,
    const wf_framebuffer_base& destination)>;

/** Render manager
 *
 * Each output has a render manager, which is responsible for all rendering
 * operations that happen on it, and also for damage tracking. */
class render_manager : public wf::signal_provider_t
{
  public:
    /** Create a render manager for the given output. Plugins do not need
     * to manually create render managers, as one is created for each output
     * automatically */
    render_manager(output_t *o);
    ~render_manager();

    /**
     * Set the render hook to be used for rendering.
     * @param rh The render hook to use, or nullptr for default renderer
     */
    void set_renderer(render_hook_t rh = nullptr);

    /**
     * Rendering an output is done on demand, that is, when the output is
     * damaged. Some plugins however need to redraw the output as often as
     * possible, for ex. when displaying some kind of animation.
     *
     * auto_redraw() provides the plugins to temporarily request redrawing
     * of the output regardless of damage.
     *
     * @param always - Whether to always redraw, regardless of damage. Call
     *        set_redraw_always(false) once for each set_redraw_always(true).
     */
    void set_redraw_always(bool always = true);

    /**
     * Schedule a frame for the output. Note that if there is no damage for
     * the next frame, nothing will be redrawn
     */
    void schedule_redraw();

    /**
     * Inhibit rendering to the output. An inhibited output will show a
     * fully black image. Used mainly for compositor fade in/out on startup.
     */
    void add_inhibit(bool add);

    /**
     * Add a new effect hook.
     * @param hook The hook callback
     * @param type The type of the effect hook
     */
    void add_effect(effect_hook_t* hook, output_effect_type_t type);
    /**
     * Remove an added effect hook. No-op if the hook wasn't really added.
     * @param hook The hook callback to be removed
     */
    void rem_effect(effect_hook_t* hook);

    /**
     * Add a new post hook.
     *
     * @param hook The hook callack
     */
    void add_post(post_hook_t* hook);

    /**
     * Remove a post hook. No-op if hook isn't active.
     *
     * @param hook The hook to be removed.
     */
    void rem_post(post_hook_t* hook);

    /**
     * @return The damaged region on the current output for the current
     * frame. Note that a larger region might actually be repainted due to
     * double buffering
     */
    wf_region get_scheduled_damage();

    /**
     * Damage all workspaces of the output. Should not be used inside render
     * hooks, view transformers, etc.
     */
    void damage_whole();

    /**
     * Same as damage_whole() but the output will actually be damaged on the
     * next time the event loop goes idle. This is safe to use inside render
     * hooks, transformers, etc.
     */
    void damage_whole_idle();

    /**
     * Same as damage_whole(), but damages only a part of the output.
     *
     * @param box The output box to be damaged, in output-local coordinates.
     */
    void damage(const wlr_box& box);

    /**
     * Same as damage_whole(), but damages only a part of the output.
     *
     * @param region The output region to be damaged, in output-local
     *        coordinates.
     */
    void damage(const wf_region& region);

    /**
     * @return A box in output-local coordinates containing the visible part
     * of the output
     */
    wlr_box get_damage_box() const;

    /**
     * @return A box in output-local coordinates containing the given
     * workspace of the output (returned value depends on current workspace).
     */
    wlr_box get_ws_box(std::tuple<int, int> ws) const;

    /**
     * @return The framebuffer on which all rendering operations except post
     * effects happen.
     */
    wf_framebuffer get_target_framebuffer() const;

    /**
     * Initialize a workspace stream. If you need to change the stream's
     * attributes, you should stop the stream, and start it again
     *
     * @param stream The stream to be initialized
     */
    void workspace_stream_start(workspace_stream_t& stream);

    /**
     * Update the workspace stream with the latest contents on the workspace.
     * This function should be called inside the rendering cycle, i.e in a
     * render or an overlay hook.
     *
     * @param stream The workspace stream to update
     * @param scale_x Unused for now
     * @param scale_y Unused for now
     */
    void workspace_stream_update(workspace_stream_t& stream,
        float scale_x = 1, float scale_y = 1);
    /**
     * Stop the workspace stream. You can change the stream's workspace
     * after this call (but before the next stream start).
     *
     * @param stream The stream to be stopped
     */
    void workspace_stream_stop(workspace_stream_t& stream);
  private:
    class impl;
    std::unique_ptr<impl> pimpl;
};
}

#endif
