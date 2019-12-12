#ifndef WM_H
#define WM_H

#include "plugin.hpp"
#include "bindings.hpp"
#include "view.hpp"

struct wm_focus_request : public wf::signal_data_t
{
    wf::surface_interface_t *surface;
};

class wayfire_close : public wf::plugin_interface_t {
    activator_callback callback;
    public:
        void init() override;
        void fini() override;
};

class wayfire_focus : public wf::plugin_interface_t {
    button_callback on_button;
    touch_callback on_touch;
    wf::signal_callback_t on_view_disappear,
        on_view_output_change, on_wm_focus_request;

    wayfire_view last_focus;
    void send_done(wayfire_view view);
    void set_last_focus(wayfire_view view);
    void check_focus_surface(wf::surface_interface_t *surface);

    public:
        void init() override;
        void fini() override;
};

class wayfire_exit : public wf::plugin_interface_t {
    key_callback key;
    public:
        void init() override;
        void fini() override;
};

class wayfire_handle_focus_parent : public wf::plugin_interface_t {
    wf::signal_callback_t focus_event, pending_focus_unmap;
    wf::wl_idle_call idle_focus;
  public:
    void init() override;
    void fini() override;
};

#endif
