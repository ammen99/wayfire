#include "render-manager.hpp"
#include "output.hpp"
#include "../core/core-impl.hpp"
#include "util.hpp"
#include "workspace-manager.hpp"
#include "../core/seat/input-manager.hpp"
#include "opengl.hpp"
#include "debug.hpp"
#include "../main.hpp"
#include <algorithm>
#include <nonstd/reverse.hpp>
#include <nonstd/safe-list.hpp>

extern "C"
{
    /* wlr uses some c99 extensions, we "disable" the static keyword to workaround */
#define static
#include <wlr/render/wlr_renderer.h>
#undef static
#include <wlr/types/wlr_output_damage.h>
#include <wlr/util/region.h>
}

namespace wf
{
/**
 * output_damage_t is responsible for tracking the damage on a given output.
 */
struct output_damage_t
{
    wf::wl_listener_wrapper on_damage_destroy;

    wf_region frame_damage;
    wlr_output *output;
    wlr_output_damage *damage_manager;
    output_t *wo;

    output_damage_t(output_t *output)
    {
        this->output = output->handle;
        this->wo = output;

        damage_manager = wlr_output_damage_create(this->output);

        on_damage_destroy.set_callback([=] (void *) { damage_manager = nullptr; });
        on_damage_destroy.connect(&damage_manager->events.destroy);
    }

    /**
     * Damage the given box
     */
    void damage(const wlr_box& box)
    {
        frame_damage |= box;

        auto sbox = box;
        if (damage_manager)
            wlr_output_damage_add_box(damage_manager, &sbox);

        schedule_repaint();
    }

    /**
     * Damage the given region
     */
    void damage(const wf_region& region)
    {
        frame_damage |= region;
        if (damage_manager)
        {
            wlr_output_damage_add(damage_manager,
                const_cast<wf_region&> (region).to_pixman());
        }

        schedule_repaint();
    }

    /**
     * Make the output current. This sets its EGL context as current, checks
     * whether there is any damage and makes sure frame_damage contains all the
     * damage needed for repainting the next frame.
     */
    bool make_current(bool& need_swap)
    {
        if (!damage_manager)
            return false;

        wf_region tmp_region;
        auto r = wlr_output_damage_attach_render(damage_manager, &need_swap,
            tmp_region.to_pixman());

        if (!r) return false;

        frame_damage |= tmp_region;
        if (runtime_config.no_damage_track)
            frame_damage |= get_damage_box();

        return true;
    }

    /**
     * Return the damage that has been scheduled for the next frame up to now,
     * or, if in a repaint, the damage for the current frame
     */
    wf_region get_scheduled_damage()
    {
        return frame_damage;
    }

    /**
     * Swap the output buffers. Also clears the scheduled damage.
     */
    void swap_buffers(wf_region& swap_damage)
    {
        if (!output)
            return;

        int w, h;
        wlr_output_transformed_resolution(output, &w, &h);

        /* Make sure that the damage is in buffer coordinates */
        wl_output_transform transform =
            wlr_output_transform_invert(output->transform);
        wlr_region_transform(swap_damage.to_pixman(), swap_damage.to_pixman(),
            transform, w, h);

        wlr_output_set_damage(output,
            const_cast<wf_region&> (swap_damage).to_pixman());
        wlr_output_commit(output);
        frame_damage.clear();
    }

    /**
     * Schedule a frame for the output
     */
    wf::wl_idle_call idle_redraw;
    void schedule_repaint()
    {
        wlr_output_schedule_frame(output);
        if (!idle_redraw.is_connected())
        {
            idle_redraw.run_once([&] () {
                wlr_output_schedule_frame(output);
            });
        }
    }

    /**
     * Return the extents of the visible region for the output.
     */
    wlr_box get_damage_box() const
    {
        int w, h;
        wlr_output_transformed_resolution(output, &w, &h);
        return {0, 0, w, h};
    }

