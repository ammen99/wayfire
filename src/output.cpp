#include "opengl.hpp"
#include "output.hpp"
#include "signal_definitions.hpp"
#include "input-manager.hpp"

#include <linux/input.h>

#include "wm.hpp"

#include <sstream>
#include <memory>
#include <dlfcn.h>
#include <algorithm>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <libweston-desktop.h>
#include <gl-renderer-api.h>

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
            errio << "Can't load plugin " << path << std::endl;
            errio << "\t" << dlerror() << std::endl;
            return nullptr;
        }

        debug << "Loading plugin " << path << std::endl;

        auto initptr = dlsym(handle, "newInstance");
        if(initptr == NULL)
        {
            errio << "Missing function newInstance in file " << path << std::endl;
            errio << dlerror();
            return nullptr;
        }

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
    }
};

/* End plugin_manager */

const weston_gl_renderer_api *render_manager::renderer_api = nullptr;

/* Start render_manager */
render_manager::render_manager(wayfire_output *o)
{
    output = o;

    if (!renderer_api)
    {
	    renderer_api = (const weston_gl_renderer_api*)
		    weston_plugin_api_get(core->ec, WESTON_GL_RENDERER_API_NAME,
				    sizeof(weston_gl_renderer_api));
    }

    if (core->backend == WESTON_BACKEND_WAYLAND) {
        output->output_dx = output->output_dy = 38;
    } else {
        output->output_dx = output->output_dy = 0;
    }

    pixman_region32_init(&frame_damage);
    pixman_region32_init(&prev_damage);
}

void render_manager::load_context()
{
    ctx = OpenGL::create_gles_context(output, core->shadersrc.c_str());
    OpenGL::bind_context(ctx);

    dirty_context = false;

    output->signal->emit_signal("reload-gl", nullptr);
}

void render_manager::release_context()
{
    OpenGL::release_context(ctx);
    dirty_context = true;
}

void redraw_idle_cb(void *data)
{
    wayfire_output *output = (wayfire_output*) data;
    assert(output);

    weston_output_schedule_repaint(output->handle);
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

    auto loop = wl_display_get_event_loop(core->ec->wl_display);
    wl_event_loop_add_idle(loop, redraw_idle_cb, output);
}

void render_manager::reset_renderer()
{
    renderer = nullptr;

    weston_output_damage(output->handle);
    weston_output_schedule_repaint(output->handle);
}

void render_manager::set_renderer(render_hook_t rh)
{
    if (!rh) {
        renderer = std::bind(std::mem_fn(&render_manager::transformation_renderer), this);
    } else {
        renderer = rh;
    }
}

void render_manager::paint(pixman_region32_t *damage)
{
    if (dirty_context)
        load_context();

    if (streams_running)
    {
        pixman_region32_union(&frame_damage,
                &core->ec->primary_plane.damage, &prev_damage);
        pixman_region32_copy(&prev_damage, &core->ec->primary_plane.damage);
    }

    if (renderer)
    {
        EGLSurface surf = renderer_api->output_get_egl_surface(output->handle);
        EGLContext context = renderer_api->compositor_get_egl_context(core->ec);
        EGLDisplay display = renderer_api->compositor_get_egl_display(core->ec);

        eglMakeCurrent(display, surf, surf, context);

        GL_CALL(glViewport(0, 0, output->handle->width, output->handle->height));

        OpenGL::bind_context(ctx);
        renderer();

        wl_signal_emit(&output->handle->frame_signal, output->handle);
        eglSwapBuffers(display, surf);
    } else {
        core->weston_repaint(output->handle, damage);
    }

    if (constant_redraw)
    {
        wl_event_loop_add_idle(wl_display_get_event_loop(core->ec->wl_display),
                redraw_idle_cb, output);
    }
    core->hijack_renderer();
}

void render_manager::pre_paint()
{
    std::vector<effect_hook_t*> active_effects;
    for (auto effect : output_effects) {
        active_effects.push_back(effect);
    }

    for (auto& effect : active_effects)
        (*effect)();
}

