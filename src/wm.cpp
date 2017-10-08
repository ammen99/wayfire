#include "wm.hpp"
#include "output.hpp"
#include "../shared/config.hpp"
#include <linux/input.h>

void wayfire_exit::init(wayfire_config*)
{
    key = [](weston_keyboard *kbd, uint32_t key) {
        weston_compositor_shutdown(core->ec);
    };

    output->add_key(MODIFIER_SUPER | MODIFIER_SHIFT, KEY_ESC, &key);
}

void wayfire_close::init(wayfire_config *config)
{
    auto key = config->get_section("core")->get_key("view_close", {MODIFIER_SUPER, KEY_Q});
    callback = [=] (weston_keyboard *kbd, uint32_t key) {
        auto view = output->get_top_view();
        if (view)
            core->close_view(view);
    };

    output->add_key(key.mod, key.keyval, &callback);
}

void wayfire_focus::init(wayfire_config *)
{
    grab_interface->name = "_wf_focus";
    grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;

    callback = [=] (weston_pointer * ptr, uint32_t button)
    {
        core->focus_output(core->get_output_at(
                    wl_fixed_to_int(ptr->x), wl_fixed_to_int(ptr->y)));

        wayfire_view view;
        if (!ptr->focus ||
            !(view = core->find_view(weston_surface_get_main_surface(ptr->focus->surface))))
            return;

        if (view->is_special || view->destroyed || !output->activate_plugin(grab_interface))
            return;
        output->deactivate_plugin(grab_interface);
        view->output->focus_view(view, ptr->seat);
    };

    output->add_button((weston_keyboard_modifier)0, BTN_LEFT, &callback);

    touch = [=] (weston_touch *touch, wl_fixed_t sx, wl_fixed_t sy)
    {
        core->focus_output(core->get_output_at(
                    wl_fixed_to_int(sx), wl_fixed_to_int(sy)));

        wayfire_view view;
        if (!touch->focus || !(view = core->find_view(weston_surface_get_main_surface(touch->focus->surface))))
            return;
        if (view->is_special || view->destroyed || !output->activate_plugin(grab_interface))
            return;

        output->deactivate_plugin(grab_interface);
        view->output->focus_view(view, touch->seat);
    };

    output->add_touch(0, &touch);
}
