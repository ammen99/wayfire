#include "debug.hpp"
#include "opengl.hpp"
#include "output.hpp"
#include "view.hpp"
#include "core.hpp"
#include "signal-definitions.hpp"
#include "input-manager.hpp"
#include "render-manager.hpp"
#include "workspace-manager.hpp"
#include "wayfire-shell-protocol.h"

#include <linux/input.h>

extern "C"
{
    /* wlr uses some c99 extensions, we "disable" the static keyword to workaround */
#define static
#include <wlr/render/wlr_renderer.h>
#undef static
#include <wlr/types/wlr_output_damage.h>
#include <wlr/util/region.h>
}

#include "wm.hpp"

#include <sstream>
#include <memory>
#include <dlfcn.h>
#include <algorithm>

#include <cstring>
#include <config.hpp>

namespace
{
template<class A, class B> B union_cast(A object)
{
    union {
        A x;
        B y;
    } helper;
    helper.x = object;
    return helper.y;
}
}

/* Controls loading of plugins */
struct plugin_manager
{
    std::vector<wayfire_plugin> plugins;

    plugin_manager(wayfire_output *o, wayfire_config *config)
    {
        load_dynamic_plugins();
        init_default_plugins();

        for (auto p : plugins)
        {
            p->grab_interface = new wayfire_grab_interface_t(o);
            p->output = o;

            p->init(config);
        }
    }

    ~plugin_manager()
    {
        for (auto p : plugins)
        {
            p->fini();
            delete p->grab_interface;

            if (p->dynamic)
                dlclose(p->handle);
            p.reset();
        }
    }



    wayfire_plugin load_plugin_from_file(std::string path, void **h)
    {
        void *handle = dlopen(path.c_str(), RTLD_NOW);
        if(handle == NULL)
        {
            log_error("error loading plugin: %s", dlerror());
            return nullptr;
        }


        auto initptr = dlsym(handle, "newInstance");
        if(initptr == NULL)
        {
            log_error("%s: missing newInstance(). %s", path.c_str(), dlerror());
            return nullptr;
        }

        log_debug("loading plugin %s", path.c_str());
        get_plugin_instance_t init = union_cast<void*, get_plugin_instance_t> (initptr);
        *h = handle;
        return wayfire_plugin(init());
    }

    void load_dynamic_plugins()
    {
        std::stringstream stream(core->plugins);
        auto path = core->plugin_path + "/wayfire/";

        std::string plugin;
        while(stream >> plugin)
        {
            if(plugin != "")
            {
                void *handle = nullptr;
                auto ptr = load_plugin_from_file(path + "/lib" + plugin + ".so", &handle);
                if(ptr)
                {
                    ptr->handle  = handle;
                    ptr->dynamic = true;
                    plugins.push_back(ptr);
                }
            }
        }
    }

    template<class T>
    wayfire_plugin create_plugin()
    {
        return std::static_pointer_cast<wayfire_plugin_t>(std::make_shared<T>());
    }

    void init_default_plugins()
    {
        plugins.push_back(create_plugin<wayfire_focus>());
        plugins.push_back(create_plugin<wayfire_close>());
        plugins.push_back(create_plugin<wayfire_exit>());
        plugins.push_back(create_plugin<wayfire_fullscreen>());
        plugins.push_back(create_plugin<wayfire_handle_focus_parent>());
    }
};

/* End plugin_manager */

/* Start render_manager */
struct wf_output_damage
{
    pixman_region32_t frame_damage;
    wlr_output *output;
    wlr_output_damage *damage_manager;

    wf_output_damage(wlr_output *output)
    {
        this->output = output;
        pixman_region32_init(&frame_damage);
        damage_manager = wlr_output_damage_create(output);
    }

    void add()
    {
        int w, h;
        wlr_output_transformed_resolution(output, &w, &h);
        add({0, 0, w, h});
    }

    void add(const wlr_box& box)
    {
        pixman_region32_union_rect(&frame_damage, &frame_damage,
                                   box.x, box.y, box.width, box.height);

        auto sbox = box;
        wlr_output_damage_add_box(damage_manager, &sbox);
        schedule_repaint();
    }

    void add(pixman_region32_t *region)
    {
        pixman_region32_union(&frame_damage, &frame_damage, region);
        wlr_output_damage_add(damage_manager, region);
        schedule_repaint();
    }

