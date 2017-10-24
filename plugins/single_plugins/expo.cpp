#include <output.hpp>
#include <opengl.hpp>
#include <core.hpp>
#include "../../shared/config.hpp"
/* TODO: this file should be included in some header maybe(plugin.hpp) */
#include <linux/input-event-codes.h>
#include "../../shared/config.hpp"
#include "view-change-viewport-signal.hpp"


class wayfire_expo : public wayfire_plugin_t {
    private:
        key_callback toggle_cb, press_cb, move_cb;
        touch_gesture_callback touch_toggle_cb;
        wayfire_button action_button;

        wayfire_color background_color;

        int max_steps;

        render_hook_t renderer;

        struct {
            bool active = false;
            bool moving = false;
            bool in_zoom = false;
            bool button_pressed = false;

            int zoom_delta = 1;
        } state;
        int target_vx, target_vy;
        std::tuple<int, int> move_started_ws;

        std::vector<std::vector<wf_workspace_stream*>> streams;
        signal_callback_t resized_cb;

        int delimiter_offset;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "expo";
        grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;

        auto section = config->get_section("expo");
        auto toggle_key = section->get_key("toggle", {MODIFIER_SUPER, KEY_E});

        if (!toggle_key.keyval || !toggle_key.mod)
            return;

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        streams.resize(vw);

        for (int i = 0; i < vw; i++) {
            for (int j = 0;j < vh; j++) {
                streams[i].push_back(new wf_workspace_stream);
                streams[i][j]->tex = streams[i][j]->fbuff = -1;
                streams[i][j]->ws = std::make_tuple(i, j);
            }
        }

        max_steps = section->get_duration("duration", 20);
        delimiter_offset = section->get_int("offset", 10);

        toggle_cb = [=] (weston_keyboard *kbd, uint32_t key) {
            if (!state.active) {
                activate();
            } else {
                deactivate();
            }
        };

        touch_toggle_cb = [=] (wayfire_touch_gesture*) {
            if (state.active)
                deactivate();
            else
                activate();
        };

        output->add_key(toggle_key.mod, toggle_key.keyval, &toggle_cb);

        wayfire_touch_gesture activate_gesture;
        activate_gesture.type = GESTURE_PINCH;
        activate_gesture.finger_count = 3;
        output->add_gesture(activate_gesture, &touch_toggle_cb);

        action_button = section->get_button("action", {0, BTN_LEFT});

        grab_interface->callbacks.pointer.button = [=] (weston_pointer *ptr,
                uint32_t button, uint32_t state)
        {
            auto kbd = weston_seat_get_keyboard(ptr->seat);
            if (kbd->modifiers.mods_depressed != action_button.mod)
                return;
            if (button != action_button.button)
                return;

            handle_input_press(ptr->x, ptr->y, state);

        };
        grab_interface->callbacks.pointer.motion = [=] (weston_pointer *ptr,
                weston_pointer_motion_event *ev)
        {
            handle_input_move(ptr->x, ptr->y);
        };

        grab_interface->callbacks.touch.down = [=] (weston_touch *touch,
                int32_t id, wl_fixed_t sx, wl_fixed_t sy)
        {
            if (id > 0) return;
            handle_input_press(sx, sy, WL_POINTER_BUTTON_STATE_PRESSED);
        };

        grab_interface->callbacks.touch.up = [=] (weston_touch *touch, int32_t id)
        {
            if (id > 0) return;
            handle_input_press(0, 0, WL_POINTER_BUTTON_STATE_RELEASED);
        };

        grab_interface->callbacks.touch.motion = [=] (weston_touch *touch,
                int32_t id, wl_fixed_t sx, wl_fixed_t sy)
        {
            if (id > 0) /* we handle just the first finger */
                return;

            handle_input_move(sx, sy);
        };

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

        background_color = section->get_color("background", {0, 0, 0, 1});
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

        state.zoom_delta = 1;

        GetTuple(vx, vy, output->workspace->get_current_workspace());

        target_vx = vx;
        target_vy = vy;
        calculate_zoom(true);