    /**
     * Same as render_manager::get_ws_box()
     */
    wlr_box get_ws_box(std::tuple<int, int> ws) const
    {
        GetTuple(vx, vy, ws);
        GetTuple(cx, cy, wo->workspace->get_current_workspace());

        wlr_box box = get_damage_box();
        box.x = (vx - cx) * box.width;
        box.y = (vy - cy) * box.height;

        return box;
    }

    /**
     * Returns the scheduled damage for the given workspace, in coordinates
     * relative to the workspace itself
     */
    wf_region get_ws_damage(std::tuple<int, int> ws)
    {
        auto ws_box = get_ws_box(ws);
        return (frame_damage & ws_box) + wf_point{-ws_box.x, -ws_box.y};
    }

    /**
     * Same as render_manager::damage_whole()
     */
    void damage_whole()
    {
        GetTuple(vw, vh, wo->workspace->get_workspace_grid_size());
        GetTuple(vx, vy, wo->workspace->get_current_workspace());

        int sw, sh;
        wlr_output_transformed_resolution(output, &sw, &sh);
        damage({-vx * sw, -vy * sh, vw * sw, vh * sh});
    }

    wf::wl_idle_call idle_damage;
    /**
     * Same as render_manager::damage_whole_idle()
     */
    void damage_whole_idle()
    {
        damage_whole();
        if (!idle_damage.is_connected())
            idle_damage.run_once([&] () { damage_whole(); });
    }
};

/**
 * Very simple class to manage effect hooks
 */
struct effect_hook_manager_t
{
    using effect_container_t = wf::safe_list_t<effect_hook_t*>;
    effect_container_t effects[OUTPUT_EFFECT_TOTAL];

    void add_effect(effect_hook_t* hook, output_effect_type_t type)
    {
        effects[type].push_back(hook);
    }

    void rem_effect(effect_hook_t *hook)
    {
        for (int i = 0; i < OUTPUT_EFFECT_TOTAL; i++)
            effects[i].remove_all(hook);
    }

    void run_effects(output_effect_type_t type)
    {
        effects[type].for_each([] (auto effect)
            { (*effect)(); });
    }
};

/**
 * A class to manage and run postprocessing effects
 */
struct postprocessing_manager_t
{
    using post_container_t = wf::safe_list_t<post_hook_t*>;
    post_container_t post_effects;
    wf_framebuffer_base post_buffers[3];
    /* Buffer to which other operations render to */
    static constexpr uint32_t default_out_buffer = 0;

    output_t *output;
    uint32_t output_width, output_height;
    postprocessing_manager_t(output_t *output)
    {
        this->output = output;
    }

    void allocate(int width, int height)
    {
        if (post_effects.size() == 0)
            return;

        output_width = width;
        output_height = height;

        OpenGL::render_begin();
        post_buffers[default_out_buffer].allocate(width, height);
        OpenGL::render_end();
    }

    void add_post(post_hook_t* hook)
    {
        post_effects.push_back(hook);
        output->render->damage_whole_idle();
    }

    void rem_post(post_hook_t *hook)
    {
        post_effects.remove_all(hook);
        output->render->damage_whole_idle();
    }

    /* Run all postprocessing effects, rendering to alternating buffers and
     * finally to the screen.
     *
     * NB: 2 buffers just aren't enough. We render to the zero buffer, and then
     * we alternately render to the second and the third. The reason: We track
     * damage. So, we need to keep the whole buffer each frame. */
    void run_post_effects()
    {
        static wf_framebuffer_base default_framebuffer;
        default_framebuffer.tex = default_framebuffer.fb = 0;

        int last_buffer_idx = default_out_buffer;
        int next_buffer_idx = 1;

        post_effects.for_each([&] (auto post) -> void
        {
            /* The last postprocessing hook renders directly to the screen, others to
             * the currently free buffer */
            wf_framebuffer_base& next_buffer =
                (post == post_effects.back() ? default_framebuffer :
                 post_buffers[next_buffer_idx]);

            OpenGL::render_begin();
            /* Make sure we have the correct resolution */
            next_buffer.allocate(output_width, output_height);
            OpenGL::render_end();

            (*post) (post_buffers[last_buffer_idx], next_buffer);

            last_buffer_idx = next_buffer_idx;
            next_buffer_idx ^= 0b11; // alternate 1 and 2
        });
    }