    bool make_current(pixman_region32_t *out_damage, bool& need_swap)
    {
        auto r = wlr_output_damage_make_current(damage_manager, &need_swap, out_damage);
        if (r)
        {
            pixman_region32_t regular;

            int w, h;
            wlr_output_transformed_resolution(output, &w, &h);

            pixman_region32_init_rect(&regular, 0, 0, w, h);
            pixman_region32_subtract(&frame_damage, &frame_damage, &regular);

            pixman_region32_union(out_damage, out_damage, &frame_damage);

            pixman_region32_fini(&regular);
        }
        return r;
    }

    void swap_buffers(timespec *when, pixman_region32_t *swap_damage)
    {
        wlr_output_damage_swap_buffers(damage_manager, when, swap_damage);
        pixman_region32_clear(&frame_damage);
    }

    void schedule_repaint()
    {
        wlr_output_schedule_frame(output);
    }
};

void frame_cb (wl_listener*, void *data)
{
    auto damage = static_cast<wlr_output*>(data);
    assert(damage);

    auto output = core->get_output(damage);
    assert(output);
    output->render->paint();
}

render_manager::render_manager(wayfire_output *o)
{
    output = o;

    /* TODO: do we really need a unique_ptr? */
    output_damage = std::unique_ptr<wf_output_damage>(new wf_output_damage(output->handle));
    output_damage->add();

    frame_listener.notify = frame_cb;
    wl_signal_add(&output->handle->events.frame, &frame_listener);

    pixman_region32_init(&frame_damage);

    schedule_redraw();
}

void render_manager::load_context()
{
    ctx = OpenGL::create_gles_context(output, core->shadersrc.c_str());
    OpenGL::bind_context(ctx);

    dirty_context = false;
    output->emit_signal("reload-gl", nullptr);

    GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
    output_streams.resize(vw);

    for (int i = 0; i < vw; i++) {
        for (int j = 0;j < vh; j++) {
            output_streams[i].push_back(wf_workspace_stream{});
            output_streams[i][j].tex = output_streams[i][j].fbuff = 0;
            output_streams[i][j].ws = std::make_tuple(i, j);
        }
    }
}

void render_manager::release_context()
{
    OpenGL::release_context(ctx);
    dirty_context = true;
}

render_manager::~render_manager()
{
    wl_list_remove(&frame_listener.link);

    if (idle_redraw_source)
        wl_event_source_remove(idle_redraw_source);
    if (idle_damage_source)
        wl_event_source_remove(idle_damage_source);

    pixman_region32_fini(&frame_damage);
    release_context();
}

void render_manager::damage(const wlr_box& box)
{
    output_damage->add(box);
}

void render_manager::damage(pixman_region32_t *region)
{
    if (region)
        output_damage->add(region);
    else
        output_damage->add();
}

void redraw_idle_cb(void *data)
{
    wayfire_output *output = (wayfire_output*) data;
    assert(output);

    wlr_output_schedule_frame(output->handle);
    output->render->idle_redraw_source = NULL;
}

void render_manager::auto_redraw(bool redraw)
{
    constant_redraw += (redraw ? 1 : -1);
    if (constant_redraw > 1) /* no change, exit */
        return;
    if (constant_redraw < 0)
    {
        constant_redraw = 0;
        return;
    }

    schedule_redraw();
}

void render_manager::schedule_redraw()
{
    if (idle_redraw_source == NULL)
        idle_redraw_source = wl_event_loop_add_idle(core->ev_loop, redraw_idle_cb, output);
}

/* return damage from this frame for the given workspace, coordinates relative to the workspace */
void render_manager::get_ws_damage(std::tuple<int, int> ws, pixman_region32_t *out_damage)
{
    GetTuple(vx, vy, ws);
    GetTuple(cx, cy, output->workspace->get_current_workspace());

    int sw, sh;
    wlr_output_transformed_resolution(output->handle, &sw, &sh);

    if (!pixman_region32_selfcheck(out_damage))
        pixman_region32_init(out_damage);

    pixman_region32_intersect_rect(out_damage,
                                   &frame_damage,
                                   (vx - cx) * sw,
                                   (vy - cy) * sh,
                                   sw, sh);

    pixman_region32_translate(out_damage, (cx - vx) * sw, (cy - vy) * sh);
}

void damage_idle_cb(void *data)
{
    auto rm = (render_manager*) data;
    assert(rm);

    rm->damage(NULL);
    rm->idle_damage_source = NULL;
}


void render_manager::reset_renderer()
{
    renderer = nullptr;

    if (!idle_damage_source)
        idle_damage_source = wl_event_loop_add_idle(core->ev_loop, damage_idle_cb, this);
}

void render_manager::set_renderer(render_hook_t rh)
{
    renderer = rh;
}

void render_manager::set_hide_overlay_panels(bool set)
{
    draw_overlay_panel = !set;
}

