#include "wm.hpp"
#include "wayfire/output.hpp"
#include "wayfire/view.hpp"
#include "wayfire/core.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/output-layout.hpp"

#include <wayfire/util/log.hpp>

#include "../output/output-impl.hpp"
#include "wayfire/signal-definitions.hpp"

extern "C"
{
#include <wlr/config.h>
#if WLR_HAS_X11_BACKEND
 #include <wlr/backend/x11.h>
#endif
#include <wlr/backend/wayland.h>
#include <wlr/backend/noop.h>
}

static void idle_shutdown(void *data)
{
    wf::get_core().emit_signal("shutdown", nullptr);
    wl_display_terminate(wf::get_core().display);
}

void wayfire_exit::init()
{
    key = [] (uint32_t key)
    {
        auto output_impl =
            static_cast<wf::output_impl_t*>(wf::get_core().get_active_output());
        if (output_impl->is_inhibited())
        {
            return false;
        }

        idle_shutdown(nullptr);

        return true;
    };

    // make sure to shut down wayfire if destroying the last
    // nested backend output
    on_output_removed.set_callback([=] (wf::signal_data_t *data)
    {
        auto output = wf::get_signaled_output(data);

        bool is_nested_compositor = wlr_output_is_wl(output->handle);
#if WLR_HAS_X11_BACKEND
        is_nested_compositor |= wlr_output_is_x11(output->handle);
#endif

        int cnt_other_outputs = 0;
        for (auto& wo : wf::get_core().output_layout->get_outputs())
        {
            if ((wo != output) && !wlr_output_is_noop(wo->handle))
            {
                ++cnt_other_outputs;
            }
        }

        if (is_nested_compositor && (cnt_other_outputs == 0))
        {
            wl_event_loop_add_idle(wf::get_core().ev_loop, idle_shutdown, nullptr);
        }
    });

    output->connect_signal("pre-remove", &on_output_removed);
    output->add_key(wf::create_option_string<wf::keybinding_t>(
        "<ctrl> <alt> KEY_BACKSPACE"), &key);
}

void wayfire_exit::fini()
{
    output->rem_binding(&key);
}

void wayfire_close::init()
{
    grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;
    wf::option_wrapper_t<wf::activatorbinding_t> key("core/close_top_view");
    callback = [=] (wf::activator_source_t, uint32_t)
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        output->deactivate_plugin(grab_interface);
        auto view = output->get_active_view();
        if (view && (view->role == wf::VIEW_ROLE_TOPLEVEL))
        {
            view->close();
        }

        return true;
    };

    output->add_activator(key, &callback);
}

void wayfire_close::fini()
{
    output->rem_binding(&callback);
}

void wayfire_focus::init()
{
    grab_interface->name = "_wf_focus";
    grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP;

    on_wm_focus_request = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<wm_focus_request*>(data);
        check_focus_surface(ev->surface);
    };
    output->connect_signal("wm-focus-request", &on_wm_focus_request);

    on_button = [=] (uint32_t button, int x, int y)
    {
        this->check_focus_surface(wf::get_core().get_cursor_focus());

        return true;
    };
    output->add_button(
        wf::create_option_string<wf::buttonbinding_t>("BTN_LEFT"), &on_button);

    on_touch = [=] (int x, int y)
    {
        this->check_focus_surface(wf::get_core().get_touch_focus());

        return true;
    };
    output->add_touch(wf::create_option<wf::keybinding_t>({0, 0}), &on_touch);
}

void wayfire_focus::check_focus_surface(wf::surface_interface_t *focus)
{
    /* Find the main view */
    auto main_surface = focus ? focus->get_main_surface() : nullptr;
    auto view = dynamic_cast<wf::view_interface_t*>(main_surface);

    if (!view || !view->is_mapped() || !view->get_keyboard_focus_surface() ||
        !output->activate_plugin(grab_interface))
    {
        return;
    }

    output->deactivate_plugin(grab_interface);
    view->get_output()->focus_view(view->self(), true);
}

void wayfire_focus::fini()
{
    output->rem_binding(&on_button);
    output->rem_binding(&on_touch);
    output->disconnect_signal("wm-focus-request", &on_wm_focus_request);
}
