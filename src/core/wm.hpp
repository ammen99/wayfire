#ifndef WM_H
#define WM_H

#include "wayfire/plugin.hpp"
#include "wayfire/bindings.hpp"
#include "wayfire/view.hpp"

struct wm_focus_request : public wf::signal_data_t
{
    wf::surface_interface_t *surface;
};

class wayfire_close : public wf::plugin_interface_t
{
    wf::activator_callback callback;

  public:
    void init() override;
    void fini() override;
};

class wayfire_focus : public wf::plugin_interface_t
{
    wf::button_callback on_button;
    wf::touch_callback on_touch;
    wf::signal_callback_t on_wm_focus_request;

    void check_focus_surface(wf::surface_interface_t *surface);

  public:
    void init() override;
    void fini() override;
};

class wayfire_exit : public wf::plugin_interface_t
{
    wf::key_callback key;
    wf::signal_connection_t on_output_removed;

  public:
    void init() override;
    void fini() override;
};
#endif