void render_manager::render_panels()
{

    auto vp = output->workspace->get_current_workspace();
    auto views = output->workspace->get_views_on_workspace(vp, WF_ABOVE_LAYERS);
    auto it = views.rbegin();
    while (it != views.rend())
    {
        auto view = *it;
        if (!view->is_hidden) /* use is_visible() when implemented */
        {
            auto og = view->get_output_position();
            view->render(og.x, og.y, NULL);
        }

        ++it;
    }
}

static inline int64_t timespec_to_msec(const struct timespec *a) {
     return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

void render_manager::paint()
{
    timespec repaint_started;
    clock_gettime(CLOCK_MONOTONIC, &repaint_started);

    /* TODO: perhaps we don't need to copy frame damage */
    pixman_region32_clear(&frame_damage);

    bool needs_swap;
    if (!output_damage->make_current(&frame_damage, needs_swap) || !needs_swap)
        if (!constant_redraw && false)
            return;

    pixman_region32_t swap_damage;
    pixman_region32_init(&swap_damage);

  //  log_info("begin repaint %ld", timespec_to_msec(&repaint_started));
    auto rr = wlr_backend_get_renderer(core->backend);
    wlr_renderer_begin(rr, output->handle->width, output->handle->height);

    int w, h;
    wlr_output_transformed_resolution(output->handle, &w, &h);

    if (dirty_context)
        load_context();

    if (renderer)
    {
        renderer();

        /* TODO: let custom renderers specify what they want to repaint... */
        pixman_region32_union_rect(&swap_damage, &swap_damage, 0, 0, w, h);
    } else
    {
        pixman_region32_intersect_rect(&frame_damage, &frame_damage, 0, 0, w, h);

        if (pixman_region32_not_empty(&frame_damage))
        {
            /*
            log_info("region32_t is done");
            int n;
            auto rects = pixman_region32_rectangles(&frame_damage, &n);
            for (int i = 0;i < n; i++)

                log_info("got damage rect: %d@%d %d@%d", rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2);
                */

            pixman_region32_copy(&swap_damage, &frame_damage);
            GetTuple(vx, vy, output->workspace->get_current_workspace());
            auto target_stream = &output_streams[vx][vy];
            if (current_ws_stream != target_stream)
            {
                if (current_ws_stream)
                    workspace_stream_stop(current_ws_stream);

                current_ws_stream = target_stream;
                workspace_stream_start(current_ws_stream);
            } else
            {
                workspace_stream_update(current_ws_stream);
            }
        } else
        {
        }
    }

    wlr_renderer_end(rr);

//    wlr_region_scale(&swap_damage, &swap_damage, output->handle->scale);
    output_damage->swap_buffers(&repaint_started, &swap_damage);

    pixman_region32_fini(&swap_damage);
    post_paint();

    /*
       OpenGL::bind_context(ctx);
    if (renderer)
    {
        OpenGL::bind_context(ctx);
        renderer();
    }  else {
    } */
}

void render_manager::post_paint()
{
    run_effects();
    /*
    if (!renderer || draw_overlay_panel)
        render_panels();
        */

    if (constant_redraw)
        schedule_redraw();

    auto views = output->workspace->get_views_on_workspace(
        output->workspace->get_current_workspace(), WF_ALL_LAYERS);

    /* TODO: do this only if the view isn't fully occluded by another */
    for (auto v : views)
    {
        v->for_each_surface([] (wayfire_surface_t *surface, int, int)
        {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);

            if (surface->is_mapped())
                wlr_surface_send_frame_done(surface->surface, &now);
        });
    }
}

void render_manager::run_effects()
{
    std::vector<effect_hook_t*> active_effects;
    for (auto effect : output_effects)
        active_effects.push_back(effect);

    for (auto& effect : active_effects)
        (*effect)();
}

void render_manager::add_output_effect(effect_hook_t* hook)
{
    output_effects.push_back(hook);
}

void render_manager::rem_effect(const effect_hook_t *hook)
{

    auto it = std::remove_if(output_effects.begin(), output_effects.end(),
                             [hook] (const effect_hook_t *h)
                             {
                                 if (h == hook)
                                     return true;
                                 return false;
                             });

    output_effects.erase(it, output_effects.end());
}