    /**
     * Get the input framebuffer and texture for the postprocessing manager
     */
    void get_default_target(uint32_t& fb, uint32_t& tex)
    {
        if (post_effects.size())
        {
            fb = post_buffers[default_out_buffer].fb;
            tex = post_buffers[default_out_buffer].tex;
        } else
        {
            fb = 0;
            tex = 0;
        }
    }
};

class wf::render_manager::impl
{
  public:
    wf::wl_listener_wrapper on_frame;

    output_t *output;
    std::unique_ptr<output_damage_t> output_damage;
    std::unique_ptr<effect_hook_manager_t> effects;
    std::unique_ptr<postprocessing_manager_t> postprocessing;

    impl(output_t *o)
        : output(o)
    {
        output_damage = std::make_unique<output_damage_t> (o);
        output_damage->damage(output_damage->get_damage_box());

        effects = std::make_unique<effect_hook_manager_t> ();
        postprocessing = std::make_unique<postprocessing_manager_t>(o);

        on_frame.set_callback([&] (void*) { paint(); });
        on_frame.connect(&output_damage->damage_manager->events.frame);

        init_default_streams();
        output_damage->schedule_repaint();
    }

    /* A stream for each workspace */
    std::vector<std::vector<workspace_stream_t>> default_streams;
    /* The stream pointing to the current workspace */
    nonstd::observer_ptr<workspace_stream_t> current_ws_stream;
    void init_default_streams()
    {
        GetTuple(vwidth, vheight, output->workspace->get_workspace_grid_size());
        default_streams.resize(vwidth);
        for (int i = 0; i < vwidth; i++)
        {
            default_streams[i].resize(vheight);
            for (int j = 0; j < vheight; j++)
            {
                default_streams[i][j].buffer.fb = 0;
                default_streams[i][j].buffer.tex = 0;
                default_streams[i][j].ws = std::make_tuple(i, j);
            }
        }
    }

    render_hook_t renderer;
    void set_renderer(render_hook_t rh)
    {
        renderer = rh;
        output_damage->damage_whole_idle();
    }

    int constant_redraw_counter = 0;
    void set_redraw_always(bool always)
    {
        constant_redraw_counter += (always ? 1 : -1);
        if (constant_redraw_counter > 1) /* no change, exit */
            return;

        if (constant_redraw_counter < 0)
        {
            log_error("constant_redraw_counter got below 0!");
            constant_redraw_counter = 0;
            return;
        }

        output_damage->schedule_repaint();
    }

    int output_inhibit_counter = 0;
    void add_inhibit(bool add)
    {
        output_inhibit_counter += add ? 1 : -1;
        if (output_inhibit_counter == 0)
        {
            output_damage->damage_whole_idle();
            output->emit_signal("start-rendering", nullptr);
        }
    }

    wf_framebuffer get_target_framebuffer() const
    {
        wf_framebuffer fb;
        fb.geometry = output->get_relative_geometry();
        fb.wl_transform = output->handle->transform;
        fb.transform = get_output_matrix_from_transform(
            (wl_output_transform)fb.wl_transform);
        fb.scale = output->handle->scale;

        postprocessing->get_default_target(fb.fb, fb.tex);

        fb.viewport_width = output->handle->width;
        fb.viewport_height = output->handle->height;

        return fb;
    }

    /* Actual rendering functions */

