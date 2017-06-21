#include <output.hpp>
#include <opengl.hpp>
#include <core.hpp>
#include "../../shared/config.hpp"
/* TODO: this file should be included in some header maybe(plugin.hpp) */
#include <linux/input-event-codes.h>

class wayfire_expo : public wayfire_plugin_t {
    private:
        key_callback toggle_cb, press_cb, move_cb;
        wayfire_button action_button;

        int max_steps;

        render_hook_t renderer;

        struct {
            bool active = false;
            bool moving = false;
            bool in_zoom = false;
            bool button_pressed = false;
            bool first_press_skipped = false;

            int zoom_delta = 1;
        } state;
        int target_vx, target_vy;

        std::vector<std::vector<wf_workspace_stream*>> streams;
        signal_callback_t resized_cb;

    public:
    void init(wayfire_config *config) {
        grab_interface->name = "expo";
        grab_interface->compatAll = false;
        grab_interface->compat.insert("screenshot");

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        streams.resize(vw);

        for (int i = 0; i < vw; i++) {
            for (int j = 0;j < vh; j++) {
                streams[i].push_back(new wf_workspace_stream);
                streams[i][j]->tex = streams[i][j]->fbuff = -1;
                streams[i][j]->ws = {i, j};
            }
        }

        auto section = config->get_section("expo");
        max_steps = section->get_duration("duration", 20);
        auto toggle_key = section->get_key("toggle", {MODIFIER_SUPER, KEY_E});

        if (!toggle_key.keyval || !toggle_key.mod)
            return;

        toggle_cb = [=] (weston_keyboard *kbd, uint32_t key) {
            activate();
        };

        core->input->add_key(toggle_key.mod, toggle_key.keyval, &toggle_cb, output);

        action_button = section->get_button("action", {0, BTN_LEFT});
        grab_interface->callbacks.keyboard.key =
            [=] (weston_keyboard *kbd, uint32_t key, uint32_t st) {
                /* TODO: check if we use the same mod */
                if (st != WL_KEYBOARD_KEY_STATE_RELEASED || key != toggle_key.keyval)
                    return;

                if (!state.first_press_skipped) {
                    state.first_press_skipped = true;
                    return;
                }
                deactivate();
        };

        grab_interface->callbacks.pointer.motion = [=] (weston_pointer *ptr,
                weston_pointer_motion_event *ev)
        {
            handle_pointer_move(ptr);
        };

        using namespace std::placeholders;
        grab_interface->callbacks.pointer.button = std::bind(
                std::mem_fn(&wayfire_expo::handle_pointer_button), this, _1, _2, _3);

        renderer = std::bind(std::mem_fn(&wayfire_expo::render), this);

        resized_cb = [=] (signal_data*) {
            for (int i = 0; i < vw; i++) {
                for (int j = 0; j < vh; j++) {
                    GL_CALL(glDeleteTextures(1, &streams[i][j]->tex));
                    GL_CALL(glDeleteFramebuffers(1, &streams[i][j]->fbuff));
                    streams[i][j]->tex = streams[i][j]->fbuff = -1;
                }
            }
        };

        output->signal->connect_signal("output-resized", &resized_cb);
    }

    void activate()
    {
        if (!output->activate_plugin(grab_interface))
            return;

        grab_interface->grab();

        state.active = true;
        state.in_zoom = true;
        state.button_pressed = false;
        state.moving = false;
        state.first_press_skipped = false;

        state.zoom_delta = 1;

        GetTuple(vx, vy, output->workspace->get_current_workspace());

        target_vx = vx;
        target_vy = vy;

        calculate_zoom(true);

        output->render->set_renderer(renderer);
        output->render->auto_redraw(true);
        output->focus_view(nullptr, core->get_current_seat());
    }

    void deactivate()
    {
        state.in_zoom = true;
        state.zoom_delta = -1;
        state.moving = false;

        output->workspace->set_workspace(std::make_tuple(target_vx, target_vy));
        calculate_zoom(false);
        update_zoom();
    }

    int sx, sy;
    wayfire_view moving_view;
    void handle_pointer_move(weston_pointer *ptr)
    {
        if (state.button_pressed && !state.in_zoom) {
            state.button_pressed = false;
            start_move();
        }

        if (!state.moving || !moving_view)
            return;

        int cx = wl_fixed_to_int(ptr->x);
        int cy = wl_fixed_to_int(ptr->y);

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());

        /*
        pixman_region32_union(&core->ec->primary_plane.damage,
                &core->ec->primary_plane.damage,
                &moving_view->handle->transform.boundingbox);
                */

        moving_view->move(moving_view->geometry.origin.x + (cx - sx) * vw,
                moving_view->geometry.origin.y + (cy - sy) * vh);


        sx = cx;
        sy = cy;