void render_manager::workspace_stream_start(wf_workspace_stream *stream)
{
    stream->running = true;
    stream->scale_x = stream->scale_y = 1;

    OpenGL::bind_context(output->render->ctx);

    if (stream->fbuff == (uint)-1 || stream->tex == (uint)-1)
        OpenGL::prepare_framebuffer(stream->fbuff, stream->tex);

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, stream->fbuff));
    GL_CALL(glClearColor(0, 0, 0, 1));
    GL_CALL(glClear(GL_COLOR_BUFFER_BIT));

    GetTuple(vx, vy, stream->ws);
    GetTuple(cx, cy, output->workspace->get_current_workspace());

    int sw, sh;
    wlr_output_transformed_resolution(output->handle, &sw, &sh);

    /* damage the whole workspace region, so that we get a full repaint */
    pixman_region32_union_rect(&frame_damage,
                               &frame_damage,
                               (vx - cx) * sw,
                               (vy - cy) * sh,
                               sw, sh);

    workspace_stream_update(stream, 1, 1);

}

void render_manager::workspace_stream_update(wf_workspace_stream *stream,
                                             float scale_x, float scale_y)
{
    OpenGL::bind_context(output->render->ctx);
    auto g = output->get_relative_geometry();

    GetTuple(x, y, stream->ws);
    GetTuple(cx, cy, output->workspace->get_current_workspace());

    int dx = g.x + (x - cx) * g.width,
        dy = g.y + (y - cy) * g.height;

    pixman_region32_t ws_damage;
    pixman_region32_init(&ws_damage);
    get_ws_damage(stream->ws, &ws_damage);

    float current_resolution = stream->scale_x * stream->scale_y;
    float target_resolution = scale_x * scale_y;

    float scaling = std::max(current_resolution / target_resolution,
                             target_resolution / current_resolution);

    if (scaling > 2)
    {
        int sw, sh;
        wlr_output_transformed_resolution(output->handle, &sw, &sh);

        pixman_region32_union_rect(&ws_damage, &ws_damage, dx, dy,
                sw, sh);
    }

    /* we don't have to update anything */
    if (!pixman_region32_not_empty(&ws_damage))
    {
        pixman_region32_fini(&ws_damage);
        return;
    }

    if (scale_x != stream->scale_x || scale_y != stream->scale_y)
    {
        /* FIXME: enable scaled rendering */
//        stream->scale_x = scale_x;
//        stream->scale_y = scale_y;

        int sw, sh;
        wlr_output_transformed_resolution(output->handle, &sw, &sh);
        pixman_region32_union_rect(&ws_damage, &ws_damage, dx, dy,
                                   sw, sh);
    }

    auto views = output->workspace->get_views_on_workspace(stream->ws, WF_ALL_LAYERS);

    struct damaged_surface_t
    {
        wayfire_surface_t *surface;

        int x, y;
        pixman_region32_t damage;

        ~damaged_surface_t()
        { pixman_region32_fini(&damage); }
    };

    using damaged_surface = std::unique_ptr<damaged_surface_t>;
    std::vector<damaged_surface> to_render;

    auto it = views.begin();

    while (it != views.end() && pixman_region32_not_empty(&ws_damage))
    {
        auto view = *it;
        int view_dx = 0, view_dy = 0;

        if (!view->is_visible())
            goto next;

        if (!view->is_special)
        {
            view_dx = -dx;
            view_dy = -dy;
        }

        /* We use the snapshot of a view if either condition is happening:
         * 1. The view has a transform
         * 2. The view is visible, but not mapped
         *    => it is snapshoted and kept alive by some plugin */

        /* Snapshoted views include all of their subsurfaces, so we handle them separately */
        if (view->get_transformer() || !view->is_mapped())
        {
            auto ds = damaged_surface(new damaged_surface_t);

            auto bbox = view->get_bounding_box();
            auto obox = view->get_output_geometry();

            bbox = bbox + wf_point{view_dx, view_dy};
            bbox = get_output_box_from_box(bbox, output->handle->scale,
                                           WL_OUTPUT_TRANSFORM_NORMAL);

            pixman_region32_init_rect(&ds->damage,
                                      bbox.x, bbox.y, bbox.width, bbox.height);

            pixman_region32_intersect(&ds->damage, &ds->damage, &ws_damage);
            if (pixman_region32_not_empty(&ds->damage))
            {
                ds->x = obox.x + view_dx;
                ds->y = obox.y + view_dy;
                ds->surface = view.get();

                to_render.push_back(std::move(ds));
            }

            goto next;
        }

        /* Iterate over all subsurfaces/menus of a "regular" view */
        view->for_each_surface([&] (wayfire_surface_t *surface, int x, int y)
        {
            if (!surface->is_mapped())
                return;

            if (!wlr_surface_has_buffer(surface->surface)
                || !pixman_region32_not_empty(&ws_damage))
                return;

            /* make sure all coordinates are in workspace-local coords */
            x += view_dx;
            y += view_dy;

            auto ds = damaged_surface(new damaged_surface_t);

            auto obox = surface->get_output_geometry();
            obox.x = x;
            obox.y = y;

            obox = get_output_box_from_box(obox, output->handle->scale,
                                           WL_OUTPUT_TRANSFORM_NORMAL);

            pixman_region32_init_rect(&ds->damage,
                                      obox.x, obox.y,
                                      obox.width, obox.height);

            pixman_region32_intersect(&ds->damage, &ds->damage, &ws_damage);
            if (pixman_region32_not_empty(&ds->damage))
            {
                ds->x = x;
                ds->y = y;
                ds->surface = surface;

                if (ds->surface->alpha >= 0.999f)
                {
                    pixman_region32_t opaque;
                    pixman_region32_init(&opaque);
                    pixman_region32_copy(&opaque, &surface->surface->current->opaque);
                    pixman_region32_translate(&opaque, x, y);
                    wlr_region_scale(&opaque, &opaque, output->handle->scale);
                    //pixman_region32_subtract(&ws_damage, &ws_damage, &opaque);
                    pixman_region32_fini(&opaque);
                }

                to_render.push_back(std::move(ds));
            }
        });

        next: ++it;
    };

    /*
     TODO; implement scale != 1
    glm::mat4 scale = glm::scale(glm::mat4(), glm::vec3(scale_x, scale_y, 1));
    glm::mat4 translate = glm::translate(glm::mat4(), glm::vec3(scale_x - 1, scale_y - 1, 0));
    std::swap(wayfire_view_transform::global_scale, scale);
    std::swap(wayfire_view_transform::global_translate, translate);
    */


    auto rev_it = to_render.rbegin();
    while(rev_it != to_render.rend())
    {
        auto ds = std::move(*rev_it);
        ds->surface->render_fb(ds->x, ds->y, &ds->damage, stream->fbuff);

        ++rev_it;
    }

   // std::swap(wayfire_view_transform::global_scale, scale);
   // std::swap(wayfire_view_transform::global_translate, translate);

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    pixman_region32_fini(&ws_damage);
}

