#include "opengl.hpp"
#include "output.hpp"
#include "wm.hpp"

#include <sstream>
#include <memory>

/* Start plugin manager */
Output::PluginManager::PluginManager(Output *o, Config *config) {
    init_default_plugins();
    load_dynamic_plugins();

    for (auto p : plugins) {
        p->owner = std::make_shared<_Ownership>();

        p->owner->output = o;
        p->output = o;

        p->initOwnership();
        p->init();
        config->setOptionsForPlugin(p);
        p->updateConfiguration();
    }
}

Output::PluginManager::~PluginManager() {
    for (auto p : plugins) {
        p->fini();
        if (p->dynamic)
            dlclose(p->handle);
        p.reset();
    }
}

namespace {
    template<class A, class B> B unionCast(A object) {
        union {
            A x;
            B y;
        } helper;
        helper.x = object;
        return helper.y;
    }
}

PluginPtr Output::PluginManager::load_plugin_from_file(std::string path, void **h) {
    void *handle = dlopen(path.c_str(), RTLD_NOW);
    if(handle == NULL){
        std::cout << "Error loading plugin " << path << std::endl;
        std::cout << dlerror() << std::endl;
        return nullptr;
    }

    auto initptr = dlsym(handle, "newInstance");
    if(initptr == NULL) {
        std::cout << "Failed to load newInstance from file " <<
            path << std::endl;
        std::cout << dlerror();
        return nullptr;
    }
    LoadFunction init = unionCast<void*, LoadFunction>(initptr);
    *h = handle;
    return std::shared_ptr<Plugin>(init());
}

void Output::PluginManager::load_dynamic_plugins() {
    std::stringstream stream(core->plugins);
    auto path = core->plugin_path + "/wayfire/";

    std::string plugin;
    while(stream >> plugin){
        if(plugin != "") {
            void *handle;
            auto ptr = load_plugin_from_file(path + "/lib" + plugin + ".so", &handle);
            if(ptr) ptr->handle  = handle,
                    ptr->dynamic = true,
                    plugins.push_back(ptr);
        }
    }
}

template<class T>
PluginPtr Output::PluginManager::create_plugin() {
    return std::static_pointer_cast<Plugin>(std::make_shared<T>());
}

void Output::PluginManager::init_default_plugins() {
    plugins.push_back(create_plugin<Focus>());
    plugins.push_back(create_plugin<Exit>());
    plugins.push_back(create_plugin<Close>());
    plugins.push_back(create_plugin<Refresh>());
}

/* End PluginManager */

/* Start HookManager */

void Output::HookManager::add_key(KeyBinding *kb, bool grab) {
    if (!kb)
        return;

    keys.push_back(kb);
    kb->id = core->get_nextid();

    if (grab)
        kb->enable();
}

void Output::HookManager::rem_key(uint key) {
    auto it = std::remove_if(keys.begin(), keys.end(), [key] (KeyBinding *kb) {
                if(kb) return kb->id == key;
                else return true;
            });

    keys.erase(it, keys.end());
}

void Output::HookManager::add_but(ButtonBinding *bb, bool grab) {
    if (!bb)
        return;

    buttons.push_back(bb);
    bb->id = core->get_nextid();

    if (grab)
        bb->enable();
}

void Output::HookManager::rem_but(uint key) {
    auto it = std::remove_if(buttons.begin(), buttons.end(), [key] (ButtonBinding *bb) {
                if(bb) return bb->id == key;
                else return true;
            });
    buttons.erase(it, buttons.end());
}

void Output::HookManager::add_hook(Hook *hook){
    if(hook) {
        hook->id = core->get_nextid();
        hooks.push_back(hook);
    }
}

void Output::HookManager::rem_hook(uint key) {
    auto it = std::remove_if(hooks.begin(), hooks.end(), [key] (Hook *hook) {
                if (hook && hook->id == key) {
                    hook->disable();
                    return true;
                } else {
                    return false;
                }
            });

    hooks.erase(it, hooks.end());
}

void Output::HookManager::run_hooks() {
    for (auto h : hooks) {
        if (h->getState())
            h->action();
    }
}

/* End HookManager */
/* Start InputManager */

