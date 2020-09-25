#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/debug.hpp>

#include <wayfire/plugins/common/view-change-viewport-signal.hpp>
#include <wayfire/plugins/common/workspace-wall.hpp>
#include <wayfire/plugins/common/geometry-animation.hpp>
#include <wayfire/plugins/common/move-snap-helper.hpp>

/* TODO: this file should be included in some header maybe(plugin.hpp) */
#include <linux/input-event-codes.h>

static bool begins_with(std::string word, std::string prefix)
{
    if (word.length() < prefix.length())
    {
        return false;
    }

    return word.substr(0, prefix.length()) == prefix;
}

class wayfire_expo : public wf::plugin_interface_t
{
  private:
    wf::point_t convert_workspace_index_to_coords(int index)
    {
        index--; // compensate for indexing from 0
        auto wsize = output->workspace->get_workspace_grid_size();
        int x = index % wsize.width;
        int y = index / wsize.width;

        return wf::point_t{x, y};
    }

    wf::activator_callback toggle_cb = [=] (wf::activator_source_t, uint32_t)
    {
        if (!state.active)
        {
            return activate();
        } else
        {
            if (!zoom_animation.running() || state.zoom_in)
            {
                deactivate();

                return true;
            }
        }

        return false;
    };

    wf::option_wrapper_t<wf::activatorbinding_t> toggle_binding{"expo/toggle"};
    wf::option_wrapper_t<wf::color_t> background_color{"expo/background"};
    wf::option_wrapper_t<int> zoom_duration{"expo/duration"};
    wf::option_wrapper_t<int> delimiter_offset{"expo/offset"};
    wf::geometry_animation_t zoom_animation{zoom_duration};


    std::vector<wf::activator_callback> keyboard_select_cbs;
    std::vector<wf::option_sptr_t<wf::activatorbinding_t>> keyboard_select_options;
    wf::signal_callback_t view_removed = [=] (wf::signal_data_t *event)
    {
        if (get_signaled_view(event) == moving_view)
        {
            end_move(true);
        }
    };

    struct
    {
        bool active = false;
        bool button_pressed = false;
        bool zoom_in = false;
    } state;

    int target_vx, target_vy;
    std::unique_ptr<wf::workspace_wall_t> wall;

  public:
    void setup_workspace_bindings_from_config()
    {
        auto section = wf::get_core().config.get_section("expo");

        std::vector<std::string> workspace_numbers;
        const std::string select_prefix = "select_workspace_";
        for (auto binding : section->get_registered_options())
        {
            if (begins_with(binding->get_name(), select_prefix))
            {
                workspace_numbers.push_back(
                    binding->get_name().substr(select_prefix.length()));
            }
        }

        for (size_t i = 0; i < workspace_numbers.size(); i++)
        {
            auto binding = select_prefix + workspace_numbers[i];
            int workspace_index = atoi(workspace_numbers[i].c_str());

            auto wsize = output->workspace->get_workspace_grid_size();
            if ((workspace_index > (wsize.width * wsize.height)) ||
                (workspace_index < 1))
            {
                continue;
            }

            wf::point_t target = convert_workspace_index_to_coords(workspace_index);

            auto opt   = section->get_option(binding);
            auto value = wf::option_type::from_string<wf::activatorbinding_t>(
                opt->get_value_str());
            keyboard_select_options.push_back(wf::create_option(value.value()));

            keyboard_select_cbs.push_back([=] (wf::activator_source_t, uint32_t)
            {
                if (!state.active)
                {
                    return false;
                } else
                {
                    if (!zoom_animation.running() || state.zoom_in)
                    {
                        target_vx = target.x;
                        target_vy = target.y;
                        deactivate();
                    }
                }

                return true;
            });
        }
    }

