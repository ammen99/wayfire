#include "opengl.hpp"
#include "output.hpp"
#include "signal_definitions.hpp"
#include <linux/input.h>

#include "wm.hpp"

#include <sstream>
#include <memory>
#include <dlfcn.h>
#include <algorithm>

/* Start plugin manager */
plugin_manager::plugin_manager(wayfire_output *o, wayfire_config *config) {
    init_default_plugins();
    load_dynamic_plugins();

    for (auto p : plugins) {
        p->grab_interface = new wayfire_grab_interface_t(o);
        p->output = o;

        p->init(config);
    }
}

plugin_manager::~plugin_manager() {
    for (auto p : plugins) {
        p->fini();
        delete p->grab_interface;

        if (p->dynamic)
            dlclose(p->handle);
        p.reset();
    }
}

namespace {
    template<class A, class B> B union_cast(A object) {
        union {
            A x;
            B y;
        } helper;
        helper.x = object;
        return helper.y;
    }
}

wayfire_plugin plugin_manager::load_plugin_from_file(std::string path, void **h) {
    void *handle = dlopen(path.c_str(), RTLD_NOW);
    if(handle == NULL){
        error << "Can't load plugin " << path << std::endl;
        error << "\t" << dlerror() << std::endl;
        return nullptr;
    }

    auto initptr = dlsym(handle, "newInstance");
    if(initptr == NULL) {
        error << "Missing function newInstance in file " << path << std::endl;
        error << dlerror();
        return nullptr;
    }
    get_plugin_instance_t init = union_cast<void*, get_plugin_instance_t> (initptr);
    *h = handle;
    return wayfire_plugin(init());
}

void plugin_manager::load_dynamic_plugins() {
    std::stringstream stream(core->plugins);
    auto path = core->plugin_path + "/wayfire/";

    std::string plugin;
    while(stream >> plugin){
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
wayfire_plugin plugin_manager::create_plugin() {
    return std::static_pointer_cast<wayfire_plugin_t>(std::make_shared<T>());
}

void plugin_manager::init_default_plugins() {
    // TODO: rewrite default plugins */
    plugins.push_back(create_plugin<wayfire_focus>());
    /*
    plugins.push_back(create_plugin<Focus>());
    plugins.push_back(create_plugin<Exit>());
    plugins.push_back(create_plugin<Close>());
    plugins.push_back(create_plugin<Refresh>());
    */
}

/* End plugin_manager */

/* Start input_manager */

/* pointer grab callbacks */
void pointer_grab_focus(weston_pointer_grab*) { }
void pointer_grab_axis(weston_pointer_grab *grab, uint32_t time, weston_pointer_axis_event *ev) {
    core->get_active_output()->input->propagate_pointer_grab_axis(grab->pointer, ev);
}
void pointer_grab_axis_source(weston_pointer_grab*, uint32_t) {}
void pointer_grab_frame(weston_pointer_grab*) {}
void pointer_grab_motion(weston_pointer_grab *grab, uint32_t time, weston_pointer_motion_event *ev) {
    debug << "pointer_grab_motion" << std::endl;
    weston_pointer_move(grab->pointer, ev);
    core->get_active_output()->input->propagate_pointer_grab_motion(grab->pointer, ev);
}
void pointer_grab_button(weston_pointer_grab *grab, uint32_t, uint32_t b, uint32_t s) {
    core->get_active_output()->input->propagate_pointer_grab_button(grab->pointer, b, s);
}
void pointer_grab_cancel(weston_pointer_grab *grab) {
    core->get_active_output()->input->end_grabs();
}

namespace {
    const weston_pointer_grab_interface pointer_grab_interface = {
        pointer_grab_focus,
        pointer_grab_motion,
        pointer_grab_button,
        pointer_grab_axis,
        pointer_grab_axis_source,
        pointer_grab_frame,
        pointer_grab_cancel
    };
}

/* keyboard grab callbacks */
void keyboard_grab_key(weston_keyboard_grab *grab, uint32_t time, uint32_t key, uint32_t state) {
    core->get_active_output()->input->propagate_keyboard_grab_key(grab->keyboard, key, state);
}
void keyboard_grab_mod(weston_keyboard_grab*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
void keyboard_grab_cancel(weston_keyboard_grab*) {
    core->get_active_output()->input->end_grabs();
}
namespace {
    const weston_keyboard_grab_interface keyboard_grab_interface = {
        keyboard_grab_key,
        keyboard_grab_mod,
        keyboard_grab_cancel
    };
}

input_manager::input_manager() {
    pgrab.interface = &pointer_grab_interface;
    kgrab.interface = &keyboard_grab_interface;
}

void input_manager::grab_input(wayfire_grab_interface iface) {
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

void input_manager::ungrab_input(wayfire_grab_interface iface) {
    active_grabs.erase(iface);
    if (active_grabs.empty()) {
        weston_pointer_end_grab(weston_seat_get_pointer(core->get_current_seat()));
        weston_keyboard_end_grab(weston_seat_get_keyboard(core->get_current_seat()));
    }
}

void input_manager::propagate_pointer_grab_axis(weston_pointer *ptr,
        weston_pointer_axis_event *ev) {
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.pointer.axis)
            grab.push_back(x);
    }

    for (auto x : grab)
        x->callbacks.pointer.axis(ptr, ev);
}

void input_manager::propagate_pointer_grab_motion(weston_pointer *ptr,
        weston_pointer_motion_event *ev) {
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.pointer.motion)
            grab.push_back(x);
    }

    for (auto x : grab)
        x->callbacks.pointer.motion(ptr, ev);
}

