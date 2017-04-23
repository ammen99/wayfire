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

void slide_done_cb(weston_view_animation *animation, void *data);

using pair = std::pair<int, int>;
class vswitch : public wayfire_plugin_t {
    private:
        key_callback callback_left, callback_right, callback_up, callback_down;
        int duration;

        std::queue<pair>dirs; // series of moves we have to do

        bool running = false;
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
    }

    void add_direction(int dx, int dy) {
        if (!running)
            dirs.push({0, 0});

        if (dirs.size() < MAX_DIRS_IN_QUEUE)
            dirs.push({dx, dy});

        /* this is the first direction, we have pushed {0, 0} so that slide_done()
         * will do nothing on the first time */
        if (!running) {
            running = true;
            slide_done();
        }
    }

    void slide_done() {
        auto front = dirs.front();
        dirs.pop();

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        int dx = front.first, dy = front.second;

        vx += dx;
        vy += dy;
        output->workspace->set_workspace({vx, vy});

        if (dirs.size() == 0) {
            running = false;
            return;
        }

        dx = dirs.front().first, dy = dirs.front().second;
        GetTuple(vwidth, vheight, output->workspace->get_workspace_grid_size());
        if (vx + dx < 0 || vx + dx >= vwidth || vy + dy < 0 || vy + dy >= vheight) {
            dirs = std::queue<pair> ();
            running = false;
            return;
        }

        auto current_views = output->workspace->get_views_on_workspace(
                output->workspace->get_current_workspace());
        auto next_views = output->workspace->get_views_on_workspace({vx + dx, vy + dy});

        int index = 0;

        std::unordered_set<wayfire_view> views_to_move;
        for (auto view : current_views)
            views_to_move.insert(view);
        for (auto view : next_views)
            views_to_move.insert(view);

        for (auto view : views_to_move) {
            if (view->is_mapped && !view->destroyed)
            weston_move_run(view->handle, -dx * output->handle->width, -dy * output->handle->height,
                    1, 1, false, slide_done_cb, new slide_data {this, index++});
        }

        /* both workspaces are empty, so no animation, just switch */
        if (index == 0)
            slide_done();
    }
};


void timer_cb(void *data)
{
    vswitch *plugin = (vswitch*) data;
    plugin->slide_done();
}

void slide_done_cb(weston_view_animation*, void *data)
{
    auto converted = (slide_data*) data;

    if (converted->index == 0)
        converted->plugin->slide_done();

    delete converted;
}

extern "C" {
    wayfire_plugin_t* newInstance() {
        return new vswitch();
    }
}
