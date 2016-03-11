#include "commonincludes.hpp"
#include "opengl.hpp"
#include "core.hpp"
#include "wm.hpp"

#include <sstream>
#include <memory>


bool wmDetected;

Core *core;
int refreshrate;

GLuint vao, vbo;

class CorePlugin : public Plugin {
    public:
        void init() {
            options.insert(newIntOption("rrate", 100));
            options.insert(newIntOption("vwidth", 3));
            options.insert(newIntOption("vheight", 3));
            options.insert(newStringOption("background", ""));
            options.insert(newStringOption("shadersrc", "/usr/local/share/"));
            options.insert(newStringOption("pluginpath", "/usr/local/lib/"));
            options.insert(newStringOption("plugins", ""));
        }
        void initOwnership() {
            owner->name = "core";
            owner->compatAll = true;
        }
        void updateConfiguration() {
            refreshrate = options["rrate"]->data.ival;
        }
};
PluginPtr plug; // used to get core options

Core::Core(int vx, int vy) {
    config = new Config();

    this->vx = vx;
    this->vy = vy;

    width = 1366;
    height = 768;

}
void Core::init() {

    init_default_plugins();

    /* load core options */
    plug->owner = std::make_shared<_Ownership>();
    plug->initOwnership();
    plug->init();
    config->setOptionsForPlugin(plug);
    plug->updateConfiguration();

    vwidth = plug->options["vwidth"]->data.ival;
    vheight= plug->options["vheight"]->data.ival;

    load_dynamic_plugins();

    for(auto p : plugins) {
        p->owner = std::make_shared<_Ownership>();
        p->initOwnership();
        regOwner(p->owner);
        p->init();
        config->setOptionsForPlugin(p);
        p->updateConfiguration();
    }

    add_default_signals();
}

Core::~Core(){
    for(auto p : plugins) {
        p->fini();
        if(p->dynamic)
            dlclose(p->handle);
        p.reset();
    }
}

void Core::run(const char *command) {
    auto pid = fork();

    if(!pid) {
        std::exit(execl("/bin/sh", "/bin/sh", "-c", command, NULL));
    }
}

Hook::Hook() : active(false) {}

void Core::add_hook(Hook *hook){
    if(hook)
        hook->id = nextID++,
        hooks.push_back(hook);
}

void Core::rem_hook(uint key) {
    auto it = std::remove_if(hooks.begin(), hooks.end(), [key] (Hook *hook) {
                if(hook && hook->id == key) {
                        hook->disable();
                        return true;
                }
                else
                    return false;
            });

    hooks.erase(it, hooks.end());
}

void Core::run_hooks() {
    for(auto h : hooks) {
        if(h->getState())
            h->action();
    }
}

void Core::add_effect(EffectHook *hook){
    if(!hook) return;

    hook->id = nextID++;

    if(hook->type == EFFECT_OVERLAY)
        effects.push_back(hook);
    else if(hook->type == EFFECT_WINDOW)
        hook->win->effects[hook->id] = hook;
}

void Core::rem_effect(uint key, View w) {
    if(w == nullptr) {
        auto it = std::remove_if(effects.begin(), effects.end(),
                [key] (EffectHook *hook) {
            if(hook && hook->id == key) {
                hook->disable();
                return true;
            }
            else
                return false;
        });

        effects.erase(it, effects.end());
    }
    else {
        auto it = w->effects.find(key);
        it->second->disable();
        w->effects.erase(it);
    }
}

bool Hook::getState() { return this->active; }

void Hook::enable() {
    if(active)
        return;
    active = true;
    core->cntHooks++;
}

void Hook::disable() {
    if(!active)
        return;

    active = false;
    core->cntHooks--;
}

void KeyBinding::enable() {
    if(active) return;
    active = true;
}

void KeyBinding::disable() {
    if(!active) return;
    active = false;
}

void Core::add_key(KeyBinding *kb, bool grab) {
    if(!kb) return;
    keys.push_back(kb);
    if(grab) kb->enable();
}

void Core::rem_key(uint key) {
    auto it = std::remove_if(keys.begin(), keys.end(), [key] (KeyBinding *kb) {
                if(kb) return kb->id == key;
                else return true;
            });

    keys.erase(it, keys.end());
}

void ButtonBinding::enable() {
    if(active) return;
    active = true;
}

