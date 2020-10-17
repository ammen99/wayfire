/**
 * Original code by: Scott Moreau, Daniel Kondor
 */
#include <map>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/vswitch.hpp>
#include <wayfire/touch/touch.hpp>

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
    wf_scale *transformer = nullptr; /* avoid UB from uninitialized member */
    wf::animation::simple_animation_t fade_animation;
    wf_scale_animation_attribs animation;
};

class wayfire_scale : public wf::plugin_interface_t
{
    int grid_cols;
    int grid_rows;
    int grid_last_row_cols;
    wf::point_t initial_workspace;
    bool input_release_impending = false;
    bool active, hook_set;
    const std::string transformer_name = "scale";
    /* View that was active before scale began. */
    wayfire_view initial_focus_view;
    /* View that has active focus. */
    wayfire_view current_focus_view;
    // View over which the last input press happened, might become dangling
    wayfire_view last_selected_view;
    std::map<wayfire_view, view_scale_data> scale_data;
    wf::option_wrapper_t<int> spacing{"scale/spacing"};
    /* If interact is true, no grab is acquired and input events are sent
     * to the scaled surfaces. If it is false, the hard coded bindings
     * are as follows:
     * KEY_ENTER:
     * - Ends scale, switching to the workspace of the focused view
     * KEY_ESC:
     * - Ends scale, switching to the workspace where scale was started,
     *   and focuses the initially active view
     * KEY_UP:
     * KEY_DOWN:
     * KEY_LEFT:
     * KEY_RIGHT:
     * - When scale is active, change focus of the views
     *
     * BTN_LEFT:
     * - Ends scale, switching to the workspace of the surface clicked
     * BTN_MIDDLE:
     * - If middle_click_close is true, closes the view clicked
     */
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
    std::unique_ptr<wf::vswitch::control_bindings_t> workspace_bindings;

