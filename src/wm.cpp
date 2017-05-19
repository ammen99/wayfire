#include "wm.hpp"
#include "output.hpp"
#include <linux/input.h>

void wayfire_exit::init(wayfire_config*)
{
    key = [](weston_keyboard *kbd, uint32_t key) {
        weston_compositor_shutdown(core->ec);
    };

    core->input->add_key(MODIFIER_SUPER, KEY_ESC, &key, output);
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
    callback = [ = ] (weston_pointer * ptr, uint32_t button) {
        if (!ptr->focus)
            return;

        auto surf = weston_surface_get_main_surface(ptr->focus->surface);
        weston_desktop_surface *ds;
        wayfire_view view;
        if ((ds = weston_surface_get_desktop_surface(surf)) && (view = core->find_view(ds)) && !view->destroyed) {
            view->output->focus_view(view, ptr->seat);
    };
    core->input->add_button((weston_keyboard_modifier)0, BTN_LEFT, &callback, output);
}