    /**
     * Bind the output's EGL surface, allocate buffers
     */
    void bind_output()
    {
        OpenGL::bind_output(output);

        /* Make sure the default buffer has enough size */
        postprocessing->allocate(output->handle->width, output->handle->height);
    }

    /**
     * The default renderer, which just makes sure the correct workspace stream
     * is drawn to the framebuffer
     */
    void default_renderer(wf_region& swap_damage)
    {
        if (runtime_config.damage_debug)
        {
            /* Clear the screen to yellow, so that the repainted parts are
             * visible */
            swap_damage |= output_damage->get_damage_box();

            OpenGL::render_begin(output->handle->width, output->handle->height, 0);
            OpenGL::clear({1, 1, 0, 1});
            OpenGL::render_end();
        }

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        auto target_stream = &default_streams[vx][vy];
        if (current_ws_stream.get() != target_stream)
        {
            if (current_ws_stream)
                workspace_stream_stop(*current_ws_stream);

            current_ws_stream = nonstd::make_observer(target_stream);
            workspace_stream_start(*current_ws_stream);
        } else
        {
            workspace_stream_update(*current_ws_stream);
        }
    }

    /**
     * Render an output. Either calls the built-in renderer, or the render hook
     * of a plugin
     */
    void render_output(wf_region& swap_damage)
    {
        if (renderer)
        {
            renderer(get_target_framebuffer());
            /* TODO: let custom renderers specify what they want to repaint... */
            swap_damage |= output_damage->get_damage_box();
        } else
        {
            swap_damage = output_damage->get_scheduled_damage();
            swap_damage &= output_damage->get_damage_box();
            default_renderer(swap_damage);
        }
    }

    /**
     * Repaints the whole output, includes all effects and hooks
     */
    void paint()
    {
        /* Part 1: frame setup: query damage, etc. */
        timespec repaint_started;
        clock_gettime(CLOCK_MONOTONIC, &repaint_started);
        wf_region swap_damage;

        effects->run_effects(OUTPUT_EFFECT_PRE);

        bool needs_swap;
        if (!output_damage->make_current(needs_swap))
            return;

        if (!needs_swap && !constant_redraw_counter)
        {
            /* Optimization: the output doesn't need a swap (so isn't damaged),
             * and no plugin wants custom redrawing - we can just skip the whole
             * repaint */
            post_paint();
            return;
        }

        bind_output();

        /* Part 2: call the renderer, which draws the scenegraph */
        render_output(swap_damage);

        /* Part 3: finalize the scene: overlay effects and sw cursors */
        effects->run_effects(OUTPUT_EFFECT_OVERLAY);

        if (postprocessing->post_effects.size())
            swap_damage |= output_damage->get_damage_box();

        OpenGL::render_begin(get_target_framebuffer());
        wlr_output_render_software_cursors(output->handle, swap_damage.to_pixman());
        OpenGL::render_end();

        /* Part 4: postprocessing effects */
        postprocessing->run_post_effects();
        if (output_inhibit_counter)
        {
            OpenGL::render_begin(output->handle->width, output->handle->height, 0);
            OpenGL::clear({0, 0, 0, 1});
            OpenGL::render_end();
        }

        /* Part 5: finalize frame: swap buffers, send frame_done, etc */
        OpenGL::unbind_output(output);
        output_damage->swap_buffers(swap_damage);
        post_paint();
    }

