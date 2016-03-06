#include <wm.hpp>

class Resize : public Plugin {
    private:
        int sx, sy; // starting pointer x, y
        int cx, cy; // coordinates of the center of the window
        View win; // window we're operating on

        Button iniButton;

        SignalListener resize_request;

        uint32_t edges;

    private:
        ButtonBinding press;
        ButtonBinding release;
        Hook hook;
    public:

    void initOwnership() {
        owner->name = "resize";
        owner->compatAll = true;
    }
    void updateConfiguration(){
        iniButton = *options["activate"]->data.but;
        if(iniButton.button == 0)
            return;

        using namespace std::placeholders;
        hook.action = std::bind(std::mem_fn(&Resize::intermediate), this);
        core->add_hook(&hook);
        press.type   = BindingTypePress;
        press.mod    = iniButton.mod;
        press.button = iniButton.button;
        press.action = std::bind(std::mem_fn(&Resize::initiate), this, _1, nullptr);
        core->add_but(&press, true);


        release.type   = BindingTypeRelease;
        release.mod    = 0;
        release.button = iniButton.button;
        release.action = std::bind(std::mem_fn(&Resize::terminate), this, _1);
        core->add_but(&release, false);

        resize_request.action = std::bind(std::mem_fn(&Resize::on_resize_request), this, _1);
        core->connect_signal("resize-request", &resize_request);
    }

    void init() {
        options.insert(newButtonOption("activate", Button{0, 0}));
        using namespace std::placeholders;
    }

    void initiate(Context ctx, View pwin) {

        auto xev = ctx.xev.xbutton;

        if(!pwin) {
            auto win_at_coord = core->get_view_at_point(xev.x_root,xev.y_root);

            if(!win_at_coord) return;
            else win = win_at_coord;
        }
        else win = pwin;

        if(!core->activate_owner(owner)) {
            return;
        }

        owner->grab();

        core->focus_window(win);

        hook.enable();
        release.enable();

        sx = xev.x_root;
        sy = xev.y_root;

        const int32_t halfw = win->attrib.origin.x + win->attrib.size.w / 2;
        const int32_t halfh = win->attrib.origin.y + win->attrib.size.h / 2;

        edges = (sx < halfw ? WLC_RESIZE_EDGE_LEFT : (sx >= halfw ? WLC_RESIZE_EDGE_RIGHT : 0)) |
            (sy < halfh ? WLC_RESIZE_EDGE_TOP : (sy >= halfh ? WLC_RESIZE_EDGE_BOTTOM : 0));

        if(!edges) terminate(ctx);

        wlc_view_set_state(win->get_id(), WLC_BIT_RESIZING, true);
    }

    void terminate(Context ctx) {
        hook.disable();
        release.disable();
        core->deactivate_owner(owner);

        wlc_view_set_state(win->get_id(), WLC_BIT_RESIZING, false);
        win->set_mask(core->get_mask_for_view(win));
    }

    void intermediate() {

        GetTuple(cmx, cmy, core->get_pointer_position());

        const int32_t dx = cmx - sx;
        const int32_t dy = cmy - sy;

        if (edges) {
            const struct wlc_size min = { 10, 10 };

            struct wlc_geometry n = win->attrib;

            if (edges & WLC_RESIZE_EDGE_LEFT) n.size.w -= dx, n.origin.x += dx;
            else if (edges & WLC_RESIZE_EDGE_RIGHT) n.size.w += dx;

            if (edges & WLC_RESIZE_EDGE_TOP) n.size.h -= dy, n.origin.y += dy;
            else if (edges & WLC_RESIZE_EDGE_BOTTOM) n.size.h += dy;

            if (n.size.w < min.w) {
                n.size.w = min.w;
                n.origin.x = win->attrib.origin.x;
            }

            if (n.size.h < min.h) {
                n.origin.y = win->attrib.origin.y;
                n.size.h = min.h;
            }

            win->set_geometry(n.origin.x, n.origin.y, n.size.w, n.size.h);

        }

        sx = cmx;
        sy = cmy;
    }

    void on_resize_request(SignalListenerData data) {
        View w = *(View*)data[0];
        wlc_point point = *(wlc_point*)data[1];

        initiate(Context(point.x, point.y, 0, 0), w);
    }

};

extern "C" {
    Plugin *newInstance() {
        return new Resize();
    }
}