void render_manager::workspace_stream_stop(wf_workspace_stream *stream)
{
    stream->running = false;
}

/* End render_manager */

/* Start SignalManager */

void wayfire_output::connect_signal(std::string name, signal_callback_t* callback)
{
    signals[name].push_back(callback);
}

void wayfire_output::disconnect_signal(std::string name, signal_callback_t* callback)
{
    auto it = std::remove_if(signals[name].begin(), signals[name].end(),
    [=] (const signal_callback_t *call) {
        return call == callback;
    });

    signals[name].erase(it, signals[name].end());
}

void wayfire_output::emit_signal(std::string name, signal_data *data)
{
    std::vector<signal_callback_t> callbacks;
    for (auto x : signals[name])
        callbacks.push_back(*x);

    for (auto x : callbacks)
        x(data);
}

/* End SignalManager */
/* Start output */
wayfire_output* wl_output_to_wayfire_output(uint32_t output)
{
    if (output == (uint32_t) -1)
        return core->get_active_output();

    wayfire_output *result = nullptr;
    core->for_each_output([output, &result] (wayfire_output *wo) {
        if (wo->id == (int)output)
            result = wo;
    });

    return result;
}


void shell_add_background(struct wl_client *client, struct wl_resource *resource,
        uint32_t output, struct wl_resource *surface, int32_t x, int32_t y)
{
    auto view = wl_surface_to_wayfire_view(surface);
    auto wo = wl_output_to_wayfire_output(output);
    if (!wo) wo = view ? view->get_output() : nullptr;

    if (!wo || !view)
    {
        log_error("shell_add_background called with invalid surface or output %p %p", wo, view.get());
        return;
    }

    log_debug("wf_shell: add_background to %s", wo->handle->name);

    view->is_special = 1;
    view->get_output()->detach_view(view);
    view->set_output(wo);
    view->move(x, y);

    wo->workspace->add_view_to_layer(view, WF_LAYER_BACKGROUND);
}

void shell_add_panel(struct wl_client *client, struct wl_resource *resource,
        uint32_t output, struct wl_resource *surface)
{
    auto view = wl_surface_to_wayfire_view(surface);
    auto wo = wl_output_to_wayfire_output(output);
    if (!wo) wo = view ? view->get_output() : nullptr;


    if (!wo || !view) {
        log_error("shell_add_panel called with invalid surface or output");
        return;
    }

    log_debug("wf_shell: add_panel");

    view->is_special = 1;
    view->get_output()->detach_view(view);
    view->set_output(wo);

    wo->workspace->add_view_to_layer(view, WF_LAYER_TOP);
}

