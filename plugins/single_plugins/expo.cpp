#include <plugin.hpp>
#include <output.hpp>
#include <debug.hpp>
#include <opengl.hpp>
#include <core.hpp>
#include <render-manager.hpp>
#include <workspace-stream.hpp>
#include <workspace-manager.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <animation.hpp>

/* TODO: this file should be included in some header maybe(plugin.hpp) */
#include <linux/input-event-codes.h>
#include "view-change-viewport-signal.hpp"
#include "../wobbly/wobbly-signal.hpp"

class wayfire_expo : public wf::plugin_interface_t
{
    private:

    activator_callback toggle_cb = [=] (wf_activator_source, uint32_t)
    {
        if (!state.active) {
            activate();
        } else {
            if (!zoom_animation.running() || state.zoom_in)
                deactivate();
        }
    };

    wf_option action_button;
    wf_option background_color, zoom_animation_duration;
    wf_option delimiter_offset;

    wf_duration zoom_animation;

    wf::render_hook_t renderer;

    struct {
        bool active = false;
        bool moving = false;
        bool button_pressed = false;

        bool zoom_in = false;
    } state;
    int target_vx, target_vy;
    std::tuple<int, int> move_started_ws;

    std::vector<std::vector<wf::workspace_stream_t>> streams;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "expo";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        auto section = config->get_section("expo");
        auto toggle_binding = section->get_option("toggle",
            "<super> KEY_E | pinch in 3");

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        streams.resize(vw);

        for (int i = 0; i < vw; i++)
        {
            streams[i].resize(vh);
            for (int j = 0; j < vh; j++)
                streams[i][j].ws = std::make_tuple(i, j);
        }

        zoom_animation_duration = section->get_option("duration", "300");
        zoom_animation = wf_duration(zoom_animation_duration);

        delimiter_offset = section->get_option("offset", "10");


        output->add_activator(toggle_binding, &toggle_cb);
        grab_interface->callbacks.pointer.button = [=] (uint32_t button, uint32_t state)
        {
            if (button != BTN_LEFT)
                return;

            GetTuple(x, y, output->get_cursor_position());
            handle_input_press(x, y, state);

        };
        grab_interface->callbacks.pointer.motion = [=] (int32_t x, int32_t y)
        {
            handle_input_move(x, y);
        };

        grab_interface->callbacks.touch.down = [=] (int32_t id, wl_fixed_t sx, wl_fixed_t sy)
        {
            if (id > 0) return;
            handle_input_press(sx, sy, WLR_BUTTON_PRESSED);
        };

        grab_interface->callbacks.touch.up = [=] (int32_t id)
        {
            if (id > 0) return;
            handle_input_press(0, 0, WLR_BUTTON_RELEASED);
        };

        grab_interface->callbacks.touch.motion = [=] (int32_t id, int32_t sx, int32_t sy)
        {
            if (id > 0) // we handle just the first finger
                return;

            handle_input_move(sx, sy);
        };

        grab_interface->callbacks.cancel = [=] ()
        {
            finalize_and_exit();
        };

