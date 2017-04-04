#include "opengl.hpp"
#include "output.hpp"
#include "signal_definitions.hpp"
#include <linux/input.h>

#include "wm.hpp"
#include "img.hpp"

#include <sstream>
#include <memory>
#include <dlfcn.h>
#include <algorithm>

#include <EGL/egl.h>
#include <EGL/eglext.h>

/* Start plugin manager */
plugin_manager::plugin_manager(wayfire_output *o, wayfire_config *config)
{
    init_default_plugins();
    load_dynamic_plugins();

    for (auto p : plugins) {
        p->grab_interface = new wayfire_grab_interface_t(o);
        p->output = o;

        p->init(config);
    }
}

plugin_manager::~plugin_manager()
{
    for (auto p : plugins) {
        p->fini();
        delete p->grab_interface;

        if (p->dynamic)
            dlclose(p->handle);
        p.reset();
    }
}

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

wayfire_plugin plugin_manager::load_plugin_from_file(std::string path, void **h)
{
    void *handle = dlopen(path.c_str(), RTLD_NOW);
    if(handle == NULL) {
        errio << "Can't load plugin " << path << std::endl;
        errio << "\t" << dlerror() << std::endl;
        return nullptr;
    }

    auto initptr = dlsym(handle, "newInstance");
    if(initptr == NULL) {
        errio << "Missing function newInstance in file " << path << std::endl;
        errio << dlerror();
        return nullptr;
    }
    get_plugin_instance_t init = union_cast<void*, get_plugin_instance_t> (initptr);
    *h = handle;
    return wayfire_plugin(init());
}

void plugin_manager::load_dynamic_plugins()
{
    std::stringstream stream(core->plugins);
    auto path = core->plugin_path + "/wayfire/";

    std::string plugin;
    while(stream >> plugin) {
        if(plugin != "") {
            void *handle;
            auto ptr = load_plugin_from_file(path + "/lib" + plugin + ".so", &handle);
            if(ptr) {
                ptr->handle  = handle;
                ptr->dynamic = true;
                plugins.push_back(ptr);
            }
        }
    }
}

template<class T>
wayfire_plugin plugin_manager::create_plugin()
{
    return std::static_pointer_cast<wayfire_plugin_t>(std::make_shared<T>());
}

void plugin_manager::init_default_plugins()
{
    // TODO: rewrite default plugins */
    plugins.push_back(create_plugin<wayfire_focus>());
    /*
    plugins.push_back(create_plugin<Exit>());
    plugins.push_back(create_plugin<Close>());
    plugins.push_back(create_plugin<Refresh>());
    */
}

/* End plugin_manager */

/* Start input_manager */

/* pointer grab callbacks */
void pointer_grab_focus(weston_pointer_grab*) { }
void pointer_grab_axis(weston_pointer_grab *grab, uint32_t time, weston_pointer_axis_event *ev)
{
    core->get_active_output()->input->propagate_pointer_grab_axis(grab->pointer, ev);
}
void pointer_grab_axis_source(weston_pointer_grab*, uint32_t) {}
void pointer_grab_frame(weston_pointer_grab*) {}
void pointer_grab_motion(weston_pointer_grab *grab, uint32_t time, weston_pointer_motion_event *ev)
{
    weston_pointer_move(grab->pointer, ev);
    core->get_active_output()->input->propagate_pointer_grab_motion(grab->pointer, ev);
}
void pointer_grab_button(weston_pointer_grab *grab, uint32_t, uint32_t b, uint32_t s)
{
    core->get_active_output()->input->propagate_pointer_grab_button(grab->pointer, b, s);
}
void pointer_grab_cancel(weston_pointer_grab *grab)
{
    core->get_active_output()->input->end_grabs();
}

namespace
{
const weston_pointer_grab_interface pointer_grab_interface = {
    pointer_grab_focus, pointer_grab_motion,      pointer_grab_button,
    pointer_grab_axis,  pointer_grab_axis_source, pointer_grab_frame,
    pointer_grab_cancel
};
}

