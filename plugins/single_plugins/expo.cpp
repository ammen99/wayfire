#include <core.hpp>
#include <opengl.hpp>

void trigger_scale_change(int scX, int scY) {
    SignalListenerData sigData;
    sigData.push_back(&scX);
    sigData.push_back(&scY);
    core->trigger_signal("screen-scale-changed", sigData);
}

class Expo : public Plugin {
    private:
        KeyBinding toggle;
        ButtonBinding press, move_release, move;

        int max_steps;

        Hook hook, move_hook;
        bool active;
        std::function<View(int, int)> save; // used to restore

        Key toggleKey;

        /* maximal viewports are 32, so these are enough for making it */
        GLuint fbuffs[32][32], textures[32][32];
    public:
    void updateConfiguration() {
        max_steps = getSteps(options["duration"]->data.ival);

        toggleKey = *options["activate"]->data.key;
        if(toggleKey.key == 0)
            return;

        using namespace std::placeholders;
        toggle.key = toggleKey.key;
        toggle.mod = toggleKey.mod;
        toggle.action = std::bind(std::mem_fn(&Expo::Toggle), this, _1);
        core->add_key(&toggle, true);

        auto move_button = *options["activate"]->data.but;
        if (move_button.button != 0) {
            std::cout << "enabling move " << std::endl;

            move.button = move_button.button;
            move.mod = move_button.mod;
            move.type = BindingTypePress;
            move.action = std::bind(std::mem_fn(&Expo::on_move), this, _1);

            core->add_but(&move, false);
//
//            move_hook.action = std::bind(std::mem_fn(&Expo::on_pointer_moved), this);
//            core->add_hook(&move_hook);
//            move_hook.disable();
//
//            move_release.button = move_button.button;
//            move_release.mod = 0;
//            move_release.type = BindingTypeRelease;
//            move_release.action = std::bind(std::mem_fn(&Expo::on_button_release), this, _1);
//
//            core->add_but(&move_release, false);
        }

//        release.action =
//            std::bind(std::mem_fn(&Expo::buttonRelease), this, _1);
//        release.type = BindingTypeRelease;
//        release.mod = AnyModifier;
//        release.button = Button1;
//        core->add_but(&release, false);
//
//        press.action =
//            std::bind(std::mem_fn(&Expo::buttonPress), this, _1);
//        press.type = BindingTypePress;
//        press.mod = Mod4Mask;
//        press.button = Button1;
//        core->add_but(&press, false);

        hook.action = std::bind(std::mem_fn(&Expo::zoom), this);
        core->add_hook(&hook);
    }

    void init() {
        options.insert(newIntOption("duration", 1000));
        options.insert(newKeyOption("activate", Key{0, 0}));
        options.insert(newButtonOption("move", Button{0, 0}));

        core->add_signal("screen-scale-changed");
        active = false;

        std::memset(fbuffs, -1, sizeof(fbuffs));
        std::memset(textures, -1, sizeof(textures));
    }
    void initOwnership() {
        owner->name = "expo";
        owner->compatAll = false;
    }

    struct tup {
        float begin, end;
    };

    struct {
        int steps = 0;
        tup scale_x, scale_y,
            off_x, off_y;
    } zoom_target;

    struct {
        float scale_x, scale_y,
              off_x, off_y;
    } render_params;