        output->render->set_renderer(renderer);
        output->render->auto_redraw(true);
        output->focus_view(nullptr);
    }

    void deactivate()
    {
        state.in_zoom = true;
        state.zoom_delta = -1;
        state.moving = false;

        output->workspace->set_workspace(std::make_tuple(target_vx, target_vy));
        output->focus_view(nullptr);

        calculate_zoom(false);
        update_zoom();
    }

    weston_geometry get_grid_geometry()
    {
        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        auto full_g = output->get_full_geometry();

        weston_geometry grid;
        grid.x = grid.y = 0;
        grid.width = full_g.width * vw;
        grid.height = full_g.height * vh;

        return grid;
    }

    int sx, sy;
    wayfire_view moving_view;
    void handle_input_move(wl_fixed_t x, wl_fixed_t y)
    {
        int cx = wl_fixed_to_int(x);
        int cy = wl_fixed_to_int(y);

        if (state.button_pressed && !state.in_zoom)
        {
            start_move(cx, cy);
            state.button_pressed = false;
        }

        if (!state.moving || !moving_view)
            return;

        int global_x = cx, global_y = cy;
        input_coordinates_to_global_coordinates(global_x, global_y);

        auto grid = get_grid_geometry();
        if (!point_inside({global_x, global_y}, grid))
            return;

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        int max = std::max(vw, vh);

        moving_view->move(moving_view->geometry.x + (cx - sx) * max,
                moving_view->geometry.y + (cy - sy) * max);

        sx = cx;
        sy = cy;

        update_target_workspace(sx, sy);
    }

    void start_move(int x, int y)
    {
        /* target workspace has been updated on the last click
         * so it has accurate information about views' viewport */
        if (!moving_view)
            return;

        move_started_ws = {target_vx, target_vy};
        state.moving = true;
        output->bring_to_front(moving_view);
    }

    void end_move()
    {
        state.moving = false;

        if (moving_view)
        {
            view_change_viewport_signal data;
            data.view = moving_view;
            data.from = move_started_ws;
            data.to   = {target_vx, target_vy};

            output->signal->emit_signal("view-change-viewport", &data);
        }
    }
    void input_coordinates_to_global_coordinates(int &sx, int &sy)
    {
        auto og = output->get_full_geometry();
        sx -= og.x;
        sy -= og.y;

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());

        float max = std::max(vw, vh);

        float grid_start_x = og.width * (max - vw) / float(max) / 2;
        float grid_start_y = og.height * (max - vh) / float(max) / 2;

        sx -= grid_start_x;
        sy -= grid_start_y;

        sx *= max;
        sy *= max;
    }

    wayfire_view find_view_at(int sx, int sy)
    {
        GetTuple(vx, vy, output->workspace->get_current_workspace());
        auto og = output->get_full_geometry();

        input_coordinates_to_global_coordinates(sx, sy);

        sx -= vx * og.width;
        sy -= vy * og.height;

        wayfire_view search = nullptr;
        output->workspace->for_each_view([&search, og, sx, sy] (wayfire_view v) {
            if (!search && point_inside({sx + og.x, sy + og.y}, v->geometry))
            search = v;
        });

        return search;
    }

    void update_target_workspace(int x, int y) {
        auto og = output->get_full_geometry();

        input_coordinates_to_global_coordinates(x, y);

        auto grid = get_grid_geometry();
        if (!point_inside({x, y}, grid))
            return;

        target_vx = x / og.width;
        target_vy = y / og.height;
    }

    void handle_input_press(wl_fixed_t x, wl_fixed_t y, uint32_t state)
    {
        if (state == WL_POINTER_BUTTON_STATE_RELEASED && !this->state.moving) {
            deactivate();
        } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
            end_move();
        } else {
            this->state.button_pressed = true;
            sx = wl_fixed_to_int(x);
            sy = wl_fixed_to_int(y);

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
                break;
            default:
                angle = 0;
                break;
        }

        glm::mat4 matrix;
        matrix = glm::rotate(matrix, angle, glm::vec3(0, 0, 1));
        matrix = glm::translate(matrix, glm::vec3(render_params.off_x, render_params.off_y, 0));
        matrix = glm::scale(matrix, glm::vec3(render_params.scale_x, render_params.scale_y, 1));

        glClearColor(background_color.r, background_color.g,
                     background_color.b, background_color.a);
        glClear(GL_COLOR_BUFFER_BIT);
        for(int j = 0; j < vh; j++) {
            for(int i = 0; i < vw; i++) {
                if (!streams[i][j]->running) {
                    output->render->workspace_stream_start(streams[i][j]);
                } else {
                    output->render->workspace_stream_update(streams[i][j],
                            render_params.scale_x, render_params.scale_y);
                }

                weston_geometry g = {
                    (i - vx) * w + delimiter_offset,
                    (j - vy) * h + delimiter_offset,
                    w - 2 * delimiter_offset,
                    h - 2 * delimiter_offset
                };

                OpenGL::texture_geometry texg;
                texg.x1 = 0;
                texg.y1 = 0;
                texg.x2 = streams[i][j]->scale_x;
                texg.y2 = streams[i][j]->scale_y;

                GL_CALL(glEnable(GL_SCISSOR_TEST));
                GL_CALL(glScissor(0, 0, output->render->ctx->device_width,
                            output->render->ctx->device_height));

                OpenGL::render_transformed_texture(streams[i][j]->tex, g, texg, matrix,
                        glm::vec4(1), TEXTURE_TRANSFORM_USE_DEVCOORD | TEXTURE_TRANSFORM_INVERT_Y |
                        TEXTURE_USE_TEX_GEOMETRY);

                GL_CALL(glDisable(GL_SCISSOR_TEST));
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

        int max = std::max(vw, vh);

        float diff_w = (max - vw) / (1. * max);
        float diff_h = (max - vh) / (1. * max);

        vw = vh = max;

        float center_w = vw / 2.f;
        float center_h = vh / 2.f;

        if (zoom_in) {
            zoom_target.steps = 0;
            render_params.scale_x = render_params.scale_y = 1;
        } else {
            zoom_target.steps = max_steps;
            render_params.scale_x = 1.f / vw;
            render_params.scale_y = 1.f / vh;
        }

        float mf_x = 2. * delimiter_offset / output->handle->width;
        float mf_y = 2. * delimiter_offset / output->handle->height;

        zoom_target.scale_x = {1, 1.f / vw};
        zoom_target.scale_y = {1, 1.f / vh};

        zoom_target.off_x   = {-mf_x, ((target_vx - center_w) * 2.f + 1.f) / vw + diff_w};
        zoom_target.off_y   = { mf_y, ((center_h - target_vy) * 2.f - 1.f) / vh - diff_h};
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
        output->focus_view(output->get_top_view());
    }
};

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_expo();
    }
}