/* keyboard grab callbacks */
void keyboard_grab_key(weston_keyboard_grab *grab, uint32_t time, uint32_t key,
                       uint32_t state)
{
    core->get_active_output()->input->propagate_keyboard_grab_key(grab->keyboard, key, state);
}
void keyboard_grab_mod(weston_keyboard_grab *grab, uint32_t time,
                       uint32_t depressed, uint32_t locked, uint32_t latched,
                       uint32_t group)
{
    core->get_active_output()->input->propagate_keyboard_grab_mod(grab->keyboard, depressed, locked, latched, group);
}
void keyboard_grab_cancel(weston_keyboard_grab *)
{
    core->get_active_output()->input->end_grabs();
}
namespace
{
const weston_keyboard_grab_interface keyboard_grab_interface = {
    keyboard_grab_key, keyboard_grab_mod, keyboard_grab_cancel
};
}

input_manager::input_manager()
{
    pgrab.interface = &pointer_grab_interface;
    kgrab.interface = &keyboard_grab_interface;
}

void input_manager::grab_input(wayfire_grab_interface iface)
{
    if (!iface->grabbed)
        return;

    active_grabs.insert(iface);
    if (1 == active_grabs.size()) {
        weston_pointer_start_grab(weston_seat_get_pointer(core->get_current_seat()),
                                  &pgrab);
        weston_keyboard_start_grab(weston_seat_get_keyboard(core->get_current_seat()),
                                   &kgrab);
    }
}

void input_manager::ungrab_input(wayfire_grab_interface iface)
{
    active_grabs.erase(iface);
    if (active_grabs.empty()) {
        weston_pointer_end_grab(weston_seat_get_pointer(core->get_current_seat()));
        weston_keyboard_end_grab(weston_seat_get_keyboard(core->get_current_seat()));
    }
}

void input_manager::propagate_pointer_grab_axis(weston_pointer *ptr,
        weston_pointer_axis_event *ev)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.pointer.axis)
            grab.push_back(x);
    }

    for (auto x : grab) x->callbacks.pointer.axis(ptr, ev);
}

void input_manager::propagate_pointer_grab_motion(
    weston_pointer *ptr, weston_pointer_motion_event *ev)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.pointer.motion) grab.push_back(x);
    }

    for (auto x : grab) x->callbacks.pointer.motion(ptr, ev);
}

void input_manager::propagate_pointer_grab_button(weston_pointer *ptr,
        uint32_t button,
        uint32_t state)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.pointer.button)
            grab.push_back(x);
    }

    for (auto x : grab)
        x->callbacks.pointer.button(ptr, button, state);
}

void input_manager::propagate_keyboard_grab_key(weston_keyboard *kbd,
        uint32_t key, uint32_t state)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.keyboard.key) {
            grab.push_back(x);
        }
    }

    for (auto x : grab)
        x->callbacks.keyboard.key(kbd, key, state);
}

void input_manager::propagate_keyboard_grab_mod(weston_keyboard *kbd,
        uint32_t depressed, uint32_t locked, uint32_t latched, uint32_t group)
{
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.keyboard.mod)
            grab.push_back(x);
    }

    for (auto x : grab)
        x->callbacks.keyboard.mod(kbd, depressed, locked, latched, group);
}

void input_manager::end_grabs()
{
    std::vector<wayfire_grab_interface> v;

    for (auto x : active_grabs)
        v.push_back(x);

    for (auto x : v)
        ungrab_input(x);
}

bool input_manager::activate_plugin(wayfire_grab_interface owner)
{
    if (!owner)
        return false;

    if (active_plugins.find(owner) != active_plugins.end())
        return true;

    for(auto act_owner : active_plugins) {
        bool owner_in_act_owner_compat =
            act_owner->compat.find(owner->name) != act_owner->compat.end();

        bool act_owner_in_owner_compat =
            owner->compat.find(act_owner->name) != owner->compat.end();

        if(!owner_in_act_owner_compat && !act_owner->compatAll)
            return false;

        if(!act_owner_in_owner_compat && !owner->compatAll)
            return false;
    }

    active_plugins.insert(owner);
    return true;
}