void Output::InputManager::grab_pointer() {++pointer_grab_count;}
void Output::InputManager::ungrab_pointer() {pointer_grab_count = std::max(0, pointer_grab_count - 1);}
void Output::InputManager::grab_keyboard() {++keyboard_grab_count;}
void Output::InputManager::ungrab_keyboard() {keyboard_grab_count = std::max(0, keyboard_grab_count - 1);}

bool Output::InputManager::activate_owner(Ownership owner) {
    if (!owner)
        return false;

    if (active_owners.find(owner) != active_owners.end())
        return true;

    for(auto act_owner : active_owners) {
        bool owner_in_act_owner_compat =
            act_owner->compat.find(owner->name) != act_owner->compat.end();

        bool act_owner_in_owner_compat =
            owner->compat.find(act_owner->name) != owner->compat.end();

        if(!owner_in_act_owner_compat && !act_owner->compatAll)
            return false;

        if(!act_owner_in_owner_compat && !owner->compatAll)
            return false;
    }

    active_owners.insert(owner);
    return true;
}

bool Output::InputManager::deactivate_owner(Ownership owner) {
    owner->ungrab();
    active_owners.erase(owner);
    return true;
}

bool Output::InputManager::is_owner_active(std::string name) {
    for (auto act : active_owners)
        if (act && act->name == name)
            return true;

    return false;
}

bool Output::InputManager::check_key(KeyBinding *kb, uint32_t key, uint32_t mod) {
    if(!kb->active)
        return false;

    if(kb->key != key)
        return false;

    if(kb->mod != mod)
        return false;

    return true;
}

bool Output::InputManager::check_but_press(ButtonBinding *bb, uint32_t button, uint32_t mod) {
    if(!bb->active)
        return false;

    if(bb->type != BindingTypePress)
        return false;

    if(bb->mod != mod)
        return false;

    if(bb->button != button)
        return false;

    return true;
}

bool Output::InputManager::check_but_release(ButtonBinding *bb, uint32_t button) {
    if(!bb->active)
        return false;

    if(bb->type != BindingTypeRelease)
        return false;

    if(bb->button != button)
        return false;

    return true;
}

bool Output::InputManager::process_key_event(uint32_t key_in, uint32_t mod, wlc_key_state state) {
    if (state == WLC_KEY_STATE_RELEASED)
        return false;

    if (key_in == XKB_KEY_r && (mod & WLC_BIT_MOD_ALT)) {
        core->run("dmenu_run");
    }

    for(auto key : hook_mgr->keys) {
        if(check_key(key, key_in, mod)) {
            key->action(EventContext(0, 0, key_in, mod));
            return true;
        }
    }

    return keyboard_grab_count > 0;
}

bool Output::InputManager::process_scroll_event(uint32_t mod, double amount[2]) {
    for (auto but : hook_mgr->buttons) {
        if (but->button == BTN_SCROLL && but->mod == mod && but->active) {
            but->action(EventContext{amount[0], amount[1]});
            return true;
        }
    }

    return pointer_grab_count > 0;
}

bool Output::InputManager::process_button_event(uint32_t button, uint32_t mod,
        wlc_button_state state, wlc_point point) {

    mousex = point.x;
    mousey = point.y;

    bool processed = false;

    for(auto but : hook_mgr->buttons) {
        if(state == WLC_BUTTON_STATE_PRESSED && check_but_press(but, button, mod)) {
            but->action(EventContext(mousex, mousey, 0, 0));
            if (but->mod != 0)
                processed = true;
        }

        if(state == WLC_BUTTON_STATE_RELEASED && check_but_release(but, button)) {
            but->action(EventContext(mousex, mousey, 0, 0));
            processed = true;
        }
    }

    return processed || pointer_grab_count > 0;
}

bool Output::InputManager::process_pointer_motion_event(wlc_point point) {
    mousex = point.x;
    mousey = point.y;

    return pointer_grab_count > 0;
}

std::tuple<int, int> Output::InputManager::get_pointer_position() {
    return std::make_tuple(mousex, mousey);
}
/* End InputManager */
/* Start RenderManager */

#include <wlc/wlc-wayland.h>
#include <wlc/wlc-render.h>
#include "jpeg.hpp"