void render_manager::transformation_renderer()
{
    auto views = output->workspace->get_renderable_views_on_workspace(
            output->workspace->get_current_workspace());

    GL_CALL(glClear(GL_COLOR_BUFFER_BIT));

    auto it = views.rbegin();
    while (it != views.rend())
    {
        auto view = *it;
        if (!view->is_hidden) /* use is_visible() when implemented */
        {
            view->render();
        }

        ++it;
    }
}

void render_manager::add_output_effect(effect_hook_t* hook, wayfire_view v)
{
    if (v)
        v->effects.push_back(hook);
    else
        output_effects.push_back(hook);
}

void render_manager::rem_effect(const effect_hook_t *hook, wayfire_view v)
{
    if (v)
    {
        auto it = std::remove_if(v->effects.begin(), v->effects.end(),
                                 [hook] (const effect_hook_t *h)
                                 {
                                     if (h == hook)
                                         return true;
                                     return false;
                                 });

        v->effects.erase(it, v->effects.end());
    } else
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
}

void render_manager::texture_from_workspace(std::tuple<int, int> vp,
        GLuint &fbuff, GLuint &tex)
{
    OpenGL::bind_context(output->render->ctx);

    if (fbuff == (uint)-1 || tex == (uint)-1)
        OpenGL::prepare_framebuffer(fbuff, tex);

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbuff));
    GL_CALL(glViewport(0, 0, output->handle->width, output->handle->height));

    auto g = output->get_full_geometry();

    GetTuple(x, y, vp);
    GetTuple(cx, cy, output->workspace->get_current_workspace());

    int dx = -g.x + (cx - x)  * output->handle->width,
        dy = -g.y + (cy - y)  * output->handle->height;

    auto views = output->workspace->get_renderable_views_on_workspace(vp);
    auto it = views.rbegin();

    while (it != views.rend())
    {
        auto v = *it;
        if (v->is_visible())
        {
            if (!v->is_special)
            {
                v->geometry.x += dx;
                v->geometry.y += dy;

                v->render();

                v->geometry.x -= dx;
                v->geometry.y -= dy;
            } else
            {
                v->render();
            }
        }
        ++it;
    };

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void render_manager::workspace_stream_start(wf_workspace_stream *stream)
{
    streams_running++;
    stream->running = true;
    stream->scale_x = stream->scale_y = 1;

    OpenGL::bind_context(output->render->ctx);

    if (stream->fbuff == (uint)-1 || stream->tex == (uint)-1)
        OpenGL::prepare_framebuffer(stream->fbuff, stream->tex);

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, stream->fbuff));
    GL_CALL(glViewport(0, 0, output->handle->width * stream->scale_x,
                output->handle->height * stream->scale_y));

    GetTuple(x, y, stream->ws);
    GetTuple(cx, cy, output->workspace->get_current_workspace());

    /* TODO: this assumes we use viewports arranged in a grid
     * It would be much better to actually ask the workspace_manager
     * for view's position on the given workspace*/
    int dx = (cx - x)  * output->handle->width,
        dy = (cy - y)  * output->handle->height;

    auto views = output->workspace->get_renderable_views_on_workspace(stream->ws);
    auto it = views.rbegin();

    while (it != views.rend())
    {
        auto v = *it;
        if (v->is_visible())
        {
            if (!v->is_special)
            {
                v->geometry.x += dx;
                v->geometry.y += dy;
                v->render();
                v->geometry.x -= dx;
                v->geometry.y -= dy;
            } else
            {
                v->render();
            }
        }
        ++it;
    };

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void render_manager::workspace_stream_update(wf_workspace_stream *stream,
                                             float scale_x, float scale_y)
{
    OpenGL::bind_context(output->render->ctx);
    auto g = output->get_full_geometry();

    GetTuple(x, y, stream->ws);
    GetTuple(cx, cy, output->workspace->get_current_workspace());

    int dx = g.x + (x - cx) * g.width,
        dy = g.y + (y - cy) * g.height;

    pixman_region32_t ws_damage;
    pixman_region32_init_rect(&ws_damage, dx, dy, g.width, g.height);
    pixman_region32_intersect(&ws_damage, &frame_damage, &ws_damage);

    /* we don't have to update anything */
    if (!pixman_region32_not_empty(&ws_damage))
    {
        pixman_region32_fini(&ws_damage);
        return;
    }

    if (scale_x != stream->scale_x || scale_y != stream->scale_y)
    {
        stream->scale_x = scale_x;
        stream->scale_y = scale_y;

        pixman_region32_union_rect(&ws_damage, &ws_damage, dx, dy,
                g.width, g.height);
    }

    auto views = output->workspace->get_renderable_views_on_workspace(stream->ws);

    struct damaged_view {
        wayfire_view view;
        pixman_region32_t *damage;
    };

    std::vector<damaged_view> update_views;

    auto it = views.begin();

    while (it != views.end() && pixman_region32_not_empty(&ws_damage))
    {
        damaged_view dv;
        dv.view = *it;

        if (dv.view->is_visible())
        {
            dv.damage = new pixman_region32_t;
            if (dv.view->is_special)
            {
                /* make background's damage be at the target viewport */
                pixman_region32_init_rect(dv.damage,
                    dv.view->geometry.x - dv.view->ds_geometry.x + (dx - g.x),
                    dv.view->geometry.y - dv.view->ds_geometry.y + (dy - g.y),
                    dv.view->surface->width, dv.view->surface->height);
            } else
            {
                pixman_region32_init_rect(dv.damage,
                        dv.view->geometry.x - dv.view->ds_geometry.x,
                        dv.view->geometry.y - dv.view->ds_geometry.y,
                        dv.view->surface->width, dv.view->surface->height);
            }

            pixman_region32_intersect(dv.damage, dv.damage, &ws_damage);
            if (pixman_region32_not_empty(dv.damage)) {
                update_views.push_back(dv);
                /* If we are processing background, then this is not correct, as its
                 * transform.opaque isn't positioned properly. But as
                 * background is the last in the list, we don' care */
                pixman_region32_subtract(&ws_damage, &ws_damage, &dv.view->handle->transform.opaque);
            } else {
                pixman_region32_fini(dv.damage);
                delete dv.damage;
            }
        }
        ++it;
    };

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, stream->fbuff));
    GL_CALL(glViewport(0, 0, g.width * scale_x, g.height * scale_y));

    glm::mat4 scale = glm::scale(glm::mat4(), glm::vec3(scale_x, scale_y, 1));
    glm::mat4 translate = glm::translate(glm::mat4(), glm::vec3(scale_x - 1, scale_y - 1, 0));
    std::swap(wayfire_view_transform::global_scale, scale);
    std::swap(wayfire_view_transform::global_translate, translate);

    auto rev_it = update_views.rbegin();
    while(rev_it != update_views.rend())
    {
        auto dv = *rev_it;

#define render_op(dx, dy) \
        dv.view->geometry.x -= dx; \
        dv.view->geometry.y -= dy; \
        dv.view->render(0, dv.damage); \
        dv.view->geometry.x += dx; \
        dv.view->geometry.y += dy;


        pixman_region32_translate(dv.damage, -(dx - g.x), -(dy - g.y));
        if (dv.view->is_special)
        {
            render_op(0, 0);
        } else
        {
            render_op(dx - g.x, dy - g.y);
        }

        pixman_region32_fini(dv.damage);
        delete dv.damage;
        ++rev_it;
    }

    std::swap(wayfire_view_transform::global_scale, scale);
    std::swap(wayfire_view_transform::global_translate, translate);

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    pixman_region32_fini(&ws_damage);
}