bool input_manager::deactivate_plugin(wayfire_grab_interface owner)
{
    owner->ungrab();
    active_plugins.erase(owner);
    return true;
}

bool input_manager::is_plugin_active(owner_t name)
{
    for (auto act : active_plugins)
        if (act && act->name == name)
            return true;

    return false;
}

static void keybinding_handler(weston_keyboard *kbd, uint32_t time, uint32_t key, void *data)
{
    key_callback call = *((key_callback*)data);
    call(kbd, key);
}

static void buttonbinding_handler(weston_pointer *ptr, uint32_t time, uint32_t button, void *data)
{
    button_callback call = *((button_callback*)data);
    call(ptr, button);
}
weston_binding* input_manager::add_key(uint32_t mod, uint32_t key, key_callback *call)
{
    return weston_compositor_add_key_binding(core->ec, key, (weston_keyboard_modifier)mod, keybinding_handler, (void*)call);
}

weston_binding* input_manager::add_button(uint32_t mod,
        uint32_t button, button_callback *call)
{
    return weston_compositor_add_button_binding(core->ec, button, (weston_keyboard_modifier)mod,
            buttonbinding_handler, (void*)call);
}
/* End input_manager */

/* Start render_manager */

/* TODO: currently wayfire hijacks the built-in renderer, assuming that it is the gl-renderer
 * However, this isn't always true. Also, hijacking isn't the best option
 * We should render to a surface which is made standalone */
void repaint_output_callback(weston_output *o, pixman_region32_t *damage)
{
    auto output = core->get_output(o);
    if (output) {
        output->render->pre_paint();
        output->render->paint(damage);
    }
}
render_manager::render_manager(wayfire_output *o)
{
    output = o;

    /* TODO: this should be done in core, otherwise this might cause infite recusion
     * if using multiple outputs */
    weston_renderer_repaint = core->ec->renderer->repaint_output;
    core->ec->renderer->repaint_output = repaint_output_callback;

    pixman_region32_init(&old_damage);
    pixman_region32_copy(&old_damage, &output->handle->region);
}

/* TODO: do not rely on glBlitFramebuffer, provide fallback
 * to texture rendering for older systems */
void render_manager::load_background()
{
    background.tex = image_io::load_from_file(core->background, background.w, background.h);

    GL_CALL(glGenFramebuffers(1, &background.fbuff));
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, background.fbuff));

    GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   background.tex, 0));

    auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
    if (status != GL_FRAMEBUFFER_COMPLETE)
        errio << "Can't setup background framebuffer!" << std::endl;

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void render_manager::load_context()
{
    ctx = OpenGL::create_gles_context(output, core->shadersrc.c_str());
    OpenGL::bind_context(ctx);
    load_background();

    dirty_context = false;

    output->signal->emit_signal("reload-gl", nullptr);
}

void render_manager::release_context()
{
    GL_CALL(glDeleteFramebuffers(1, &background.fbuff));
    GL_CALL(glDeleteTextures(1, &background.tex));

    OpenGL::release_context(ctx);
    dirty_context = true;
}

#ifdef USE_GLES3
void render_manager::blit_background(GLuint dest, pixman_region32_t *damage)
{
    background.times_blitted++;
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dest));
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, background.fbuff));

    int nrects;
    auto rects = pixman_region32_rectangles(damage, &nrects);
    for (int i = 0; i < nrects; i++) {
        double topx = rects[i].x1 * 1.0 / output->handle->width;
        double topy = rects[i].y1 * 1.0 / output->handle->height;
        double botx = rects[i].x2 * 1.0 / output->handle->width;
        double boty = rects[i].y2 * 1.0 / output->handle->height;

        GL_CALL(glBlitFramebuffer(topx * background.w, topy * background.h,
                                  botx * background.w, boty * background.h,
                                  rects[i].x1, output->handle->height - rects[i].y1,
                                  rects[i].x2, output->handle->height - rects[i].y2, GL_COLOR_BUFFER_BIT, GL_LINEAR));
    }

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, 0));
}
#endif