void shell_configure_panel(struct wl_client *client, struct wl_resource *resource,
        uint32_t output, struct wl_resource *surface, int32_t x, int32_t y)
{
    auto view = wl_surface_to_wayfire_view(surface);
    auto wo = wl_output_to_wayfire_output(output);
    if (!wo) wo = view ? view->get_output() : nullptr;

    if (!wo || !view) {
        log_error("shell_configure_panel called with invalid surface or output");
        return;
    }

    view->move(x, y);
}

void shell_focus_panel(struct wl_client *client, struct wl_resource *resource,
        uint32_t output, struct wl_resource *surface)
{
    auto view = wl_surface_to_wayfire_view(surface);
    auto wo = wl_output_to_wayfire_output(output);
    if (!wo) wo = view ? view->get_output() : nullptr;

    if (!wo || !view) {
        log_error("shell_focus_panel called with invalid surface or output");
        return;
    }

    if (wo == view->get_output())
        wo->focus_view(view);
}

void shell_return_focus(struct wl_client *client, struct wl_resource *resource,
        uint32_t output)
{
    auto wo = wl_output_to_wayfire_output(output);

    if (!wo) {
        log_error("shell_return_focus called with invalid surface or output");
        return;
    }

    if (wo == core->get_active_output())
        wo->focus_view(wo->get_top_view());
}

void shell_reserve(struct wl_client *client, struct wl_resource *resource,
        uint32_t output, uint32_t side, uint32_t width, uint32_t height)
{
    auto wo = wl_output_to_wayfire_output(output);

    if (!wo) {
        log_error("shell_reserve called with invalid output");
        return;
    }

    log_debug("wf_shell: reserve width:%d height: %d", width, height);
    wo->workspace->reserve_workarea((wayfire_shell_panel_position)side, width, height);
}

void shell_set_color_gamma(wl_client *client, wl_resource *res,
        uint32_t output, wl_array *r, wl_array *g, wl_array *b)
{
    /*
    auto wo = wl_output_to_wayfire_output(output);
    if (!wo || !wo->handle->set_gamma) {
        errio << "shell_set_gamma called with invalid/unsupported output" << std::endl;
        return;
    }

    size_t size = wo->handle->gamma_size * sizeof(uint16_t);
    if (r->size != size || b->size != size || g->size != size) {
        errio << "gamma size is not equal to output's gamma size " << r->size << " " << size << std::endl;
        return;
    }

    size /= sizeof(uint16_t);
#ifndef ushort
#define ushort unsigned short
    wo->handle->set_gamma(wo->handle, size, (ushort*)r->data, (ushort*)g->data, (ushort*)b->data);
#undef ushort
#endif */
}

void shell_output_fade_in_start(wl_client *client, wl_resource *res, uint32_t output)
{
    auto wo = wl_output_to_wayfire_output(output);
    if (!wo)
    {
        log_error("output_fade_in_start called for wrong output!");
        return;
    }

    wo->emit_signal("output-fade-in-request", nullptr);
}

const struct wayfire_shell_interface shell_interface_impl {
    .add_background = shell_add_background,
    .add_panel = shell_add_panel,
    .configure_panel = shell_configure_panel,
    .focus_panel = shell_focus_panel,
    .return_focus = shell_return_focus,
    .reserve = shell_reserve,
    .set_color_gamma = shell_set_color_gamma,
    .output_fade_in_start = shell_output_fade_in_start
};

wayfire_output::wayfire_output(wlr_output *handle, wayfire_config *c)
{
    this->handle = handle;

    if (wl_list_length(&handle->modes) > 0)
    {
        struct wlr_output_mode *mode =                                                                                                      
            wl_container_of((&handle->modes)->prev, mode, link);
        wlr_output_set_mode(handle, mode);
    }

    render = new render_manager(this);

    auto section = c->get_section(handle->name);
    wlr_output_set_scale(handle, section->get_double("scale", 1));

    wlr_output_set_transform(handle, WL_OUTPUT_TRANSFORM_NORMAL);
    wlr_output_layout_add_auto(core->output_layout, handle);

    core->set_default_cursor();
    plugin = new plugin_manager(this, c);

    unmap_view_cb = [=] (signal_data *data)
    {
        if (get_signaled_view(data) == active_view)
        {
            wayfire_view next_focus = nullptr;
            auto views = workspace->get_views_on_workspace(workspace->get_current_workspace(),
                                                           WF_LAYER_WORKSPACE);

            for (auto v : views)
            {
                if (v != active_view && v->is_mapped())
                {
                    next_focus = v;
                    break;
                }
            }

            set_active_view(next_focus);
        }
    };

    connect_signal("unmap-view", &unmap_view_cb);
}