void render_manager::workspace_stream_stop(wf_workspace_stream *stream)
{
    streams_running--;
    stream->running = false;
}

/* End render_manager */

/* Start SignalManager */

void signal_manager::connect_signal(std::string name, signal_callback_t* callback)
{
    sig[name].push_back(callback);
}

void signal_manager::disconnect_signal(std::string name, signal_callback_t* callback)
{
    auto it = std::remove_if(sig[name].begin(), sig[name].end(),
    [=] (const signal_callback_t *call) {
        return call == callback;
    });

    sig[name].erase(it, sig[name].end());
}

void signal_manager::emit_signal(std::string name, signal_data *data)
{
    std::vector<signal_callback_t> callbacks;
    for (auto x : sig[name])
        callbacks.push_back(*x);

    for (auto x : callbacks)
        x(data);
}

/* End SignalManager */
/* Start output */
wayfire_output* wl_output_to_wayfire_output(uint32_t output)
{
    wayfire_output *result = nullptr;
    core->for_each_output([output, &result] (wayfire_output *wo) {
        if (wo->handle->id == output)
            result = wo;
    });

    return result;
}

void shell_add_background(struct wl_client *client, struct wl_resource *resource,
        uint32_t output, struct wl_resource *surface, int32_t x, int32_t y)
{
    weston_surface *wsurf = (weston_surface*) wl_resource_get_user_data(surface);
    auto wo = wl_output_to_wayfire_output(output);

    wayfire_view view = nullptr;

    if (!wo || !wsurf || !(view = core->find_view(wsurf))) {
        errio << "shell_add_background called with invalid surface or output" << std::endl;
        return;
    }

    debug << "wf_shell: add_background" << std::endl;
    wo->workspace->add_background(view, x, y);
}

