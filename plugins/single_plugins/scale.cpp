/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <map>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

#include <linux/input-event-codes.h>


using namespace wf::animation;

class scale_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t scale_x{*this};
    timed_transition_t scale_y{*this};
    timed_transition_t translation_x{*this};
    timed_transition_t translation_y{*this};
};

struct wf_scale_animation_attribs
{
    wf::option_wrapper_t<int> duration{"scale/duration"};
    scale_animation_t scale_animation{duration};
};

class wf_scale : public wf::view_2D
{
  public:
    wf_scale(wayfire_view view) : wf::view_2D(view)
    {}
    ~wf_scale()
    {}

    uint32_t get_z_order() override
    {
        return wf::TRANSFORMER_HIGHLEVEL + 1;
    }
};

struct view_scale_data
{
    int row, col;
    wf_scale *transformer = 0; /* avoid potential UB from uninitialized member */
    wf::animation::simple_animation_t fade_animation;
    wf_scale_animation_attribs animation;
};

class wayfire_scale : public wf::plugin_interface_t
{
    int grid_cols;
    int grid_rows;
    int grid_last_row_cols;
    bool input_release_impending = false;
    bool active, hook_set, button_connected;
    const std::string transformer_name = "scale";
    wayfire_view initial_focus_view, last_focused_view;
    std::map<wayfire_view, view_scale_data> scale_data;
    wf::option_wrapper_t<int> spacing{"scale/spacing"};
    wf::option_wrapper_t<bool> interact{"scale/interact"};
    wf::option_wrapper_t<bool> middle_click_close{"scale/middle_click_close"};
    wf::option_wrapper_t<double> inactive_alpha{"scale/inactive_alpha"};
    wf::option_wrapper_t<bool> allow_scale_zoom{"scale/allow_zoom"};

    /* maximum scale -- 1.0 means we will not "zoom in" on a view */
    const double max_scale_factor = 1.0;
    /* maximum scale for child views (relative to their parents)
     * zero means unconstrained, 1.0 means child cannot be scaled
     * "larger" than the parent */
    const double max_scale_child = 1.0;

    /* true if the currently running scale should include views from
     * all workspaces */
    bool all_workspaces;

