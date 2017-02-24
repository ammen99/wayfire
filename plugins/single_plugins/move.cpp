#include <output.hpp>

class wayfire_move : public wayfire_plugin_t {
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
            output->hook->add_hook(&hook);

            using namespace std::placeholders;
            press.type   = BindingTypePress;
            press.mod    = iniButton.mod;
            press.button = iniButton.button;
            press.action = std::bind(std::mem_fn(&Move::Initiate), this, _1, nullptr);
            output->hook->add_but(&press, true);

            release.type   = BindingTypeRelease;
            release.mod    = 0;
            release.button = iniButton.button;
            release.action = std::bind(std::mem_fn(&Move::Terminate), this, _1);
            output->hook->add_but(&release, false);
        }

        void init() {
            using namespace std::placeholders;
            options.insert(newButtonOption("activate", Button{0, 0}));

            sigScl.action = std::bind(std::mem_fn(&Move::onScaleChanged), this, _1);
            output->signal->connect_signal("screen-scale-changed", &sigScl);

            move_request.action = std::bind(std::mem_fn(&Move::on_move_request), this, _1);
            output->signal->connect_signal("move-request", &move_request);
        }

        bool called_from_expo = false;
        void Initiate(EventContext ctx, View pwin) {
            /* Do not deny request if expo is active and has requested moving a window */
            /* TODO: this is a workaround, should add caller name to signals */
            if(!(output->input->is_owner_active("expo") && pwin) && !output->input->activate_owner(owner))
                return;

            if (output->input->is_owner_active("expo")) {
                called_from_expo = true;
            } else {
                called_from_expo = false;
            }

            auto xev = ctx.xev.xbutton;
            win = (pwin == nullptr ? output->get_view_at_point(xev.x_root, xev.y_root) : pwin);

            if (!win) {
                output->input->deactivate_owner(owner);
                return;
            }

            owner->grab();

            core->focus_view(win);
            output->render->set_redraw_everything(true);

            hook.enable();
            release.enable();

            this->sx = xev.x_root;
            this->sy = xev.y_root;
        }

        void Terminate(EventContext ctx) {
            hook.disable();
            release.disable();

            output->input->deactivate_owner(owner);
            output->render->set_redraw_everything(false);
        }

        int clamp(int x, int min, int max) {
            if(x < min) return min;
            if(x > max) return max;
            return x;
        }

        void Intermediate() {
            GetTuple(cmx, cmy, output->input->get_pointer_position());
            GetTuple(sw, sh, output->get_screen_size());

            int nx = win->attrib.origin.x + (cmx - sx) * scX;
            int ny = win->attrib.origin.y + (cmy - sy) * scY;

            /* TODO: implement edge offset */

            if (called_from_expo) {
                int vx, vy;
                output->viewport->get_viewport_for_view(win, vx, vy);

                win->move(nx + sw * (win->vx - vx), ny + sh * (win->vy - vy));
                win->vx = vx;
                win->vy = vy;
            } else {
                win->move(nx, ny);
            }

            win->set_mask(output->viewport->get_mask_for_view(win));

            sx = cmx;
            sy = cmy;
        }

        void onScaleChanged(SignalListenerData data) {
            scX = *(int*)data[0];
            scY = *(int*)data[1];
        }

        void on_move_request(SignalListenerData data) {
            View v = *(View*)data[0];
            if(!v) return;

            wlc_point origin = *(wlc_point*)data[1];
            Initiate(EventContext(origin.x, origin.y, 0, 0), v);
        }
};

extern "C" {
    Plugin *newInstance() {
        return new Move();
    }
}

