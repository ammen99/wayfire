#include <output.hpp>
#include <opengl.hpp>
#include <queue>
#include <core.hpp>
#include <linux/input-event-codes.h>
#include <algorithm>
#include "../../shared/config.hpp"

struct duple
{
    float start, end;
};

enum paint_attribs
{
    UPDATE_SCALE = 1,
    UPDATE_OFFSET = 2,
    UPDATE_ROTATION = 4
};

struct view_paint_attribs
{
    wayfire_view view;
    duple scale_x, scale_y, off_x, off_y, off_z;
    duple rot;

    uint32_t updates;
};

float clamp(float min, float x, float max)
{
    if (x < min) return min;
    if (x > max) return max;
    return x;
}

/* get an appropriate scaling so that a view with dimensions [w, h] takes
 * about c% of the screen with dimensions [sw, sh] and make sure that this scaling
 * won't resize the view too much */
float get_scale_factor(float w, float h, float sw, float sh, float c)
{
    float d = w * w + h * h;
    float sd = sw * sw + sh * sh;

    return clamp(0.66, std::sqrt(sd / d), 1.5) * c;
}

/* This plugin rovides abilities to switch between views.
 * There are two modes : "fast" switching and regular switching.
 * Fast switching works similarly to the alt-esc binding in Windows or GNOME
 * Regular switching provides the same, but with more "effects". Namely, it
 * runs in several stages:
 * 1. "Fold" - views are moved to the center of the screen(they might overlap)
 *    and all except the focused one are made smaller
 * 2. "Unfold" - views are moved to the left/right and rotated
 * 3. "Rotate" - views are rotated from left to right and vice versa 
 * 4. "Reverse unfold"
 * 5. "Reverse fold"
 * */

class view_switcher : public wayfire_plugin_t
{
    key_callback init_binding, fast_switch_binding;
    wayfire_key next_view, prev_view, terminate;
    wayfire_key activate_key, fast_switch_key;

#define MAX_ACTIONS 4
    std::queue<int> next_actions;

    struct
    {
        bool active = false;

        bool mod_released = false;
        bool in_fold = false;
        bool in_unfold = false;
        bool in_rotate = false;

        bool reversed_folds = false;
        bool first_press_skipped = false;

        /* the following are needed for fast switching, for ex.
         * if the user presses alt-tab(assuming this is our binding)
         * and then presses tab several times, holding alt, we assume
         * he/she wants to switch between windows, so we track if this is the case */
        bool in_continuous_switch = false;
        bool in_fast_switch = false;
    } state;

    size_t current_view_index;

    int max_steps, current_step, initial_animation_steps;

    struct
    {
        float offset = 0.6f;
        float angle = M_PI / 6.;
        float back = 0.3f;
    } attribs;

    render_hook_t renderer;

    std::vector<wayfire_view> views; // all views on current viewport
    std::vector<view_paint_attribs> active_views; // views that are rendered

    public:

    void init(wayfire_config *config)
    {
        grab_interface->name = "switcher";
        grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;

        auto section = config->get_section("switcher");

        fast_switch_key = section->get_key("fast_switch", {MODIFIER_ALT, KEY_ESC});
        fast_switch_binding = [=] (weston_keyboard *kbd, uint32_t key)
        {
            fast_switch();
        };

        if (fast_switch_key.keyval)
            output->add_key(fast_switch_key.mod, fast_switch_key.keyval, &fast_switch_binding);

        max_steps = section->get_duration("duration", 30);
        initial_animation_steps = section->get_duration("initial_animation", 5);;

        activate_key = section->get_key("activate", {MODIFIER_ALT, KEY_TAB});

        init_binding = [=] (weston_keyboard *, uint32_t)
        {
            if (!state.active)
            {
                activate();
            } else if (state.mod_released)
            {
                push_exit();
            }
        };

        if (activate_key.keyval)
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

    void setup_graphics()
    {
        auto view = glm::lookAt(glm::vec3(0., 0., 1. * output->handle->width / output->handle->height),
                glm::vec3(0., 0., 0.),
                glm::vec3(0., 1., 0.));
        auto proj = glm::perspective(45.f, 1.f, .1f, 100.f);

        wayfire_view_transform::global_view_projection = proj * view;

        if (views.size() == 2) {
            attribs.offset = 0.4f;
            attribs.angle = M_PI / 5.;
            attribs.back = 0.;
        } else {
            attribs.offset = 0.6f;
            attribs.angle = M_PI / 6.;
            attribs.back = 0.3f;
        }
    }

    void activate()
    {
        if (!output->activate_plugin(grab_interface))
            return;

        update_views();
        if (!views.size())
        {
            output->deactivate_plugin(grab_interface);
            return;
        }

        state.active = true;
        state.mod_released = false;
        state.first_press_skipped = false;
        state.in_continuous_switch = false;
        state.reversed_folds = false;

        grab_interface->grab();
        output->focus_view(nullptr, core->get_current_seat());

        output->render->auto_redraw(true);
        output->render->set_renderer(renderer);

        setup_graphics();
        start_fold();

        auto bg = output->workspace->get_background_view();
        if (bg) {
            bg->transform.translation = glm::translate(glm::mat4(),
                    glm::vec3(0, 0, -9));
            bg->transform.scale = glm::scale(glm::mat4(),
                    glm::vec3(6, 6, 1));
        }
    }

    void push_exit()
    {
        if (state.in_rotate || state.in_fold || state.in_unfold)
            next_actions.push(0);
        else
        {
            state.reversed_folds = true;
            if (views.size() >= 2)
                start_unfold();
            else
                start_fold();
        }
    }

    void push_next_view(int delta)
    {
        if ((state.in_rotate || state.in_fold || state.in_unfold) &&
                next_actions.size() < MAX_ACTIONS)
            next_actions.push(delta);
        else
            start_rotate(delta);
    }

    void stop_continuous_switch(weston_keyboard *kbd, uint32_t depressed, uint32_t locked,
            uint32_t latched, uint32_t group)
    {

        weston_keyboard_send_modifiers(kbd, wl_display_get_serial(core->ec->wl_display),
                depressed, locked, latched, group);
        state.in_continuous_switch = false;
        if (state.in_fast_switch)
        {
            fast_switch_terminate();
        } else
        {
            push_exit();
        }
    }

    void handle_mod(weston_keyboard *kbd, uint32_t depressed, uint32_t locked,
            uint32_t latched, uint32_t group)
    {
        bool mod_released = (depressed & activate_key.mod) == 0;
        bool fast_mod_released = (depressed & fast_switch_key.mod) == 0;

        if ((mod_released && state.in_continuous_switch) ||
            (fast_mod_released && state.in_fast_switch))
        {
            stop_continuous_switch(kbd, depressed, locked, latched, group);
        } else if (mod_released)
        {
            state.mod_released = true;
        }
    }

    void handle_key(weston_keyboard *kbd, uint32_t key, uint32_t kstate)
    {
        /* when we setup keyboard grab, we receive a signal for it
         * it is not necessary so we skip it, as there is no way to circumvent this */
        if ((key == activate_key.keyval || key == fast_switch_key.keyval) &&
            !state.first_press_skipped)
        {
            state.first_press_skipped = true;
            return;
        }

        if (kstate != WL_KEYBOARD_KEY_STATE_PRESSED)
            return;

#define fast_switch_on (state.in_fast_switch && key == fast_switch_key.keyval)

        if (!state.mod_released && (key == activate_key.keyval || fast_switch_on))
            state.in_continuous_switch = true;

        if (key == activate_key.keyval && state.in_continuous_switch)
        {
            push_next_view(1);
            return;
        }

        if (fast_switch_on && state.in_continuous_switch)
        {
            fast_switch_next();
            return;
        }

        if (state.active && (key == terminate.keyval || key == activate_key.keyval))
            push_exit();

        if (key == prev_view.keyval || key == next_view.keyval)
        {
            int dx = (key == prev_view.keyval ? -1 : 1);
            push_next_view(dx);
        }
    }

    void update_views()
    {
        current_view_index = 0;
        views = output->workspace->get_views_on_workspace(output->workspace->get_current_workspace());
    }

    void render_view(wayfire_view v)
    {
        GetTuple(sw, sh, output->get_screen_size());

        int cx = sw / 2;
        int cy = sh / 2;

        weston_geometry compositor_geometry = v->geometry;
        v->geometry.x = cx - compositor_geometry.width / 2;
        v->geometry.y = cy - compositor_geometry.height / 2;

        v->render(0);

        v->geometry = compositor_geometry;
    }

    void render()
    {
        OpenGL::use_default_program();

        /* folds require views to be sorted according to their rendering order,
         * not upon their z-values which aren't set yet */
        if (!state.in_fold)
        {
            GL_CALL(glEnable(GL_DEPTH_TEST));
            GL_CALL(glEnable(GL_BLEND));
        }

        GL_CALL(glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT));

        auto bg = output->workspace->get_background_view();
        if (bg)
        {
            bg->transform.color = glm::vec4(0.7, 0.7, 0.7, 1.0);
            bg->render(0);
        }
        for(int i = active_views.size() - 1; i >= 0; i--)
            render_view(active_views[i].view);

        if (state.in_fold)
            update_fold();
        else if (state.in_unfold)
            update_unfold();
        else if (state.in_rotate)
            update_rotate();

        GL_CALL(glDisable(GL_DEPTH_TEST));
        GL_CALL(glDisable(GL_BLEND));
    }