    void Toggle(Context ctx) {
        GetTuple(vw, vh, core->get_viewport_grid_size());
        GetTuple(vx, vy, core->get_current_viewport());

        float center_w = vw / 2.f;
        float center_h = vh / 2.f;

        if (!active) {
            if (!core->activate_owner(owner))
                return;

            core->set_renderer(0, std::bind(std::mem_fn(&Expo::render), this));
            move.enable();

            zoom_target.steps = 0;
            zoom_target.scale_x = {1, 1.f / vw};
            zoom_target.scale_y = {1, 1.f / vh};
            zoom_target.off_x   = {0, ((vx - center_w) * 2.f + 1.f) / vw};
            zoom_target.off_y   = {0, ((center_h - vy) * 2.f - 1.f) / vh};

        } else {
            zoom_target.steps = 0;
            zoom_target.scale_x = {1.f / vw, 1};
            zoom_target.scale_y = {1.f / vh, 1};
            zoom_target.off_x   = {((vx - center_w) * 2.f + 1.f) / vw, 0};
            zoom_target.off_y   = {((center_h - vy) * 2.f - 1.f) / vh, 0};
        }

        active = !active;
        hook.enable();
    }


#define GetProgress(start,end,curstep,steps) ((float(end)*(curstep)+float(start) \
                                            *((steps)-(curstep)))/(steps))
    void zoom() {
        if(zoom_target.steps == max_steps) {
            hook.disable();
            if (!active) {

                core->set_redraw_everything(false);
                core->reset_renderer();
                core->deactivate_owner(owner);
                move.disable();
                trigger_scale_change(1, 1);

            } else {
                GetTuple(vw, vh, core->get_viewport_grid_size());
                trigger_scale_change(vw, vh);
            }

            render_params.scale_x = zoom_target.scale_x.end;
            render_params.scale_y = zoom_target.scale_y.end;
            render_params.off_x   = zoom_target.off_x.end;
            render_params.off_y   = zoom_target.off_y.end;
        } else {
            render_params.scale_x = GetProgress(zoom_target.scale_x.begin,
                    zoom_target.scale_x.end, zoom_target.steps, max_steps);
            render_params.scale_y = GetProgress(zoom_target.scale_y.begin,
                    zoom_target.scale_y.end, zoom_target.steps, max_steps);
            render_params.off_x = GetProgress(zoom_target.off_x.begin,
                    zoom_target.off_x.end, zoom_target.steps, max_steps);
            render_params.off_y = GetProgress(zoom_target.off_y.begin,
                    zoom_target.off_y.end, zoom_target.steps, max_steps);
            ++zoom_target.steps;
        }
    }

    void render() {
        GetTuple(vw, vh, core->get_viewport_grid_size());
        GetTuple(vx, vy, core->get_current_viewport());
        GetTuple(w,  h,  core->getScreenSize());

        auto matrix = glm::translate(glm::mat4(), glm::vec3(render_params.off_x, render_params.off_y, 0));
        matrix = glm::scale(matrix, glm::vec3(render_params.scale_x, render_params.scale_y, 1));

        OpenGL::useDefaultProgram();
        //OpenGL::set_transform(matrix);

        for(int i = 0; i < vw; i++) {
            for(int j = 0; j < vh; j++) {
                core->texture_from_viewport(std::make_tuple(i, j),
                        fbuffs[i][j], textures[i][j]);

                wlc_geometry g = {
                    .origin = {(i - vx) * w, (j - vy) * h},
                    .size = {(uint32_t) w, (uint32_t) h}};

                OpenGL::renderTransformedTexture(textures[i][j], g, matrix, TEXTURE_TRANSFORM_INVERT_Y);
            }
        }
    }


    View moving_view = nullptr;
    int32_t start_x, start_y;

    void on_move(Context ctx) {
        std::cout << "on move" << std::endl;
        moving_view = find_view_at_point(ctx.xev.xbutton.x_root, ctx.xev.xbutton.y_root);

        if (!moving_view)
            return;

        SignalListenerData data;
        data.push_back(&moving_view);
        wlc_point p = {ctx.xev.xbutton.x_root, ctx.xev.xbutton.y_root};
        data.push_back(&p);
        core->trigger_signal("move-request", data);
//        move_hook.enable();
//        move_release.enable();
//
//        start_x = ctx.xev.xbutton.x_root;
//        start_y = ctx.xev.xbutton.y_root;
    }

    void on_pointer_moved() {
        GetTuple(cmx, cmy, core->get_pointer_position());
        GetTuple(vw, vh, core->get_viewport_grid_size());

        int32_t dx = (cmx - start_x) * vw;
        int32_t dy = (cmy - start_y) * vh;

        moving_view->move(dx + moving_view->attrib.origin.x, dy + moving_view->attrib.origin.y);
    }

    void on_button_release(Context ctx) {
        move_release.disable();
        move_hook.disable();
        moving_view = nullptr;
    }

    View find_view_at_point(int px, int py) {
        GetTuple(w, h, core->getScreenSize());
        GetTuple(vw, vh, core->get_viewport_grid_size());
        GetTuple(cvx, cvy, core->get_current_viewport());

        int vpw = w / vw;
        int vph = h / vh;

        int vx = px / vpw;
        int vy = py / vph;
        int x =  px % vpw;
        int y =  py % vph;

        int realx = (vx - cvx) * w + x * vw;
        int realy = (vy - cvy) * h + y * vh;

        std::cout << realx << " " << realy << std::endl;

        return core->get_view_at_point(realx, realy);
    }
};

extern "C" {
    Plugin *newInstance() {
        return new Expo();
    }
}