    /**
     * Execute post-paint actions.
     */
    void post_paint()
    {
        effects->run_effects(OUTPUT_EFFECT_POST);

        if (constant_redraw_counter)
            output_damage->schedule_repaint();

        /* TODO: do this only if the view isn't fully occluded by another */
        std::vector<wayfire_view> visible_views;
        if (renderer)
        {
            visible_views = output->workspace->get_views_in_layer(
                wf::VISIBLE_LAYERS);
        } else
        {
            visible_views = output->workspace->get_views_on_workspace(
                output->workspace->get_current_workspace(),
                wf::MIDDLE_LAYERS, false);

            // send to all panels/backgrounds/etc
            auto additional_views = output->workspace->get_views_in_layer(
                wf::BELOW_LAYERS | wf::ABOVE_LAYERS);

            visible_views.insert(visible_views.end(),
                additional_views.begin(), additional_views.end());
        }

        timespec repaint_ended;
        clock_gettime(CLOCK_MONOTONIC, &repaint_ended);
        for (auto& view : visible_views)
        {
            if (!view->is_mapped())
                continue;

            for (auto& child : view->enumerate_surfaces())
                child.surface->send_frame_done(repaint_ended);
        }
    }

    /* Workspace stream implementation */
    void workspace_stream_start(workspace_stream_t& stream)
    {
        stream.running = true;
        stream.scale_x = stream.scale_y = 1;

        /* damage the whole workspace region, so that we get a full repaint
         * when updating the workspace */
        output_damage->damage(output_damage->get_ws_box(stream.ws));
        workspace_stream_update(stream, 1, 1);
    }

    /**
     * Represents a surface together with its damage for the current frame
     */
    struct damaged_surface_t
    {
        wf::surface_interface_t *surface = nullptr;
        wf::view_interface_t *view = nullptr;

        /* For views, this is the coordinates the framebuffer should have.
         * For surfaces, this is the coordinates of the surface inside the
         * framebuffer */
        wf_point pos;
        wf_region damage;
    };
    using damaged_surface = std::unique_ptr<damaged_surface_t>;

    /**
     * Represents the state while calculating what parts of the output
     * to repaint
     */
    struct workspace_stream_repaint_t
    {
        std::vector<damaged_surface> to_render;
        wf_region ws_damage;
        wf_framebuffer fb;

        int ws_dx;
        int ws_dy;
    };

    /**
     * Calculate the damaged region of a view which renders with its snapshot
     * and add it to the render list
     *
     * @param view_delta The offset of the view so that it has workspace-local
     * coordinates
     */
    void schedule_snapshotted_view(workspace_stream_repaint_t& repaint,
        wayfire_view view, wf_point view_delta)
    {
        auto ds = damaged_surface(new damaged_surface_t);

        auto bbox = view->get_bounding_box() + (-view_delta);
        bbox = repaint.fb.damage_box_from_geometry_box(bbox);

        ds->damage = repaint.ws_damage & bbox;
        if (!ds->damage.empty())
        {
            ds->pos = view_delta;
            ds->view = view.get();

            repaint.to_render.push_back(std::move(ds));
        }
    }

    /**
     * Calculate the damaged region of a simple wayfire_surface_t and
     * push it in the repaint list if needed.
     */
    void schedule_surface(workspace_stream_repaint_t& repaint,
        wf::surface_interface_t *surface, wf_point pos)
    {
        if (!surface->is_mapped())
            return;

        if (repaint.ws_damage.empty())
            return;

        auto ds = damaged_surface(new damaged_surface_t);

        wlr_box obox = {
            .x = pos.x,
            .y = pos.y,
            .width = surface->get_size().width,
            .height = surface->get_size().height
        };
        obox = repaint.fb.damage_box_from_geometry_box(obox);

        ds->damage = repaint.ws_damage & obox;
        if (!ds->damage.empty())
        {
            ds->pos = pos;
            ds->surface = surface;

            /* Subtract opaque region from workspace damage. The views below
             * won't be visible, so no need to damage them */
            ds->surface->subtract_opaque(repaint.ws_damage, pos.x, pos.y);
            repaint.to_render.push_back(std::move(ds));
        }
    }

