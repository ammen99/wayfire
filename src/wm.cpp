#include "wm.hpp"
#include "output.hpp"
#include <linux/input.h>

/*
void Exit::init(weston_config*) {
    output->input->add_key()
    exit->action = [](EventContext ctx){
        wlc_terminate();
    };

    exit->mod = WLC_BIT_MOD_CTRL | WLC_BIT_MOD_ALT;
    exit->key = XKB_KEY_q;
    output->hook->add_key(exit, true);
}

//TODO: implement refresh using wlc
void Refresh::init(weston_config*) {
    ref.key = XKB_KEY_r;
    ref.type = BindingTypePress;
    ref.mod = WLC_BIT_MOD_CTRL | WLC_BIT_MOD_ALT;
    ref.action = [] (EventContext ctx) {
        //core->terminate = true;
        //core->mainrestart = true;
    };
    output->hook->add_key(&ref, true);
}

void Close::init(weston_config*) {
    KeyBinding *close = new KeyBinding();
    close->mod = WLC_BIT_MOD_ALT;
    close->type = BindingTypePress;
    close->key = XKB_KEY_F4;
    close->action = [=](EventContext ctx) {
        core->close_view(output->get_active_view());
    };
    output->hook->add_key(close, true);
}
*/

void wayfire_focus::init(weston_config* config) {
    callback = new button_callback();
    /*
    *callback = [=] (weston_pointer *ptr, uint32_t button) {
        auto surf = weston_surface_get_main_surface(ptr->focus->surface);
        weston_view_activate(surf->view)


    };
    output->input->add_button((weston_keyboard_modifier)0, BTN_LEFT, [=] (uint32_t key) {
    });
    focus.action = [=] (EventContext ctx){
        auto xev = ctx.xev.xbutton;
        auto w = output->get_view_at_point(xev.x_root, xev.y_root);
        if (w) core->focus_view(w);
    };

    output->hook->add_but(&focus, false);
    */
}