void Output::RenderManager::load_background() {
    background.tex = texture_from_jpeg(core->background.c_str(), background.w, background.h);

    GL_CALL(glGenFramebuffers(1, &background.fbuff));
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, background.fbuff));

    GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                background.tex, 0));

    auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
    if (status != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "Error in background framebuffer !!!" << std::endl;

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void Output::RenderManager::load_context() {
    ctx = OpenGL::init_opengl(output, core->shadersrc.c_str());
    OpenGL::bind_context(ctx);
    load_background();

    dirty_context = false;

    output->signal->trigger_signal("reload-gl", {});
}

void Output::RenderManager::release_context() {
    GL_CALL(glDeleteFramebuffers(1, &background.fbuff));
    GL_CALL(glDeleteTextures(1, &background.tex));

    OpenGL::release_context(ctx);
    dirty_context = true;
}

void Output::RenderManager::blit_background(GLuint dest) {
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dest));
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, background.fbuff));
    GL_CALL(glBlitFramebuffer(0, 0, background.w, background.h,
                0, output->screen_height, output->screen_width, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR));
}

Output::RenderManager::RenderManager(Output *o) {
    output = o;
    load_context();
}

void Output::RenderManager::reset_renderer() {
    renderer = nullptr;

    output->for_each_view([] (View v) {
        v->restore_mask();
    });

    wlc_output_schedule_render(wlc_get_focused_output());
}

void Output::RenderManager::set_renderer(uint32_t vis_mask, RenderHook rh) {
    if (!rh) {
        renderer = std::bind(std::mem_fn(&Output::RenderManager::transformation_renderer), this);
    } else {
        renderer = rh;
    }

    output->for_each_view([=] (View v) {
        v->set_temporary_mask(0);
    });

    visibility_mask = vis_mask;
}

void Output::RenderManager::paint() {
    if (dirty_context)
        load_context();

    OpenGL::bind_context(ctx);

    if (renderer) {
        renderer();
    } else {
        blit_background(0);
    }
}

void Output::RenderManager::post_paint() {
    std::vector<EffectHook*> active_effects;
    for (auto effect : effects) {
        if (effect->getState())
            active_effects.push_back(effect);
    }

    for (auto effect : active_effects)
        effect->action();
}

void Output::RenderManager::transformation_renderer() {
    blit_background(0);
    output->for_each_view_reverse([=](View v) {
        if(!v->is_hidden && (v->default_mask & visibility_mask) && !v->destroyed) {
            wlc_geometry g;
            wlc_view_get_visible_geometry(v->get_id(), &g);

            auto surf = wlc_view_get_surface(v->get_id());
            render_surface(surf, g, v->transform.compose());
        }
    });
}

