#include <output.hpp>
#include <core.hpp>
#include <queue>
#include <linux/input.h>
#include <utility>

#define MAX_DIRS_IN_QUEUE 4

class vswitch;
struct slide_data {
    vswitch* plugin;
    int index;
};

using pair = std::pair<int, int>;
class vswitch : public wayfire_plugin_t {
    private:
        key_callback callback_left, callback_right, callback_up, callback_down;

        std::queue<pair>dirs; // series of moves we have to do
        int current_step = 0, max_step;
        bool running = false;
        effect_hook_t hook;
    public:

    void init(wayfire_config *config) {
        grab_interface->name = "vswitch";
        grab_interface->compatAll = false;
        grab_interface->compat.insert("move");

        callback_left = [=] (weston_keyboard*, uint32_t) {
            add_direction(-1, 0);
        };
        callback_right = [=] (weston_keyboard*, uint32_t) {
            add_direction(1, 0);
        };
        callback_up = [=] (weston_keyboard*, uint32_t) {
            add_direction(0, -1);
        };
        callback_down = [=] (weston_keyboard*, uint32_t) {
            add_direction(0, 1);
        };

        auto section   = config->get_section("vswitch");
        auto key_left  = section->get_key("binding_left",  {MODIFIER_SUPER, KEY_LEFT});
        auto key_right = section->get_key("binding_right", {MODIFIER_SUPER, KEY_RIGHT});
        auto key_up    = section->get_key("binding_up",    {MODIFIER_SUPER, KEY_UP});
        auto key_down  = section->get_key("binding_down",  {MODIFIER_SUPER, KEY_DOWN});

        core->input->add_key(key_left.mod,  key_left.keyval,  &callback_left, output);
        core->input->add_key(key_right.mod, key_right.keyval, &callback_right, output);
        core->input->add_key(key_up.mod,    key_up.keyval,    &callback_up, output);
        core->input->add_key(key_down.mod,  key_down.keyval,  &callback_down, output);

        max_step = section->get_duration("duration", 15);
        hook = std::bind(std::mem_fn(&vswitch::slide_update), this);
    }

    void add_direction(int dx, int dy) {
        if (!running)
            dirs.push({0, 0});

        if (dirs.size() < MAX_DIRS_IN_QUEUE)
            dirs.push({dx, dy});

        /* this is the first direction, we have pushed {0, 0} so that slide_done()
         * will do nothing on the first time */
        if (!running) {
            start_switch();
            slide_done();
        }
    }

    float sx, sy, tx, ty;

    struct animating_view {
        wayfire_view v;
        int ox, oy;
    };
    std::vector<animating_view> views;

    void slide_update()
    {
        ++current_step;
        float dx = GetProgress(sx, tx, current_step, max_step);
        float dy = GetProgress(sy, ty, current_step, max_step);

        /* XXX: Possibly apply transform in custom rendering? */
        for (auto v : views)
            v.v->move(v.ox + dx, v.oy + dy);

        if (current_step == max_step)
            slide_done();
    }

    void slide_done() {
        auto front = dirs.front();
        dirs.pop();

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        int dx = front.first, dy = front.second;

        vx += dx;
        vy += dy;

        for (auto v : views)
            v.v->move(v.ox, v.oy);

        output->workspace->set_workspace({vx, vy});
        views.clear();

        if (dirs.size() == 0) {
            stop_switch();
            return;
        }

        current_step = 0;
        dx = dirs.front().first, dy = dirs.front().second;

        sx = sy = 0;
        tx = -dx * output->handle->width;
        ty = -dy * output->handle->height;

        GetTuple(vwidth, vheight, output->workspace->get_workspace_grid_size());
        if (vx + dx < 0 || vx + dx >= vwidth || vy + dy < 0 || vy + dy >= vheight) {
            stop_switch();
            return;
        }

        auto current_views = output->workspace->get_views_on_workspace(
                output->workspace->get_current_workspace());
        auto next_views = output->workspace->get_views_on_workspace({vx + dx, vy + dy});

        std::unordered_set<wayfire_view> views_to_move;
        for (auto view : current_views)
            views_to_move.insert(view);
        for (auto view : next_views)
            views_to_move.insert(view);

        for (auto view : views_to_move) {
            if (view->is_mapped && !view->destroyed)
                views.push_back({view, view->geometry.origin.x, view->geometry.origin.y});
        }

        /* both workspaces are empty, so no animation, just switch */
        if (views_to_move.empty())
            slide_done();
    }

    void start_switch()
    {
        if (!output->activate_plugin(grab_interface)) {
            dirs = std::queue<pair> ();
            return;
        }

        running = true;
        output->render->add_output_effect(&hook, nullptr);
        output->render->auto_redraw(true);
    }

    void stop_switch()
    {
        output->deactivate_plugin(grab_interface);
        dirs = std::queue<pair> ();
        running = false;
        output->render->rem_effect(&hook, nullptr);
        output->render->auto_redraw(false);
    }
};

extern "C" {
    wayfire_plugin_t* newInstance() {
        return new vswitch();
    }
}