        renderer = [=] (const wf_framebuffer& buffer) { render(buffer); };
        background_color = section->get_option("background", "0 0 0 1");
    }

    void activate()
    {
        if (!output->activate_plugin(grab_interface))
            return;

        grab_interface->grab();

        state.active = true;
        state.button_pressed = false;
        state.moving = false;
        zoom_animation.start();

        GetTuple(vx, vy, output->workspace->get_current_workspace());

        target_vx = vx;
        target_vy = vy;
        calculate_zoom(true);

        output->render->set_renderer(renderer);
        output->render->set_redraw_always();
    }

    void deactivate()
    {
        if (state.moving)
            end_move();

        zoom_animation.start();
        state.moving = false;

        output->workspace->set_workspace(std::make_tuple(target_vx, target_vy));

        calculate_zoom(false);
        update_zoom();
    }

    wf_geometry get_grid_geometry()
    {
        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        auto full_g = output->get_layout_geometry();

        wf_geometry grid;
        grid.x = grid.y = 0;
        grid.width = full_g.width * vw;
        grid.height = full_g.height * vh;

        return grid;
    }

    int sx, sy;
    wayfire_view moving_view;
    void handle_input_move(int x, int y)
    {
        int cx = x;
        int cy = y;

        if (state.button_pressed && !zoom_animation.running())
        {
            start_move(cx, cy);
            state.button_pressed = false;
        }

        if (!state.moving || !moving_view)
            return;

        int global_x = cx, global_y = cy;
        input_coordinates_to_global_coordinates(global_x, global_y);

        auto grid = get_grid_geometry();
        if (!(grid & wf_point{global_x, global_y}))
            return;

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        int max = std::max(vw, vh);

        auto g = moving_view->get_wm_geometry();
        moving_view->move(g.x + (cx - sx) * max, g.y + (cy - sy) * max);
        move_wobbly(moving_view, global_x, global_y);

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

        move_started_ws = std::tuple<int, int> {target_vx, target_vy};
        state.moving = true;
        output->workspace->bring_to_front(moving_view);

        moving_view->set_moving(true);

        input_coordinates_to_global_coordinates(x, y);
        GetTuple(vx, vy, output->workspace->get_current_workspace());
        auto og = output->get_relative_geometry();

        snap_wobbly(moving_view, {}, false);
        /* Translate coordinates into output-local coordinate system,
         * relative to the current workspace (because that's the coordinate system
         * in which the view sees itself) */
        start_wobbly(moving_view, x - vx * og.x, y - vy * og.y);

        if (moving_view->fullscreen)
            moving_view->fullscreen_request(moving_view->get_output(), false);

        wf::get_core().set_cursor("grabbing");
    }

    void end_move()
    {
        state.moving = false;
        wf::get_core().set_cursor("default");

        if (moving_view)
        {
            view_change_viewport_signal data;
            data.view = moving_view;
            data.from = move_started_ws;
            data.to   = std::tuple<int, int> {target_vx, target_vy};

            output->emit_signal("view-change-viewport", &data);
            moving_view->set_moving(false);
            end_wobbly(moving_view);
        }
    }
    void input_coordinates_to_global_coordinates(int &sx, int &sy)
    {
        auto og = output->get_layout_geometry();

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
        auto og = output->get_layout_geometry();

        input_coordinates_to_global_coordinates(sx, sy);

        sx -= vx * og.width;
        sy -= vy * og.height;

        /* TODO: adjust to delimiter offset */

        for (auto& view : output->workspace->get_views_in_layer(wf::WM_LAYERS))
        {
            if (view->get_wm_geometry() & wf_point{sx, sy})
                return view;
        }
        return nullptr;
    }

    void update_target_workspace(int x, int y) {
        auto og = output->get_layout_geometry();

        input_coordinates_to_global_coordinates(x, y);

        auto grid = get_grid_geometry();
        if (!(grid & wf_point{x, y}))
            return;

        target_vx = x / og.width;
        target_vy = y / og.height;
    }

    void handle_input_press(int32_t x, int32_t y, uint32_t state)
    {
        if (zoom_animation.running())
            return;

        if (state == WLR_BUTTON_RELEASED && !this->state.moving) {
            this->state.button_pressed = false;
            deactivate();
        } else if (state == WLR_BUTTON_RELEASED) {
            this->state.button_pressed = false;
            end_move();
        } else {
            this->state.button_pressed = true;
            sx = x;
            sy = y;

            moving_view = find_view_at(sx, sy);
            update_target_workspace(sx, sy);
        }
    }

    struct {
        float scale_x, scale_y,
              off_x, off_y,
              delimiter_offset;
    } render_params;

    void update_streams()
    {
        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());

        for(int j = 0; j < vh; j++)
        {
            for(int i = 0; i < vw; i++)
            {
                if (!streams[i][j].running)
                {
                    output->render->workspace_stream_start(streams[i][j]);
                } else
                {
                    output->render->workspace_stream_update(streams[i][j],
                        render_params.scale_x, render_params.scale_y);
                }
            }
        }
    }

    /* Renders a grid of all active workspaces. It "renders" the workspaces
     * in their correct place/size, then scales+translates the whole scene so
     * that all of the workspaces become visible.
     *
     * The scale+translate part is calculated in zoom_target */
    void render(const wf_framebuffer &fb)
    {
        update_streams();

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        GetTuple(vx, vy, output->workspace->get_current_workspace());
        GetTuple(w,  h,  output->get_screen_size());

        auto translate = glm::translate(glm::mat4(1.0), glm::vec3(render_params.off_x, render_params.off_y, 0));
        auto scale     = glm::scale(glm::mat4(1.0), glm::vec3(render_params.scale_x, render_params.scale_y, 1));
        auto scene_transform = fb.transform * translate * scale; // scale+translate part

        OpenGL::render_begin(fb);
        OpenGL::clear(background_color->as_cached_color());
        fb.scissor(fb.framebuffer_box_from_geometry_box(fb.geometry));

        /* Space between adjacent workspaces */
        float hspacing = 1.0 * render_params.delimiter_offset / w;
        float vspacing = 1.0 * render_params.delimiter_offset / h;
        if (fb.wl_transform & 1)
            std::swap(hspacing, vspacing);

        for(int j = 0; j < vh; j++)
        {
            for(int i = 0; i < vw; i++)
            {
                /* First, center each workspace on the output, taking spacing into account */
                gl_geometry out_geometry = {
                    .x1 = -1 + hspacing,
                    .y1 = 1 - vspacing,
                    .x2 = 1 - hspacing,
                    .y2 = -1 + vspacing,
                };

                /* Then, calculate translation matrix so that the workspace gets
                 * in its correct position relative to the focused workspace */
                auto translation = glm::translate(glm::mat4(1.0),
                    {(i - vx) * 2.0f, (vy - j) * 2.0f, 0.0f});

                auto workspace_transform = scene_transform * translation;

                /* Undo rotation of the workspace */
                workspace_transform = workspace_transform * glm::inverse(fb.transform);

                OpenGL::render_transformed_texture(streams[i][j].buffer.tex,
                    out_geometry, {}, workspace_transform);
            }
        }

        GL_CALL(glUseProgram(0));
        OpenGL::render_end();

        update_zoom();
    }

    struct {
        wf_transition scale_x, scale_y, off_x, off_y;
        wf_transition delimiter_offset;
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
            render_params.scale_x = render_params.scale_y = 1;
        } else {
            render_params.scale_x = 1.f / vw;
            render_params.scale_y = 1.f / vh;
        }

        zoom_target.scale_x = {1, 1.f / vw};
        zoom_target.scale_y = {1, 1.f / vh};

        zoom_target.off_x   = {0, ((target_vx - center_w) * 2.f + 1.f) / vw + diff_w};
        zoom_target.off_y   = {0, ((center_h - target_vy) * 2.f - 1.f) / vh - diff_h};

        zoom_target.delimiter_offset = {0, (float)delimiter_offset->as_cached_int()};

        if (!zoom_in)
        {
            std::swap(zoom_target.scale_x.start, zoom_target.scale_x.end);
            std::swap(zoom_target.scale_y.start, zoom_target.scale_y.end);
            std::swap(zoom_target.off_x.start, zoom_target.off_x.end);
            std::swap(zoom_target.off_y.start, zoom_target.off_y.end);
            std::swap(zoom_target.delimiter_offset.start,
                      zoom_target.delimiter_offset.end);
        }

        state.zoom_in = zoom_in;
        zoom_animation.start();
    }

    void update_zoom()
    {

        render_params.scale_x = zoom_animation.progress(zoom_target.scale_x);
        render_params.scale_y = zoom_animation.progress(zoom_target.scale_y);
        render_params.off_x = zoom_animation.progress(zoom_target.off_x);
        render_params.off_y = zoom_animation.progress(zoom_target.off_y);

        render_params.delimiter_offset = zoom_animation.progress(zoom_target.delimiter_offset);

        if (!zoom_animation.running() && !state.zoom_in)
            finalize_and_exit();
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

        output->render->set_renderer(nullptr);
        output->render->set_redraw_always(false);
    }

    void fini()
    {
        if (state.active)
            finalize_and_exit();

        OpenGL::render_begin();
        for (auto& row : streams)
        {
            for (auto& stream: row)
                stream.buffer.release();
        }
        OpenGL::render_end();

        output->rem_binding(&toggle_cb);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_expo);