void shell_add_panel(struct wl_client *client, struct wl_resource *resource,
        uint32_t output, struct wl_resource *surface)
{
    weston_surface *wsurf = (weston_surface*) wl_resource_get_user_data(surface);
    auto wo = wl_output_to_wayfire_output(output);

    wayfire_view view = nullptr;

    if (!wo || !wsurf || !(view = core->find_view(wsurf))) {
        errio << "shell_add_panel called with invalid surface or output" << std::endl;
        return;
    }

    debug << "wf_shell: add_panel" << std::endl;
    wo->workspace->add_panel(view);
}

void shell_configure_panel(struct wl_client *client, struct wl_resource *resource,
        uint32_t output, struct wl_resource *surface, int32_t x, int32_t y)
{
    weston_surface *wsurf = (weston_surface*) wl_resource_get_user_data(surface);
    auto wo = wl_output_to_wayfire_output(output);

    wayfire_view view = nullptr;

    if (!wo || !wsurf || !(view = core->find_view(wsurf))) {
        errio << "shell_configure_panel called with invalid surface or output" << std::endl;
        return;
    }

    debug << "wf_shell: configure_panel" << std::endl;
    wo->workspace->configure_panel(view, x, y);
}

void shell_reserve(struct wl_client *client, struct wl_resource *resource,
        uint32_t output, uint32_t side, uint32_t width, uint32_t height)
{
    auto wo = wl_output_to_wayfire_output(output);

    if (!wo) {
        errio << "shell_reserve called with invalid output" << std::endl;
        return;
    }

    debug << "wf_shell: reserve" << std::endl;
    wo->workspace->reserve_workarea((wayfire_shell_panel_position)side, width, height);
}

void shell_set_color_gamma(wl_client *client, wl_resource *res,
        uint32_t output, wl_array *r, wl_array *g, wl_array *b)
{
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
#endif
}

const struct wayfire_shell_interface shell_interface_impl {
    .add_background = shell_add_background,
    .add_panel = shell_add_panel,
    .configure_panel = shell_configure_panel,
    .reserve = shell_reserve,
    .set_color_gamma = shell_set_color_gamma
};

wayfire_output::wayfire_output(weston_output *handle, wayfire_config *c)
{
    this->handle = handle;

    render = new render_manager(this);
    signal = new signal_manager();

    plugin = new plugin_manager(this, c);

    weston_output_damage(handle);
    weston_output_schedule_repaint(handle);

    if (handle->set_dpms && c->get_section("core")->get_int("dpms_enabled", 1))
    	handle->set_dpms(handle, WESTON_DPMS_ON);
}

wayfire_output::~wayfire_output()
{
    delete plugin;
    delete signal;
    delete render;
}

weston_geometry wayfire_output::get_full_geometry()
{
    return {handle->x, handle->y,
            handle->width, handle->height};
}

void wayfire_output::set_transform(wl_output_transform new_tr)
{
    int old_w = handle->width;
    int old_h = handle->height;
    weston_output_set_transform(handle, new_tr);

    render->ctx->width = handle->width;
    render->ctx->height = handle->height;

    wayfire_shell_send_output_resized(core->wf_shell.resource, handle->id,
		    handle->width, handle->height);
    signal->emit_signal("output-resized", nullptr);

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

        pixman_region32_copy(&view->handle->damage_clip_region, &handle->region);
    });
}