  public:
    void init() override
    {
        grab_interface->name = "scale";
        grab_interface->capabilities = 0;

        active = hook_set = button_connected = false;

        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"scale/toggle"},
            &toggle_cb);
        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"scale/toggle_all"},
            &toggle_all_cb);

        grab_interface->callbacks.pointer.button =
            [=] (uint32_t button, uint32_t state)
        {
            process_button(button, state);
        };

        grab_interface->callbacks.keyboard.key = [=] (uint32_t key, uint32_t state)
        {
            process_key(key, state);
        };
        interact.set_callback(interact_option_changed);
        allow_scale_zoom.set_callback(allow_scale_zoom_option_changed);
    }

    void add_transformer(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        if (view->get_transformer(transformer_name))
        {
            return;
        }

        wf_scale *tr = new wf_scale(view);
        scale_data[view].transformer = tr;
        view->add_transformer(std::unique_ptr<wf_scale>(tr), transformer_name);
        view->connect_signal("geometry-changed", &view_geometry_changed);
    }

    void add_transformers(std::vector<wayfire_view> views)
    {
        for (auto& view : views)
        {
            add_transformer(view);
            for (auto& child : view->children)
            {
                add_transformer(child);
            }
        }
    }

    void pop_transformer(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        if (view->get_transformer(transformer_name))
        {
            view->pop_transformer(transformer_name);
        }
    }

    void remove_transformers()
    {
        for (auto& e : scale_data)
        {
            pop_transformer(e.first);
            for (auto& child : e.first->children)
            {
                pop_transformer(child);
            }
        }
    }

    bool all_same_as_current_workspace_views()
    {
        auto all_workspace_views     = get_all_workspace_views();
        auto current_workspace_views = get_current_workspace_views();

        std::sort(all_workspace_views.begin(), all_workspace_views.end());
        std::sort(current_workspace_views.begin(), current_workspace_views.end());

        return all_workspace_views == current_workspace_views;
    }

    wf::activator_callback toggle_cb = [=] (wf::activator_source_t, uint32_t)
    {
        if (active)
        {
            if (all_workspaces)
            {
                all_workspaces = false;
                if (all_same_as_current_workspace_views())
                {
                    deactivate();

                    return true;
                }

                all_workspaces_option_changed();
            } else
            {
                deactivate();
            }
        } else
        {
            all_workspaces = false;
            if (!activate())
            {
                return false;
            }
        }

        output->render->schedule_redraw();

        return true;
    };

    wf::activator_callback toggle_all_cb = [=] (wf::activator_source_t, uint32_t)
    {
        if (active)
        {
            if (!all_workspaces)
            {
                all_workspaces = true;
                if (all_same_as_current_workspace_views())
                {
                    deactivate();

                    return true;
                }

                all_workspaces_option_changed();
            } else
            {
                deactivate();
            }
        } else
        {
            all_workspaces = true;
            if (!activate())
            {
                return false;
            }
        }

        output->render->schedule_redraw();

        return true;
    };

    void connect_button_signal()
    {
        if (button_connected)
        {
            return;
        }

        wf::get_core().connect_signal("pointer_button", &on_button_event);
        button_connected = true;
    }

    void disconnect_button_signal()
    {
        if (!button_connected)
        {
            return;
        }

        wf::get_core().disconnect_signal("pointer_button", &on_button_event);
        button_connected = false;
    }

    wf::signal_callback_t on_button_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_event_pointer_button>*>(data);

        process_button(ev->event->button, ev->event->state);
    };

    void fade_out_all_except(wayfire_view view)
    {
        for (auto& e : scale_data)
        {
            auto v  = e.first;
            auto tr = e.second.transformer;
            if (!v || !tr || (v == view) ||
                (view && (v == view->parent)) ||
                (v->parent == view))
            {
                continue;
            }

            fade_out(v);
        }
    }

    void fade_in(wayfire_view view)
    {
        if (!view || !scale_data[view].transformer)
        {
            return;
        }

        set_hook();
        auto alpha = scale_data[view].transformer->alpha;
        scale_data[view].fade_animation.animate(alpha, 1);
        if (view->children.size())
        {
            fade_in(view->children.front());
        }
    }

    void fade_out(wayfire_view view)
    {
        if (!view || !scale_data[view].transformer)
        {
            return;
        }

        set_hook();
        auto alpha = scale_data[view].transformer->alpha;
        scale_data[view].fade_animation.animate(alpha, (double)inactive_alpha);
        for (auto& child : view->children)
        {
            fade_out(child);
        }
    }

    void select_view(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        auto ws = get_view_main_workspace(view);
        output->workspace->request_workspace(ws);
    }

    void finish_input()
    {
        input_release_impending = false;
        grab_interface->ungrab();
        if (!animation_running())
        {
            finalize();
        }
    }

    void remove_view(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        pop_transformer(view);
        scale_data.erase(view);
        for (auto& child : view->children)
        {
            pop_transformer(child);
            scale_data.erase(child);
        }
    }

    void process_button(uint32_t button, uint32_t state)
    {
        if (!active)
        {
            finish_input();

            return;
        }

        if ((button == BTN_LEFT) || (state == WLR_BUTTON_RELEASED))
        {
            input_release_impending = false;
        }

        if (state != WLR_BUTTON_PRESSED)
        {
            return;
        }

        switch (button)
        {
          case BTN_LEFT:
            break;

          case BTN_MIDDLE:
            if (!middle_click_close)
            {
                return;
            }

            break;

          default:
            return;
        }

        auto view = wf::get_core().get_view_at(wf::get_core().get_cursor_position());
        if (!view)
        {
            return;
        }

        if (!scale_view(view) && (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            return;
        }

        if (button == BTN_MIDDLE)
        {
            view->close();

            return;
        }

        last_focused_view = view;
        output->focus_view(view, true);
        fade_out_all_except(view);
        fade_in(view);

        if (interact)
        {
            return;
        }

        /* end scale */
        input_release_impending = true;
        deactivate();
        select_view(view);
    }

    wf::point_t get_view_main_workspace(wayfire_view view)
    {
        while (view->parent)
        {
            view = view->parent;
        }

        auto ws     = output->workspace->get_current_workspace();
        auto og     = output->get_layout_geometry();
        auto vg     = view->get_output_geometry();
        auto center = wf::point_t{vg.x + vg.width / 2, vg.y + vg.height / 2};

        return wf::point_t{
            ws.x + ((center.x - ws.x * og.width) / og.width),
            ws.y + ((center.y - ws.y * og.height) / og.height)};
    }

    wayfire_view find_view_in_grid(int row, int col)
    {
        for (auto& view : get_views())
        {
            if ((scale_data[view].row == row) && (scale_data[view].col == col))
            {
                return view;
            }
        }

        return get_views().front();
    }

    void process_key(uint32_t key, uint32_t state)
    {
        if (!active)
        {
            finish_input();

            return;
        }

        auto view = output->get_active_view();
        if (!view)
        {
            view = last_focused_view;
            fade_out_all_except(view);
            fade_in(view);
            output->focus_view(view, true);

            return;
        }

        if (!scale_view(view) && (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            return;
        }

        int row = scale_data[view].row;
        int col = scale_data[view].col;

        if ((state == WLR_KEY_RELEASED) &&
            ((key == KEY_ENTER) || (key == KEY_ESC)))
        {
            input_release_impending = false;
        }

        if ((state != WLR_KEY_PRESSED) ||
            wf::get_core().get_keyboard_modifiers())
        {
            return;
        }

        switch (key)
        {
          case KEY_UP:
            row--;
            break;

          case KEY_DOWN:
            row++;
            break;

          case KEY_LEFT:
            col--;
            break;

          case KEY_RIGHT:
            col++;
            break;

          case KEY_ENTER:
            input_release_impending = true;
            deactivate();
            select_view(last_focused_view);

            return;

          case KEY_ESC:
            input_release_impending = true;
            deactivate();
            output->focus_view(initial_focus_view, true);
            select_view(initial_focus_view);

            return;

          default:
            return;
        }

        if ((grid_rows > 1) && (grid_cols > 1) &&
            (grid_last_row_cols > 1))
        {
            /* when moving to and from the last row, the number of columns
             * may be different, so this bit figures out which view we
             * should switch focus to */
            if (((key == KEY_DOWN) && (row == grid_rows - 1)) ||
                ((key == KEY_UP) && (row == -1)))
            {
                auto p = col / (float)(grid_cols - 1);
                col = p * (grid_last_row_cols - 1);
                col = std::clamp(col, 0, grid_last_row_cols - 1);
            } else if (((key == KEY_UP) && (row == grid_rows - 2)) ||
                       ((key == KEY_DOWN) && (row == grid_rows)))
            {
                auto p = (col + 0.5) / (float)grid_last_row_cols;
                col = p * grid_cols;
                col = std::clamp(col, 0, grid_cols - 1);
            }
        }

        if (row < 0)
        {
            row = grid_rows - 1;
        }

        if (row >= grid_rows)
        {
            row = 0;
        }

        int current_row_cols = (row == grid_rows - 1) ?
            grid_last_row_cols : grid_cols;
        if (col < 0)
        {
            col = current_row_cols - 1;
        }

        if (col >= current_row_cols)
        {
            col = 0;
        }

        view = find_view_in_grid(row, col);
        if (view && (last_focused_view != view))
        {
            fade_out_all_except(view);
        }

        if (!view)
        {
            return;
        }

        last_focused_view = view;
        output->focus_view(view, true);
        fade_in(view);
    }

    void transform_views()
    {
        for (auto& e : scale_data)
        {
            auto view = e.first;
            auto& view_data = e.second;
            if (!view || !view_data.transformer ||
                ((output->workspace->get_view_layer(view) != wf::LAYER_WORKSPACE) &&
                 (view->role != wf::VIEW_ROLE_TOPLEVEL)))
            {
                continue;
            }

            view_data.transformer->scale_x =
                view_data.animation.scale_animation.scale_x;
            view_data.transformer->scale_y =
                view_data.animation.scale_animation.scale_y;
            view_data.transformer->translation_x =
                view_data.animation.scale_animation.translation_x;
            view_data.transformer->translation_y =
                view_data.animation.scale_animation.translation_y;
            view_data.transformer->alpha = view_data.fade_animation;

            view->damage();

            for (auto& child : view->children)
            {
                if (!child)
                {
                    continue;
                }

                auto it = scale_data.find(child);
                if ((it == scale_data.end()) || !it->second.transformer)
                {
                    /* note: child views can show up here before they
                     * should be visible (between they are attached and
                     * mapped), these should be skipped */
                    continue;
                }

                auto& view_data = it->second;

                view_data.transformer->scale_x =
                    view_data.animation.scale_animation.scale_x;
                view_data.transformer->scale_y =
                    view_data.animation.scale_animation.scale_y;
                view_data.transformer->translation_x =
                    view_data.animation.scale_animation.translation_x;
                view_data.transformer->translation_y =
                    view_data.animation.scale_animation.translation_y;
                view_data.transformer->alpha = view_data.fade_animation;

                child->damage();
            }
        }

        output->render->damage_whole();
    }

    std::vector<wayfire_view> get_all_workspace_views()
    {
        std::vector<wayfire_view> views;

        views = output->workspace->get_views_in_layer(wf::LAYER_WORKSPACE);

        return std::move(views);
    }

    std::vector<wayfire_view> get_current_workspace_views()
    {
        std::vector<wayfire_view> views;

        for (auto& view :
             output->workspace->get_views_in_layer(wf::LAYER_WORKSPACE))
        {
            if (view->role != wf::VIEW_ROLE_TOPLEVEL)
            {
                continue;
            }

            auto vg = view->get_wm_geometry();
            auto og = output->get_relative_geometry();
            wf::region_t wr{og};
            wf::point_t center{vg.x + vg.width / 2, vg.y + vg.height / 2};

            if (wr.contains_point(center))
            {
                views.push_back(view);
            }
        }

        return std::move(views);
    }

    std::vector<wayfire_view> get_views()
    {
        std::vector<wayfire_view> views;

        if (all_workspaces)
        {
            views = get_all_workspace_views();
        } else
        {
            views = get_current_workspace_views();
        }

        return std::move(views);
    }

    bool scale_view(wayfire_view view)
    {
        if (!view)
        {
            return false;
        }

        bool found = false;
        auto views = get_views();
        for (auto& v : views)
        {
            if (v == view)
            {
                found = true;
                break;
            }
        }

        return found;
    }

    void setup_view_transform(view_scale_data& view_data,
        double scale_x,
        double scale_y,
        double translation_x,
        double translation_y,
        double target_alpha)
    {
        view_data.animation.scale_animation.scale_x.set(
            view_data.transformer->scale_x, scale_x);
        view_data.animation.scale_animation.scale_y.set(
            view_data.transformer->scale_y, scale_y);
        view_data.animation.scale_animation.translation_x.set(
            view_data.transformer->translation_x, translation_x);
        view_data.animation.scale_animation.translation_y.set(
            view_data.transformer->translation_y, translation_y);
        view_data.animation.scale_animation.start();
        view_data.fade_animation =
            wf::animation::simple_animation_t(wf::create_option<int>(1000));
        view_data.fade_animation.animate(view_data.transformer->alpha,
            target_alpha);
    }

    /* compiz scale plugin algorithm */
    void layout_slots(std::vector<wayfire_view> views)
    {
        if (!views.size())
        {
            if (!all_workspaces && active)
            {
                deactivate();
            }

            return;
        }

        add_transformers(views);

        auto workarea    = output->workspace->get_workarea();
        auto active_view = output->get_active_view();
        if (active_view && !scale_view(active_view))
        {
            active_view = nullptr;
        }

        if (active_view)
        {
            last_focused_view = active_view;
        } else
        {
            active_view = last_focused_view = views.front();
        }

        if (!initial_focus_view)
        {
            initial_focus_view = active_view;
        }

        if (all_workspaces)
        {
            output->focus_view(active_view, true);
        }

        fade_in(active_view);
        fade_out_all_except(active_view);
        int lines = sqrt(views.size() + 1);
        grid_rows = lines;
        grid_cols = (int)std::ceil((double)views.size() / lines);
        grid_last_row_cols = std::min(grid_cols, (int)views.size() -
            (grid_rows - 1) * grid_cols);
        int slots = 0;

        int i, j, n;
        double x, y, width, height;

        y = workarea.y + (int)spacing;
        height = (workarea.height - (lines + 1) * (int)spacing) / lines;

        std::sort(views.begin(), views.end());

        for (i = 0; i < lines; i++)
        {
            n = (i == lines - 1) ? grid_last_row_cols : grid_cols;

            std::vector<size_t> row;
            x     = workarea.x + (int)spacing;
            width = (workarea.width - (n + 1) * (int)spacing) / n;

            for (j = 0; j < n; j++)
            {
                auto view = views[slots];
                auto& view_data = scale_data[view];

                auto vg = view->get_wm_geometry();

                double scale_x    = width / vg.width;
                double scale_y    = height / vg.height;
                int translation_x = x - vg.x + ((width - vg.width) / 2.0);
                int translation_y = y - vg.y + ((height - vg.height) / 2.0);

                scale_x = scale_y = std::min(scale_x, scale_y);
                if (!allow_scale_zoom)
                {
                    scale_x = scale_y = std::min(scale_x, max_scale_factor);
                }

                double target_alpha;
                if (active)
                {
                    target_alpha = (view == active_view) ? 1 :
                        (double)inactive_alpha;
                    setup_view_transform(view_data, scale_x, scale_y,
                        translation_x, translation_y, target_alpha);
                } else
                {
                    target_alpha = 1;
                    setup_view_transform(view_data, 1, 1, 0, 0, 1);
                }

                view_data.row = i;
                view_data.col = j;

                for (auto& child : view->children)
                {
                    vg = child->get_wm_geometry();

                    double child_scale_x = width / vg.width;
                    double child_scale_y = height / vg.height;
                    child_scale_x = child_scale_y = std::min(child_scale_x,
                        child_scale_y);

                    if (!allow_scale_zoom)
                    {
                        child_scale_x = child_scale_y = std::min(child_scale_x,
                            max_scale_factor);
                        if ((max_scale_child > 0.0) &&
                            (child_scale_x > max_scale_child * scale_x))
                        {
                            child_scale_x = max_scale_child * scale_x;
                            child_scale_y = child_scale_x;
                        }
                    }

                    translation_x = x - vg.x + ((width - vg.width) / 2.0);
                    translation_y = y - vg.y + ((height - vg.height) / 2.0);

                    auto& view_data = scale_data[child];

                    if (active)
                    {
                        setup_view_transform(view_data, scale_x, scale_y,
                            translation_x, translation_y, target_alpha);
                    } else
                    {
                        setup_view_transform(view_data, 1, 1, 0, 0, 1);
                    }

                    view_data.row = i;
                    view_data.col = j;
                }

                x += width + (int)spacing;

                slots++;
            }

            y += height + (int)spacing;
        }

        set_hook();
        transform_views();
    }

    wf::config::option_base_t::updated_callback_t interact_option_changed = [=] ()
    {
        if (!output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        if (interact)
        {
            connect_button_signal();

            return;
        }

        grab_interface->grab();
        disconnect_button_signal();
    };

    void all_workspaces_option_changed()
    {
        if (!output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        if (all_workspaces)
        {
            layout_slots(get_views());

            return;
        }

        bool rearrange = false;
        auto views     = get_views();
        for (auto& e : scale_data)
        {
            auto view  = e.first;
            bool found = false;
            for (auto& v : views)
            {
                if (v == view)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                auto& view_data = scale_data[view];
                setup_view_transform(view_data, 1, 1, 0, 0, 1);
                rearrange = true;
            }
        }

        if (rearrange)
        {
            layout_slots(get_views());
        }
    }

    wf::config::option_base_t::updated_callback_t allow_scale_zoom_option_changed =
        [=] ()
    {
        if (!output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        layout_slots(get_views());
    };

    wf::signal_connection_t view_attached{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);

            auto it = scale_data.find(view->parent);
            if (it != scale_data.end())
            {
                layout_slots(get_views());

                return;
            }

            if (!scale_view(view) && (view->role != wf::VIEW_ROLE_TOPLEVEL))
            {
                return;
            }

            auto v = view;
            while (v->parent)
            {
                v = v->parent;
            }

            last_focused_view = v;
            output->focus_view(v, true);

            it = scale_data.find(view);
            if (it != scale_data.end())
            {
                if (!view->get_transformer(transformer_name))
                {
                    layout_slots(get_views());
                }

                return;
            }

            add_transformer(view);
            layout_slots(get_views());
        }
    };

    wf::signal_connection_t view_detached{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);

            auto it = scale_data.find(view->parent);
            if (it != scale_data.end())
            {
                if (view == last_focused_view)
                {
                    last_focused_view = output->get_active_view();
                }

                remove_view(view);
                auto views = get_views();
                if (!views.size())
                {
                    finalize();
                }

                return;
            }

            it = scale_data.find(view);
            if (it == scale_data.end())
            {
                return;
            }

            if (view == last_focused_view)
            {
                last_focused_view = output->get_active_view();
            }

            remove_view(view);

            auto views = get_views();
            if (!views.size())
            {
                finalize();

                return;
            }

            layout_slots(std::move(views));
        }
    };

    wf::signal_connection_t workspace_changed{[this] (wf::signal_data_t *data)
        {
            if (last_focused_view)
            {
                output->focus_view(last_focused_view, true);
            }
        }
    };

    wf::signal_connection_t view_geometry_changed{[this] (wf::signal_data_t *data)
        {
            auto views = get_views();
            if (!views.size())
            {
                deactivate();

                return;
            }

            layout_slots(std::move(views));
        }
    };

    wf::signal_connection_t view_minimized{[this] (wf::signal_data_t *data)
        {
            auto ev = static_cast<wf::view_minimized_signal*>(data);

            if (ev->state)
            {
                remove_view(ev->view);
                if (scale_data.empty())
                {
                    deactivate();

                    return;
                }
            } else if (!scale_view(ev->view))
            {
                return;
            }

            layout_slots(get_views());
        }
    };

    wf::signal_connection_t view_unmapped{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);

            if (view == last_focused_view)
            {
                last_focused_view = output->get_active_view();
            }
        }
    };

    wf::signal_connection_t view_focused{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);

            fade_out_all_except(view);
            fade_in(view);

            if ((view == last_focused_view) ||
                (view && (view == output->get_active_view())))
            {
                if (view && last_focused_view && (view != last_focused_view))
                {
                    auto v = last_focused_view;
                    while (v->parent)
                    {
                        v = v->parent;
                    }

                    if (v == view)
                    {
                        return;
                    }

                    last_focused_view = v;
                    output->focus_view(v, true);
                }

                return;
            }

            view = last_focused_view;

            if (!view || view->minimized || !view->is_mapped())
            {
                return;
            }

            if (all_workspaces)
            {
                output->focus_view(view, true);
            }

            layout_slots(get_views());
        }
    };

    void refocus()
    {
        wayfire_view next_focus = nullptr;
        auto views = get_current_workspace_views();

        for (auto v : views)
        {
            if (v->is_mapped() &&
                v->get_keyboard_focus_surface())
            {
                next_focus = v;
                break;
            }
        }

        output->focus_view(next_focus, true);
    }

    bool animation_running()
    {
        for (auto& e : scale_data)
        {
            if (e.second.fade_animation.running() ||
                e.second.animation.scale_animation.running())
            {
                return true;
            }

            for (auto& child : e.first->children)
            {
                if (scale_data[child].fade_animation.running() ||
                    scale_data[child].animation.scale_animation.running())
                {
                    return true;
                }
            }
        }

        return false;
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        transform_views();
    };

    wf::effect_hook_t post_hook = [=] ()
    {
        output->render->schedule_redraw();

        if (animation_running())
        {
            return;
        }

        unset_hook();

        if (active)
        {
            return;
        }

        finalize();
    };

    bool activate()
    {
        if (active)
        {
            return false;
        }

        grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;

        if (!output->is_plugin_active(grab_interface->name) &&
            !output->activate_plugin(grab_interface))
        {
            return false;
        }

        auto views = get_views();
        if (!views.size())
        {
            output->deactivate_plugin(grab_interface);

            return false;
        }

        initial_focus_view = output->get_active_view();
        if (!interact)
        {
            if (!grab_interface->grab())
            {
                deactivate();

                return false;
            }

            if (initial_focus_view)
            {
                output->focus_view(initial_focus_view, true);
            }
        }

        active = true;

        layout_slots(get_views());

        if (interact)
        {
            connect_button_signal();
        }

        output->connect_signal("view-layer-attached", &view_attached);
        output->connect_signal("view-attached", &view_attached);
        view_detached.disconnect();
        output->connect_signal("workspace-changed", &workspace_changed);
        output->connect_signal("view-layer-detached", &view_detached);
        output->connect_signal("view-minimized", &view_minimized);
        output->connect_signal("view-unmapped", &view_unmapped);
        output->connect_signal("view-focused", &view_focused);

        view_geometry_changed.disconnect();
        for (auto& e : scale_data)
        {
            auto view = e.first;
            view->connect_signal("geometry-changed", &view_geometry_changed);
            if ((view == initial_focus_view) || (view->parent == initial_focus_view))
            {
                continue;
            }

            fade_out(view);
        }

        return true;
    }

    void deactivate()
    {
        active = false;

        set_hook();
        view_focused.disconnect();
        view_unmapped.disconnect();
        view_attached.disconnect();
        view_minimized.disconnect();
        workspace_changed.disconnect();
        view_geometry_changed.disconnect();

        if (!input_release_impending)
        {
            grab_interface->ungrab();
            output->deactivate_plugin(grab_interface);
        }

        for (auto& e : scale_data)
        {
            fade_in(e.first);
            setup_view_transform(e.second, 1, 1, 0, 0, 1);
        }

        refocus();
        grab_interface->capabilities = 0;
    }

    void finalize()
    {
        active = false;
        input_release_impending = false;

        unset_hook();
        remove_transformers();
        scale_data.clear();
        grab_interface->ungrab();
        disconnect_button_signal();
        view_focused.disconnect();
        view_unmapped.disconnect();
        view_attached.disconnect();
        view_detached.disconnect();
        view_minimized.disconnect();
        workspace_changed.disconnect();
        view_geometry_changed.disconnect();
        output->deactivate_plugin(grab_interface);
    }

    void set_hook()
    {
        if (hook_set)
        {
            return;
        }

        output->render->add_effect(&post_hook, wf::OUTPUT_EFFECT_POST);
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->render->schedule_redraw();
        hook_set = true;
    }

    void unset_hook()
    {
        if (!hook_set)
        {
            return;
        }

        output->render->rem_effect(&post_hook);
        output->render->rem_effect(&pre_hook);
        hook_set = false;
    }

    void fini() override
    {
        finalize();
        output->rem_binding(&toggle_cb);
        output->rem_binding(&toggle_all_cb);
        output->deactivate_plugin(grab_interface);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_scale);
