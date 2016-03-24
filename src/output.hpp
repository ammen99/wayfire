#ifndef OUTPUT_HPP
#define OUTPUT_HPP

#include "plugin.hpp"
#include "opengl.hpp"

using ViewCallbackProc = std::function<void(View)>;

class Core;
class Output {

    friend struct Hook;
    friend class Core;

    wlc_handle id;

    private:
    /* TODO: add plugin->output so that plugins know which output they're running on */
    /* Controls loading of plugins */
    struct PluginManager {
        std::vector<PluginPtr> plugins;

        template<class T> PluginPtr create_plugin();
        PluginPtr load_plugin_from_file(std::string path, void **handle);
        void load_dynamic_plugins();
        void init_default_plugins();
        PluginManager(Output *o, Config *config);
        ~PluginManager();
    } *plugin;

    public:

    struct InputManager;
    /* Controls key/buttonbindings and hooks */
    struct HookManager {
        friend struct Output::InputManager;
        private:
            int cnt_hooks = 0;
            std::vector<KeyBinding*> keys;
            std::vector<ButtonBinding*> buttons;
            std::vector<Hook*> hooks;

        public:

            void add_key (KeyBinding *kb, bool grab = false);
            void rem_key (uint key);

            void add_but (ButtonBinding *bb, bool grab = false);
            void rem_but (uint key);

            void add_hook(Hook*);
            void rem_hook(uint key);
            void run_hooks();

            int running_hooks() { return cnt_hooks; }
    } *hook;

    /* Controls input/output */
    struct InputManager {
        private:
            std::unordered_set<Ownership> active_owners;
            int mousex, mousey; // pointer x, y

            int keyboard_grab_count = 0;
            int pointer_grab_count = 0;

            /* Pointer to this output's hook manager
             * Needed to activate button/keybindings */
            Output::HookManager *hook_mgr;

        public:
            InputManager(HookManager *hmgr) : hook_mgr(hmgr) {}
            bool activate_owner  (Ownership owner);
            bool deactivate_owner(Ownership owner);
            bool is_owner_active (std::string owner_name);

            void grab_keyboard();
            void ungrab_keyboard();
            void grab_pointer();
            void ungrab_pointer();

            bool check_key(KeyBinding *kb, uint32_t key, uint32_t mod);
            bool check_but_press  (ButtonBinding *bb, uint32_t button, uint32_t mod);
            bool check_but_release(ButtonBinding *bb, uint32_t button, uint32_t mod);

            bool process_key_event(uint32_t key, uint32_t mods, wlc_key_state state);
            bool process_button_event(uint32_t button, uint32_t mods, wlc_button_state state, wlc_point point);
            bool process_pointer_motion_event(wlc_point point);
            bool process_scroll_event(uint32_t mods, double amount[2]);

            std::tuple<int, int> get_pointer_position();
    } *input;

    int32_t screen_width, screen_height;
    std::tuple<int, int> get_screen_size() {return std::make_tuple(screen_width, screen_height);}

    struct RenderManager {
        private:
            Output *output;

            int redraw_timer = 0;
            struct {
                GLuint tex = -1;
                GLuint fbuff;
                unsigned long w, h;
            } background;

            RenderHook renderer;
            #define ALL_VISIBLE 4294967295 // All 32 bits are on
            uint32_t visibility_mask = ALL_VISIBLE;
        public:
            RenderManager(Output *o);
            OpenGL::Context *ctx;

            void load_background();
            void blit_background(GLuint destination_fbuff);
            GLuint get_background() {return background.tex;}

            void set_renderer(uint32_t visibility_mask = ALL_VISIBLE, RenderHook rh = nullptr);
            void transformation_renderer();
            void reset_renderer();
            bool renderer_running() { return renderer != nullptr || redraw_timer; }
            void paint();

            bool should_render_view(wlc_handle view) { return renderer == nullptr; }
            bool should_repaint_everything() { return redraw_timer > 0; }
            void set_redraw_everything(bool state) {
                if(state) ++redraw_timer;
                else if(redraw_timer) --redraw_timer;
            }

            /* this function renders a viewport and
             * saves the image in texture which is returned */
            void texture_from_viewport(std::tuple<int, int>, GLuint& fbuff, GLuint& tex);

            std::vector<EffectHook*> effects;
            void add_effect(EffectHook *);
            void rem_effect(uint key, View win = nullptr);
        } *render;

    bool should_redraw() {
        return render->renderer_running() || hook->running_hooks() > 0;
    }

    struct ViewportManager {
        private:
            int vwidth, vheight, vx, vy;
            Output *output;

        public:
            ViewportManager(Output *o);
            /* returns viewport mask for a View, assuming it is on current viewport */
            uint32_t get_mask_for_view(View);

            /* returns the coords of the viewport where top left corner of a view is,
             * assuming the view coords are on current viewport */
            void get_viewport_for_view(View, int&, int&);
            uint32_t get_mask_for_viewport(int x, int y) {
                return (1 << (x + y * vheight));
            }


            std::vector<View> get_windows_on_viewport(std::tuple<int, int>);
            void switch_workspace(std::tuple<int, int>);

            std::tuple<int, int> get_current_viewport();
            std::tuple<int, int> get_viewport_grid_size();
    } *viewport;

    struct SignalManager {
        private:
            std::unordered_map<std::string, std::vector<SignalListener*>> signals;
            void add_default_signals();

        public:
            void add_signal(std::string name);
            void connect_signal(std::string name, SignalListener *callback);
            void disconnect_signal(std::string name, uint id);
            void trigger_signal(std::string name, SignalListenerData data);
    } *signal;

    Output(wlc_handle handle, Config *config);
    ~Output();

    void init();
    wlc_handle get_handle() {return id;}
    View get_active_view();
    View get_view_at_point(int x, int y, uint32_t mask = 0);

    void for_each_view(ViewCallbackProc);
    void for_each_view_reverse(ViewCallbackProc);

    void attach_view(View v);
    void detach_view(View v);
    void focus_view(View v);
};

#endif /* end of include guard: OUTPUT_HPP */