wl_output_transform wayfire_output::get_transform()
{
    return (wl_output_transform)handle->transform;
}

std::tuple<int, int> wayfire_output::get_screen_size()
{
    return std::make_tuple(handle->width, handle->height);
}

void wayfire_output::ensure_pointer()
{
    auto ptr = weston_seat_get_pointer(core->get_current_seat());
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
    }
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
    v->output = this;
    pixman_region32_copy(&v->handle->damage_clip_region, &handle->region);

    workspace->view_bring_to_front(v);

    auto sig_data = create_view_signal{v};
    signal->emit_signal("attach-view", &sig_data);
}

void wayfire_output::detach_view(wayfire_view v)
{
    auto sig_data = destroy_view_signal{v};
    signal->emit_signal("detach-view", &sig_data);

    if (v->keep_count <= 0)
        workspace->view_removed(v);

    wayfire_view next = nullptr;

    auto views = workspace->get_views_on_workspace(workspace->get_current_workspace());
    for (auto wview : views) {
        if (wview->handle != v->handle && wview->is_mapped) {
            next = wview;
            break;
        }
    }

    if (active_view == v) {
        if (next == nullptr) {
            active_view = nullptr;
        } else {
            if (v->keep_count) {
                set_active_view(next);
            } else { /* Some plugins wants to keep the view, let it manage the position */
                focus_view(next);
            }
        }
    }
}

void wayfire_output::bring_to_front(wayfire_view v) {
    assert(v);

    weston_view_geometry_dirty(v->handle);
    weston_layer_entry_remove(&v->handle->layer_link);

    workspace->view_bring_to_front(v);

    weston_view_geometry_dirty(v->handle);
    weston_surface_damage(v->surface);
    weston_desktop_surface_propagate_layer(v->desktop_surface);
}

void wayfire_output::set_active_view(wayfire_view v)
{
    if (v == active_view)
        return;

    if (active_view && !active_view->destroyed)
        weston_desktop_surface_set_activated(active_view->desktop_surface, false);

    active_view = v;
    if (active_view) {
        weston_view_activate(v->handle, core->get_current_seat(),
                WESTON_ACTIVATE_FLAG_CLICKED | WESTON_ACTIVATE_FLAG_CONFIGURE);
        weston_desktop_surface_set_activated(v->desktop_surface, true);
    }
}

void wayfire_output::focus_view(wayfire_view v, weston_seat *seat)
{
    if (seat == nullptr)
        seat = core->get_current_seat();

    set_active_view(v);

    if (v) {
        debug << "output: " << handle->id << " focus: " << v->desktop_surface << std::endl;
        bring_to_front(v);
    } else {
        debug << "output: " << handle->id << " focus: 0" << std::endl;
        weston_keyboard_set_focus(weston_seat_get_keyboard(seat), NULL);
    }
}

wayfire_view wayfire_output::get_top_view()
{
    if (active_view)
        return active_view;

    wayfire_view view = nullptr;
    workspace->for_each_view([&view] (wayfire_view v) {
        if (!view)
            view = v;
    });

    return view;
}

wayfire_view wayfire_output::get_view_at_point(int x, int y)
{
    wayfire_view chosen = nullptr;

    workspace->for_each_view([x, y, &chosen] (wayfire_view v) {
        if (v->is_visible() && point_inside({x, y}, v->geometry)) {
            if (chosen == nullptr)
                chosen = v;
        }
    });

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
        signal->emit_signal("_activation_request", (signal_data*)1);

    active_plugins.insert(owner);
    return true;
}

bool wayfire_output::deactivate_plugin(wayfire_grab_interface owner)
{
    auto it = active_plugins.find(owner);
    if (it == active_plugins.end())
        return true;

    active_plugins.erase(it);

    if (active_plugins.count(owner) == 0)
    {
        owner->ungrab();
        active_plugins.erase(owner);

        if (active_plugins.empty())
            signal->emit_signal("_activation_request", nullptr);

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

weston_binding* wayfire_output::add_key(uint32_t mod, uint32_t key, key_callback* callback)
{
    return core->input->add_key(mod, key, callback, this);
}

weston_binding* wayfire_output::add_button(uint32_t mod, uint32_t button, button_callback* callback)
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