        update_target_workspace(sx, sy);
    }

    void start_move()
    {
        state.moving = true;
    }

    wayfire_view find_view_at(int sx, int sy)
    {
        auto og = output->get_full_geometry();
        sx -= og.origin.x;
        sy -= og.origin.y;

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        sx *= vw;
        sy *= vh;

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        sx -= vx * output->handle->width;
        sy -= vy * output->handle->height;

        wayfire_view search = nullptr;
        output->workspace->for_each_view([&search, og, sx, sy] (wayfire_view v) {
            if (!search && point_inside({sx + og.origin.x, sy + og.origin.y}, v->geometry))
            search = v;
        });

        return search;
    }

    void update_target_workspace(int x, int y) {
        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        auto og = output->get_full_geometry();
        x -= og.origin.x;
        y -= og.origin.y;

        /* TODO: these are approximate, maybe won't work between them */
        int ew = output->handle->width / vw;
        int eh = output->handle->height / vh;

        target_vx = x / ew;
        target_vy = y / eh;
    }

    void handle_pointer_button(weston_pointer *ptr, uint32_t button, uint32_t state)
    {
        auto kbd = weston_seat_get_keyboard(ptr->seat);
        if (kbd->modifiers.mods_depressed != action_button.mod)
            return;
        if (button != action_button.button)
            return;

        if (state == WL_POINTER_BUTTON_STATE_RELEASED && !this->state.moving) {
            deactivate();
        } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
            this->state.moving = false;
        } else {
            this->state.button_pressed = true;
            sx = wl_fixed_to_int(ptr->x);
            sy = wl_fixed_to_int(ptr->y);

            moving_view = find_view_at(sx, sy);
            update_target_workspace(sx, sy);
        }
    }

    struct {
        float scale_x, scale_y,
              off_x, off_y;
    } render_params;

    void render()
    {
        debug <<"expo is render" << std::endl;
        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        GetTuple(vx, vy, output->workspace->get_current_workspace());
        GetTuple(w,  h,  output->get_screen_size());

        OpenGL::use_default_program();

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
                break;
        }

        glm::mat4 matrix;
        matrix = glm::rotate(matrix, angle, glm::vec3(0, 0, 1));
        matrix = glm::translate(matrix, glm::vec3(render_params.off_x, render_params.off_y, 0));
        matrix = glm::scale(matrix, glm::vec3(render_params.scale_x, render_params.scale_y, 1));

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for(int j = 0; j < vw; j++) {
            for(int i = 0; i < vh; i++) {
                if (!streams[i][j]->running) {
                    output->render->workspace_stream_start(streams[i][j]);
                } else {
                    output->render->workspace_stream_update(streams[i][j]);
                }

#define EDGE_OFFSET 13
#define MOSAIC 0

                int mosaic_factor = EDGE_OFFSET - (1 - ((i + j) & 1)) * MOSAIC;
                wayfire_geometry g = {
                    .origin = {(i - vx) * w + mosaic_factor,
                               (j - vy) * h + mosaic_factor},
                    .size = {w - 2 * mosaic_factor, h - 2 * mosaic_factor}};

                OpenGL::render_transformed_texture(streams[i][j]->tex, g, {}, matrix,
                        glm::vec4(1), TEXTURE_TRANSFORM_INVERT_Y | TEXTURE_TRANSFORM_USE_DEVCOORD);
            }
        }

        if (state.in_zoom)
            update_zoom();
    }

    struct tup {
        float begin, end;
    };

    struct {
        int steps = 0;
        tup scale_x, scale_y,
            off_x, off_y;
    } zoom_target;

    void calculate_zoom(bool zoom_in)
    {
        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());

        float center_w = vw / 2.f;
        float center_h = vh / 2.f;

        if (zoom_in) {
            zoom_target.steps = 0;
        } else {
            zoom_target.steps = max_steps;
        }

        int mosaic_factor = (EDGE_OFFSET - (1 - ((target_vx + target_vy) & 1)) * MOSAIC);
        float mf_x = 2. * mosaic_factor / output->handle->width;
        float mf_y = 2. * mosaic_factor / output->handle->height;

        zoom_target.scale_x = {1, 1.f / vw};
        zoom_target.scale_y = {1, 1.f / vh};
        zoom_target.off_x   = {-mf_x, ((target_vx - center_w) * 2.f + 1.f) / vw};
        zoom_target.off_y   = { mf_y, ((center_h - target_vy) * 2.f - 1.f) / vh};
    }

    void update_zoom()
    {
        render_params.scale_x = GetProgress(zoom_target.scale_x.begin,
                zoom_target.scale_x.end, zoom_target.steps, max_steps);
        render_params.scale_y = GetProgress(zoom_target.scale_y.begin,
                zoom_target.scale_y.end, zoom_target.steps, max_steps);
        render_params.off_x = GetProgress(zoom_target.off_x.begin,
                zoom_target.off_x.end, zoom_target.steps, max_steps);
        render_params.off_y = GetProgress(zoom_target.off_y.begin,
                zoom_target.off_y.end, zoom_target.steps, max_steps);

        zoom_target.steps += state.zoom_delta;

        if (zoom_target.steps == max_steps + 1 && state.zoom_delta == 1) {
            state.in_zoom = false;
        } else if (state.zoom_delta == -1 && zoom_target.steps == -1) {
            state.in_zoom = false;
            finalize_and_exit();
        }
    }

    void finalize_and_exit()
    {
        state.active = false;
        output->deactivate_plugin(grab_interface);
        grab_interface->ungrab();

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());

        for (int i = 0; i < vw; i++) {
            for (int j = 0; j < vh; j++) {
                output->render->workspace_stream_stop(streams[i][j]);
            }
        }

        output->render->reset_renderer();
        output->render->auto_redraw(false);
    }
};

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_expo();
    }
}