    /**
     * Calculate the damaged region for drag icons, and add them to the repaint
     * list if necessary
     */
    void schedule_drag_icon(workspace_stream_repaint_t& repaint)
    {
        auto& drag_icon = wf::get_core_impl().input->drag_icon;
        if (renderer || !drag_icon || !drag_icon->is_mapped())
            return;

        drag_icon->set_output(output);

        auto offset = drag_icon->get_offset();
        auto og = output->get_layout_geometry();
        offset.x -= og.x;
        offset.y -= og.y;

        for (auto& child : drag_icon->enumerate_surfaces(offset))
            schedule_surface(repaint, child.surface, child.position);
    }

    /**
     * Reset the drag icon state for this output
     */
    void unschedule_drag_icon()
    {
        auto& drag_icon = wf::get_core_impl().input->drag_icon;
        if (drag_icon && drag_icon->is_mapped())
            drag_icon->set_output(nullptr);
    }

    /**
     * Iterate all visible surfaces on the workspace, and check whether
     * they need repaint.
     */
    void check_schedule_surfaces(workspace_stream_repaint_t& repaint,
        workspace_stream_t& stream)
    {
        auto views = output->workspace->get_views_on_workspace(stream.ws,
            wf::VISIBLE_LAYERS, false);

        schedule_drag_icon(repaint);

        auto it = views.begin();
        while (it != views.end() && !repaint.ws_damage.empty())
        {
            auto view = *it;
            wf_point view_delta{0, 0};

            if (!view->is_visible())
            {
                ++it;
                continue;
            }

            if (view->role != VIEW_ROLE_SHELL_VIEW)
                view_delta = {repaint.ws_dx, repaint.ws_dy};

            /* We use the snapshot of a view if either condition is happening:
             * 1. The view has a transform
             * 2. The view is visible, but not mapped
             *    => it is snapshotted and kept alive by some plugin */
            if (view->has_transformer() || !view->is_mapped())
            {
                /* Snapshotted views include all of their subsurfaces, so we
                 * handle them separately */
                schedule_snapshotted_view(repaint, view, view_delta);
            }
            else
            {
                /* Make sure view position is relative to the workspace
                 * being rendered */
                auto obox = view->get_output_geometry();
                obox.x -= view_delta.x;
                obox.y -= view_delta.y;

                for (auto& child : view->enumerate_surfaces({obox.x, obox.y}))
                    schedule_surface(repaint, child.surface, child.position);
            }

            ++it;
        }
    }

    /**
     * Setup the stream, calculate damaged region, etc.
     */
    workspace_stream_repaint_t calculate_repaint_for_stream(
        workspace_stream_t& stream, float scale_x, float scale_y)
    {
        workspace_stream_repaint_t repaint;
        repaint.ws_damage = output_damage->get_ws_damage(stream.ws);

        /* we don't have to update anything */
        if (repaint.ws_damage.empty())
            return repaint;

        if (scale_x != stream.scale_x || scale_y != stream.scale_y)
        {
            /* FIXME: enable scaled rendering */
            //        stream->scale_x = scale_x;
            //        stream->scale_y = scale_y;

            //   ws_damage |= get_damage_box();
        }

        OpenGL::render_begin();
        stream.buffer.allocate(output->handle->width, output->handle->height);
        OpenGL::render_end();

        repaint.fb = get_target_framebuffer();
        if (stream.buffer.fb != 0 && stream.buffer.tex != 0)
        {
            /* Use the workspace buffers */
            repaint.fb.fb = stream.buffer.fb;
            repaint.fb.tex = stream.buffer.tex;
        }

        auto g = output->get_relative_geometry();

        GetTuple(x, y, stream.ws);
        GetTuple(cx, cy, output->workspace->get_current_workspace());

        repaint.ws_dx = (x - cx) * g.width,
        repaint.ws_dy = (y - cy) * g.height;

        return repaint;
    }

