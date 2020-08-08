#include "wm.hpp"
#include "wayfire/output.hpp"
#include "wayfire/view.hpp"
#include "wayfire/core.hpp"
#include "wayfire/workspace-manager.hpp"

#include "../output/output-impl.hpp"
#include "wayfire/signal-definitions.hpp"

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

        wf::get_core().emit_signal("shutdown", nullptr);
        wl_display_terminate(wf::get_core().display);

        return true;
    };

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