void redraw_idle_cb(void *data)
{
    wayfire_output *output = (wayfire_output*) data;
    assert(output);

    weston_output_schedule_repaint(output->handle);
}

void render_manager::auto_redraw(bool redraw)
{
    if (redraw == constant_redraw) /* no change, exit */
        return;

    constant_redraw = redraw;
    auto loop = wl_display_get_event_loop(core->ec->wl_display);
    wl_event_loop_add_idle(loop, redraw_idle_cb, output);

    if (!constant_redraw) {
        background.times_blitted = 0;

        pixman_region32_fini(&old_damage);
        pixman_region32_init(&old_damage);
        pixman_region32_copy(&old_damage, &output->handle->region);
    }
}

void render_manager::reset_renderer()
{
    renderer = nullptr;
    /* TODO: move to core.cpp */
    //core->ec->renderer->repaint_output = weston_renderer_repaint;
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

void render_manager::update_damage(pixman_region32_t *cur_damage, pixman_region32_t *total)
{
    pixman_region32_init(total);
    pixman_region32_union(total, cur_damage, &old_damage);
    pixman_region32_copy(&old_damage, cur_damage);
}

struct weston_gl_renderer {
    weston_renderer base;
    int a, b;
    void *c, *d;
    EGLDisplay display;
    EGLContext context;
};

void initial_background_render_idle_cb(void *data)
{
    auto output = (wayfire_output*) data;
    weston_output_schedule_repaint(output->handle);
}

void render_manager::paint(pixman_region32_t *damage)
{
    pixman_region32_t total_damage;

    if (dirty_context) {
        load_context();
        weston_renderer_repaint(output->handle, damage);

        auto loop = wl_display_get_event_loop(core->ec->wl_display);
        wl_event_loop_add_idle(loop, redraw_idle_cb, output);
        return;
    }

    /* if we are on the second frame, we haven't fully drawn the background
     * to the second buffer(which is now back buffer), so just blit the whole */
    if (background.times_blitted == 1) {
        pixman_region32_fini(damage);
        pixman_region32_init(damage);
        pixman_region32_copy(damage, &output->handle->region);
    }

    if (renderer) {
        // This is a hack, weston renderer_state is a struct and the EGLSurface is the first field
        // In the future this might change so we need to track changes in weston
        EGLSurface surf = *(EGLSurface*)output->handle->renderer_state;
        weston_gl_renderer *gr = (weston_gl_renderer*) core->ec->renderer;
        eglMakeCurrent(gr->display, surf, surf, gr->context);

        OpenGL::bind_context(ctx);
        renderer();

        wl_signal_emit(&output->handle->frame_signal, output->handle);
        eglSwapBuffers(gr->display, surf);
    } else {
        update_damage(damage, &total_damage);
        blit_background(0, &total_damage);
        weston_renderer_repaint(output->handle, damage);
    }

    if (constant_redraw) {
        wl_event_loop_add_idle(wl_display_get_event_loop(core->ec->wl_display),
                redraw_idle_cb, output);
    }
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
    blit_background(0, &output->handle->region);
    output->for_each_view_reverse([=](wayfire_view v) {
        if (!v->destroyed && !v->is_hidden)
            v->render();
    });
}

#ifdef USE_GLES3
void render_manager::texture_from_viewport(std::tuple<int, int> vp,
        GLuint &fbuff, GLuint &tex)
{

    OpenGL::bind_context(ctx);

    if (fbuff == (uint)-1 || tex == (uint)-1)
        OpenGL::prepare_framebuffer(fbuff, tex);

    pixman_region32_t full_region;
    pixman_region32_init_rect(&full_region, 0, 0, output->handle->width, output->handle->height);
    blit_background(fbuff, &full_region);

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbuff));

    GetTuple(x, y, vp);
    GetTuple(cx, cy, output->viewport->get_current_viewport());

    int dx = (cx - x)  * output->handle->width,
        dy = (cy - y)  * output->handle->height;

    wayfire_geometry output_rect = {
        .origin = {-dx, -dy},
        .size = {output->handle->width, output->handle->height}
    };

    output->for_each_view_reverse([=] (wayfire_view v) {
        /* TODO: check if it really is visible */
        if (rect_inside(output_rect, v->geometry)) {
            v->geometry.origin.x += dx;
            v->geometry.origin.y += dy;
            v->render(0);
            v->geometry.origin.x -= dx;
            v->geometry.origin.y -= dy;
        }
    });

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}
#endif