    void clear_empty_areas(workspace_stream_repaint_t& repaint, wf_color color)
    {
        OpenGL::render_begin(repaint.fb);
        for (const auto& rect : repaint.ws_damage)
        {
            wlr_box damage = wlr_box_from_pixman_box(rect);
            repaint.fb.scissor(
                repaint.fb.framebuffer_box_from_damage_box(damage));

            OpenGL::clear(color,
                GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        OpenGL::render_end();
    }

    void render_views(workspace_stream_repaint_t& repaint)
    {
        wf_geometry fb_geometry = repaint.fb.geometry;

        for (auto& ds : wf::reverse(repaint.to_render))
        {
            if (ds->view)
            {
                repaint.fb.geometry.x = ds->pos.x;
                repaint.fb.geometry.y = ds->pos.y;
                ds->view->render_transformed(repaint.fb, ds->damage);
            }
            else
            {
                repaint.fb.geometry = fb_geometry;
                ds->surface->simple_render(repaint.fb,
                    ds->pos.x, ds->pos.y, ds->damage);
            }
        }
    }

    void workspace_stream_update(workspace_stream_t& stream,
        float scale_x = 1, float scale_y = 1)
    {
        workspace_stream_repaint_t repaint =
            calculate_repaint_for_stream(stream, scale_x, scale_y);

        if (repaint.ws_damage.empty())
            return;

        {
            stream_signal_t data(repaint.ws_damage, repaint.fb);
            output->render->emit_signal("workspace-stream-pre", &data);
        }

        check_schedule_surfaces(repaint, stream);
        clear_empty_areas(repaint, stream.background);
        render_views(repaint);

        unschedule_drag_icon();
        {
            stream_signal_t data(repaint.ws_damage, repaint.fb);
            output->render->emit_signal("workspace-stream-post", &data);
        }
    }

    void workspace_stream_stop(workspace_stream_t& stream)
    {
        stream.running = false;
    }
};

render_manager::render_manager(output_t *o)
    : pimpl(new impl(o)) { }
render_manager::~render_manager() = default;
void render_manager::set_renderer(render_hook_t rh) { pimpl->set_renderer(rh); }
void render_manager::set_redraw_always(bool always) { pimpl->set_redraw_always(always); }
void render_manager::schedule_redraw() { pimpl->output_damage->schedule_repaint(); }
void render_manager::add_inhibit(bool add) { pimpl->add_inhibit(add); }
void render_manager::add_effect(effect_hook_t* hook, output_effect_type_t type) {pimpl->effects->add_effect(hook, type); }
void render_manager::rem_effect(effect_hook_t* hook) { pimpl->effects->rem_effect(hook); }
void render_manager::add_post(post_hook_t* hook) { pimpl->postprocessing->add_post(hook); }
void render_manager::rem_post(post_hook_t* hook) { pimpl->postprocessing->rem_post(hook); }
wf_region render_manager::get_scheduled_damage() { return pimpl->output_damage->get_scheduled_damage(); }
void render_manager::damage_whole() { pimpl->output_damage->damage_whole(); }
void render_manager::damage_whole_idle() { pimpl->output_damage->damage_whole_idle(); }
void render_manager::damage(const wlr_box& box) { pimpl->output_damage->damage(box); }
void render_manager::damage(const wf_region& region) { pimpl->output_damage->damage(region); }
wlr_box render_manager::get_damage_box() const { return pimpl->output_damage->get_damage_box(); }
wlr_box render_manager::get_ws_box(std::tuple<int, int> ws) const { return pimpl->output_damage->get_ws_box(ws); }
wf_framebuffer render_manager::get_target_framebuffer() const { return pimpl->get_target_framebuffer(); }
void render_manager::workspace_stream_start(workspace_stream_t& stream) { pimpl->workspace_stream_start(stream); }
void render_manager::workspace_stream_update(workspace_stream_t& stream,
    float scale_x, float scale_y){ pimpl->workspace_stream_update(stream); }
void render_manager::workspace_stream_stop(workspace_stream_t& stream) { pimpl->workspace_stream_stop(stream); }

} // namespace wf

/* End render_manager */
