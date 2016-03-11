#include <core.hpp>

class Move : public Plugin {

        int sx, sy; // starting pointer x, y

        View win; // window we're operating on

        ButtonBinding press;
        ButtonBinding release;
        //Hook hook;

        SignalListener sigScl, move_request;

        int scX = 1, scY = 1;

        Hook hook;

        Button iniButton;

    public:
        void initOwnership() {
            owner->name = "move";
            owner->compatAll = true;
        }

        void updateConfiguration() {

            iniButton = *options["activate"]->data.but;
            if(iniButton.button == 0)
                return;

            hook.action = std::bind(std::mem_fn(&Move::Intermediate), this);
            core->add_hook(&hook);

            using namespace std::placeholders;
            press.type   = BindingTypePress;
            press.mod    = iniButton.mod;
            press.button = iniButton.button;
            press.action = std::bind(std::mem_fn(&Move::Initiate), this, _1, nullptr);
            core->add_but(&press, true);

            release.type   = BindingTypeRelease;
            release.mod    = 0;
            release.button = iniButton.button;
            release.action = std::bind(std::mem_fn(&Move::Terminate), this, _1);
            core->add_but(&release, false);
        }

        void init() {
            using namespace std::placeholders;
            options.insert(newButtonOption("activate", Button{0, 0}));

            sigScl.action = std::bind(std::mem_fn(&Move::onScaleChanged), this, _1);
            core->connect_signal("screen-scale-changed", &sigScl);

            move_request.action = std::bind(std::mem_fn(&Move::on_move_request), this, _1);
            core->connect_signal("move-request", &move_request);
        }

        void Initiate(Context ctx, View pwin) {
            auto xev = ctx.xev.xbutton;
            std::cout << "here" << std::endl;

            win = (pwin == nullptr ? core->get_view_at_point(xev.x_root, xev.y_root) : pwin);
            if (!win)
                return;

            /* Do not deny request if expo is active and has requested moving a window */
            if(!(core->is_owner_active("expo") && pwin) && !core->activate_owner(owner))
                return;

            std::cout << "activating" << std::endl;

            owner->grab();

            core->focus_window(win);
            core->set_redraw_everything(true);

            hook.enable();
            release.enable();

            this->sx = xev.x_root;
            this->sy = xev.y_root;
        }

        void Terminate(Context ctx) {
            hook.disable();
            release.disable();

            core->deactivate_owner(owner);
            core->set_redraw_everything(false);

            win->set_mask(core->get_mask_for_view(win));
        }

        void Intermediate() {
            GetTuple(cmx, cmy, core->get_pointer_position());

            int nx = win->attrib.origin.x + (cmx - sx) * scX;
            int ny = win->attrib.origin.y + (cmy - sy) * scY;

            std::cout << "moving " << nx << " " << ny << std::endl;
            win->move(nx, ny);

            sx = cmx;
            sy = cmy;
        }

        void onScaleChanged(SignalListenerData data) {
            scX = *(int*)data[0];
            scY = *(int*)data[1];
            std::cout << "scale is " << scX << " " << scY << std::endl;
        }

        void on_move_request(SignalListenerData data) {
            View v = *(View*)data[0];
            if(!v) return;

            wlc_point origin = *(wlc_point*)data[1];

            Initiate(Context(origin.x, origin.y, 0, 0), v);
        }
};

extern "C" {
    Plugin *newInstance() {
        return new Move();
    }
}