void ButtonBinding::disable() {
    if(!active) return;
    active = false;
}

void Core::add_but(ButtonBinding *bb, bool grab) {
    if(!bb) return;

    buttons.push_back(bb);
    if(grab) bb->enable();
}

void Core::rem_but(uint key) {
    auto it = std::remove_if(buttons.begin(), buttons.end(), [key] (ButtonBinding *bb) {
                if(bb) return bb->id == key;
                else return true;
            });
    buttons.erase(it, buttons.end());
}

void Core::regOwner(Ownership owner) {
    owners.insert(owner);
}

bool Core::activate_owner(Ownership owner) {

    if(!owner) {
        std::cout << "Error detected ?? calling with nullptr!!" << std::endl;
        return false;
    }

    if(owner->active || owner->special) {
        owner->active = true;
        return true;
    }

    for(auto act_owner : owners)
        if(act_owner && act_owner->active) {
            bool owner_in_act_owner_compat =
                act_owner->compat.find(owner->name) != act_owner->compat.end();

            bool act_owner_in_owner_compat =
                owner->compat.find(act_owner->name) != owner->compat.end();

            if(!owner_in_act_owner_compat && !act_owner->compatAll)
                return false;

            if(!act_owner_in_owner_compat && !owner->compatAll)
                return false;
        }

    owner->active = true;
    return true;
}

bool Core::deactivate_owner(Ownership owner) {
    owner->ungrab();
    owner->active = false;
    return true;
}

bool Core::is_owner_active(std::string name) {
    for (auto act : owners)
        if (act && act->active && act->name == name)
            return true;

    return false;
}

//TODO: implement grab/ungrab keyboard and pointer
void Core::grab_pointer() {}
void Core::ungrab_pointer() {}
void Core::grab_keyboard() {}
void Core::ungrad_keyboard() {}

void Core::add_signal(std::string name) {
    if(signals.find(name) == signals.end())
        signals[name] = std::vector<SignalListener*>();
}

void Core::trigger_signal(std::string name, SignalListenerData data) {
    if(signals.find(name) != signals.end()) {
        std::vector<SignalListener*> toTrigger;
        for(auto proc : signals[name])
            toTrigger.push_back(proc);

        for(auto proc : signals[name])
            proc->action(data);
    }
}

void Core::connect_signal(std::string name, SignalListener *callback){
    add_signal(name);
    callback->id = nextID++;
    signals[name].push_back(callback);
}

void Core::disconnect_signal(std::string name, uint id) {
    auto it = std::remove_if(signals[name].begin(), signals[name].end(),
            [id](SignalListener *sigl){
            return sigl->id == id;
            });

    signals[name].erase(it, signals[name].end());
}

void Core::add_default_signals() {

    /* single element: the window */
    add_signal("create-window");
    add_signal("destroy-window");

    /* move-window is triggered when a window is moved
     * Data contains 3 elements:
     * 1. raw pointer to FireView
     * 2. dx
     * 3. dy */

    add_signal("move-window");

    add_signal("move-request");
    add_signal("resize-request");

}

void Core::add_window(wlc_handle view) {
    View v = std::make_shared<FireView>(view);
    windows[view] = v;

    wlc_view_bring_to_front(view);
    wlc_view_focus(view);

    wlc_view_set_state(view, WLC_BIT_MAXIMIZED, false);
    wlc_view_set_state(view, WLC_BIT_FULLSCREEN, false);

    v->set_mask(get_mask_for_view(v));
    v->vx = vx;
    v->vy = vy;
}

void Core::focus_window(View win) {
    if(!win) return;
    auto id = win->get_id();

    wlc_view_focus(id);
   // wlc_view_bring_to_front(id);
}

View Core::find_window(wlc_handle handle) {
    auto it = windows.find(handle);
    if(it == windows.end()) return nullptr;
    return it->second;
}

View Core::get_active_window() {
    return find_window(get_top_window(wlc_get_focused_output(), 0));
}

wlc_handle Core::get_top_window(wlc_handle output, size_t offset) {
    size_t memb;
    const wlc_handle *views = wlc_output_get_views(output, &memb);

    for(int i = memb - 1; i >= 0; i--) {
        auto w = find_window(views[i]);
        if(w && w->is_visible()) return w->get_id();
    }

    return 0;
}