static int effect_hook_last_id = 0;
void render_manager::add_output_effect(effect_hook_t* hook, wayfire_view v)
{
    if (v)
        v->effects.push_back(hook);
    else
        output_effects.push_back(hook);
}

void render_manager::rem_effect(const effect_hook_t *hook, wayfire_view v)
{
    decltype(output_effects)& container = output_effects;
    if (v) container = v->effects;
    auto it = std::remove_if(container.begin(), container.end(),
    [hook] (const effect_hook_t *h) {
        if (h == hook)
            return true;
        return false;
    });

    container.erase(it, container.end());
}
/* End render_manager */

/* Start viewport_manager */
viewport_manager::viewport_manager(wayfire_output *o)
{
    output = o;
    vx = vy = 0;

    vwidth = core->vwidth;
    vheight = core->vheight;
}

std::tuple<int, int> viewport_manager::get_current_viewport()
{
    return std::make_tuple(vx, vy);
}
std::tuple<int, int> viewport_manager::get_viewport_grid_size()
{
    return std::make_tuple(vwidth, vheight);
}

int clamp(int x, int min, int max)
{
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

void viewport_manager::set_viewport(std::tuple<int, int> nPos)
{
    GetTuple(nx, ny, nPos);
    if(nx >= vwidth || ny >= vheight || nx < 0 || ny < 0)
        return;

    if (nx == vx && ny == vy) {
        auto views = get_views_on_viewport({vx, vy});
        if (views.size() >= 1)
            output->focus_view(views[0], core->get_current_seat());
        return;
    }

    auto dx = (vx - nx) * output->handle->width;
    auto dy = (vy - ny) * output->handle->height;

    output->for_each_view([=] (wayfire_view v) {
        v->move(v->geometry.origin.x + dx, v->geometry.origin.y + dy);
    });


    weston_output_schedule_repaint(output->handle);

    change_viewport_signal data;
    data.old_vx = vx;
    data.old_vy = vy;

    data.new_vx = nx;
    data.new_vy = ny;

    vx = nx;
    vy = ny;
    output->signal->emit_signal("viewport-changed", &data);

    output->focus_view(nullptr, core->get_current_seat());
    /* we iterate through views on current viewport from bottom to top
     * that way we ensure that they will be focused befor all others */
    auto views = get_views_on_viewport({vx, vy});
    auto it = views.rbegin();
    while(it != views.rend()) {
        output->focus_view(*it, core->get_current_seat());
        ++it;
    }
}

std::vector<wayfire_view> viewport_manager::get_views_on_viewport(std::tuple<int, int> vp)
{
    GetTuple(tx, ty, vp);

    wayfire_geometry g;
    g.origin = {(tx - vx) * output->handle->width, (ty - vy) * (output->handle->height)};
    g.size = {output->handle->width, output->handle->height};

    std::vector<wayfire_view> ret;
    output->for_each_view([&ret, g] (wayfire_view view) {
        if (rect_inside(g, view->geometry)) {
            ret.push_back(view);
        }
    });

    return ret;
}
/* End viewport_manager */

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

wayfire_output::wayfire_output(weston_output *handle, wayfire_config *c)
{
    this->handle = handle;

    /* TODO: init output */
    input = new input_manager();
    render = new render_manager(this);
    viewport = new viewport_manager(this);
    signal = new signal_manager();

    plugin = new plugin_manager(this, c);

    weston_layer_init(&normal_layer, core->ec);
    weston_layer_set_position(&normal_layer, WESTON_LAYER_POSITION_NORMAL);

    weston_output_damage(handle);
    weston_output_schedule_repaint(handle);
}

wayfire_output::~wayfire_output()
{
    delete plugin;
    delete signal;
    delete viewport;
    delete render;
    delete input;
}

void wayfire_output::activate()
{
}

void wayfire_output::deactivate()
{
    // TODO: what do we do?
    //render->dirty_context = true;
}

void wayfire_output::attach_view(wayfire_view v)
{
    v->output = this;

    weston_layer_entry_insert(&normal_layer.view_list, &v->handle->layer_link);
    //GetTuple(vx, vy, wayfire_viewport->get_current_wayfire_viewport());
    //v->vx = vx;
    //v->vy = vy;

    //v->set_mask(wayfire_viewport->get_mask_for_wayfire_view(v));

//    weston_fade_run(v->handle, 0.5, 1, 300, NULL, NULL);
}

void wayfire_output::detach_view(wayfire_view v)
{
    wayfire_view next = nullptr;

    auto views = viewport->get_views_on_viewport(viewport->get_current_viewport());
    for (auto wview : views) {
        if (wview->handle != v->handle) {
            next = wview;
            break;
        }
    }

    if (active_view == v && false) {
        if (next == nullptr) {
            active_view = nullptr;
        } else {
            focus_view(next, core->get_current_seat());
        }
    }

    auto sig_data = destroy_view_signal{v};
    signal->emit_signal("destroy-view", &sig_data);
}

void wayfire_output::bring_to_front(wayfire_view v) {
    assert(v);

    weston_view_geometry_dirty(v->handle);
    weston_layer_entry_remove(&v->handle->layer_link);
    weston_layer_entry_insert(&normal_layer.view_list, &v->handle->layer_link);
    weston_view_geometry_dirty(v->handle);
    weston_surface_damage(v->surface);
    weston_desktop_surface_propagate_layer(v->desktop_surface);
}

void wayfire_output::focus_view(wayfire_view v, weston_seat *seat)
{
    if (v == active_view)
        return;

    if (active_view && !active_view->destroyed)
        weston_desktop_surface_set_activated(active_view->desktop_surface, false);

    active_view = v;
    if (active_view) {
        weston_view_activate(v->handle, seat,
                             WESTON_ACTIVATE_FLAG_CLICKED | WESTON_ACTIVATE_FLAG_CONFIGURE);
        weston_desktop_surface_set_activated(v->desktop_surface, true);
        bring_to_front(v);
    } else {
        weston_keyboard_set_focus(weston_seat_get_keyboard(seat), NULL);
    }
}

void wayfire_output::for_each_view(view_callback_proc_t call)
{
    weston_view *view;
    wayfire_view v;

    wl_list_for_each(view, &handle->compositor->view_list, link) {
        if (view->output == handle && (v = core->find_view(view))) {
            call(v);
        }
    }
}

void wayfire_output::for_each_view_reverse(view_callback_proc_t call)
{
    weston_view *view;
    wayfire_view v;

    wl_list_for_each_reverse(view, &normal_layer.view_list.link, layer_link.link) {
        if (view->output == handle && (v = core->find_view(view))) {
            call(v);
        }
    }
}

wayfire_view wayfire_output::get_view_at_point(int x, int y)
{
    wayfire_view chosen = nullptr;

    for_each_view([x, y, &chosen] (wayfire_view v) {
        if (v->is_visible() && point_inside({x, y}, v->geometry)) {
            if (chosen == nullptr)
                chosen = v;
        }
    });

    return chosen;
}
