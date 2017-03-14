#include <output.hpp>
#include <opengl.hpp>
#include <queue>

/* TODO: add configuration options for left, right and exit keybindings */

float clamp(float min, float x, float max) {
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

float get_scale_factor(float w, float h, float sw, float sh, float c) {
    float d = w * w + h * h;
    float sd = sw * sw + sh * sh;

    return clamp(0.8, std::sqrt((sd / d) * c), 1.3);
}

class view_switcher : public wayfire_plugin_t {

    key_callback init_binding, fast_switch_binding;

    wayfire_key next_view, prev_view, terminate;
#define MAXDIRS 4
    std::queue<int> dirs;

    bool active = false, block = false; // block is waiting to exit
    size_t index;

    Hook center, place, rotate, exit;

    int initsteps;
    int maxsteps = 20;
    int curstep = 0;

    struct {
        float offset = 0.6f;
        float angle = M_PI / 6.;
        float back = 0.3f;
    } attribs;


    public:

    void initOwnership() {
        grab_interface->name = "switcher";
        grab_interface->compatAll = false;
        grab_interface->compat.insert("screenshot");
    }

    void updateConfiguration() {
        maxsteps = options["duration"]->data.ival;
        initsteps = options["init"]->data.ival;

        using namespace std::placeholders;
        Key fast = *options["fast_switch"]->data.key;
        fast_switch_kb.key = fast.key;
        fast_switch_kb.mod = fast.mod;
        fast_switch_kb.type = BindingTypePress;
        fast_switch_kb.action = std::bind(std::mem_fn(&Switcher::fast_switch), this, _1);

        if (fast.key)
            output->hook->add_key(&fast_switch_kb, true);

        actKey = *options["activate"]->data.key;
        if(actKey.key == 0)
            return;

        init_binding.mod = actKey.mod;
        init_binding.key = actKey.key;
        init_binding.type = BindingTypePress;
        init_binding.action =
            std::bind(std::mem_fn(&Switcher::handle_key), this, _1);
        output->hook->add_key(&init_binding, true);

        forward.mod = 0;
        forward.key = XKB_KEY_Right;
        forward.type = BindingTypePress;
        forward.action = init_binding.action;
        output->hook->add_key(&forward, false);

        backward.mod = 0;
        backward.key = XKB_KEY_Left;
        backward.type = BindingTypePress;
        backward.action = init_binding.action;
        output->hook->add_key(&backward, false);

        term.mod = 0;
        term.key = XKB_KEY_Return;
        term.type = BindingTypePress;
        term.action = init_binding.action;
        output->hook->add_key(&term, false);

        center.action = std::bind(std::mem_fn(&Switcher::center_hook), this);
        place.action  = std::bind(std::mem_fn(&Switcher::place_hook), this);
        rotate.action = std::bind(std::mem_fn(&Switcher::rotate_hook), this);
        exit.action   = std::bind(std::mem_fn(&Switcher::exit_hook), this);

        output->hook->add_hook(&center);
        output->hook->add_hook(&place);
        output->hook->add_hook(&rotate);
        output->hook->add_hook(&exit);
    }

    void init() {
        options.insert(newIntOption("duration", 1000));
        options.insert(newIntOption("init", 1000));
        options.insert(newKeyOption("activate", Key{0, 0}));
        options.insert(newKeyOption("fast_switch", Key{0, 0}));
    }

    void handle_key(EventContext ctx) {
        auto xev = ctx.xev.xkey;
        if (xev.key == options["activate"]->data.key->key) {
            if (active) {
                if (rotate.getState()) {
                    if (!block) {
                        dirs.push(0),
                        block = true;
                     }
                } else {
                    terminate();
                }
            } else if (!place.getState() && !center.getState()) {
                initiate();
            }
        }

        if (xev.key == XKB_KEY_Left) {
            if (place.getState() || center.getState() || rotate.getState()) {
                if (dirs.size() < MAXDIRS)
                    dirs.push(1);
            } else {
                move(1);
            }
        }

        if (xev.key == XKB_KEY_Right) {
            if (place.getState() || center.getState() || rotate.getState()) {
                if (dirs.size() < MAXDIRS)
                dirs.push(-1);
            } else {
                move(-1);
            }
        }

        if (xev.key == XKB_KEY_Return) {
            if (active && !block) {
                dirs.push(0);
                block = true;
            } else if (active) {
                terminate();
            }
        }
    }

    struct duple {
        float start, end;
    };

    struct view_paint_attribs {
        View v;
        duple scale_x, scale_y, off_x, off_y, off_z;
        duple rot;
    };

    std::vector<view_paint_attribs> active_views;

    void initiate() {
        if (!output->input->activate_owner(owner))
            return;

        grab_interface->grab();
        active = true;

        auto view = glm::lookAt(glm::vec3(0., 0., 1.67),
                glm::vec3(0., 0., 0.),
                glm::vec3(0., 1., 0.));
        auto proj = glm::perspective(45.f, 1.f, .1f, 100.f);

        Transform::ViewProj = proj * view;

        views = output->viewport->get_windows_on_viewport(output->viewport->get_current_viewport());

        if (!views.size()) {
            grab_interface->ungrab();
            output->input->deactivate_owner(owner);
            return;
        }
        std::reverse(views.begin(), views.end());

        GetTuple(sw, sh, output->get_screen_size());

        for (auto v : views) {
            /* center of screen minus center of view */
            float cx = -(sw / 2 - (v->attrib.origin.x + v->attrib.size.w / 2.f)) / sw * 2.f;
            float cy =  (sh / 2 - (v->attrib.origin.y + v->attrib.size.h / 2.f)) / sh * 2.f;

            float scale_factor = get_scale_factor(v->attrib.size.w, v->attrib.size.h, sw, sh, 0.28888);

            active_views.push_back(
                    { .v = v,
                      .off_x = {cx, 0},
                      .off_y = {cy, 0},
                      .scale_x = {1, scale_factor},
                      .scale_y = {1, scale_factor}});
        }

        if (views.size() == 2) {
            attribs.offset = 0.4f;
            attribs.angle = M_PI / 5.;
            attribs.back = 0.;
        } else {
            attribs.offset = 0.6f;
            attribs.angle = M_PI / 6.;
            attribs.back = 0.3f;
        }

        index = 0;

        output->render->set_redraw_everything(true);
        output->render->set_renderer(0, std::bind(std::mem_fn(&Switcher::render), this));

        curstep = 0;

        center.enable();
    }

    void render_view(View v) {
        GetTuple(sw, sh, output->get_screen_size());

        int cx = sw / 2;
        int cy = sh / 2;

        wlc_geometry g;
        wlc_view_get_visible_geometry(v->get_id(), &g);

        int mx = cx - g.size.w / 2;
        int my = cy - g.size.h / 2;

        g.origin.x = mx;
        g.origin.y = my;

        render_surface(v->get_surface(), g, v->transform.compose());
    }

    void render() {
        OpenGL::useDefaultProgram();
        GL_CALL(glDisable(GL_DEPTH_TEST));

        auto bg = output->render->get_background();
        wlc_geometry g = {.origin = {0, 0}, .size = {1366, 768}};
        output->render->ctx->color = glm::vec4(0.7, 0.7, 0.7, 1.0);
        OpenGL::renderTransformedTexture(bg, g, glm::mat4(), TEXTURE_TRANSFORM_USE_COLOR);

        for(int i = active_views.size() - 1; i >= 0; i--)
            render_view(active_views[i].v);
    }

#define GetProgress(start,end,curstep,steps) ((float(end)*(curstep)+float(start) \
                                            *((steps)-(curstep)))/(steps))

    void center_hook() {
        curstep++;
        for (auto v : active_views) {
            if (curstep < initsteps) {
                v.v->transform.translation = glm::translate(glm::mat4(), glm::vec3(
                            GetProgress(v.off_x.start, v.off_x.end, curstep, initsteps),
                            GetProgress(v.off_y.start, v.off_y.end, curstep, initsteps),
                            GetProgress(v.off_z.start, v.off_z.end, curstep, initsteps)));

                v.v->transform.scalation = glm::scale(glm::mat4(), glm::vec3(
                            GetProgress(v.scale_x.start, v.scale_x.end, curstep, initsteps),
                            GetProgress(v.scale_y.start, v.scale_y.end, curstep, initsteps), 1));
            } else {
                v.v->transform.translation = glm::mat4();
            }
        }

        if(curstep == initsteps) {
            center.disable();

            if (views.size() == 1)
                return;

            place.enable();
            curstep = 0;

            active_views.clear();

            if (views.size() == 2) {
                active_views.push_back({
                        .v = views[0],
                        .off_x = {0, attribs.offset},
                        .off_y = {0, 0},
                        .off_z = {0, -attribs.back},
                        .rot= {0, -attribs.angle}});

                active_views.push_back({
                        .v = views[1],
                        .off_x = {0, -attribs.offset},
                        .off_y = {0, 0},
                        .off_z = {0, -attribs.back},
                        .rot = {0, attribs.angle}});
            } else {
                int prev = views.size() - 1;
                int next = 1;
                active_views.push_back({
                        .v = views[0],
                        .off_x = {0, 0},
                        .off_y = {0, 0},
                        .off_z = {0, 0},
                        .rot = {0, 0}});


                active_views.push_back({
                        .v = views[prev],
                        .off_x = {0, -attribs.offset},
                        .off_y = {0, 0},
                        .off_z = {0, -attribs.back},
                        .rot = {0, +attribs.angle}});

                active_views.push_back({
                        .v = views[next],
                        .off_x = {0, +attribs.offset},
                        .off_y = {0, 0},
                        .off_z = {0, -attribs.back},
                        .rot = {0, -attribs.angle}});
            }
        }
    }

    void place_hook() {
        ++curstep;
        for (auto v : active_views) {
            v.v->transform.translation = glm::translate(glm::mat4(), glm::vec3(
                        GetProgress(v.off_x.start, v.off_x.end, curstep, maxsteps),
                        0,
                        GetProgress(v.off_z.start, v.off_z.end, curstep, maxsteps)));

            v.v->transform.rotation = glm::rotate(glm::mat4(),
                    GetProgress(v.rot.start, v.rot.end, curstep, maxsteps),
                    glm::vec3(0, 1, 0));
        }

        if (curstep == maxsteps) {
            place.disable();
            forward.enable();
            backward.enable();
            active = true;
        }
    }

    void rotate_hook() {
         ++curstep;
        for (auto v : active_views) {
            v.v->transform.translation = glm::translate(glm::mat4(), glm::vec3(
                        GetProgress(v.off_x.start, v.off_x.end, curstep, maxsteps),
                        0,
                        GetProgress(v.off_z.start, v.off_z.end, curstep, maxsteps)));

            v.v->transform.rotation = glm::rotate(glm::mat4(),
                    GetProgress(v.rot.start, v.rot.end, curstep, maxsteps),
                    glm::vec3(0, 1, 0));
        }

        if (curstep == maxsteps) {
            rotate.disable();
            if (!dirs.empty()) {
                int next_dir = dirs.front();
                dirs.pop();

                if(next_dir == 0) {
                    terminate();
                } else {
                    move(next_dir);
                }
            }
        }
    }

    void move(int dir) {
        int sz = views.size();

        index    = ((index + dir) % sz + sz) % sz;
        int next = ((index + 1  ) % sz + sz) % sz;
        int prev = ((index - 1  ) % sz + sz) % sz;

        active_views.clear();
        /* only two views */
        if (next == prev) {
            active_views.push_back({
                        .v = views[index],
                        .off_x = {-attribs.offset, attribs.offset},
                        .off_z = {attribs.back, attribs.back},
                        .rot   = {attribs.angle, -attribs.angle},
                    });

            active_views.push_back({
                        .v = views[next],
                        .off_x = {attribs.offset, -attribs.offset},
                        .off_z = {attribs.back, attribs.back},
                        .rot   = {-attribs.angle, attribs.angle},
                    });
        } else {
            active_views.push_back({
                        .v = views[index],
                        .off_x = {attribs.offset * dir, 0},
                        .off_z = {-attribs.back, 0},
                        .rot   = {-attribs.angle * dir, 0},
                    });

            if (dir == 1) {
                active_views.push_back({
                            .v = views[prev],
                            .off_x = {0, -attribs.offset},
                            .off_z = {0, -attribs.back},
                            .rot   = {0, attribs.angle},
                        });

                active_views.push_back({
                            .v = views[next],
                            .off_x = {attribs.offset, attribs.offset},
                            .off_z = {-attribs.back, -attribs.back},
                            .rot   = {-attribs.angle, -attribs.angle}
                        });

            } else {
                 active_views.push_back({
                            .v = views[next],
                            .off_x = {0, attribs.offset},
                            .off_z = {0, attribs.back},
                            .rot   = {0, -attribs.angle}
                        });

                active_views.push_back({
                            .v = views[prev],
                            .off_x = {-attribs.offset, -attribs.offset},
                            .off_z = {-attribs.back, -attribs.back},
                            .rot   = {attribs.angle, attribs.angle}
                        });
            }
        }

        rotate.enable();
        curstep = 0;
    }

    void move_right() {
        if(views.size() == 1)
            return;

        move(-1);

    }

    void move_left() {
        if(views.size() == 1)
            return;

        move(1);
    }

    void exit_hook() {
        ++curstep;

        for(auto v : active_views) {
            v.v->transform.translation = glm::translate(glm::mat4(), glm::vec3(
                        GetProgress(v.off_x.start, v.off_x.end, curstep, maxsteps),
                        GetProgress(v.off_y.start, v.off_y.end, curstep, maxsteps),
                        GetProgress(v.off_z.start, v.off_z.end, curstep, maxsteps)));

            v.v->transform.rotation = glm::rotate(glm::mat4(),
                        GetProgress(v.rot.start, v.rot.end, curstep, maxsteps),
                        glm::vec3(0, 1, 0));

            v.v->transform.scalation = glm::scale(glm::mat4(), glm::vec3(
                        GetProgress(v.scale_x.start, v.scale_x.end, curstep, maxsteps),
                        GetProgress(v.scale_y.start, v.scale_y.end, curstep, maxsteps), 1));
        }


        if (curstep == maxsteps) {
             active = false;
             output->render->reset_renderer();
             output->render->set_redraw_everything(false);

             core->focus_view(views[index]);
             exit.disable();

             Transform::ViewProj = glm::mat4();

             for(auto v : views) {
                v->transform.scalation = v->transform.translation = v->transform.rotation = glm::mat4();
             }
        }
    }

    void terminate() {
        exit.enable();
        output->input->deactivate_owner(owner);
        grab_interface->ungrab();

        GetTuple(sw, sh, output->get_screen_size());

        int sz = views.size();

        if (!sz) return;

        size_t next = ((index + 1) % sz + sz) % sz;
        size_t prev = ((index - 1) % sz + sz) % sz;

        active_views.clear();

        for (size_t i = 0; i < views.size(); i++) {

            const auto& v = views[i];
            /* center of screen minus center of view */
            float cx = -(sw / 2 - (v->attrib.origin.x + v->attrib.size.w / 2.f)) / sw * 2.f;
            float cy =  (sh / 2 - (v->attrib.origin.y + v->attrib.size.h / 2.f)) / sh * 2.f;

            float scale_factor = get_scale_factor(v->attrib.size.w, v->attrib.size.h, sw, sh, 0.28888);

            if (i != next && i != prev && prev != next) {
                view_paint_attribs attr = { .v = v,
                          .off_x = {0, cx},
                          .off_y = {0, cy},
                          .scale_x = {scale_factor, 1},
                          .scale_y = {scale_factor, 1},
                          .rot     = {0, 0}};

                if (i == index) {
                    active_views.insert(active_views.begin(), attr);
                } else {
                    active_views.push_back(attr);
                }
            } else if ((prev != next && prev == i) || (prev == next && i == prev)) {
                active_views.push_back(
                        { .v = v,
                          .off_x = {-attribs.offset, cx},
                          .off_y = {0, cy},
                          .scale_x = {scale_factor, 1},
                          .scale_y = {scale_factor, 1},
                          .rot     = {attribs.angle, 0}});
            } else if ((prev != next && next == i) || (prev == next && i == index)) {
                active_views.insert(active_views.begin(),
                        { .v = v,
                          .off_x = {attribs.offset, cx},
                          .off_y = {0, cy},
                          .scale_x = {scale_factor, 1},
                          .scale_y = {scale_factor, 1},
                          .rot     = {-attribs.angle, 0}});
            }
        }

        curstep = 0;

        backward.disable();
        forward.disable();
        term.disable();
    }

    void fast_switch(EventContext ctx) {
        if (!active && !exit.getState()) {
            if (!output->input->activate_owner(owner))
                return;

            auto views = output->viewport->get_windows_on_viewport(output->viewport->get_current_viewport());
            if (views.size() >= 2)
                core->focus_view(views[views.size() - 2]);

            output->input->deactivate_owner(owner);
        }
    }
};

extern "C" {
    Plugin *newInstance() {
        return new Switcher();
    }
}