workspace_manager::~workspace_manager()
{ }

wayfire_output::~wayfire_output()
{
    core->input->free_output_bindings(this);

    delete workspace;
    delete plugin;
    delete render;

    wl_list_remove(&destroy_listener.link);
}

wf_geometry wayfire_output::get_relative_geometry()
{
    wf_geometry g;
    g.x = g.y = 0;
    wlr_output_effective_resolution(handle, &g.width, &g.height);

    return g;
}

wf_geometry wayfire_output::get_full_geometry()
{
    wf_geometry g;
    g.x = handle->lx; g.y = handle->ly;
    wlr_output_effective_resolution(handle, &g.width, &g.height);

    return g;
}

void wayfire_output::set_transform(wl_output_transform new_tr)
{
    /*
    int old_w = handle->width;
    int old_h = handle->height;
    weston_output_set_transform(handle, new_tr);

    render->ctx->width = handle->width;
    render->ctx->height = handle->height;

    for (auto resource : core->shell_clients)
        wayfire_shell_send_output_resized(resource, handle->id,
                                          handle->width, handle->height);
    emit_signal("output-resized", nullptr);

    //ensure_pointer();

    workspace->for_each_view([=] (wayfire_view view) {
        if (view->fullscreen || view->maximized) {
            auto g = get_full_geometry();
            if (view->maximized)
                g = workspace->get_workarea();

            int vx = view->geometry.x / old_w;
            int vy = view->geometry.y / old_h;

            g.x += vx * handle->width;
            g.y += vy * handle->height;

            view->set_geometry(g);
        } else {
            float px = 1. * view->geometry.x / old_w;
            float py = 1. * view->geometry.y / old_h;
            float pw = 1. * view->geometry.width / old_w;
            float ph = 1. * view->geometry.height / old_h;

            view->set_geometry(px * handle->width, py * handle->height,
			    pw * handle->width, ph * handle->height);
        }
    }); */
}

wl_output_transform wayfire_output::get_transform()
{
    return (wl_output_transform)handle->transform;
}

std::tuple<int, int> wayfire_output::get_screen_size()
{
    int w, h;
    wlr_output_effective_resolution(handle, &w, &h);
    return std::make_tuple(w, h);
}

void wayfire_output::ensure_pointer()
{
    /*
    auto ptr = weston_seat_get_pointer(core->get_current_seat());
    if (!ptr) return;

    int px = wl_fixed_to_int(ptr->x), py = wl_fixed_to_int(ptr->y);

    auto g = get_full_geometry();
    if (!point_inside({px, py}, g)) {
        wl_fixed_t cx = wl_fixed_from_int(g.x + g.width / 2);
        wl_fixed_t cy = wl_fixed_from_int(g.y + g.height / 2);

        weston_pointer_motion_event ev;
        ev.mask |= WESTON_POINTER_MOTION_ABS;
        ev.x = wl_fixed_to_double(cx);
        ev.y = wl_fixed_to_double(cy);

        weston_pointer_move(ptr, &ev);
    } */
}

std::tuple<int, int> wayfire_output::get_cursor_position()
{
    GetTuple(x, y, core->get_cursor_position());
    auto og = get_full_geometry();

    return std::make_tuple(x - og.x, y - og.y);
}

void wayfire_output::activate()
{
}

void wayfire_output::deactivate()
{
    // TODO: what do we do?
}

void wayfire_output::attach_view(wayfire_view v)
{
    v->set_output(this);
    workspace->add_view_to_layer(v, WF_LAYER_WORKSPACE);

    _view_signal data;
    data.view = v;
    emit_signal("attach-view", &data);
}

void wayfire_output::detach_view(wayfire_view v)
{
    _view_signal data;
    data.view = v;
    emit_signal("detach-view", &data);

    workspace->add_view_to_layer(v, 0);

    wayfire_view next = nullptr;
    auto views = workspace->get_views_on_workspace(workspace->get_current_workspace(),
                                                   WF_WM_LAYERS);
    for (auto wview : views)
    {
        if (wview->is_mapped())
        {
            next = wview;
            break;
        }
    }

    if (next == nullptr)
    {
        active_view = nullptr;
    }
    else
    {
        focus_view(next);
    }
}

void wayfire_output::bring_to_front(wayfire_view v) {
    assert(v);

    workspace->add_view_to_layer(v, -1);
    v->damage();
}

