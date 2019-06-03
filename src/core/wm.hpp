#ifndef WM_H
#define WM_H

#include "plugin.hpp"
#include "bindings.hpp"
#include "view.hpp"

class wayfire_close : public wayfire_plugin_t {
    activator_callback callback;
    public:
        void init(wayfire_config*) override;
        void fini() override;
};

class wayfire_focus : public wayfire_plugin_t {
    button_callback on_button;
    touch_callback on_touch;
    wf::signal_callback_t on_view_disappear,
        on_view_output_change;

    wayfire_view last_focus;
    void send_done(wayfire_view view);
    void set_last_focus(wayfire_view view);
    void check_focus_surface(wf::surface_interface_t *surface);

    public:
        void init(wayfire_config*) override;
        void fini() override;
};

class wayfire_exit : public wayfire_plugin_t {
    key_callback key;
    public:
        void init(wayfire_config*) override;
        void fini() override;
};

class wayfire_handle_focus_parent : public wayfire_plugin_t {
    wf::signal_callback_t focus_event;

    void focus_view(wayfire_view view);
    wayfire_view last_view = nullptr;
    bool intercept_recursion = false;

    public:
    void init(wayfire_config *config) override;
    void fini() override;
};

#endif