  public:
    void init() override
    {
        grab_interface->name = "scale";
        grab_interface->capabilities = 0;

        active = hook_set = false;

        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"scale/toggle"},
            &toggle_cb);
        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"scale/toggle_all"},
            &toggle_all_cb);

        grab_interface->callbacks.keyboard.key = [=] (uint32_t key, uint32_t state)
        {
            process_key(key, state);
        };

        grab_interface->callbacks.cancel = [=] ()
        {
            finalize();
        };

        interact.set_callback(interact_option_changed);
        allow_scale_zoom.set_callback(allow_scale_zoom_option_changed);

        setup_workspace_switching();
    }

    void setup_workspace_switching()
    {
        workspace_bindings =
            std::make_unique<wf::vswitch::control_bindings_t>(output);
        workspace_bindings->setup([&] (wf::point_t delta, wayfire_view view)
        {
            if (!output->is_plugin_active(grab_interface->name))
            {
                return false;
            }

            auto ws = output->workspace->get_current_workspace() + delta;
            if (!output->workspace->is_workspace_valid(ws))
            {
                return false;
            }

            // vswitch picks the top view, we want the focused one
            std::vector<wayfire_view> fixed_views;
            if (view && !all_workspaces)
            {
                fixed_views.push_back(current_focus_view);
            }

            output->workspace->request_workspace(ws, fixed_views);

            return true;
        });
    }

    /* Add a transformer that will be used to scale the view */
    bool add_transformer(wayfire_view view)
    {
        if (view->get_transformer(transformer_name))
        {
            return false;
        }

        wf_scale *tr = new wf_scale(view);
        scale_data[view].transformer = tr;
        view->add_transformer(std::unique_ptr<wf_scale>(tr), transformer_name);
        /* Transformers are added only once when scale is activated so
         * this is a good place to connect the geometry-changed handler */
        view->connect_signal("geometry-changed", &view_geometry_changed);

        return true;
    }

    /* Remove the scale transformer from the view */
    void pop_transformer(wayfire_view view)
    {
        view->pop_transformer(transformer_name);
    }

    /* Remove scale transformers from all views */
    void remove_transformers()
    {
        for (auto& e : scale_data)
        {
            for (auto& toplevel : e.first->enumerate_views(false))
            {
                pop_transformer(toplevel);
            }
        }
    }

    /* Check whether views exist on other workspaces */
    bool all_same_as_current_workspace_views()
    {
        return get_all_workspace_views().size() ==
               get_current_workspace_views().size();
    }

    /* Activate scale, switch activator modes and deactivate */
    bool handle_toggle(bool want_all_workspaces)
    {
        if (active && (all_same_as_current_workspace_views() ||
                       (want_all_workspaces == this->all_workspaces)))
        {
            deactivate();

            return true;
        }

        this->all_workspaces = want_all_workspaces;
        if (active)
        {
            switch_scale_modes();

            return true;
        } else
        {
            return activate();
        }
    }

    /* Activate scale for views on the current workspace */
    wf::activator_callback toggle_cb = [=] (wf::activator_source_t, uint32_t)
    {
        if (handle_toggle(false))
        {
            output->render->schedule_redraw();

            return true;
        }

        return false;
    };

    /* Activate scale for views on all workspaces */
    wf::activator_callback toggle_all_cb = [=] (wf::activator_source_t, uint32_t)
    {
        if (handle_toggle(true))
        {
            output->render->schedule_redraw();

            return true;
        }

        return false;
    };

    /* Connect button signal */
    void connect_button_signal()
    {
        disconnect_button_signal();
        wf::get_core().connect_signal("pointer_button_post", &on_button_event);
        wf::get_core().connect_signal("touch_down_post", &on_touch_down_event);
        // connect to the signal before touching up, so that the touch point
        // is still active.
        wf::get_core().connect_signal("touch_up", &on_touch_up_event);
    }

    /* Disconnect button signal */
    void disconnect_button_signal()
    {
        on_button_event.disconnect();
        on_touch_down_event.disconnect();
        on_touch_up_event.disconnect();
    }

    /* For button processing without grabbing */
    wf::signal_connection_t on_button_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_event_pointer_button>*>(data);

        process_input(ev->event->button, ev->event->state,
            wf::get_core().get_cursor_position());
    };

    wf::signal_connection_t on_touch_down_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_event_touch_down>*>(data);
        if (ev->event->touch_id == 0)
        {
            process_input(BTN_LEFT, WLR_BUTTON_PRESSED,
                wf::get_core().get_touch_position(0));
        }
    };

    wf::signal_connection_t on_touch_up_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_event_touch_up>*>(data);
        if (ev->event->touch_id == 0)
        {
            process_input(BTN_LEFT, WLR_BUTTON_RELEASED,
                wf::get_core().get_touch_position(0));
        }
    };

    /** Return the topmost parent */
    wayfire_view get_top_parent(wayfire_view view)
    {
        while (view && view->parent)
        {
            view = view->parent;
        }

        return view;
    }

    /* Fade all views' alpha to inactive alpha except the
     * view argument */
    void fade_out_all_except(wayfire_view view)
    {
        for (auto& e : scale_data)
        {
            auto v = e.first;
            if (get_top_parent(v) == get_top_parent(view))
            {
                continue;
            }

            fade_out(v);
        }
    }

    /* Fade in view alpha */
    void fade_in(wayfire_view view)
    {
        if (!view || !scale_data.count(view))
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

    /* Fade out view alpha */
    void fade_out(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        set_hook();
        for (auto v : view->enumerate_views(false))
        {
            // Could happen if we have a never-mapped child view
            if (!scale_data.count(v))
            {
                continue;
            }

            auto alpha = scale_data[v].transformer->alpha;
            scale_data[v].fade_animation.animate(alpha, (double)inactive_alpha);
        }
    }

    /* Switch to the workspace for the untransformed view geometry */
    void select_view(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        auto ws = get_view_main_workspace(view);
        output->workspace->request_workspace(ws);
    }

    /* To avoid sending key up events to clients on enter to select */
    void finish_input()
    {
        input_release_impending = false;
        grab_interface->ungrab();
        if (!animation_running())
        {
            finalize();
        }
    }

    /* Updates current and initial view focus variables accordingly */
    void check_focus_view(wayfire_view view)
    {
        if (view == current_focus_view)
        {
            current_focus_view = output->get_active_view();
        }

        if (view == initial_focus_view)
        {
            initial_focus_view = nullptr;
        }
    }

    /* Remove transformer from view and remove view from the scale_data map */
    void remove_view(wayfire_view view)
    {
        if (!view)
        {
            return;
        }

        for (auto v : view->enumerate_views(false))
        {
            check_focus_view(v);
            pop_transformer(v);
            scale_data.erase(v);
        }
    }

    /* Process button event */
    void process_input(uint32_t button, uint32_t state,
        wf::pointf_t input_position)
    {
        if (!active)
        {
            return;
        }

        if (state == WLR_BUTTON_PRESSED)
        {
            auto view = wf::get_core().get_view_at(input_position);
            if (view && should_scale_view(view))
            {
                // Mark the view as the target of the next input release operation
                last_selected_view = view;
            } else
            {
                last_selected_view = nullptr;
            }

            return;
        }

        auto view = wf::get_core().get_view_at(input_position);
        if (!view || (last_selected_view != view))
        {
            // Operation was cancelled, for ex. dragged outside of the view
            return;
        }

        switch (button)
        {
          case BTN_LEFT:
            // Focus the view under the mouse
            current_focus_view = view;
            output->focus_view(view, false);
            fade_out_all_except(view);
            fade_in(get_top_parent(view));
            if (!interact)
            {
                // End scale
                initial_focus_view = nullptr;
                deactivate();
                select_view(view);
            }

            break;

          case BTN_MIDDLE:
            // Check kill the view
            if (middle_click_close)
            {
                view->close();
            }

            break;

          default:
            break;
        }
    }

    /* Get the workspace for the center point of the untransformed view geometry */
    wf::point_t get_view_main_workspace(wayfire_view view)
    {
        while (view->parent)
        {
            view = view->parent;
        }

        auto ws = output->workspace->get_current_workspace();
        auto og = output->get_layout_geometry();
        auto vg = scale_data.count(view) > 0 ?
            view->get_bounding_box(scale_data[view].transformer) :
            view->get_bounding_box();
        auto center = wf::point_t{vg.x + vg.width / 2, vg.y + vg.height / 2};

        return wf::point_t{
            ws.x + (int)std::floor((double)center.x / og.width),
            ws.y + (int)std::floor((double)center.y / og.height)};
    }

    /* Given row and column, return a view at this position in the scale grid,
     * or the first scaled view if none is found */
    wayfire_view find_view_in_grid(int row, int col)
    {
        for (auto& view : scale_data)
        {
            if ((view.first->parent == nullptr) &&
                ((view.second.row == row) &&
                 (view.second.col == col)))
            {
                return view.first;
            }
        }

        return get_views().front();
    }

    /* Process key event */
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
            view = current_focus_view;
            fade_out_all_except(view);
            fade_in(view);
            output->focus_view(view, true);

            return;
        }

        if (!scale_data.count(view))
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
            select_view(current_focus_view);

            return;

          case KEY_ESC:
            input_release_impending = true;
            deactivate();
            output->focus_view(initial_focus_view, true);
            initial_focus_view = nullptr;
            output->workspace->request_workspace(initial_workspace);

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
        if (view && (current_focus_view != view))
        {
            // view_focused handler will update the view state
            output->focus_view(view, false);
        }
    }

    /* Assign the transformer values to the view transformers */
    void transform_views()
    {
        for (auto& e : scale_data)
        {
            auto view = e.first;
            auto& view_data = e.second;
            if (!view || !view_data.transformer)
            {
                continue;
            }

            view->damage();
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
        }

        output->render->damage_whole();
    }

    /* Returns a list of views for all workspaces */
    std::vector<wayfire_view> get_all_workspace_views()
    {
        std::vector<wayfire_view> views;

        for (auto& view :
             output->workspace->get_views_in_layer(wf::LAYER_WORKSPACE))
        {
            if ((view->role != wf::VIEW_ROLE_TOPLEVEL) || !view->is_mapped())
            {
                continue;
            }

            views.push_back(view);
        }

        return views;
    }

    /* Returns a list of views for the current workspace */
    std::vector<wayfire_view> get_current_workspace_views()
    {
        std::vector<wayfire_view> views;

        for (auto& view :
             output->workspace->get_views_in_layer(wf::LAYER_WORKSPACE))
        {
            if ((view->role != wf::VIEW_ROLE_TOPLEVEL) || !view->is_mapped())
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

        return views;
    }

    /* Returns a list of views to be scaled */
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

        return views;
    }

    /**
     * @return true if the view is to be scaled.
     */
    bool should_scale_view(wayfire_view view)
    {
        auto views = get_views();

        return std::find(
            views.begin(), views.end(), get_top_parent(view)) != views.end();
    }

    /* Convenience assignment function */
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
        view_data.fade_animation = wf::animation::simple_animation_t(
            wf::option_wrapper_t<int>{"scale/duration"});
        view_data.fade_animation.animate(view_data.transformer->alpha,
            target_alpha);
    }

    /* Compute target scale layout geometry for all the view transformers
     * and start animating. Initial code borrowed from the compiz scale
     * plugin algorithm */
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

        auto workarea = output->workspace->get_workarea();

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

                add_transformer(view);
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
                    target_alpha = (view == current_focus_view) ? 1 :
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

                for (auto& child : view->enumerate_views(false))
                {
                    // skip the view itself
                    if (child == view)
                    {
                        continue;
                    }

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

                    translation_x = view_data.transformer->translation_x;
                    translation_y = view_data.transformer->translation_y;

                    auto new_child   = add_transformer(child);
                    auto& child_data = scale_data[child];

                    if (new_child)
                    {
                        child_data.transformer->translation_x =
                            view_data.transformer->translation_x;
                        child_data.transformer->translation_y =
                            view_data.transformer->translation_y;
                    }

                    translation_x = x - vg.x + ((width - vg.width) / 2.0);
                    translation_y = y - vg.y + ((height - vg.height) / 2.0);

                    if (active)
                    {
                        setup_view_transform(child_data, scale_x, scale_y,
                            translation_x, translation_y, target_alpha);
                    } else
                    {
                        setup_view_transform(child_data, 1, 1, 0, 0, 1);
                    }

                    child_data.row = i;
                    child_data.col = j;
                }

                x += width + (int)spacing;

                slots++;
            }

            y += height + (int)spacing;
        }

        set_hook();
        transform_views();
    }

    /* Handle interact option changed */
    wf::config::option_base_t::updated_callback_t interact_option_changed = [=] ()
    {
        if (!output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        if (interact)
        {
            grab_interface->ungrab();
        } else
        {
            grab_interface->grab();
        }
    };

    /* Called when adding or removing a group of views to be scaled,
     * in this case between views on all workspaces and views on the
     * current workspace */
    void switch_scale_modes()
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
        for (auto& e : scale_data)
        {
            if (!should_scale_view(e.first))
            {
                setup_view_transform(e.second, 1, 1, 0, 0, 1);
                rearrange = true;
            }
        }

        if (rearrange)
        {
            layout_slots(get_views());
        }
    }

    /* Toggle between restricting maximum scale to 100% or allowing it
     * to become the greater. This is particularly noticeable when
     * scaling a single view or a view with child views. */
    wf::config::option_base_t::updated_callback_t allow_scale_zoom_option_changed =
        [=] ()
    {
        if (!output->is_plugin_active(grab_interface->name))
        {
            return;
        }

        layout_slots(get_views());
    };

    /* New view or view moved to output with scale active */
    wf::signal_connection_t view_attached = [this] (wf::signal_data_t *data)
    {
        if (!should_scale_view(get_signaled_view(data)))
        {
            return;
        }

        layout_slots(get_views());
    };

    void handle_view_disappeared(wayfire_view view)
    {
        if (scale_data.count(get_top_parent(view)) != 0)
        {
            remove_view(view);
            if (scale_data.empty())
            {
                finalize();
            }

            if (!view->parent)
            {
                layout_slots(get_views());
            }
        }
    }

    /* Destroyed view or view moved to another output */
    wf::signal_connection_t view_detached = [this] (wf::signal_data_t *data)
    {
        handle_view_disappeared(get_signaled_view(data));
    };

    /* Workspace changed */
    wf::signal_connection_t workspace_changed{[this] (wf::signal_data_t *data)
        {
            if (current_focus_view)
            {
                output->focus_view(current_focus_view, true);
            }
        }
    };

    /* View geometry changed. Also called when workspace changes */
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

    /* View minimized */
    wf::signal_connection_t view_minimized = [this] (wf::signal_data_t *data)
    {
        auto ev = static_cast<wf::view_minimized_signal*>(data);

        if (ev->state)
        {
            handle_view_disappeared(ev->view);
        } else if (should_scale_view(ev->view))
        {
            layout_slots(get_views());
        }
    };

    /* View unmapped */
    wf::signal_connection_t view_unmapped{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);

            check_focus_view(view);
        }
    };

    /* View focused. This handler makes sure our view remains focused */
    wf::signal_connection_t view_focused = [this] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        fade_out_all_except(view);
        fade_in(view);
        current_focus_view = view;
    };

    /* Our own refocus that uses untransformed coordinates */
    void refocus()
    {
        if (current_focus_view)
        {
            output->focus_view(current_focus_view, true);
            select_view(current_focus_view);

            return;
        }

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

    /* Returns true if any scale animation is running */
    bool animation_running()
    {
        for (auto& e : scale_data)
        {
            if (e.second.fade_animation.running() ||
                e.second.animation.scale_animation.running())
            {
                return true;
            }
        }

        return false;
    }

    /* Assign transform values to the actual transformer */
    wf::effect_hook_t pre_hook = [=] ()
    {
        transform_views();
    };

    /* Keep rendering until all animation has finished */
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

    /* Activate and start scale animation */
    bool activate()
    {
        if (active)
        {
            return false;
        }

        grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;

        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        auto views = get_views();
        if (views.empty())
        {
            output->deactivate_plugin(grab_interface);

            return false;
        }

        initial_workspace  = output->workspace->get_current_workspace();
        initial_focus_view = output->get_active_view();
        current_focus_view = initial_focus_view ?: views.front();
        // Make sure no leftover events from the activation binding
        // trigger an action in scale
        last_selected_view = nullptr;

        if (!interact)
        {
            if (!grab_interface->grab())
            {
                deactivate();

                return false;
            }
        }

        if (current_focus_view != output->get_active_view())
        {
            output->focus_view(current_focus_view, true);
        }

        active = true;

        layout_slots(get_views());

        connect_button_signal();
        output->connect_signal("view-layer-attached", &view_attached);
        output->connect_signal("view-mapped", &view_attached);
        output->connect_signal("workspace-changed", &workspace_changed);
        output->connect_signal("view-layer-detached", &view_detached);
        output->connect_signal("view-minimized", &view_minimized);
        output->connect_signal("view-unmapped", &view_unmapped);
        output->connect_signal("view-focused", &view_focused);

        fade_out_all_except(current_focus_view);
        fade_in(current_focus_view);

        return true;
    }

    /* Deactivate and start unscale animation */
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

    /* Completely end scale, including animation */
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

    /* Utility hook setter */
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

    /* Utility hook unsetter */
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
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_scale);
