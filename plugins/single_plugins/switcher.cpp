#include <output.hpp>
#include <opengl.hpp>
#include <queue>
#include <core.hpp>
#include <linux/input-event-codes.h>
#include <algorithm>
#include "../../shared/config.hpp"

/* A general TODO: make the code much more organized */

struct duple {
    float start, end;
};

struct view_paint_attribs {
    wayfire_view view;
    duple scale_x, scale_y, off_x, off_y, off_z;
    duple rot;
};

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
    wayfire_key activate_key, fast_switch_key;

#define MAXDIRS 4
    std::queue<int> dirs;

    struct {
        bool active = false;
        bool in_center = false;
        bool in_place = false;
        bool in_switch = false;
        bool in_terminate = false;

        /* the following are needed for fast switching, for ex.
         * if the user presses alt-tab(assuming this is our binding)
         * and then presses tab several times, holding alt, we assume
         * he/she wants to switch between windows, so we track if this is the case */
        bool in_continuous_switch = false;
        bool in_fast_switch = false;
        bool first_key = false;
    } state;

    size_t index;

    int max_steps, current_step, initial_animation_steps;

    struct {
        float offset = 0.6f;
        float angle = M_PI / 6.;
        float back = 0.3f;
    } attribs;

    render_hook_t renderer;

    std::vector<wayfire_view> views; // all views on current viewport
    std::vector<view_paint_attribs> active_views; // views that are rendered

    public:

    void init(wayfire_config *config) {
        grab_interface->name = "switcher";
        grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;

        auto section = config->get_section("switcher");

        fast_switch_key = section->get_key("fast_switch", {MODIFIER_ALT, KEY_ESC});
        fast_switch_binding = [=] (weston_keyboard *kbd, uint32_t key) {
            fast_switch();
        };

        output->add_key(fast_switch_key.mod, fast_switch_key.keyval, &fast_switch_binding);

        max_steps = section->get_duration("duration", 30);
        initial_animation_steps = section->get_duration("initial_animation", 5);;

        activate_key = section->get_key("activate", {MODIFIER_ALT, KEY_TAB});

        init_binding = [=] (weston_keyboard *, uint32_t) {
            if (!state.active)
                activate();
            else {
                if (state.in_switch || state.in_place || state.in_center) {
                    dirs.push(0);
                } else {
                    start_exit();
                }
            }
        };

        output->add_key(activate_key.mod, activate_key.keyval, &init_binding);

        using namespace std::placeholders;
        grab_interface->callbacks.keyboard.key = std::bind(std::mem_fn(&view_switcher::handle_key),
                this, _1, _2, _3);

        grab_interface->callbacks.keyboard.mod = std::bind(std::mem_fn(&view_switcher::handle_mod),
                this, _1, _2, _3, _4, _5);

        next_view = section->get_key("next", {0, KEY_RIGHT});
        prev_view = section->get_key("prev", {0, KEY_LEFT});
        terminate = section->get_key("exit", {0, KEY_ENTER});

        renderer = std::bind(std::mem_fn(&view_switcher::render), this);
    }

    void update_views()
    {
        views = output->workspace->get_views_on_workspace(output->workspace->get_current_workspace());
        //std::reverse(views.begin(), views.end());
    }

    void activate()
    {
        if (!output->activate_plugin(grab_interface))
            return;

        update_views();
        if (!views.size()) {
            output->deactivate_plugin(grab_interface);
            return;
        }

        state.active = true;
        state.in_center = true;
        state.first_key = true;

        grab_interface->grab();
        output->focus_view(nullptr, core->get_current_seat());

        output->render->auto_redraw(true);
        output->render->set_renderer(renderer);
        weston_output_schedule_repaint(output->handle);

        auto view = glm::lookAt(glm::vec3(0., 0.,
                    1. * output->render->ctx->device_width / output->render->ctx->device_height),
                glm::vec3(0., 0., 0.),
                glm::vec3(0., 1., 0.));
        auto proj = glm::perspective(45.f, 1.f, .1f, 100.f);

        float angle;
        switch(output->get_transform()) {
            case WL_OUTPUT_TRANSFORM_NORMAL:
                angle = 0;
                break;
            case WL_OUTPUT_TRANSFORM_90:
                angle = 3 * M_PI / 2;
                break;
            case WL_OUTPUT_TRANSFORM_180:
                angle = M_PI;
                break;
            case WL_OUTPUT_TRANSFORM_270:
                angle = M_PI / 2;
            default:
                angle = 0;
                break;
        }
        auto rot = glm::rotate(glm::mat4(), angle, glm::vec3(0.0, 0.0, 1.0));

        wayfire_view_transform::global_view_projection = proj * view * rot;

        GetTuple(sw, sh, output->get_screen_size());

        active_views.clear();
        for (auto v : views) {
            /* center of screen minus center of view */
            float cx = -(sw / 2 - (v->geometry.x + v->geometry.width / 2.f)) / sw * 2.f;
            float cy =  (sh / 2 - (v->geometry.y + v->geometry.height / 2.f)) / sh * 2.f;

            float scale_factor = get_scale_factor(v->geometry.width, v->geometry.height, sw, sh, 0.28888);

            view_paint_attribs elem;
            elem.view = v;
            elem.off_x = {cx, 0};
            elem.off_y = {cy, 0};
            elem.scale_x = {1, scale_factor};
            elem.scale_y = {1, scale_factor};
            active_views.push_back(elem);
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
        current_step = 0;

        auto bg = output->workspace->get_background_view();
        if (bg) {
            bg->transform.translation = glm::translate(glm::mat4(),
                    glm::vec3(0, 0, -9));
            bg->transform.scale = glm::scale(glm::mat4(),
                    glm::vec3(6, 6, 1));
        }
    }

    void render_view(wayfire_view v)
    {
        GetTuple(sw, sh, output->get_screen_size());

        int cx = sw / 2;
        int cy = sh / 2;


        auto compositor_geometry = v->geometry;
        v->geometry.x = cx - compositor_geometry.width / 2;
        v->geometry.y = cy - compositor_geometry.height / 2;
        v->render(TEXTURE_TRANSFORM_USE_DEVCOORD);
        v->geometry = compositor_geometry;
    }

    void render()
    {
        OpenGL::use_default_program();

        GL_CALL(glEnable(GL_DEPTH_TEST));
        GL_CALL(glDepthFunc(GL_LESS));

        GL_CALL(glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT));

        auto bg = output->workspace->get_background_view();
        if (bg) {
            bg->transform.color = glm::vec4(0.7, 0.7, 0.7, 1.0);
            bg->render(TEXTURE_TRANSFORM_USE_COLOR | TEXTURE_TRANSFORM_USE_DEVCOORD);
        }

        if (state.in_center)
            update_center();
        else if (state.in_place)
            update_place();
        else if (state.in_switch)
            update_rotate();
        else if (state.in_terminate)
            update_exit();

        for(int i = active_views.size() - 1; i >= 0; i--)
            render_view(active_views[i].view);

        GL_CALL(glDisable(GL_DEPTH_TEST));

        if (!state.active)
            finish_exit();
    }

    void stop_continuous_switch(weston_keyboard *kbd, uint32_t depressed, uint32_t locked,
            uint32_t latched, uint32_t group)
    {

        weston_keyboard_send_modifiers(kbd, wl_display_get_serial(core->ec->wl_display),
                depressed, locked, latched, group);
        state.in_continuous_switch = false;
        if (state.in_fast_switch) {
            fast_switch_terminate();
        } else {
            if (state.in_place || state.in_center || state.in_switch) {
                dirs.push(0);
            } else {
                start_exit();
            }
        }
    }

    void handle_mod(weston_keyboard *kbd, uint32_t depressed, uint32_t locked,
            uint32_t latched, uint32_t group)
    {
        bool mod_released = (depressed & activate_key.mod) == 0;
        bool fast_mod_released = (depressed & fast_switch_key.mod) == 0;

        if ((mod_released && state.in_continuous_switch) ||
                (fast_mod_released && state.in_fast_switch))
            stop_continuous_switch(kbd, depressed, locked, latched, group);
        else if (mod_released)
            state.first_key = false;
    }

    /* either queue the next direction if we are currently switching/animating,
     * otherwise simply start animating */
    void enqueue (int dx)
    {
        if (state.in_center || state.in_place || state.in_switch) {
            if (dirs.size() < MAXDIRS)
                dirs.push(dx);
        } else if (!state.in_terminate) {
            start_move(dx);
        }
    }

    void handle_key(weston_keyboard *kbd, uint32_t key, uint32_t kstate) {

        if (kstate != WL_KEYBOARD_KEY_STATE_PRESSED)
            return;

#define fast_switch_on (state.in_fast_switch && key == fast_switch_key.keyval)
        if (state.first_key && (key == activate_key.keyval || fast_switch_on)) {
            state.in_continuous_switch = true;
        }

        state.first_key = false;

        if (key == activate_key.keyval && state.in_continuous_switch) {
            enqueue(1);
            return;
        }

        if (fast_switch_on && state.in_continuous_switch) {
            fast_switch_next();
            return;
        }

        if (state.active && (key == terminate.keyval || key == activate_key.keyval)) {
            if (state.in_center || state.in_place || state.in_switch) {
                dirs.push(0);
            } else {
                start_exit();
            }
        }

        if (key == prev_view.keyval || key == next_view.keyval) {
            int dx = (key == prev_view.keyval ? -1 : 1);
            enqueue(dx);
        }
    }

    void update_center()
    {
        ++current_step;
        for (auto v : active_views) {
            if (current_step < initial_animation_steps) {
                v.view->transform.translation = glm::translate(glm::mat4(), glm::vec3(
                            GetProgress(v.off_x.start, v.off_x.end, current_step, initial_animation_steps),
                            GetProgress(v.off_y.start, v.off_y.end, current_step, initial_animation_steps),
                            GetProgress(v.off_z.start, v.off_z.end, current_step, initial_animation_steps)));

                v.view->transform.scale = glm::scale(glm::mat4(), glm::vec3(
                            GetProgress(v.scale_x.start, v.scale_x.end, current_step, initial_animation_steps),
                            GetProgress(v.scale_y.start, v.scale_y.end, current_step, initial_animation_steps),
                            1));
            } else {
                v.view->transform.translation = glm::mat4();
            }
        }

        if(current_step == initial_animation_steps) {
            state.in_center = false;

            if (active_views.size() == 1)
                return;

            start_place();
        }
    }

    void start_place() {
        state.in_place = true;
        current_step = 0;

        active_views.clear();
        //update_views();

        if (views.size() == 2) {
            view_paint_attribs elem;
            elem.view = views[0];
            elem.off_x = {0, attribs.offset};
            elem.off_y = {0, 0};
            elem.off_z = {0, -attribs.back};
            elem.rot= {0, -attribs.angle};

            active_views.push_back(elem);
            elem.view = views[1];
            elem.off_x = {0, -attribs.offset};
            elem.off_y = {0, 0};
            elem.off_z = {0, -attribs.back};
            elem.rot = {0, attribs.angle};
            active_views.push_back(elem);
        } else {
            int prev = views.size() - 1;
            int next = 1;

            view_paint_attribs elem;
            elem.view = views[0];
            elem.off_x = {0, 0};
            elem.off_y = {0, 0};
            elem.off_z = {0, 0};
            elem.rot = {0, 0};
            active_views.push_back(elem);

            elem.view = views[prev];
            elem.off_x = {0, -attribs.offset};
            elem.off_y = {0, 0};
            elem.off_z = {0, -attribs.back};
            elem.rot = {0, +attribs.angle};
            active_views.push_back(elem);

            elem.view = views[next];
            elem.off_x = {0, +attribs.offset};
            elem.off_y = {0, 0};
            elem.off_z = {0, -attribs.back};
            elem.rot = {0, -attribs.angle};
            active_views.push_back(elem);
        }
    }

    void update_place() {
        ++current_step;
        for (auto v : active_views) {
            v.view->transform.translation = glm::translate(glm::mat4(), glm::vec3(
                        GetProgress(v.off_x.start, v.off_x.end, current_step, max_steps),
                        0,
                        GetProgress(v.off_z.start, v.off_z.end, current_step,  max_steps)));

            v.view->transform.rotation = glm::rotate(glm::mat4(),
                    GetProgress(v.rot.start, v.rot.end, current_step, max_steps),
                    glm::vec3(0, 1, 0));
        }

        if (current_step == max_steps) {
            state.in_place = false;

            if (!dirs.empty()) {
                int next_dir = dirs.front();
                dirs.pop();

                if(next_dir == 0) {
                    start_exit();
                } else {
                    start_move(next_dir);
                }
            }
        }
    }

    void update_rotate() {
        ++current_step;
        for (auto v : active_views) {
            v.view->transform.translation = glm::translate(glm::mat4(), glm::vec3(
                        GetProgress(v.off_x.start, v.off_x.end, current_step, max_steps),
                        0,
                        GetProgress(v.off_z.start, v.off_z.end, current_step, max_steps)));

            v.view->transform.rotation = glm::rotate(glm::mat4(),
                    GetProgress(v.rot.start, v.rot.end, current_step, max_steps),
                    glm::vec3(0, 1, 0));
        }

        if (current_step == max_steps) {
            state.in_switch = false;
            if (!dirs.empty()) {
                int next_dir = dirs.front();
                dirs.pop();

                if(next_dir == 0) {
                    start_exit();
                } else {
                    start_move(next_dir);
                }
            }
        }
    }

    void start_move(int dir) {
        //update_views();
        int sz = views.size();

        /* TODO: whap happens if view gets destroyed? */
        index    = (index + dir + sz) % sz;
        int next = (index + 1) % sz;
        int prev = (index - 1 + sz) % sz;

        active_views.clear();
        /* only two views */

        view_paint_attribs elem;
        if (next == prev) {
            elem.view = views[index],
            elem.off_x = {-attribs.offset, attribs.offset};
            elem.off_z = {attribs.back, attribs.back};
            elem.rot   = {attribs.angle, -attribs.angle};
            active_views.push_back(elem);

            elem.view = views[next],
            elem.off_x = {attribs.offset, -attribs.offset};
            elem.off_z = {attribs.back, attribs.back};
            elem.rot   = {-attribs.angle, attribs.angle};
            active_views.push_back(elem);
        } else {
            elem.view = views[index];
            elem.off_x = {attribs.offset * dir, 0};
            elem.off_z = {-attribs.back, 0};
            elem.rot   = {-attribs.angle * dir, 0};
            active_views.push_back(elem);

            if (dir == 1) {
                elem.view = views[prev];
                elem.off_x = {0, -attribs.offset};
                elem.off_z = {0, -attribs.back};
                elem.rot   = {0, attribs.angle};
                active_views.push_back(elem);

                elem.view = views[next];
                elem.off_x = {attribs.offset, attribs.offset};
                elem.off_z = {-attribs.back, -attribs.back};
                elem.rot   = {-attribs.angle, -attribs.angle};
                active_views.push_back(elem);

            } else {
                elem.view = views[next];
                elem.off_x = {0, attribs.offset};
                elem.off_z = {0, attribs.back};
                elem.rot   = {0, -attribs.angle};
                active_views.push_back(elem);

                elem.view = views[prev];
                elem.off_x = {-attribs.offset, -attribs.offset};
                elem.off_z = {-attribs.back, -attribs.back};
                elem.rot   = {attribs.angle, attribs.angle};
                active_views.push_back(elem);
            }
        }

        current_step = 0;
        state.in_switch = true;
    }

    void update_exit() {
        ++current_step;

        for(auto v : active_views) {
            v.view->transform.translation = glm::translate(glm::mat4(), glm::vec3(
                        GetProgress(v.off_x.start, v.off_x.end, current_step, max_steps),
                        GetProgress(v.off_y.start, v.off_y.end, current_step, max_steps),
                        GetProgress(v.off_z.start, v.off_z.end, current_step, max_steps)));

            v.view->transform.rotation = glm::rotate(glm::mat4(),
                        GetProgress(v.rot.start, v.rot.end, current_step, max_steps),
                        glm::vec3(0, 1, 0));

            v.view->transform.scale = glm::scale(glm::mat4(), glm::vec3(
                        GetProgress(v.scale_x.start, v.scale_x.end, current_step, max_steps),
                        GetProgress(v.scale_y.start, v.scale_y.end, current_step, max_steps), 1));
        }

        if (current_step == max_steps) {
            state.in_terminate = false;
            state.active = false;
        }
    }

    void finish_exit() {
        output->render->auto_redraw(false);
        output->render->reset_renderer();
        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);

        auto bg = output->workspace->get_background_view();
        if (bg) {
            bg->transform.color = glm::vec4(1);
            bg->transform.translation = glm::mat4();
            bg->transform.scale = glm::mat4();
        }

        wayfire_view_transform::global_view_projection = glm::mat4();

        //update_views();
        for(auto v : views)
            v->transform.scale = v->transform.translation = v->transform.rotation = glm::mat4();
    }

    void start_exit() {
        state.in_terminate = true;

        GetTuple(sw, sh, output->get_screen_size());

        int sz = views.size();

        if (!sz) return;

        output->focus_view(views[index], core->get_current_seat());
        size_t next = (index + 1) % sz;
        size_t prev = (index - 1 + sz) % sz;

        active_views.clear();

        view_paint_attribs elem;
        for (size_t i = 0; i < views.size(); i++) {

            const auto& v = views[i];
            /* center of screen minus center of view */
            float cx = -(sw / 2 - (v->geometry.x + v->geometry.width / 2.f)) / sw * 2.f;
            float cy =  (sh / 2 - (v->geometry.y + v->geometry.height / 2.f)) / sh * 2.f;

            float scale_factor = get_scale_factor(v->geometry.width, v->geometry.height, sw, sh, 0.28888);

            if (i != next && i != prev && prev != next) {
                elem.view = v;
                elem.off_x = {0, cx};
                elem.off_y = {0, cy};
                elem.scale_x = {scale_factor, 1};
                elem.scale_y = {scale_factor, 1};
                elem.rot     = {0, 0};

                if (i == index) {
                    active_views.insert(active_views.begin(), elem);
                } else {
                    active_views.push_back(elem);
                }
            } else if ((prev != next && prev == i) || (prev == next && i == prev)) {
                elem.view = v;
                elem.off_x = {-attribs.offset, cx};
                elem.off_y = {0, cy};
                elem.scale_x = {scale_factor, 1};
                elem.scale_y = {scale_factor, 1};
                elem.rot     = {attribs.angle, 0};
                active_views.push_back(elem);
            } else if ((prev != next && next == i) || (prev == next && i == index)) {
                elem.view = v;
                elem.off_x = {attribs.offset, cx};
                elem.off_y = {0, cy};
                elem.scale_x = {scale_factor, 1};
                elem.scale_y = {scale_factor, 1};
                elem.rot     = {-attribs.angle, 0};
                active_views.insert(active_views.begin(), elem);
            }
        }

        current_step = 0;
    }

    void fast_switch() {
        if (!state.active) {
            if (!output->activate_plugin(grab_interface))
                return;

            update_views();
            index = 0;
            state.in_fast_switch = true;
            state.in_continuous_switch = true;
            state.active = true;
            state.first_key = false;

            for (auto view : views) {
                if (view && view->handle) {
                    view->handle->alpha = 0.5;
                    weston_surface_damage(view->surface);
                    weston_view_geometry_dirty(view->handle);
                    weston_view_update_transform(view->handle);
                }
            }

            grab_interface->grab();
            output->focus_view(nullptr, core->get_current_seat());

            if (views[0])
                views[0]->handle->alpha = 1.0;
        }
    }

    void fast_switch_terminate()
    {
        for (auto view : views)
        {
            if (view)
            {
                view->handle->alpha = 1.0;
                weston_surface_damage(view->surface);
                weston_view_geometry_dirty(view->handle);
                weston_view_update_transform(view->handle);
            }
        }
        output->focus_view(views[index], core->get_current_seat());
        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
        state.active = false;
        state.in_fast_switch = false;
    }

    void fast_switch_next()
    {
        if (views[index]) {
            views[index]->handle->alpha = 0.5;
            weston_surface_damage(views[index]->surface);
            weston_view_geometry_dirty(views[index]->handle);
            weston_view_update_transform(views[index]->handle);
        }

        index = (index + 1) % views.size();

        if (views[index]) {
            views[index]->handle->alpha = 1.0;
            weston_surface_damage(views[index]->surface);
            weston_view_geometry_dirty(views[index]->handle);
            weston_view_update_transform(views[index]->handle);
        }

        output->bring_to_front(views[index]);
    }
};

void frame_idle_callback(void *data) {
    auto switcher = (view_switcher*) data;
    weston_output_schedule_repaint(switcher->output->handle);
}

extern "C" {
    wayfire_plugin_t* newInstance() {
        return new view_switcher();
    }
}