void input_manager::propagate_pointer_grab_button(weston_pointer *ptr,
        uint32_t button, uint32_t state) {
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.pointer.button)
            grab.push_back(x);
    }

    for (auto x : grab)
        x->callbacks.pointer.button(ptr, button, state);
}

void input_manager::propagate_keyboard_grab_key(weston_keyboard *kbd,
        uint32_t key, uint32_t state) {
    std::vector<wayfire_grab_interface> grab;
    for (auto x : active_grabs) {
        if (x->callbacks.keyboard.key)
            grab.push_back(x);
    }

    for (auto x : grab)
        x->callbacks.keyboard.key(kbd, key, state);
}

void input_manager::end_grabs() {
    std::vector<wayfire_grab_interface> v;

    for (auto x : active_grabs)
        v.push_back(x);

    for (auto x : v)
        ungrab_input(x);
}

bool input_manager::activate_plugin(wayfire_grab_interface owner) {
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

bool input_manager::deactivate_plugin(wayfire_grab_interface owner) {
    owner->ungrab();
    active_plugins.erase(owner);
    return true;
}

bool input_manager::is_plugin_active(owner_t name) {
    for (auto act : active_plugins)
        if (act && act->name == name)
            return true;

    return false;
}

static void keybinding_handler(weston_keyboard *kbd, uint32_t time, uint32_t key, void *data) {
    key_callback call = *((key_callback*)data);
    call(kbd, key);
}

static void buttonbinding_handler(weston_pointer *ptr, uint32_t time, uint32_t button, void *data) {
    button_callback call = *((button_callback*)data);
    call(ptr, button);
}
weston_binding* input_manager::add_key(weston_keyboard_modifier mod, uint32_t key, key_callback *call) {
    return weston_compositor_add_key_binding(core->ec, key, mod, keybinding_handler, (void*)call);
}

weston_binding* input_manager::add_button(weston_keyboard_modifier mod,
        uint32_t button, button_callback *call) {
    return weston_compositor_add_button_binding(core->ec, button, mod,
            buttonbinding_handler, (void*)call);
}
/* End input_manager */

/* Start render_manager */

#include "img.hpp"

/* TODO: do not rely on glBlitFramebuffer, provide fallback
 * to texture rendering for older systems */
void render_manager::load_background() {
    background.tex = image_io::load_from_file(core->background, background.w, background.h);

    GL_CALL(glGenFramebuffers(1, &background.fbuff));
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, background.fbuff));

    GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                background.tex, 0));

    auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
    if (status != GL_FRAMEBUFFER_COMPLETE)
        error << "Can't setup background framebuffer!" << std::endl;

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void render_manager::load_context() {
    ctx = OpenGL::create_gles_context(output, core->shadersrc.c_str());
    OpenGL::bind_context(ctx);
    load_background();

    dirty_context = false;

    output->signal->emit_signal("reload-gl", nullptr);
}