void Output::RenderManager::texture_from_viewport(std::tuple<int, int> vp,
        GLuint &fbuff, GLuint &tex) {

    OpenGL::bind_context(ctx);

    if (fbuff == (uint)-1 || tex == (uint)-1)
        OpenGL::prepareFramebuffer(fbuff, tex);

    blit_background(fbuff);
    GetTuple(x, y, vp);

    uint32_t mask = output->viewport->get_mask_for_viewport(x, y);

    output->for_each_view_reverse([=] (View v) {
        if (v->default_mask & mask) {
            int dx = (v->vx - x) * output->screen_width;
            int dy = (v->vy - y) * output->screen_height;

            wlc_geometry g;
            wlc_view_get_visible_geometry(v->get_id(), &g);
            g.origin.x += dx;
            g.origin.y += dy;

            render_surface(v->get_surface(), g, v->transform.compose());
        }
    });

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void Output::RenderManager::add_effect(EffectHook *hook){
    if(!hook) return;

    hook->id = core->get_nextid();
    if(hook->type == EFFECT_OVERLAY)
        effects.push_back(hook);
    else if(hook->type == EFFECT_WINDOW)
        hook->win->effects[hook->id] = hook;
}

void Output::RenderManager::rem_effect(uint key, View v) {
    if (!v) {
        auto it = std::remove_if(effects.begin(), effects.end(),
                [key] (EffectHook *hook) {
            if (hook && hook->id == key) {
                hook->disable();
                return true;
            } else {
                return false;
            }
        });

        effects.erase(it, effects.end());
    } else {
        auto it = v->effects.find(key);
        it->second->disable();
        v->effects.erase(it);
    }
}
/* End RenderManager */
/* Start ViewportManager */

Output::ViewportManager::ViewportManager(Output *o) {
    output = o;
    vx = vy = 0;

    vwidth = core->vwidth;
    vheight = core->vheight;
}

std::tuple<int, int> Output::ViewportManager::get_current_viewport() { return std::make_tuple(vx, vy); }
std::tuple<int, int> Output::ViewportManager::get_viewport_grid_size() { return std::make_tuple(vwidth, vheight); }

int clamp(int x, int min, int max) {
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

uint32_t Output::ViewportManager::get_mask_for_view(View v) {
    assert(output);
    GetTuple(width, height, output->get_screen_size());
    int sdx, sdy, edx, edy;

    if (v->attrib.origin.x < 0) {
        sdx = v->attrib.origin.x / width - 1;
    } else {
        sdx = v->attrib.origin.x / width;
    }

    if (v->attrib.origin.y < 0) {
        sdy = v->attrib.origin.y / height - 1;
    } else {
        sdy = v->attrib.origin.y / height;
    }

    int sx = v->vx + sdx;
    int sy = v->vy + sdy;

    /* We must substract a bit to prevent having windows with only 5 pixels visible */
    int bottom_right_x = v->attrib.origin.x + (int32_t)v->attrib.size.w - 5;
    int bottom_right_y = v->attrib.origin.y + (int32_t)v->attrib.size.h - 5;

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

    std::cout << sdx << " " << width << std::endl;
    std::cout << "generating mask " << sx << " " << ex << " __ " << sy << " " << ey << std::endl;
    std::cout << "### " << v->attrib.origin.x << " ## " << v->attrib.origin.y << std::endl;
    std::cout << "*** " << bottom_right_x << " ** " << bottom_right_y << std::endl;
    std::cout << "&&& " << v->vx << " && " << v->vy << std::endl;

    for (int i = sx; i <= ex; i++)
        for (int j = sy; j <= ey; j++)
            mask |= get_mask_for_viewport(i, j);

    return mask;
}

void Output::ViewportManager::get_viewport_for_view(View v, int &x, int &y) {
    GetTuple(width, height, output->get_screen_size());
    int dx, dy;
    if (v->attrib.origin.x < 0) {
        dx = v->attrib.origin.x / width - 1;
    } else {
        dx = v->attrib.origin.x / width;
    }
    if (v->attrib.origin.y < 0) {
        dy = v->attrib.origin.y / height - 1;
    } else {
        dy = v->attrib.origin.y / height;
    }

    x = clamp(v->vx + dx, 0, vwidth - 1);
    y = clamp(v->vy + dy, 0, vheight - 1);
}

void Output::ViewportManager::switch_workspace(std::tuple<int, int> nPos) {
    GetTuple(nx, ny, nPos);

    std::cout << "switch workspace " << nx << " " << ny << std::endl;
    if(nx >= vwidth || ny >= vheight || nx < 0 || ny < 0 || (nx == vx && ny == vy))
        return;

    auto dx = (vx - nx) * output->screen_width;
    auto dy = (vy - ny) * output->screen_height;

    output->for_each_view([=] (View v) {
        bool has_been_before = v->default_mask & get_mask_for_viewport(vx, vy);
        bool visible_now = v->default_mask & get_mask_for_viewport(nx, ny);

        if(has_been_before && visible_now) {
            std::cout << "MOVING WINDOW ID " << v->get_id() << std::endl;
            v->move(v->attrib.origin.x + dx, v->attrib.origin.y + dy),
            v->vx = nx,
            v->vy = ny;
        }
    });

    wlc_output_set_mask(wlc_get_focused_output(), get_mask_for_viewport(nx, ny));

    /* TODO: use tuples for SignalListenerData, this is just an ugly hack */
    SignalListenerData data;
    data.push_back(&vx);
    data.push_back(&vy);
    data.push_back(&nx);
    data.push_back(&ny);

    output->signal->trigger_signal("viewport-change-notify", data);

    vx = nx;
    vy = ny;

    auto new_mask = get_mask_for_viewport(vx, vy);

    output->for_each_view_reverse([=] (View v) {
        if(v->default_mask & new_mask)
            core->focus_view(v);
    });
}

std::vector<View> Output::ViewportManager::get_windows_on_viewport(std::tuple<int, int> vp) {

    GetTuple(x, y, vp);
    uint32_t mask = get_mask_for_viewport(x, y);

    std::vector<View> ret;

    output->for_each_view_reverse([&ret, mask] (View v) {
                if(v->default_mask & mask)
                    ret.push_back(v);
            });

    return ret;
}
/* End ViewportManager */
/* Start SignalManager */

void Output::SignalManager::add_signal(std::string name) {
    if(signals.find(name) == signals.end())
        signals[name] = std::vector<SignalListener*>();
}

void Output::SignalManager::trigger_signal(std::string name, SignalListenerData data) {
    if(signals.find(name) != signals.end()) {
        std::vector<SignalListener*> toTrigger;
        for(auto proc : signals[name])
            toTrigger.push_back(proc);

        for(auto proc : signals[name])
            proc->action(data);
    }
}

void Output::SignalManager::connect_signal(std::string name, SignalListener *callback){
    add_signal(name);
    callback->id = core->get_nextid();
    signals[name].push_back(callback);
}

void Output::SignalManager::disconnect_signal(std::string name, uint id) {
    auto it = std::remove_if(signals[name].begin(), signals[name].end(),
            [id](SignalListener *sigl){
            return sigl->id == id;
            });

    signals[name].erase(it, signals[name].end());
}

void Output::SignalManager::add_default_signals() {
    add_signal("create-view");
    add_signal("destroy-view");

    add_signal("move-request");
    add_signal("resize-request");

    /* Doesn't actually change anything,
     * connected listeners should do it */
    add_signal("change-viewport-request");

    /* Emitted when viewport is changed,
     * contains the old and the new viewport */
    add_signal("change-viewport-notify");

    /* Emitted when a the GL context is reloaded
     * This happens when the tty is switched and then
     * the whole context is destroyed. At this point
     * we must recreate all programs and framebuffers */
    add_signal("reload-gl");
}
/* End SignalManager */
/* Start output */

Output::Output(wlc_handle handle, Config *c) {
    id = handle;
    auto res = wlc_output_get_resolution(handle);

    screen_width = res->w;
    screen_height = res->h;

    signal = new SignalManager();
    hook = new HookManager();
    input = new InputManager(hook);
    render = new RenderManager(this);
    viewport = new ViewportManager(this);
    plugin = new PluginManager(this, c);
}

Output::~Output(){
    delete plugin;
    delete signal;
    delete viewport;
    delete render;
    delete input;
    delete hook;
}

void Output::activate() {
}

void Output::deactivate() {
    render->dirty_context = true;
}

void Output::attach_view(View v) {
    v->output = this;
    GetTuple(vx, vy, viewport->get_current_viewport());
    v->vx = vx;
    v->vy = vy;

    v->set_mask(viewport->get_mask_for_view(v));

    SignalListenerData data;
    data.push_back(&v);
    signal->trigger_signal("create-view", data);
}

void Output::detach_view(View v) {
    SignalListenerData data;
    data.push_back(&v);
    signal->trigger_signal("destroy-view", data);
}

void Output::focus_view(View v) {
    if (!v)
        return;

    wlc_view_focus(v->get_id());
}

wlc_handle get_top_view(wlc_handle output) {
    size_t memb;
    const wlc_handle *views = wlc_output_get_views(output, &memb);

    for (int i = memb - 1; i >= 0; i--) {
        auto v = core->find_view(views[i]);
        if (v && v->is_visible())
            return v->get_id();
    }

    return 0;
}

View Output::get_active_view() {
    return core->find_view(get_top_view(id));
}

void Output::for_each_view(ViewCallbackProc call) {
    size_t num;
    const wlc_handle* views = wlc_output_get_views(id, &num);
    for (int i = num - 1; i >= 0; i--) {
        auto v = core->find_view(views[i]);
        if (v)
           call(v);
    }
}

void Output::for_each_view_reverse(WindowCallbackProc call) {
    size_t num;
    const wlc_handle *views = wlc_output_get_views(id, &num);

    for (size_t i = 0; i < num; i++) {
        auto v = core->find_view(views[i]);
        if (v)
           call(v);
    }
}

View Output::get_view_at_point(int x, int y, uint32_t mask) {
    View chosen = nullptr;

    for_each_view([x, y, &chosen, mask] (View v) {
        if ((!mask && v->is_visible() && point_inside({x, y}, v->attrib)) ||
                (mask && (v->default_mask & mask) && point_inside({x, y}, v->attrib))) {

            if (chosen == nullptr)
               chosen = v;

        }
    });

    return chosen;
}