void Core::for_each_window(WindowCallbackProc call) {
    size_t num;
    const wlc_handle* views = wlc_output_get_views(wlc_get_focused_output(), &num);

    for(int i = num - 1; i >= 0; i--) {
        auto w = find_window(views[i]);
        if(w) call(w);
    }
}

void Core::for_each_window_reverse(WindowCallbackProc call) {
    size_t num;
    const wlc_handle *views = wlc_output_get_views(wlc_get_focused_output(), &num);

    for(int i = 0; i < num; i++) {
        auto w = find_window(views[i]);
        if(w) call(w);
    }
}


void Core::close_window(View win) {
    if(!win) return;

    wlc_view_close(win->get_id());
    focus_window(get_active_window());
}

void Core::remove_window(View win) {
    assert(win);

    windows.erase(win->get_id());
    win.reset();
}

bool Core::check_key(KeyBinding *kb, uint32_t key, uint32_t mod) {
    if(!kb->active)
        return false;

    if(kb->key != key)
        return false;

    if(kb->mod != mod)
        return false;

    return true;
}

bool Core::check_but_press(ButtonBinding *bb, uint32_t button, uint32_t mod) {
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

bool Core::check_but_release(ButtonBinding *bb, uint32_t button, uint32_t mod) {
    if(!bb->active)
        return false;

    if(bb->type != BindingTypeRelease)
        return false;

    if(bb->button != button)
        return false;

    return true;
}

bool Core::process_key_event(uint32_t key_in, uint32_t mod, wlc_key_state state) {
    if (state == WLC_KEY_STATE_RELEASED) return false;

    if (key_in == XKB_KEY_r && (mod & WLC_BIT_MOD_ALT)) {
        run("dmenu_run");
    }

    for(auto key : keys) {
        if(check_key(key, key_in, mod)) {
            key->action(Context(0, 0, key_in, mod));
            return true;
        }
    }

    return false;
}


bool Core::process_button_event(uint32_t button, uint32_t mod,
        wlc_button_state state, wlc_point point) {

    mousex = point.x;
    mousey = point.y;

    bool processed = false;

    for(auto but : buttons) {
        if(state == WLC_BUTTON_STATE_PRESSED && check_but_press(but, button, mod)) {
            but->action(Context(mousex, mousey, 0, 0));

            if (but->mod != 0)
                processed = true;
        }

        if(state == WLC_BUTTON_STATE_RELEASED && check_but_release(but, button, mod)) {
            but->action(Context(mousex, mousey, 0, 0));
            processed = true;
        }
    }

    return processed;
}

bool Core::process_pointer_motion_event(wlc_point point) {
    mousex = point.x;
    mousey = point.y;

    return false;
}

#define Second 1000000
#define MaxDelay 1000
#define MinRR 61

#include <wlc/wlc-wayland.h>
#include <wlc/wlc-render.h>

void Core::reset_renderer() {
    renderer = nullptr;

    for_each_window([] (View v) {
                v->restore_mask();
            });

    wlc_output_schedule_render(wlc_get_focused_output());
}

void Core::set_renderer(uint32_t vis_mask, RenderHook rh) {
    if (!rh) {
        renderer = std::bind(std::mem_fn(&Core::transformation_renderer), this);
    } else {
        renderer = rh;
    }

    for_each_window([=] (View v) {
                v->set_temporary_mask(0);
            });

    visibility_mask = vis_mask;
}

void Core::render() {
    if (renderer) {
        renderer();
    } else {
        OpenGL::useDefaultProgram();
        GLuint bg = get_background();
        const wlc_geometry g = {{0, 0}, {1366, 768}};
        OpenGL::renderTransformedTexture(bg, g, glm::mat4());
    }
}

void Core::transformation_renderer() {

    OpenGL::useDefaultProgram();
    GLuint bg = get_background();
    const wlc_geometry g = {{0, 0}, {1366, 768}};
    OpenGL::renderTransformedTexture(bg, g, glm::mat4());

    for_each_window_reverse([=](View w) {
        if(w->default_mask & visibility_mask) {
            wlc_geometry g;
            wlc_view_get_visible_geometry(w->get_id(), &g);

            auto surf = wlc_view_get_surface(w->get_id());
            render_surface(surf, g, w->transform.compose());
        }
    });
}

std::tuple<int, int> Core::get_current_viewport() {
    return std::make_tuple(vx, vy);
}

std::tuple<int, int> Core::get_viewport_grid_size() {
    return std::make_tuple(vwidth, vheight);
}

std::tuple<int, int> Core::getScreenSize() {
    return std::make_tuple(1366, 768);
    return std::make_tuple(width, height);
}

std::tuple<int, int> Core::get_pointer_position() {
    return std::make_tuple(mousex, mousey);
}

View Core::get_view_at_point(int x, int y) {

    View chosen = nullptr;

    for_each_window([x,y, &chosen] (View w) {
            if(w->is_visible() && point_inside({x, y}, w->attrib)) {
                if(chosen == nullptr) chosen = w;

            }
    });

    return chosen;
}

uint32_t Core::get_mask_for_view(View v) {
    uint32_t mask = get_mask_for_viewport(vx, vy);

    if (vx > 0) {
        wlc_geometry viewp = {.origin = {-(int)width, 0}, .size = {width, height}};

        if (rect_inside(viewp, v->attrib))
            mask |= get_mask_for_viewport(vx - 1, vy);
    }

    if (vx < vwidth - 1) {
        wlc_geometry viewp = {.origin = {(int)width, 0}, .size = {width, height}};

        if (rect_inside(viewp, v->attrib))
            mask |= get_mask_for_viewport(vx + 1, vy);
    }

    if (vy > 0) {
        wlc_geometry viewp = {.origin = {0, -(int)height}, .size = {width, height}};

        if (rect_inside(viewp, v->attrib))
            mask |= get_mask_for_viewport(vx, vy - 1);
    }

    if (vy < vheight - 1) {
        wlc_geometry viewp = {.origin = {0, (int)height}, .size = {width, height}};

        if (rect_inside(viewp, v->attrib))
            mask |= get_mask_for_viewport(vx, vy + 1);
    }

    return mask;
}

void Core::get_viewport_for_view(View v, int &x, int &y) {
    if(0 <= v->attrib.origin.x && v->attrib.origin.x < width) {
        x = vx;
    } else if(v->attrib.origin.x < 0) {
        x = vx - 1;
    }

    if(0 <= v->attrib.origin.y && v->attrib.origin.y < height) {
        y = vy;
    } else if(v->attrib.origin.y < 0) {
        y = vy - 1;
    }
}

void Core::switch_workspace(std::tuple<int, int> nPos) {
    GetTuple(nx, ny, nPos);

    if(nx >= vwidth || ny >= vheight || nx < 0 || ny < 0)
        return;

    auto dx = (vx - nx) * width;
    auto dy = (vy - ny) * height;

    for_each_window([=] (View w) {
        bool has_been_before = w->default_mask & get_mask_for_viewport(vx, vy);
        bool visible_now = w->default_mask & get_mask_for_viewport(nx, ny);

        if(has_been_before && visible_now)
            w->move(w->attrib.origin.x + dx, w->attrib.origin.y + dy),
            w->vx = nx,
            w->vy = ny;
    });

    wlc_output_set_mask(wlc_get_focused_output(), get_mask_for_viewport(nx, ny));
    std::cout << wlc_get_focused_output() << std::endl;

    vx = nx;
    vy = ny;

    auto new_mask = get_mask_for_viewport(vx, vy);

    for_each_window_reverse([=] (View v) {
        if(v->default_mask & new_mask)
            focus_window(v);
    });
}

std::vector<View> Core::get_windows_on_viewport(std::tuple<int, int> vp) {

    GetTuple(x, y, vp);
    uint32_t mask = get_mask_for_viewport(x, y);

    std::vector<View> ret;

    for_each_window_reverse([&ret, mask] (View v) {
                if(v->default_mask & mask)
                    ret.push_back(v);
            });

    return ret;
}

void Core::texture_from_viewport(std::tuple<int, int> vp,
        GLuint &fbuff, GLuint &texture) {

    OpenGL::useDefaultProgram();
    if (fbuff == -1 || texture == -1)
        OpenGL::prepareFramebuffer(fbuff, texture);

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbuff));
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, background.fbuff));
    GL_CALL(glBlitFramebuffer(0, 0, background.w, background.h,
                0, 768, 1366, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR));

    GetTuple(x, y, vp);
    uint32_t mask = get_mask_for_viewport(x, y);

    for_each_window_reverse([=] (View v) {
        if (v->default_mask & mask) {
            int dx = (v->vx - x) * width;
            int dy = (v->vy - y) * height;

            wlc_geometry g;
            wlc_view_get_visible_geometry(v->get_id(), &g);
            g.origin.x += dx;
            g.origin.y += dy;

            render_surface(v->get_surface(), g, v->transform.compose());
        }
    });

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

