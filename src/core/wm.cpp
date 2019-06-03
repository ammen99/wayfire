#include "wm.hpp"
#include "output.hpp"
#include "view.hpp"
#include "debug.hpp"
#include "core.hpp"
#include "workspace-manager.hpp"

#include "../view/xdg-shell.hpp"
#include "seat/input-inhibit.hpp"
#include "signal-definitions.hpp"

void wayfire_exit::init(wayfire_config*)
{
    key = [](uint32_t key)
    {
        if (is_output_inhibited(wf::get_core().get_active_output()))
            return;

        wf::get_core().emit_signal("shutdown", nullptr);
        wl_display_terminate(wf::get_core().display);
    };

    output->add_key(new_static_option("<ctrl> <alt> KEY_BACKSPACE"), &key);
}

void wayfire_exit::fini()
{
    output->rem_binding(&key);
}

void wayfire_close::init(wayfire_config *config)
{
    grab_interface->abilities_mask = WF_ABILITY_GRAB_INPUT;
    auto key = config->get_section("core")
        ->get_option("close_top_view", "<super> KEY_Q | <alt> KEY_FN_F4");

    callback = [=] (wf_activator_source, uint32_t)
    {
        if (!output->activate_plugin(grab_interface))
            return;

        output->deactivate_plugin(grab_interface);
        auto view = output->get_active_view();
        if (view && view->role == wf::VIEW_ROLE_TOPLEVEL) view->close();
    };

    output->add_activator(key, &callback);
}

void wayfire_close::fini()
{
    output->rem_binding(&callback);
}

void wayfire_focus::init(wayfire_config *)
{
    grab_interface->name = "_wf_focus";
    grab_interface->abilities_mask = WF_ABILITY_CHANGE_VIEW_GEOMETRY;

    on_button = [=] (uint32_t button, int x, int y) {
        this->check_focus_surface(wf::get_core().get_cursor_focus());
    };
    output->add_button(new_static_option("BTN_LEFT"), &on_button);

    on_touch = [=] (int x, int y) {
        this->check_focus_surface(wf::get_core().get_touch_focus());
    };
    output->add_touch(new_static_option(""), &on_touch);

    on_view_disappear = [=] (wf::signal_data_t *data) {
        set_last_focus(nullptr);
    };

    on_view_output_change = [=] (wf::signal_data_t *data)
    {
        if (get_signaled_output(data) != this->output)
            send_done(last_focus); // will also reset last_focus
    };
}

void wayfire_focus::check_focus_surface(wf::surface_interface_t* focus)
{
    /* Find the main view */
    auto main_surface = focus ? focus->get_main_surface() : nullptr;
    auto view = dynamic_cast<wf::view_interface_t*> (main_surface);

    /* Close popups from the lastly focused view */
    if (last_focus.get() != view)
        send_done(last_focus);

    if (!view || !view->is_mapped() || !view->get_keyboard_focus_surface()
        || !output->activate_plugin(grab_interface))
    {
        return;
    }

    output->deactivate_plugin(grab_interface);
    view->get_output()->focus_view(view->self());
    set_last_focus(view->self());
}

void wayfire_focus::send_done(wayfire_view view)
{
    if (!last_focus)
        return;

    /* Do not send done while running */
    auto surfaces = view->enumerate_surfaces();
    for (auto& child : surfaces)
    {
        auto popup =
            dynamic_cast<wayfire_xdg_popup<wlr_xdg_popup>*> (child.surface);
        auto popup_v6 =
            dynamic_cast<wayfire_xdg_popup<wlr_xdg_popup_v6>*> (child.surface);

        if (popup)
            popup->send_done();
        if (popup_v6)
            popup->send_done();
    }

    set_last_focus(nullptr);
}

void wayfire_focus::set_last_focus(wayfire_view view)
{
    if (last_focus)
    {
        last_focus->disconnect_signal("disappeared", &on_view_disappear);
        last_focus->disconnect_signal("set-output", &on_view_output_change);
    }

    last_focus = view;
    if (last_focus)
    {
        last_focus->connect_signal("disappeared", &on_view_disappear);
        last_focus->connect_signal("set-output", &on_view_output_change);
    }
}

void wayfire_focus::fini()
{
    output->rem_binding(&on_button);
    output->rem_binding(&on_touch);
    set_last_focus(nullptr);
}

void wayfire_handle_focus_parent::focus_view(wayfire_view view)
{
    last_view = view;
    view->get_output()->workspace->bring_to_front(view);
    for (auto child : view->children)
        focus_view(child);
}

void wayfire_handle_focus_parent::init(wayfire_config*)
{
    focus_event = [&] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        if (!view || intercept_recursion)
            return;

        auto to_focus = view;
        while(to_focus->parent)
            to_focus = to_focus->parent;

        focus_view(to_focus);

        /* because output->focus_view() will fire focus-view signal again,
         * we use this flag to know that this is happening and don't fall
         * into the depths of the infinite recursion */
        intercept_recursion = true;
        output->focus_view(last_view);
        intercept_recursion = false;

        /* free shared_ptr reference */
        last_view.reset();
    };
    output->connect_signal("focus-view", &focus_event);
}

void wayfire_handle_focus_parent::fini()
{
    output->disconnect_signal("focus-view", &focus_event);
}
