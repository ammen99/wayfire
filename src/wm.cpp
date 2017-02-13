#include "wm.hpp"
#include "output.hpp"

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

void Focus::init(weston_config*) {
    focus.type = BindingTypePress;

    focus.button = BTN_LEFT;
    focus.mod = 0;
    focus.active = true;

    focus.action = [=] (EventContext ctx){
        auto xev = ctx.xev.xbutton;
        auto w = output->get_view_at_point(xev.x_root, xev.y_root);
        if (w) core->focus_view(w);
    };

    output->hook->add_but(&focus, false);
}
*/