namespace {
    int fullRedraw = 0;
}

int Core::getRefreshRate() {
    return refreshrate;
}

#define uchar unsigned char
namespace {
    GLuint getFilledTexture(int w, int h, uchar r, uchar g, uchar b, uchar a) {
        uchar *arr = new uchar[w * h * 4];
        for(int i = 0; i < w * h; i = i + 4) {
            arr[i + 0] = r;
            arr[i + 1] = g;
            arr[i + 2] = b;
            arr[i + 3] = a;
        }
        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
                w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, arr);

        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    }
}

#include "jpeg.hpp"

GLuint Core::get_background() {
    if(background.tex != -1) return background.tex;

    OpenGL::initOpenGL("/usr/share/wayfire/shaders/");
    background.tex = texture_from_jpeg(plug->options["background"]->data.sval->c_str(), background.w, background.h);

    GL_CALL(glGenFramebuffers(1, &background.fbuff));
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, background.fbuff));


    GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                background.tex, 0));

    auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
    if (status != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "Error in background framebuffer !!!" << std::endl;

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    return background.tex;
}

//void Core::set_background(const char *path) {
//    std::cout << "[DD] Background file: " << path << std::endl;
//
//    auto texture = GLXUtils::loadImage(const_cast<char*>(path));
//
//    if(texture == -1)
//        texture = getFilledTexture(width, height, 128, 128, 128, 255);
//
//    uint vao, vbo;
//    OpenGL::generateVAOVBO(0, height, width, -height, vao, vbo);
//
//    backgrounds.clear();
//    backgrounds.resize(vheight);
//    for(int i = 0; i < vheight; i++)
//        backgrounds[i].resize(vwidth);
//
//    for(int i = 0; i < vheight; i++){
//        for(int j = 0; j < vwidth; j++) {
//
//            backgrounds[i][j] = std::make_shared<FireView>(0, false);
//
//            backgrounds[i][j]->vao = vao;
//            backgrounds[i][j]->vbo = vbo;
//            backgrounds[i][j]->norender = false;
//            backgrounds[i][j]->texture  = texture;
//
//            backgrounds[i][j]->attrib.x = (j - vx) * width;
//            backgrounds[i][j]->attrib.y = (i - vy) * height;
//            backgrounds[i][j]->attrib.width  = width;
//            backgrounds[i][j]->attrib.height = height;
//
//            backgrounds[i][j]->type = WindowTypeDesktop;
//            backgrounds[i][j]->updateVBO();
//            backgrounds[i][j]->transform.color = glm::vec4(1,1,1,1);
//            backgrounds[i][j]->updateRegion();
//            wins->addWindow(backgrounds[i][j]);
//        }
//    }
//}

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

PluginPtr Core::load_plugin_from_file(std::string path, void **h) {
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

void Core::load_dynamic_plugins() {
    std::stringstream stream(*plug->options["plugins"]->data.sval);
    auto path = *plug->options["pluginpath"]->data.sval + "/wayfire/";

    std::string plugin;
    while(stream >> plugin){
        if(plugin != "") {
            void *handle;
            auto ptr = load_plugin_from_file(path + "/lib" + plugin + ".so",
                    &handle);
            if(ptr) ptr->handle  = handle,
                    ptr->dynamic = true,
                    plugins.push_back(ptr);
        }
    }
}

template<class T>
PluginPtr Core::create_plugin() {
    return std::static_pointer_cast<Plugin>(std::make_shared<T>());
}

void Core::init_default_plugins() {
    plug = create_plugin<CorePlugin>();
    plugins.push_back(create_plugin<Focus>());
    plugins.push_back(create_plugin<Exit>());
    plugins.push_back(create_plugin<Close>());
    plugins.push_back(create_plugin<Refresh>());
}
