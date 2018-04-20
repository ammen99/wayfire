#include "wm.hpp"
#include "output.hpp"
#include "view.hpp"
#include "debug.hpp"
#include "core.hpp"
#include "workspace-manager.hpp"
#include "../shared/config.hpp"
#include <linux/input.h>
#include "signal-definitions.hpp"

void wayfire_exit::init(wayfire_config*)
{
    key = [](uint32_t key)
    {
        wl_display_terminate(core->display);
    };

    output->add_key(MODIFIER_SUPER, KEY_Z,       &key);
    output->add_key(MODIFIER_ALT   | MODIFIER_CTRL,  KEY_BACKSPACE, &key);
}

void wayfire_close::init(wayfire_config *config)
{
    auto key = config->get_section("core")->get_key("view_close", {MODIFIER_SUPER, KEY_Q});
    callback = [=] (uint32_t key)
    {
        auto view = output->get_top_view();
        if (view) view->close();
    };

    output->add_key(key.mod, key.keyval, &callback);
}

void wayfire_focus::init(wayfire_config *)
{
    grab_interface->name = "_wf_focus";
    grab_interface->abilities_mask = WF_ABILITY_GRAB_INPUT;

    callback = [=] (uint32_t button, int x, int y)
    {
        auto output = core->get_output_at(x, y);
        core->focus_output(output);

        wayfire_surface_t *focus = nullptr, *main_surface = nullptr;
        wayfire_view view = nullptr;

        auto f = core->get_cursor_focus();
        log_info("found %p", f);

        if (!f)
            return;
        log_info("acutally is %p", f->surface);

        auto ff = f->get_main_surface();
        log_info("mf is: %p", ff);

        if (!ff)
            return;

        log_info("base is %p", ff->surface);

        if (!(focus = core->get_cursor_focus()) || !(main_surface = focus->get_main_surface())
            || !(view = core->find_view(main_surface->surface)))
            return;

//        if (!view || view->destroyed || !output->activate_plugin(grab_interface, false))
 //           return;

        output->deactivate_plugin(grab_interface);
        view->get_output()->focus_view(view);
    };

    output->add_button(0, BTN_LEFT, &callback);

    /* TODO: touch focus
    touch = [=] (weston_touch *touch, wl_fixed_t sx, wl_fixed_t sy)
    {
        core->focus_output(core->get_output_at(
                    wl_fixed_to_int(sx), wl_fixed_to_int(sy)));

        wayfire_view view;
        if (!touch->focus || !(view = core->find_view(weston_surface_get_main_surface(touch->focus->surface))))
            return;
        if (view->is_special || view->destroyed || !output->activate_plugin(grab_interface, false))
            return;

        output->deactivate_plugin(grab_interface);
        view->get_output()->focus_view(view, touch->seat);
    };

    output->add_touch(0, &touch);
    */
}

/* TODO: remove, it is no longer necessary */
void wayfire_fullscreen::init(wayfire_config *conf)
{
    grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;
    grab_interface->name = "__fs_grab";
}

void wayfire_handle_focus_parent::focus_view(wayfire_view view)
{
    last_view = view;
    view->get_output()->bring_to_front(view);
    for (auto child : view->children)
        focus_view(child);
}

void wayfire_handle_focus_parent::init(wayfire_config*)
{
    focus_event = [&] (signal_data *data)
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