    void start_fold()
    {
        GetTuple(sw, sh, output->get_screen_size());
        active_views.clear();
        state.in_fold = true;
        current_step = 0;

        update_views();
        for (size_t i = current_view_index, cnt = 0; cnt < views.size(); ++cnt, i = (i + 1) % views.size())
        {
            const auto& v = views[i];
            /* center of screen minus center of view */
            float cx = -(sw / 2 - (v->geometry.x + v->geometry.width / 2.f)) / sw * 2.f;
            float cy =  (sh / 2 - (v->geometry.y + v->geometry.height / 2.f)) / sh * 2.f;

            float scale_factor = get_scale_factor(v->geometry.width, v->geometry.height, sw, sh, 0.28888);

            view_paint_attribs elem;
            elem.view = v;
            elem.off_z = {0, 0};

            if (state.reversed_folds)
            {
                elem.off_x = {0, cx};
                elem.off_y = {0, cy};
                elem.scale_x = {scale_factor, 1};
                elem.scale_y = {scale_factor, 1};
            } else
            {
                elem.off_x = {cx, 0};
                elem.off_y = {cy, 0};
                elem.scale_x = {1, scale_factor};
                elem.scale_y = {1, scale_factor};
            }

            elem.updates = UPDATE_OFFSET | UPDATE_SCALE;
            active_views.push_back(elem);
        }
    }

    void update_view_transforms(int step, int maxstep)
    {
        for (auto v : active_views)
        {
            if (v.updates & UPDATE_OFFSET)
            {
                v.view->transform.translation = glm::translate(glm::mat4(), glm::vec3(
                            GetProgress(v.off_x.start, v.off_x.end, step, maxstep),
                            GetProgress(v.off_y.start, v.off_y.end, step, maxstep),
                            GetProgress(v.off_z.start, v.off_z.end, step, maxstep)));
            }
            if (v.updates & UPDATE_SCALE)
            {
                v.view->transform.scale = glm::scale(glm::mat4(), glm::vec3(
                            GetProgress(v.scale_x.start, v.scale_x.end, step, maxstep),
                            GetProgress(v.scale_y.start, v.scale_y.end, step, maxstep),
                            1));
            }
            if (v.updates & UPDATE_ROTATION)
            {
                v.view->transform.rotation = glm::rotate(glm::mat4(),
                        GetProgress(v.rot.start, v.rot.end, step, maxstep),
                        glm::vec3(0, 1, 0));
            }
        }
    }

    void dequeue_next_action()
    {
        if (!next_actions.empty())
        {
            int next = next_actions.front(); next_actions.pop();

            /* we aren't in any fold, unfold or rotation,
             * so the following will call the necessary functions
             * and not push to the queue */
            assert(!state.in_fold && !state.in_unfold && !state.in_rotate);

            if (next == 0)
                push_exit();
            else
                push_next_view(next);
        }
    }

    void update_fold()
    {
        ++current_step;
        update_view_transforms(current_step, initial_animation_steps);

        if(current_step == initial_animation_steps)
        {
            for (auto &v : active_views)
                v.view->transform.translation = glm::mat4();

            state.in_fold = false;
            if (!state.reversed_folds)
            {
                if (active_views.size() == 1)
                    return;
                start_unfold();
            } else
            {
                deactivate();
            }
        }
    }