    void init() override
    {
        grab_interface->name = "expo";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        setup_workspace_bindings_from_config();
        wall = std::make_unique<wf::workspace_wall_t>(this->output);
        wall->connect_signal("frame", &on_frame);

        output->add_activator(toggle_binding, &toggle_cb);
        grab_interface->callbacks.pointer.button =
            [=] (uint32_t button, uint32_t state)
        {
            if (button != BTN_LEFT)
            {
                return;
            }

            auto gc = output->get_cursor_position();
            handle_input_press(gc.x, gc.y, state);
        };
        grab_interface->callbacks.pointer.motion = [=] (int32_t x, int32_t y)
        {
            handle_input_move({x, y});
        };

        grab_interface->callbacks.touch.down =
            [=] (int32_t id, wl_fixed_t sx, wl_fixed_t sy)
        {
            if (id > 0)
            {
                return;
            }

            handle_input_press(sx, sy, WLR_BUTTON_PRESSED);
        };

        grab_interface->callbacks.touch.up = [=] (int32_t id)
        {
            if (id > 0)
            {
                return;
            }

            handle_input_press(0, 0, WLR_BUTTON_RELEASED);
        };

        grab_interface->callbacks.touch.motion =
            [=] (int32_t id, int32_t sx, int32_t sy)
        {
            if (id > 0) // we handle just the first finger
            {
                return;
            }

            handle_input_move({sx, sy});
        };

        grab_interface->callbacks.cancel = [=] ()
        {
            finalize_and_exit();
        };

        output->connect_signal("view-detached", &view_removed);
        output->connect_signal("view-disappeared", &view_removed);

        output->connect_signal("view-move-check", &on_view_check_move);
        output->connect_signal("view-move-request", &on_move_request);
    }

    wf::signal_connection_t on_move_request = [=] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        if (!can_start_move(view))
        {
            return;
        }

        auto input = wf::get_core().get_touch_position(0);
        if (std::isnan(input.x) || std::isnan(input.y))
        {
            input = wf::get_core().get_cursor_position();
        }

        auto og = output->get_layout_geometry();
        input.x -= og.x;
        input.y -= og.y;

        // assume input was pressed, otherwise, move request doesn't make sense
        this->state.button_pressed = true;
        this->input_grab_origin    = offscreen_point;

        start_move(view, {(int)input.x, (int)input.y});
    };

    /**
     * Check whether we can start moving the view, i.e if expo is active and
     * not already moving a view
     */
    bool can_start_move(wayfire_view view)
    {
        return view != nullptr &&
               output->is_plugin_active(grab_interface->name) &&
               this->moving_view == nullptr;
    }

    wf::signal_connection_t on_view_check_move = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<wf::view_move_check_signal*>(data);
        if (!ev->can_continue && can_start_move(ev->view))
        {
            ev->can_continue = true;
        }
    };

    bool activate()
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        grab_interface->grab();

        state.active = true;
        state.button_pressed = false;
        start_zoom(true);

        auto cws = output->workspace->get_current_workspace();
        target_vx = cws.x;
        target_vy = cws.y;

        for (size_t i = 0; i < keyboard_select_cbs.size(); i++)
        {
            output->add_activator(keyboard_select_options[i],
                &keyboard_select_cbs[i]);
        }

        return true;
    }

    void start_zoom(bool zoom_in)
    {
        wall->set_background_color(background_color);
        wall->set_gap_size(this->delimiter_offset);
        if (zoom_in)
        {
            zoom_animation.set_start(wall->get_workspace_rectangle(
                output->workspace->get_current_workspace()));

            /* Make sure workspaces are centered */
            auto wsize = output->workspace->get_workspace_grid_size();
            auto size  = output->get_screen_size();
            const int maxdim = std::max(wsize.width, wsize.height);
            const int gap    = this->delimiter_offset;

            const int fullw = (gap + size.width) * maxdim + gap;
            const int fullh = (gap + size.height) * maxdim + gap;

            auto rectangle = wall->get_wall_rectangle();
            rectangle.x    -= (fullw - rectangle.width) / 2;
            rectangle.y    -= (fullh - rectangle.height) / 2;
            rectangle.width = fullw;
            rectangle.height = fullh;
            zoom_animation.set_end(rectangle);
        } else
        {
            zoom_animation.set_start(zoom_animation);
            zoom_animation.set_end(
                wall->get_workspace_rectangle({target_vx, target_vy}));
        }

        state.zoom_in = zoom_in;
        zoom_animation.start();
        wall->set_viewport(zoom_animation);
        wall->start_output_renderer();
        output->render->schedule_redraw();
    }

    void deactivate()
    {
        end_move(false);
        start_zoom(false);
        output->workspace->set_workspace({target_vx, target_vy});
        for (size_t i = 0; i < keyboard_select_cbs.size(); i++)
        {
            output->rem_binding(&keyboard_select_cbs[i]);
        }
    }

    wf::geometry_t get_grid_geometry()
    {
        auto wsize  = output->workspace->get_workspace_grid_size();
        auto full_g = output->get_layout_geometry();

        wf::geometry_t grid;
        grid.x     = grid.y = 0;
        grid.width = full_g.width * wsize.width;
        grid.height = full_g.height * wsize.height;

        return grid;
    }

    wf::point_t input_grab_origin;
    void handle_input_press(int32_t x, int32_t y, uint32_t state)
    {
        if (zoom_animation.running())
        {
            return;
        }

        if ((state == WLR_BUTTON_RELEASED) && !this->moving_view)
        {
            this->state.button_pressed = false;
            deactivate();
        } else if (state == WLR_BUTTON_RELEASED)
        {
            this->state.button_pressed = false;
            end_move(false);
        } else
        {
            this->state.button_pressed = true;

            input_grab_origin = {x, y};
            update_target_workspace(x, y);
        }
    }