void render_manager::release_context() {
    /*
    GL_CALL(glDeleteFramebuffers(1, &background.fbuff));
    GL_CALL(glDeleteTextures(1, &background.tex));
    */

    OpenGL::release_context(ctx);
    dirty_context = true;
}

#ifdef USE_GLES3
void render_manager::blit_background(GLuint dest, pixman_region32_t *damage) {
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dest));
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, background.fbuff));

    int nrects;
    auto rects = pixman_region32_rectangles(damage, &nrects);
    for (int i = 0; i < nrects; i++) {
        float topx = rects[i].x1 * 1.0 / output->handle->width;
        float topy = rects[i].y1 * 1.0 / output->handle->height;
        float botx = rects[i].x2 * 1.0 / output->handle->width;
        float boty = rects[i].y2 * 1.0 / output->handle->height;

        /* invy1 and invy2 are actually (1 - topy) and (1 - boty),
         * but we calculate them separately because otherwise precision issues might arise */
        float invy1 = (output->handle->height - rects[i].y1) * 1.0 / output->handle->height;
        float invy2 = (output->handle->height - rects[i].y2) * 1.0 / output->handle->height;

        GL_CALL(glBlitFramebuffer(topx * background.w, topy * background.h,
                    botx * background.w, boty * background.h,
                    topx * output->handle->width, invy1 * output->handle->height,
                    botx * output->handle->width, invy2 * output->handle->height, GL_COLOR_BUFFER_BIT, GL_LINEAR));
    }

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0));
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, 0));
}
#endif

int repaint_output_callback(weston_output *o, pixman_region32_t *damage) {
    auto output = core->get_output(o);
    if (output) {
        output->render->paint(damage);
        output->render->post_paint();
    }
    return 0;
}

render_manager::render_manager(wayfire_output *o) {
    output = o;
    weston_renderer_repaint = output->handle->repaint;
    output->handle->repaint = repaint_output_callback;
    pixman_region32_init(&old_damage);
}

void render_manager::reset_renderer() {
    renderer = nullptr;

    output->for_each_view([] (wayfire_view v) {
        v->restore_mask();
    });

    weston_output_schedule_repaint(output->handle);
}

void render_manager::set_renderer(uint32_t vis_mask, render_hook_t rh) {
    if (!rh) {
        renderer = std::bind(std::mem_fn(&render_manager::transformation_renderer), this);
    } else {
        renderer = rh;
    }

    output->for_each_view([=] (wayfire_view v) {
        v->set_temporary_mask(0);
    });

    visibility_mask = vis_mask;
}

void render_manager::update_damage(pixman_region32_t *cur_damage, pixman_region32_t *total) {
    pixman_region32_init(total);
    pixman_region32_union(total, cur_damage, &old_damage);
    pixman_region32_copy(&old_damage, cur_damage);
}

void render_manager::paint(pixman_region32_t *damage) {
    pixman_region32_t total_damage;

    if (dirty_context) {
        load_context();
        update_damage(damage, &total_damage);
        weston_renderer_repaint(output->handle, damage);
        return;
    }

    if (renderer) {
        OpenGL::bind_context(ctx);
        renderer();
    } else {
        update_damage(damage, &total_damage);
        blit_background(0, &total_damage);
        weston_renderer_repaint(output->handle, damage);
    }
}