    void start_unfold()
    {
        state.in_unfold = true;
        current_step = 0;

        active_views.clear();

        if (views.size() == 2)
        {
            view_paint_attribs elem;
            elem.view = views[current_view_index];
            elem.off_x = {0, attribs.offset};
            elem.off_z = {0, -attribs.back};
            elem.rot= {0, -attribs.angle};

            active_views.push_back(elem);
            elem.view = views[1 - current_view_index];
            elem.off_x = {0, -attribs.offset};
            elem.off_z = {0, -attribs.back};
            elem.rot = {0, attribs.angle};
            active_views.push_back(elem);
        } else
        {
            int prev = (current_view_index + views.size() - 1) % views.size();
            int next = (current_view_index + 1) % views.size();

            view_paint_attribs elem;

            elem.view = views[current_view_index];
            elem.off_x = {0, 0};
            elem.off_z = {0, 0};
            elem.rot = {0, 0};
            active_views.push_back(elem);

            elem.view = views[prev];
            elem.off_x = {0, -attribs.offset};
            elem.off_z = {0, -attribs.back};
            elem.rot = {0, +attribs.angle};
            active_views.push_back(elem);

            elem.view = views[next];
            elem.off_x = {0, +attribs.offset};
            elem.off_z = {0, -attribs.back};
            elem.rot = {0, -attribs.angle};
            active_views.push_back(elem);
        }

        for (auto& elem : active_views)
        {
            elem.off_y = {0, 0};
            if (state.reversed_folds)
            {
                std::swap(elem.off_x.start, elem.off_x.end);
                std::swap(elem.off_z.start, elem.off_z.end);
                std::swap(elem.rot.start, elem.rot.end);
            }

            elem.updates = UPDATE_ROTATION | UPDATE_OFFSET;
        }
    }

    void update_unfold()
    {
        ++current_step;
        update_view_transforms(current_step, max_steps);

        if (current_step == max_steps)
        {
            state.in_unfold = false;
            if (!state.reversed_folds)
            {
                dequeue_next_action();
            } else
            {
                start_fold();
            }
        }
    }

    void start_rotate (int dir)
    {
        //update_views();
        int sz = views.size();
        if (sz <= 1)
            return;

        state.in_rotate = true;
        current_step = 0;

        /* TODO: whap happens if view gets destroyed? */
        current_view_index    = (current_view_index + dir + sz) % sz;
        output->bring_to_front(views[current_view_index]);

        int next = (current_view_index + 1) % sz;
        int prev = (current_view_index - 1 + sz) % sz;

        active_views.clear();

        /* only two views */

        view_paint_attribs elem;
        if (next == prev) {
            elem.view = views[current_view_index],
            elem.off_x = {-attribs.offset, attribs.offset};
            elem.off_z = {attribs.back, attribs.back};
            elem.rot   = {attribs.angle, -attribs.angle};
            active_views.push_back(elem);

            elem.view = views[next],
            elem.off_x = {-attribs.offset, -attribs.offset};
            elem.off_z = {attribs.back, attribs.back};
            elem.rot   = {attribs.angle, attribs.angle};
            active_views.push_back(elem);
        } else {
            elem.view = views[current_view_index];
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
                elem.off_z = {0, -attribs.back};
                elem.rot   = {0, -attribs.angle};
                active_views.push_back(elem);

                elem.view = views[prev];
                elem.off_x = {-attribs.offset, -attribs.offset};
                elem.off_z = {-attribs.back, -attribs.back};
                elem.rot   = {attribs.angle, attribs.angle};
                active_views.push_back(elem);
            }
        }

        for (auto& elem : active_views)
        {
            elem.off_y = {0, 0};
            elem.updates = UPDATE_ROTATION | UPDATE_OFFSET;
        }

        current_step = 0;
    }

    void update_rotate()
    {
        ++current_step;
        update_view_transforms(current_step, max_steps);

        if (current_step == max_steps)
        {
            state.in_rotate = false;
            dequeue_next_action();
        }
    }

    void deactivate()
    {
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

        for(auto v : views)
            v->transform.scale = v->transform.translation = v->transform.rotation = glm::mat4();

        state.active = false;
        output->focus_view(views[current_view_index], core->get_current_seat());
    }

    void fast_switch()
    {
        if (!state.active)
        {
            if (!output->activate_plugin(grab_interface))
                return;

            update_views();

            if (views.size() < 1)
            {
                output->deactivate_plugin(grab_interface);
                return;
            }

            current_view_index = 0;

            state.in_fast_switch = true;
            state.in_continuous_switch = true;
            state.active = true;
            state.mod_released = false;
            state.first_press_skipped = false;

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

            fast_switch_next();
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
        output->focus_view(views[current_view_index], core->get_current_seat());

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
        state.active = false;
        state.in_fast_switch = false;
    }

    void fast_switch_next()
    {
#define index current_view_index
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
#undef index
    }
};

extern "C" {
    wayfire_plugin_t* newInstance() {
        return new view_switcher();
    }
}
