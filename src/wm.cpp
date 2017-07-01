#include "wm.hpp"
#include "output.hpp"
#include "../shared/config.hpp"
#include <linux/input.h>

void wayfire_exit::init(wayfire_config*)
{
    key = [](weston_keyboard *kbd, uint32_t key) {
        weston_compositor_shutdown(core->ec);
    };

    core->input->add_key(MODIFIER_SUPER | MODIFIER_SHIFT, KEY_ESC, &key, output);
}

void wayfire_close::init(wayfire_config *config)
{
    auto key = config->get_section("core")->get_key("view_close", {MODIFIER_SUPER, KEY_Q});
    callback = [=] (weston_keyboard *kbd, uint32_t key) {
        auto view = output->get_top_view();
        if (view)
            core->close_view(view);
    };

    core->input->add_key(key.mod, key.keyval, &callback, output);
}

void wayfire_focus::init(wayfire_config *)
{
    grab_interface->name = "_wf_focus";
    grab_interface->compatAll = false;

    callback = [=] (weston_pointer * ptr, uint32_t button) {
        if (!ptr->focus)
            return;

        if (!output->activate_plugin(grab_interface))
            return;
        output->deactivate_plugin(grab_interface);

        auto view = output->get_view_at_point(wl_fixed_to_int(ptr->x), wl_fixed_to_int(ptr->y));
        if (view)
            view->output->focus_view(view, ptr->seat);
    };

    core->input->add_button((weston_keyboard_modifier)0, BTN_LEFT, &callback, output);
}
