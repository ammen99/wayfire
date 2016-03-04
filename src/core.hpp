#ifndef FIRE_H
#define FIRE_H

#include "commonincludes.hpp"
#include "view.hpp"
#include "config.hpp"
#include "plugin.hpp"

class WinStack;

struct Context{
    struct {
        struct {
            int x_root, y_root;
        } xbutton;

        struct {
            uint32_t key;
            uint32_t mod;
        } xkey;
    } xev;

    Context(int x, int y, int key, int mod) {
        xev.xbutton.x_root = x;
        xev.xbutton.y_root = y;

        xev.xkey.key = key;
        xev.xkey.mod = mod;
    }
};

enum BindingType {
    BindingTypePress,
    BindingTypeRelease
};

struct Binding{
    bool active = false;
    BindingType type;
    uint32_t mod;
    uint id;
    std::function<void(Context)> action;
};

struct KeyBinding : Binding {
    uint32_t key;

    void enable();
    void disable();
};

struct ButtonBinding : Binding {
    uint32_t button;

    void enable();
    void disable();
};

// hooks are used to handle pointer motion
struct Hook {
    protected:
        bool active;
    public:
        uint id;
        std::function<void(void)> action;

        virtual void enable();
        virtual void disable();
        bool getState();
        Hook();
};

/* render hooks are used to replace the renderAllWindows()
 * when necessary, i.e to render completely custom
 * image */
using RenderHook = std::function<void()>;

/* Effects are used to draw over the screen, e.g when animating window actions */
/* Effects render directly to Core's framebuffer
 * They are either as overlays, which means they are always drawn
 * or they are on per-window basis */

enum EffectType { EFFECT_OVERLAY, EFFECT_WINDOW };
struct EffectHook : public Hook {
    EffectType type;
    /* used only if type == EFFECT_WINDOW */
    View win;
};

using SignalListenerData = std::vector<void*>;

struct SignalListener {
    std::function<void(SignalListenerData)> action;
    uint id;
};

#define GetTuple(x,y,t) auto x = std::get<0>(t); \
                        auto y = std::get<1>(t)

using WindowCallbackProc = std::function<void(View)>;

class Core {

    // used to optimize idle time by counting hooks(cntHooks)
    friend struct Hook;

    private:

        int cntHooks = 0, redraw_timer = 0;

        uint32_t width, height;
        int mousex, mousey; // pointer x, y
        int vwidth, vheight, vx, vy;

        std::unordered_map<wlc_handle, View> windows;

        uint nextID = 0;

        Config *config;

        std::vector<PluginPtr> plugins;
        std::vector<KeyBinding*> keys;
        std::vector<ButtonBinding*> buttons;
        std::vector<Hook*> hooks;
        std::vector<EffectHook*> effects;
        std::unordered_set<Ownership> owners;

        RenderHook renderer;

        std::unordered_map<std::string,
            std::vector<SignalListener*>> signals;
        void add_default_signals();
        void init_default_plugins();

        template<class T> PluginPtr create_plugin();
        PluginPtr load_plugin_from_file(std::string path, void **handle);
        void load_dynamic_plugins();

        GLuint background = -1;

    public:
        Core(int vx, int vy);
        ~Core();
        void init();

        void run(const char *command);
        View find_window(wlc_handle handle);
        View get_active_window();

        wlc_handle get_top_window(wlc_handle output, size_t offset);

        View get_view_at_point(int x, int y);

        uint32_t get_mask_for_viewport(int x, int y) {
            return (1 << (x + y * vheight));
        }

        /* returns viewport mask for a View, assuming it is on current viewport */
        uint32_t get_mask_for_view(View);

        /* returns the coords of the viewport where top left corner of a view is,
         * assuming the view coords are on current viewport */
        void get_viewport_for_view(View, int&, int&);

        void add_window(wlc_handle view);

        void focus_window(View win);
        void close_window(View win);

        void remove_window(View win);

        void for_each_window(WindowCallbackProc);
        void for_each_window_reverse(WindowCallbackProc);

        bool process_key_event(uint32_t key, uint32_t mods, wlc_key_state state);
        bool process_button_event(uint32_t button, uint32_t mods, wlc_button_state state, wlc_point point);
        bool process_pointer_motion_event(wlc_point point);

        void grab_keyboard();
        void ungrad_keyboard();
        void grab_pointer();
        void ungrab_pointer();


        bool should_redraw() { return redraw_timer > 0 || cntHooks > 0 || renderer; }
        bool should_repaint_everything() { return redraw_timer > 0; }
        void set_redraw_everything(bool state) {
            if(state) ++redraw_timer;
            else if(redraw_timer) --redraw_timer;
        }

        void render();
        void transformation_renderer();

        int getRefreshRate();

        GLuint get_background();
        void set_background(const char *path);

#define ALL_VISIBLE 4294967295 // All 32 bits are on

        uint32_t visibility_mask = ALL_VISIBLE;
        void set_renderer(uint32_t visibility_mask = ALL_VISIBLE, RenderHook rh = nullptr);
        void reset_renderer();

        void add_key (KeyBinding *kb, bool grab = false);
        void rem_key (uint key);

        void add_but (ButtonBinding *bb, bool grab = false);
        void rem_but (uint key);

        void add_hook(Hook*);
        void rem_hook(uint key);
        void run_hooks();

        void add_effect(EffectHook *);
        void rem_effect(uint key, View win = nullptr);

        void add_signal(std::string name);
        void connect_signal(std::string name, SignalListener *callback);
        void disconnect_signal(std::string name, uint id);
        void trigger_signal(std::string name, SignalListenerData data);

        void regOwner(Ownership owner);

        bool check_key(KeyBinding *kb, uint32_t key, uint32_t mod);
        bool check_but_press  (ButtonBinding *bb, uint32_t button, uint32_t mod);
        bool check_but_release(ButtonBinding *bb, uint32_t button, uint32_t mod);

        bool activate_owner  (Ownership owner);
        bool deactivate_owner(Ownership owner);

        /* this function renders a viewport and
         * saves the image in texture which is returned */
        void texture_from_viewport(std::tuple<int, int>, GLuint& fbuff,
                GLuint& tex);

        std::vector<View> get_windows_on_viewport(std::tuple<int, int>);
        void switch_workspace(std::tuple<int, int>);

        std::tuple<int, int> get_current_viewport ();
        std::tuple<int, int> get_viewport_grid_size  ();
        std::tuple<int, int> getScreenSize();
        std::tuple<int, int> get_pointer_position();
};
extern Core *core;
#endif