void render_manager::post_paint() {
    std::vector<effect_hook> active_effects;
    for (auto effect : output_effects) {
         active_effects.push_back(effect);
    }

    for (auto& effect : active_effects)
        effect.action();
}

void render_manager::transformation_renderer() {
    // TODO: paint background
    //blit_background(0);
    output->for_each_view_reverse([=](wayfire_view v) {
        if(!v->is_hidden && (v->default_mask & visibility_mask) && !v->destroyed) {
        // TODO: render with weston
        /*
            wlc_geometry g;
            wlc_wayfire_view_get_visible_geometry(v->get_id(), &g);

            auto surf = wlc_wayfire_view_get_surface(v->get_id());
            render_surface(surf, g, v->transform.compose());
            */
        }
    });
}

#ifdef USE_GLES3
void render_manager::texture_from_viewport(std::tuple<int, int> vp,
        GLuint &fbuff, GLuint &tex) {

    OpenGL::bind_context(ctx);

    if (fbuff == (uint)-1 || tex == (uint)-1)
        OpenGL::prepare_framebuffer(fbuff, tex);

    /* Rendering code, taken from wlc's get_visible_wayfire_views */

    //blit_background(fbuff);
    //GetTuple(x, y, vp);

    //uint32_t mask = output->viewport->get_mask_for_viewport(x, y);

    /* TODO: implement this function as well
    output->for_each_view_reverse([=] (wayfire_view v) {
        if (v->default_mask & mask) {
            int dx = (v->vx - x) * output->handle->width;
            int dy = (v->vy - y) * output->handle->height;

            wayfire_geometry g;
            wlc_wayfire_view_get_visible_geometry(v->get_id(), &g);
            g.origin.x += dx;
            g.origin.y += dy;

            render_surface(v->get_surface(), g, v->transform.compose());
        }
    });
    */

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}
#endif

static int effect_hook_last_id = 0;
void render_manager::add_output_effect(effect_hook& hook, wayfire_view v){
    hook.id = effect_hook_last_id++;

    if (v)
        v->effects.push_back(hook);
    else
        output_effects.push_back(hook);
}

void render_manager::rem_effect(const effect_hook& hook, wayfire_view v) {
    decltype(output_effects)& container = output_effects;
    if (v) container = v->effects;
    auto it = std::remove_if(output_effects.begin(), output_effects.end(),
            [hook] (const effect_hook& h) {
                if (h.id == hook.id)
            return true;
            return false;
            });

    container.erase(it, container.end());
}
/* End render_manager */

/* Start viewport_manager */
viewport_manager::viewport_manager(wayfire_output *o) {
    output = o;
    vx = vy = 0;

    vwidth = core->vwidth;
    vheight = core->vheight;
}

std::tuple<int, int> viewport_manager::get_current_viewport() { return std::make_tuple(vx, vy); }
std::tuple<int, int> viewport_manager::get_viewport_grid_size() { return std::make_tuple(vwidth, vheight); }