#define MOVE_HELPER moving_view->get_data<wf::move_snap_helper_t>()
    const wf::point_t offscreen_point = {-10, -10};

    void handle_input_move(wf::point_t to)
    {
        if (!state.button_pressed)
        {
            return;
        }

        if (abs(to - input_grab_origin) < 5)
        {
            /* Ignore small movements */
            return;
        }

        bool first_click = (input_grab_origin != offscreen_point);
        /* As input coordinates are always positive, this will ensure that any
         * subsequent motion events while grabbed are allowed */
        input_grab_origin = offscreen_point;

        if (!zoom_animation.running() && first_click)
        {
            start_move(find_view_at_coordinates(to.x, to.y), to);
            /* Fall through to the moving view case */
        }

        if (moving_view)
        {
            auto og = output->get_layout_geometry();
            auto target_output = wf::get_core().output_layout->get_output_at(
                og.x + to.x, og.y + to.y);
            if (target_output != output)
            {
                /** First, reset moving view so that we don't remove its snap
                 * helper when the output is changed. */
                auto moving_view_copy = moving_view;
                this->moving_view = nullptr;
                start_move_on_output(moving_view_copy, target_output);

                return;
            }

            int global_x = to.x, global_y = to.y;
            input_coordinates_to_global_coordinates(global_x, global_y);

            auto grid = get_grid_geometry();
            if (!(grid & wf::point_t{global_x, global_y}))
            {
                return;
            }

            auto coordinates = input_coordinates_to_output_local_coordinates(to);
            coordinates = view_local_coordinates(moving_view, coordinates);
            MOVE_HELPER->handle_motion(coordinates);

            update_target_workspace(to.x, to.y);
        }
    }

    wayfire_view moving_view;
    wf::point_t move_started_ws;
    void start_move(wayfire_view view, wf::point_t grab)
    {
        /* target workspace has been updated on the last click
         * so it has accurate information about views' viewport */
        if (!view)
        {
            return;
        }

        move_started_ws = {target_vx, target_vy};
        moving_view     = view;

        output->workspace->bring_to_front(moving_view);

        wf::ensure_move_helper_at(moving_view,
            input_coordinates_to_output_local_coordinates(grab));

        wf::get_core().set_cursor("grabbing");
    }

    /**
     * End the moving action.
     *
     * @param view_destroyed Whether the view was destroyed.
     */
    void end_move(bool view_destroyed)
    {
        wf::get_core().set_cursor("default");
        if (!moving_view)
        {
            return;
        }

        MOVE_HELPER->handle_input_released();
        moving_view->erase_data<wf::move_snap_helper_t>();
        if (!view_destroyed)
        {
            view_change_viewport_signal data;
            data.view = moving_view;
            data.from = move_started_ws;
            data.to   = {target_vx, target_vy};
            output->emit_signal("view-change-viewport", &data);
        }

        moving_view = nullptr;
    }

    /**
     * Find the coordinate of the given point from output-local coordinates
     * to coordinates relative to the first workspace (i.e (0,0))
     */
    void input_coordinates_to_global_coordinates(int & sx, int & sy)
    {
        auto og = output->get_layout_geometry();

        auto wsize = output->workspace->get_workspace_grid_size();
        float max  = std::max(wsize.width, wsize.height);

        float grid_start_x = og.width * (max - wsize.width) / float(max) / 2;
        float grid_start_y = og.height * (max - wsize.height) / float(max) / 2;

        sx -= grid_start_x;
        sy -= grid_start_y;

        sx *= max;
        sy *= max;
    }

    /**
     * Find the coordinate of the given point from output-local coordinates
     * to output-workspace-local coordinates
     */
    wf::point_t input_coordinates_to_output_local_coordinates(wf::point_t ip)
    {
        input_coordinates_to_global_coordinates(ip.x, ip.y);

        auto cws = output->workspace->get_current_workspace();
        auto og  = output->get_relative_geometry();

        /* Translate coordinates into output-local coordinate system,
         * relative to the current workspace */
        return {
            ip.x - cws.x * og.width,
            ip.y - cws.y * og.height,
        };
    }

    /**
     * If the view is sticky, return the pos relative to the current workspace.
     * Otherwise, it stays the same.
     */
    wf::point_t view_local_coordinates(wayfire_view view, wf::point_t pos)
    {
        auto ssize = output->get_screen_size();
        if (view->sticky)
        {
            return {
                (pos.x % ssize.width + ssize.width) % ssize.width,
                (pos.y % ssize.height + ssize.height) % ssize.height
            };
        } else
        {
            return pos;
        }
    }

    wayfire_view find_view_at_coordinates(int gx, int gy)
    {
        auto local = input_coordinates_to_output_local_coordinates({gx, gy});
        /* TODO: adjust to delimiter offset */

        for (auto& view : output->workspace->get_views_in_layer(wf::WM_LAYERS))
        {
            if (!view->is_mapped() || !view->is_visible())
            {
                continue;
            }

            auto view_local = view_local_coordinates(view, local);
            wlr_box box     = {view_local.x, view_local.y, 1, 1};
            for (auto& v : view->enumerate_views())
            {
                if (v->intersects_region(box))
                {
                    return v;
                }
            }
        }

        return nullptr;
    }

    void update_target_workspace(int x, int y)
    {
        auto og = output->get_layout_geometry();

        input_coordinates_to_global_coordinates(x, y);

        auto grid = get_grid_geometry();
        if (!(grid & wf::point_t{x, y}))
        {
            return;
        }

        target_vx = x / og.width;
        target_vy = y / og.height;
    }

    wf::signal_connection_t on_frame = {[=] (wf::signal_data_t*)
        {
            if (zoom_animation.running())
            {
                output->render->schedule_redraw();
                wall->set_viewport(zoom_animation);
            } else if (!state.zoom_in)
            {
                finalize_and_exit();
            }
        }
    };

    void finalize_and_exit()
    {
        state.active = false;
        output->deactivate_plugin(grab_interface);
        grab_interface->ungrab();
        wall->stop_output_renderer(true);
    }

    void fini() override
    {
        output->disconnect_signal("view-detached", &view_removed);
        output->disconnect_signal("view-disappeared", &view_removed);

        if (state.active)
        {
            finalize_and_exit();
        }

        output->rem_binding(&toggle_cb);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_expo);
