#include <output.hpp>
#include <debug.hpp>
#include <opengl.hpp>
#include <signal-definitions.hpp>
#include <view.hpp>
#include <view-transform.hpp>
#include <render-manager.hpp>
#include <workspace-manager.hpp>

#include <queue>
#include <linux/input-event-codes.h>
#include <algorithm>
#include "../../shared/config.hpp"

/* TODO: possibly decouple fast-switch and regular switching, they don't have much in common these days */

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

    signal_callback_t destroyed;

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

    effect_hook_t hook;

    std::vector<wayfire_view> views; // all views on current viewport
    std::vector<view_paint_attribs> active_views; // views that are rendered

    float view_scale_config;

    public:

    void init(wayfire_config *config)
    {
        grab_interface->name = "switcher";
        grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;

        auto section = config->get_section("switcher");

        fast_switch_key = section->get_key("fast_switch", {WLR_MODIFIER_ALT, KEY_ESC});
        fast_switch_binding = [=] (uint32_t key)
        {
            if (state.active && !state.in_fast_switch)
                return;

            fast_switch();
        };

        if (fast_switch_key.keyval)
            output->add_key(fast_switch_key.mod, fast_switch_key.keyval, &fast_switch_binding);

        max_steps = section->get_duration("duration", 30);
        initial_animation_steps = section->get_duration("initial_animation", 5);;
        view_scale_config = section->get_double("view_thumbnail_size", 0.4);

        activate_key = section->get_key("activate", {WLR_MODIFIER_ALT, KEY_TAB});

        init_binding = [=] (uint32_t)
        {
            if (state.in_fast_switch)
                return;

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
                this, _1, _2);

        grab_interface->callbacks.keyboard.mod = std::bind(std::mem_fn(&view_switcher::handle_mod),
                this, _1, _2);

        next_view = section->get_key("next", {0, KEY_RIGHT});
        prev_view = section->get_key("prev", {0, KEY_LEFT});
        terminate = section->get_key("exit", {0, KEY_ENTER});

        hook = [=] () { update_animation(); };
        destroyed = [=] (signal_data *data)
        {
            cleanup_view(get_signaled_view(data));
        };
    }

    void setup_graphics()
    {
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
        if (output->is_plugin_active(grab_interface->name))
            return;
        if (!output->activate_plugin(grab_interface))
            return;

        update_views();
        update_transforms();

        if (!views.size())
        {
            output->deactivate_plugin(grab_interface);
            return;
        }

        state.active = true;
        state.mod_released = false;
        state.in_continuous_switch = false;
        state.reversed_folds = false;
        next_actions = std::queue<int>();

        grab_interface->grab();
        output->focus_view(nullptr);

        output->render->auto_redraw(true);
        output->render->damage(NULL);
        output->render->add_output_effect(&hook);

        output->connect_signal("destroy-view", &destroyed);
        output->connect_signal("detach-view", &destroyed);

        setup_graphics();
        start_fold();

        auto bg = output->workspace->get_background_view();
        if (bg) {
            GetTuple(sw, sh, output->get_screen_size());
            bg->set_transformer(std::unique_ptr<wf_3D_view> (new wf_3D_view(sw, sh)));
            auto tr = dynamic_cast<wf_3D_view*> (bg->get_transformer());
            assert(tr);

            tr->color = {0.6, 0.6, 0.6, 1.0};
            tr->translation = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -9));
            tr->scaling = glm::scale(glm::mat4(1.0f), glm::vec3(1, 1, 1));
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
        log_info("push next view %d", state.in_rotate || state.in_fold || state.in_unfold);
        if ((state.in_rotate || state.in_fold || state.in_unfold) &&
                next_actions.size() < MAX_ACTIONS)
            next_actions.push(delta);
        else
            start_rotate(delta);
    }

    void stop_continuous_switch()
    {

        state.in_continuous_switch = false;
        if (state.in_fast_switch)
        {
            fast_switch_terminate();
        } else
        {
            push_exit();
        }
    }

    void handle_mod(uint32_t mod, uint32_t st)
    {
        bool mod_released = (mod == activate_key.mod && st == WLR_KEY_RELEASED);
        bool fast_mod_released = (mod == fast_switch_key.mod && st == WLR_KEY_RELEASED);

        if ((mod_released && state.in_continuous_switch) ||
            (fast_mod_released && state.in_fast_switch))
        {
            stop_continuous_switch();
        } else if (mod_released)
        {
            state.mod_released = true;
        }
    }

    void handle_key(uint32_t key, uint32_t kstate)
    {
        log_info("handle key %u %u %u %u", key, KEY_ENTER, kstate, WLR_KEY_PRESSED);
        if (kstate != WLR_KEY_PRESSED)
            return;

        log_info("good state");

#define fast_switch_on (state.in_fast_switch && key == fast_switch_key.keyval)

        if (!state.mod_released && (key == activate_key.keyval || fast_switch_on))
        {
            log_info("continuous");
            state.in_continuous_switch = true;
        }

        if (key == activate_key.keyval && state.in_continuous_switch && !state.in_fast_switch)
        {
            log_info("nowadays");
            push_next_view(1);
            return;
        }

        if (fast_switch_on && state.in_continuous_switch)
        {
            fast_switch_next();
            return;
        }

        if (state.active && (key == terminate.keyval || key == activate_key.keyval) && !state.in_fast_switch)
            push_exit();

        if ((key == prev_view.keyval || key == next_view.keyval) && !state.in_fast_switch)
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

    void update_transforms()
    {
        GetTuple(sw, sh, output->get_screen_size());

        for (auto v : views)
        {
            auto tr = v->get_transformer();
            if (!tr || !dynamic_cast<wf_3D_view*>(tr))
                v->set_transformer(std::unique_ptr<wf_3D_view> (new wf_3D_view(sw, sh)));
        }
    }

    void view_chosen(int i)
    {
        for (int i = views.size() - 1; i >= 0; i--)
            output->bring_to_front(views[i]);

        output->focus_view(views[i]);
    }

    void cleanup_view(wayfire_view view)
    {
        size_t i = 0;
        for (; i < views.size() && views[i] != view; i++);
        if (i == views.size())
            return;

        views.erase(views.begin() + i);

        if (views.empty())
            deactivate();

        if (i <= current_view_index)
            current_view_index = (current_view_index + views.size() - 1) % views.size();

        auto it = active_views.begin();
        while(it != active_views.end())
        {
            if (it->view == view)
                it = active_views.erase(it);
            else
                ++it;
        }

        if (views.size() == 2)
            push_next_view(1);
    }

    void update_animation()
    {
        if (state.in_fold)
            update_fold();
        else if (state.in_unfold)
            update_unfold();
        else if (state.in_rotate)
            update_rotate();
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
            auto wm_geometry = v->get_wm_geometry();
            float cx = (sw / 2.0 - wm_geometry.width / 2.0f) - wm_geometry.x;
            float cy = wm_geometry.y - (sh / 2.0 - wm_geometry.height / 2.0f);

            log_info("go to %f@%f", cx, cy);

            float scale_factor = get_scale_factor(wm_geometry.width, wm_geometry.height, sw, sh, view_scale_config);

            view_paint_attribs elem;
            elem.view = v;
            elem.off_z = {0, 0};

            if (state.reversed_folds)
            {
                elem.off_x = {cx, 0};
                elem.off_y = {cy, 0};
                elem.scale_x = {scale_factor, 1};
                elem.scale_y = {scale_factor, 1};
            } else
            {
                elem.off_x = {0, cx};
                elem.off_y = {0, cy};
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
            auto tr = dynamic_cast<wf_3D_view*> (v.view->get_transformer());
            assert(tr);

            v.view->damage();
            if (v.updates & UPDATE_OFFSET)
            {
                tr->translation = glm::translate(glm::mat4(), glm::vec3(
                            GetProgress(v.off_x.start, v.off_x.end, step, maxstep),
                            GetProgress(v.off_y.start, v.off_y.end, step, maxstep),
                            GetProgress(v.off_z.start, v.off_z.end, step, maxstep)));
            }
            if (v.updates & UPDATE_SCALE)
            {
                tr->scaling = glm::scale(glm::mat4(), glm::vec3(
                            GetProgress(v.scale_x.start, v.scale_x.end, step, maxstep),
                            GetProgress(v.scale_y.start, v.scale_y.end, step, maxstep),
                            1));
            }
            if (v.updates & UPDATE_ROTATION)
            {
                tr->rotation = glm::rotate(glm::mat4(),
                        GetProgress(v.rot.start, v.rot.end, step, maxstep),
                        glm::vec3(0, 1, 0));
            }

            v.view->damage();
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

    void push_unfolded_transformed_view(wayfire_view v,
                               duple off_x, duple off_z, duple rot)
    {
        GetTuple(sw, sh, output->get_screen_size());
        auto wm_geometry = v->get_wm_geometry();

        float cx = (sw / 2.0 - wm_geometry.width / 2.0f) - wm_geometry.x;
        float cy = wm_geometry.y - (sh / 2.0 - wm_geometry.height / 2.0f);

        view_paint_attribs elem;
        elem.view = v;
        elem.off_x = {cx + off_x.start * sw / 2.0f, cx + off_x.end * sw / 2.0f};
        log_info("moved to %f@%f %fx%f", elem.off_x.start, elem.off_x.end, off_x.start, off_x.end);
        elem.off_y = {cy, cy};
        log_info("%f@%f", elem.off_y.start, elem.off_y.end);
        elem.off_z = off_z;
        elem.rot = rot;

        elem.updates = UPDATE_ROTATION | UPDATE_OFFSET;

        active_views.push_back(elem);
    }

    void start_unfold()
    {
        state.in_unfold = true;
        current_step = 0;

        active_views.clear();

        if (views.size() == 2)
        {
            push_unfolded_transformed_view(views[current_view_index],
                                           {0, attribs.offset},
                                           {0, -attribs.back},
                                           {0, -attribs.angle});

            push_unfolded_transformed_view(views[1 - current_view_index],
                                           {0, -attribs.offset},
                                           {0, -attribs.back},
                                           {0, attribs.angle});
        } else
        {
            int prev = (current_view_index + views.size() - 1) % views.size();
            int next = (current_view_index + 1) % views.size();

            view_paint_attribs elem;

            push_unfolded_transformed_view(views[current_view_index],
                                           {0, 0},
                                           {0, 0},
                                           {0, 0});

            push_unfolded_transformed_view(views[prev],
                                           {0, -attribs.offset},
                                           {0, -attribs.back},
                                           {0, +attribs.angle});

            push_unfolded_transformed_view(views[next],
                                           {0, +attribs.offset},
                                           {0, -attribs.back},
                                           {0, -attribs.angle});
        }

        for (auto& elem : active_views)
        {
            if (state.reversed_folds)
            {
                std::swap(elem.off_x.start, elem.off_x.end);
                std::swap(elem.off_z.start, elem.off_z.end);
                std::swap(elem.rot.start, elem.rot.end);
            }
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
        int sz = views.size();
        if (sz <= 1)
            return;

        state.in_rotate = true;
        current_step = 0;

        current_view_index    = (current_view_index + dir + sz) % sz;
        output->bring_to_front(views[current_view_index]);

        int next = (current_view_index + 1) % sz;
        int prev = (current_view_index - 1 + sz) % sz;

        active_views.clear();

        /* only two views */

        if (next == prev) {
            push_unfolded_transformed_view(views[current_view_index],
                                           {-attribs.offset, attribs.offset},
                                           {attribs.back, attribs.back},
                                           {attribs.angle, -attribs.angle});

            push_unfolded_transformed_view(views[next],
                                           {-attribs.offset, -attribs.offset},
                                           {attribs.back, attribs.back},
                                           {attribs.angle, attribs.angle});
        } else {
            push_unfolded_transformed_view(views[current_view_index],
                                           {attribs.offset * dir, 0},
                                           {-attribs.back, 0},
                                           {-attribs.angle * dir, 0});

            if (dir == 1) {
                push_unfolded_transformed_view(views[prev],
                                               {0, -attribs.offset},
                                               {0, -attribs.back},
                                               {0, attribs.angle});

                push_unfolded_transformed_view(views[next],
                                               {attribs.offset, attribs.offset},
                                               {-attribs.back, -attribs.back},
                                               {-attribs.angle, -attribs.angle});

            } else {
                push_unfolded_transformed_view(views[next],
                                               {0, attribs.offset},
                                               {0, -attribs.back},
                                               {0, -attribs.angle});

                push_unfolded_transformed_view(views[prev],
                                               {-attribs.offset, -attribs.offset},
                                               {-attribs.back, -attribs.back},
                                               {attribs.angle, attribs.angle});
            }
        }

        for (auto& elem : active_views)
            elem.updates = UPDATE_ROTATION | UPDATE_OFFSET;

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
        if (bg)
            bg->set_transformer(nullptr);

        log_info("reset tranforms");
        for(auto v : views)
            v->set_transformer(nullptr);

        state.active = false;
        view_chosen(current_view_index);

        output->disconnect_signal("destroy-view", &destroyed);
        output->disconnect_signal("detach-view", &destroyed);
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

            for (auto view : views) {
                if (view) {
                    view->alpha = 0.7;
                    view->damage();
                }
            }

            grab_interface->grab();
            output->focus_view(nullptr);

            fast_switch_next();
        }
    }

    void fast_switch_terminate()
    {
        for (auto view : views)
        {
            view->set_transformer(nullptr);
            if (view)
            {
                view->alpha = 1.0;
                view->damage();
            }
        }
        view_chosen(current_view_index);

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
        state.active = false;
        state.in_fast_switch = false;

        output->disconnect_signal("destroy-view", &destroyed);
        output->disconnect_signal("detach-view", &destroyed);
    }

    void fast_switch_next()
    {
#define index current_view_index
        if (views[index]) {
            views[index]->alpha = 0.7;
            views[index]->damage();
        }

        index = (index + 1) % views.size();

        if (views[index]) {
            views[index]->alpha = 1.0;
            views[index]->damage();
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