int clamp(int x, int min, int max) {
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

uint32_t viewport_manager::get_mask_for_view(wayfire_view v) {
    assert(output);
    GetTuple(width, height, output->get_screen_size());
    int sdx, sdy, edx, edy;

    if (v->geometry.origin.x < 0) {
        sdx = v->geometry.origin.x / width - 1;
    } else {
        sdx = v->geometry.origin.x / width;
    }

    if (v->geometry.origin.y < 0) {
        sdy = v->geometry.origin.y / height - 1;
    } else {
        sdy = v->geometry.origin.y / height;
    }

    int sx = v->vx + sdx;
    int sy = v->vy + sdy;

    /* We must substract a bit to prevent having windows with only 5 pixels visible */
    int bottom_right_x = v->geometry.origin.x + (int32_t)v->geometry.size.w - 5;
    int bottom_right_y = v->geometry.origin.y + (int32_t)v->geometry.size.h - 5;

    if (bottom_right_x < 0) {
        edx = bottom_right_x / width - 1;
    } else {
        edx = bottom_right_x / width;
    }

    if (bottom_right_y < 0) {
        edy = bottom_right_y / height - 1;
    } else {
        edy = bottom_right_y / height;
    }

    int ex = v->vx + edx;
    int ey = v->vy + edy;

    uint32_t mask = 0;

    for (int i = sx; i <= ex; i++)
        for (int j = sy; j <= ey; j++)
            mask |= get_mask_for_viewport(i, j);

    return mask;
}

void viewport_manager::get_viewport_for_view(wayfire_view v, int &x, int &y) {
    GetTuple(width, height, output->get_screen_size());
    int dx, dy;
    if (v->geometry.origin.x < 0) {
        dx = v->geometry.origin.x / width - 1;
    } else {
        dx = v->geometry.origin.x / width;
    }
    if (v->geometry.origin.y < 0) {
        dy = v->geometry.origin.y / height - 1;
    } else {
        dy = v->geometry.origin.y / height;
    }

    x = clamp(v->vx + dx, 0, vwidth - 1);
    y = clamp(v->vy + dy, 0, vheight - 1);
}

void viewport_manager::set_viewport(std::tuple<int, int> nPos) {
    GetTuple(nx, ny, nPos);
    if(nx >= vwidth || ny >= vheight || nx < 0 || ny < 0 || (nx == vx && ny == vy))
        return;

    debug << "switching workspace target:" << nx << " " << ny << " current:" << vx << " " << vy << std::endl;

    auto dx = (vx - nx) * output->handle->width;
    auto dy = (vy - ny) * output->handle->height;

    output->for_each_view([=] (wayfire_view v) {
        bool has_been_before = v->default_mask & get_mask_for_viewport(vx, vy);
        bool visible_now = v->default_mask & get_mask_for_viewport(nx, ny);

        if(has_been_before && visible_now) {
            v->move(v->geometry.origin.x + dx, v->geometry.origin.y + dy),
            v->vx = nx,
            v->vy = ny;
        }
    });

    //wlc_output_set_mask(wlc_get_focused_output(), get_mask_for_wayfire_viewport(nx, ny));

    /* TODO: use tuples for SignalListenerData, this is just an ugly hack */
    /* FIXME
    SignalListenerData data;
    data.push_back(&vx);
    data.push_back(&vy);
    data.push_back(&nx);
    data.push_back(&ny);

    output->signal->emit_signal("viewport-change-notify", data);

    vx = nx;
    vy = ny;

    auto new_mask = get_mask_for_wayfire_viewport(vx, vy);

    output->for_each_wayfire_view_reverse([=] (wayfire_view v) {
        if(v->default_mask & new_mask)
            core->focus_wayfire_view(v);
    });
    */
}

std::vector<wayfire_view> viewport_manager::get_views_on_viewport(std::tuple<int, int> vp) {

    /*
    GetTuple(x, y, vp);
    uint32_t mask = get_mask_for_wayfire_viewport(x, y);

    std::vector<wayfire_view> ret;

    output->for_each_wayfire_view_reverse([&ret, mask] (wayfire_view v) {
                if(v->default_mask & mask)
                    ret.push_back(v);
            });

    return ret;
    */
    return std::vector<wayfire_view>();
}
/* End viewport_manager */

/* Start SignalManager */

void signal_manager::connect_signal(std::string name, signal_callback_t* callback) {
    sig[name].push_back(callback);
}

void signal_manager::disconnect_signal(std::string name, signal_callback_t* callback) {
    auto it = std::remove_if(sig[name].begin(), sig[name].end(),
            [=] (const signal_callback_t *call) {
                return call == callback;
            });

    sig[name].erase(it, sig[name].end());
}

void signal_manager::emit_signal(std::string name, signal_data *data) {
    std::vector<signal_callback_t> callbacks;
    for (auto x : sig[name])
        callbacks.push_back(*x);

    for (auto x : callbacks)
        x(data);
}

/* End SignalManager */
/* Start output */

/* TODO: move to command plugin */
key_callback terminal_callback;

wayfire_output::wayfire_output(weston_output *handle, wayfire_config *c) {
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

    terminal_callback = [=] (weston_keyboard *kbd, uint32_t key) {
        core->run("weston-terminal");
    };

    input->add_key(MODIFIER_SUPER, KEY_T, &terminal_callback);
}

wayfire_output::~wayfire_output(){
    delete plugin;
    delete signal;
    delete viewport;
    delete render;
    delete input;
}

void wayfire_output::activate() {
}

void wayfire_output::deactivate() {
    // TODO: what do we do?
    //render->dirty_context = true;
}

void wayfire_output::attach_view(wayfire_view v) {
    v->output = this;

    weston_layer_entry_insert(&normal_layer.view_list, &v->handle->layer_link);
    //GetTuple(vx, vy, wayfire_viewport->get_current_wayfire_viewport());
    //v->vx = vx;
    //v->vy = vy;

    //v->set_mask(wayfire_viewport->get_mask_for_wayfire_view(v));

    auto sig_data = new create_view_signal{v};
    signal->emit_signal("create-view", sig_data);
    delete sig_data;
}

void wayfire_output::detach_view(wayfire_view v) {
    weston_layer_entry_remove(&v->handle->layer_link);
    weston_view *wview, *next;

    wl_list_for_each(wview, &normal_layer.view_list.link, layer_link.link) {
        if (wview->surface == v->surface)
            continue;

        if (core->find_view(wview)->output != this)
            continue;

        next = wview;
        break;
    }

    if (active_view == v)
        focus_view(core->find_view(next), core->get_current_seat());

    auto sig_data = new destroy_view_signal{v};
    signal->emit_signal("destroy-view", sig_data);
    delete sig_data;
}

void wayfire_output::focus_view(wayfire_view v, weston_seat *seat) {
    if (!v || v == active_view)
        return;

    if (active_view)
        weston_desktop_surface_set_activated(active_view->desktop_surface, false);

    debug << "focus view" << std::endl;
    active_view = v;
    weston_view_activate(v->handle, seat,
            WESTON_ACTIVATE_FLAG_CLICKED | WESTON_ACTIVATE_FLAG_CONFIGURE);
    weston_desktop_surface_set_activated(v->desktop_surface, true);

    weston_view_geometry_dirty(v->handle);
    weston_layer_entry_remove(&v->handle->layer_link);
    weston_layer_entry_insert(&normal_layer.view_list, &v->handle->layer_link);
    weston_view_geometry_dirty(v->handle);
    weston_surface_damage(v->surface);

    weston_desktop_surface_propagate_layer(v->desktop_surface);
}

void wayfire_output::for_each_view(view_callback_proc_t call) {
    weston_view *view;

    wl_list_for_each(view, &handle->compositor->view_list, link) {
        if (view->output == handle) {
            call(core->find_view(view));
        }
    }
}

void wayfire_output::for_each_view_reverse(view_callback_proc_t call) {
    weston_view *view;

    wl_list_for_each_reverse(view, &handle->compositor->view_list, link) {
        if (view->output == handle) {
            call(core->find_view(view));
        }
    }
}

wayfire_view wayfire_output::get_view_at_point(int x, int y) {
    wayfire_view chosen = nullptr;

    for_each_view([x, y, &chosen] (wayfire_view v) {
        if (v->is_visible() && point_inside({x, y}, v->geometry)) {
            if (chosen == nullptr)
               chosen = v;
        }
    });

    return chosen;
}