static void set_keyboard_focus(wlr_seat *seat, wlr_surface *surface)
{
    auto kbd = wlr_seat_get_keyboard(seat);
    if (kbd != NULL) {
        wlr_seat_keyboard_notify_enter(seat, surface,
                                       kbd->keycodes, kbd->num_keycodes,
                                       &kbd->modifiers);                                                                                                            
    } else
    {
        wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
    }
}

void wayfire_output::set_active_view(wayfire_view v, wlr_seat *seat)
{
    if (v == active_view)
        return;

    if (v && !v->is_mapped())
        return set_active_view(nullptr, seat);

    if (seat == nullptr)
        seat = core->get_current_seat();

    if (active_view && active_view->is_mapped())
        active_view->activate(false);

    active_view = v;
    if (active_view)
    {
        set_keyboard_focus(seat, active_view->get_keyboard_focus_surface());
        active_view->activate(true);
    } else
    {
        set_keyboard_focus(seat, NULL);
    }
}


void wayfire_output::focus_view(wayfire_view v, wlr_seat *seat)
{
    if (v && v->is_mapped() && v->get_keyboard_focus_surface())
    {
        set_active_view(v, seat);
        bring_to_front(v);

        focus_view_signal data;
        data.view = v;
        emit_signal("focus-view", &data);
    }
    else
    {
        set_active_view(nullptr, seat);
        if (v)
            bring_to_front(v);
    }
}

wayfire_view wayfire_output::get_top_view()
{
    wayfire_view view = nullptr;
    workspace->for_each_view([&view] (wayfire_view v) {
        if (!view)
            view = v;
    }, WF_LAYER_WORKSPACE);

    return view;
}

wayfire_view wayfire_output::get_view_at_point(int x, int y)
{
    wayfire_view chosen = nullptr;

    workspace->for_each_view([x, y, &chosen] (wayfire_view v) {
        if (v->is_visible() && point_inside({x, y}, v->get_wm_geometry())) {
            if (chosen == nullptr)
                chosen = v;
        }
    }, WF_WM_LAYERS);

    return chosen;
}

bool wayfire_output::activate_plugin(wayfire_grab_interface owner, bool lower_fs)
{
    if (!owner)
        return false;

    if (core->get_active_output() != this)
        return false;

    if (active_plugins.find(owner) != active_plugins.end())
    {
        log_debug("output %s: activate plugin %s again", handle->name, owner->name.c_str());
        active_plugins.insert(owner);
        return true;
    }

    for(auto act_owner : active_plugins)
    {
        bool compatible = (act_owner->abilities_mask & owner->abilities_mask) == 0;
        if (!compatible)
            return false;
    }

    /* _activation_request is a special signal,
     * used to specify when a plugin is activated. It is used only internally, plugins
     * shouldn't listen for it */
    if (lower_fs && active_plugins.empty())
        emit_signal("_activation_request", (signal_data*)1);

    active_plugins.insert(owner);
    log_debug("output %s: activate plugin %s", handle->name, owner->name.c_str());
    return true;
}

bool wayfire_output::deactivate_plugin(wayfire_grab_interface owner)
{
    auto it = active_plugins.find(owner);
    if (it == active_plugins.end())
        return true;

    active_plugins.erase(it);
    log_debug("output %s: deactivate plugin %s", handle->name, owner->name.c_str());

    if (active_plugins.count(owner) == 0)
    {
        owner->ungrab();
        active_plugins.erase(owner);

        if (active_plugins.empty())
            emit_signal("_activation_request", nullptr);

        return true;
    }


    return false;
}

bool wayfire_output::is_plugin_active(owner_t name)
{
    for (auto act : active_plugins)
        if (act && act->name == name)
            return true;

    return false;
}

wayfire_grab_interface wayfire_output::get_input_grab_interface()
{
    for (auto p : active_plugins)
        if (p && p->is_grabbed())
            return p;

    return nullptr;
}

/* simple wrappers for core->input, as it isn't exposed to plugins */

int wayfire_output::add_key(uint32_t mod, uint32_t key, key_callback* callback)
{
    return core->input->add_key(mod, key, callback, this);
}

int wayfire_output::add_button(uint32_t mod, uint32_t button, button_callback* callback)
{
    return core->input->add_button(mod, button, callback, this);
}

int wayfire_output::add_touch(uint32_t mod, touch_callback* callback)
{
    return core->input->add_touch(mod, callback, this);
}

void wayfire_output::rem_touch(int32_t id)
{
    core->input->rem_touch(id);
}

int wayfire_output::add_gesture(const wayfire_touch_gesture& gesture,
                                touch_gesture_callback* callback)
{
    return core->input->add_gesture(gesture, callback, this);
}

void wayfire_output::rem_gesture(int id)
{
    core->input->rem_gesture(id);
}
