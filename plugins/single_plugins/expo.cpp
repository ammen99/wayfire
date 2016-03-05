#include <core.hpp>
#include <opengl.hpp>

void triggerScaleChange(int scX, int scY) {
        SignalListenerData sigData;
        sigData.push_back(&scX);
        sigData.push_back(&scY);
        core->trigger_signal("screen-scale-changed", sigData);
}

class Expo : public Plugin {
    private:
        KeyBinding toggle;
        ButtonBinding press, release;

        int max_steps;

        Hook hook;
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
        core->add_signal("screen-scale-changed");
        active = false;

        std::memset(fbuffs, -1, sizeof(fbuffs));
        std::memset(textures, -1, sizeof(textures));
    }
    void initOwnership() {
        owner->name = "expo";
        owner->compatAll = false;
        owner->compat.insert("move");
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
            core->set_renderer(0, std::bind(std::mem_fn(&Expo::render), this));


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

    View findWindow(int px, int py) {
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

        return save(realx, realy);
    }
};

extern "C" {
    Plugin *newInstance() {
        return new Expo();
    }
}
