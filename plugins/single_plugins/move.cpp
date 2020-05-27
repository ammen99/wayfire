#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/output-layout.hpp>

#include <cmath>
#include <linux/input.h>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/preview-indication.hpp>

#include "snap_signal.hpp"
#include "move-snap-helper.hpp"
#include <wayfire/plugins/common/view-change-viewport-signal.hpp>

class wf_move_mirror_view : public wf::mirror_view_t
{
    int _dx, _dy;
    wf::geometry_t geometry;
    public:

    wf_move_mirror_view(wayfire_view view, wf::output_t *output, int dx, int dy) :
        wf::mirror_view_t(view), _dx(dx), _dy(dy)
    {
        set_output(output);
        get_output()->workspace->add_view(self(), wf::LAYER_WORKSPACE);
        emit_map_state_change(this);
    }

    virtual wf::geometry_t get_output_geometry()
    {
        if (base_view)
            geometry = base_view->get_bounding_box() + wf::point_t{_dx, _dy};

        return geometry;
    }

    /* By default show animation. If move doesn't want it, it will reset this
     * flag.  Plus, we want to show animation if the view itself is destroyed
     * (and in this case unmap comes not from move, but from the mirror-view
     * implementation) */
    bool show_animation = true;

    virtual void close()
    {
        if (show_animation)
            emit_view_pre_unmap();

        wf::mirror_view_t::close();
    }
};


class wayfire_move : public wf::plugin_interface_t
{
    wf::signal_callback_t move_request, view_destroyed;
    wf::button_callback activate_binding;
    wf::touch_callback touch_activate_binding;
    wayfire_view view;

    wf::option_wrapper_t<bool> enable_snap{"move/enable_snap"};
    wf::option_wrapper_t<bool> join_views{"move/join_views"};
    wf::option_wrapper_t<int> snap_threshold{"move/snap_threshold"};
    wf::option_wrapper_t<wf::buttonbinding_t> activate_button{"move/activate"};

    bool is_using_touch;
    bool was_client_request;

    struct {
        nonstd::observer_ptr<wf::preview_indication_view_t> preview;
        int slot_id = 0;
    } slot;

#define MOVE_HELPER view->get_data<wf::move_snap_helper_t>()

    public:
        void init() override
        {
            grab_interface->name = "move";
            grab_interface->capabilities =
                wf::CAPABILITY_GRAB_INPUT | wf::CAPABILITY_MANAGE_DESKTOP;

            activate_binding = [=] (uint32_t, int, int)
            {
                is_using_touch = false;
                was_client_request = false;
                auto view = wf::get_core().get_cursor_focus_view();

                if (view && view->role != wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
                    return initiate(view);

                return false;
            };

            touch_activate_binding = [=] (int32_t sx, int32_t sy)
            {
                is_using_touch = true;
                was_client_request = false;
                auto view = wf::get_core().get_touch_focus_view();

                if (view && view->role != wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
                    return initiate(view);

                return false;
            };

            output->add_button(activate_button, &activate_binding);
            output->add_touch(
                wf::create_option_string<wf::keybinding_t>("<super>"),
                &touch_activate_binding);

            using namespace std::placeholders;
            grab_interface->callbacks.pointer.button =
            [=] (uint32_t b, uint32_t state)
            {
                /* the request usually comes with the left button ... */
                if (state == WLR_BUTTON_RELEASED && was_client_request && b == BTN_LEFT)
                    return input_pressed(state, false);

                if (b != wf::buttonbinding_t(activate_button).get_button())
                    return;

                is_using_touch = false;
                input_pressed(state, false);
            };

            grab_interface->callbacks.pointer.motion = [=] (int x, int y)
            {
                handle_input_motion();
            };

            grab_interface->callbacks.touch.motion = [=] (int32_t id, int32_t sx, int32_t sy)
            {
                if (id > 0) return;
                handle_input_motion();
            };

            grab_interface->callbacks.touch.up = [=] (int32_t id)
            {
                if (id == 0)
                    input_pressed(WLR_BUTTON_RELEASED, false);
            };

            grab_interface->callbacks.cancel = [=] ()
            {
                input_pressed(WLR_BUTTON_RELEASED, false);
            };

            move_request = std::bind(std::mem_fn(&wayfire_move::move_requested), this, _1);
            output->connect_signal("move-request", &move_request);

            view_destroyed = [=] (wf::signal_data_t* data)
            {
                if (get_signaled_view(data) == view)
                    input_pressed(WLR_BUTTON_RELEASED, true);
            };
            output->connect_signal("detach-view", &view_destroyed);
            output->connect_signal("view-disappeared", &view_destroyed);
        }

        void move_requested(wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);
            if (!view)
                return;

            auto touch = wf::get_core().get_touch_position(0);
            if (!std::isnan(touch.x) && !std::isnan(touch.y)) {
                is_using_touch = true;
            } else {
                is_using_touch = false;
            }

            was_client_request = true;
            initiate(view);
        }

        bool initiate(wayfire_view view)
        {
            if (!view || !view->is_mapped())
                return false;

            while (view->parent && join_views)
                view = view->parent;

            auto current_ws_impl =
                output->workspace->get_workspace_implementation();
            if (!current_ws_impl->view_movable(view))
                return false;

            if (view->get_output() != output)
                return false;

            uint32_t view_layer = output->workspace->get_view_layer(view);
            /* Allow moving an on-screen keyboard while screen is locked */
            bool ignore_inhibit = view_layer == wf::LAYER_DESKTOP_WIDGET;
            uint32_t act_flags = 0;
            if (ignore_inhibit)
                act_flags |= wf::PLUGIN_ACTIVATION_IGNORE_INHIBIT;

            if (!output->activate_plugin(grab_interface, act_flags))
                return false;

            if (!grab_interface->grab()) {
                output->deactivate_plugin(grab_interface);
                return false;
            }

            view->store_data(std::make_unique<wf::move_snap_helper_t> (
                    view, get_input_coords()));

            output->focus_view(view, true);
            if (enable_snap)
                slot.slot_id = 0;

            this->view = view;
            output->render->set_redraw_always();
            update_multi_output();

            return true;
        }

        void input_pressed(uint32_t state, bool view_destroyed)
        {
            if (state != WLR_BUTTON_RELEASED)
                return;

            grab_interface->ungrab();
            output->deactivate_plugin(grab_interface);
            output->render->set_redraw_always(false);

            /* The view was moved to another output or was destroyed,
             * we don't have to do anything more */
            if (view_destroyed)
            {
                view->erase_data<wf::move_snap_helper_t>();
                this->view = nullptr;
                return;
            }

            MOVE_HELPER->handle_input_released();
            view->erase_data<wf::move_snap_helper_t>();

            /* Delete any mirrors we have left, showing an animation */
            delete_mirror_views(true);

            /* Don't do snapping, etc for shell views */
            if (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
            {
                this->view = nullptr;
                return;
            }

            /* Snap the view */
            if (enable_snap && slot.slot_id != 0)
            {
                snap_signal data;
                data.view = view;
                data.slot = (slot_type)slot.slot_id;
                output->emit_signal("view-snap", &data);

                /* Update slot, will hide the preview as well */
                update_slot(0);
            }

            view_change_viewport_signal workspace_may_changed;
            workspace_may_changed.view = this->view;
            workspace_may_changed.to = output->workspace->get_current_workspace();
            workspace_may_changed.old_viewport_invalid = false;
            output->emit_signal("view-change-viewport", &workspace_may_changed);

            this->view = nullptr;
        }

        /* Calculate the slot to which the view would be snapped if the input
         * is released at output-local coordinates (x, y) */
        int calc_slot(int x, int y)
        {
            auto g = output->workspace->get_workarea();
            if (!(output->get_relative_geometry() & wf::point_t{x, y}))
                return 0;

            if (view && output->workspace->get_view_layer(view) != wf::LAYER_WORKSPACE)
                return 0;

            int threshold = snap_threshold;

            bool is_left = x - g.x <= threshold;
            bool is_right = g.x + g.width - x <= threshold;
            bool is_top = y - g.y < threshold;
            bool is_bottom = g.x + g.height - y < threshold;

            int slot = 1;
            if (is_top) {
                slot += 6; // top slots are 7, 8, 9
            } else if (!is_bottom) { // one of 4, 5, 6
                slot += 3;
            }

            if (is_right) {
                slot += 2; // one of 3, 6, 9
            } else if (!is_left) {
                slot += 1;
            }

            if (slot == 5) // in the center, no snap
                slot = 0;

            if (slot == 8) // maximize is drag to top
                slot = 5;

            return slot;
        }

        void update_slot(int new_slot_id)
        {
            /* No changes in the slot, just return */
            if (slot.slot_id == new_slot_id)
                return;

            /* Destroy previous preview */
            if (slot.preview)
            {
                auto input = get_input_coords();
                slot.preview->set_target_geometry(
                    {input.x, input.y, 1, 1}, 0, true);
                slot.preview = nullptr;
            }

            slot.slot_id = new_slot_id;

            /* Show a preview overlay */
            if (new_slot_id)
            {
                snap_query_signal query;
                query.slot = (slot_type)new_slot_id;
                query.out_geometry = {0, 0, -1, -1};
                output->emit_signal("query-snap-geometry", &query);

                /* Unknown slot geometry, can't show a preview */
                if (query.out_geometry.width <= 0 || query.out_geometry.height <= 0)
                    return;

                auto input = get_input_coords();
                auto preview = new wf::preview_indication_view_t(output,
                    {input.x, input.y, 1, 1});

                wf::get_core().add_view(
                    std::unique_ptr<wf::view_interface_t> (preview));

                preview->set_output(output);
                preview->set_target_geometry(query.out_geometry, 1);
                slot.preview = nonstd::make_observer(preview);
            }
        }

        /* Returns the currently used input coordinates in global compositor space */
        wf::point_t get_global_input_coords()
        {
            wf::pointf_t input;
            if (is_using_touch) {
                input = wf::get_core().get_touch_position(0);
            } else {
                input = wf::get_core().get_cursor_position();
            }

            return {(int)input.x, (int)input.y};
        }

        /* Returns the currently used input coordinates in output-local space */
        wf::point_t get_input_coords()
        {
            auto og = output->get_layout_geometry();
            auto coords = get_global_input_coords() - wf::point_t{og.x, og.y};

            /* If the currently moved view is not a toplevel view, but a child
             * view, do not move it outside its outpup */
            if (view && view->parent)
            {
                double x = coords.x;
                double y = coords.y;
                auto local = output->get_relative_geometry();
                wlr_box_closest_point(&local, x, y, &x, &y);
                coords = {(int)x, (int)y};
            }

            return coords;
        }

        /* Moves the view to another output and sends a move request */
        void move_to_output(wf::output_t *new_output)
        {
            move_request_signal req;
            req.view = view;

            auto old_g = output->get_layout_geometry();
            auto new_g = new_output->get_layout_geometry();
            auto wm_g = view->get_wm_geometry();

            int dx = old_g.x - new_g.x;
            int dy = old_g.y - new_g.y;

            /* First erase the move snap helper, so that we can set the
             * correct position on the other output. */
            view->erase_data<wf::move_snap_helper_t> ();
            view->move(wm_g.x + dx, wm_g.y + dy);
            wf::get_core().move_view_to_output(view, new_output);
            wf::get_core().focus_output(new_output);

            new_output->emit_signal("move-request", &req);
        }

        struct wf_move_output_state : public wf::custom_data_t
        {
            nonstd::observer_ptr<wf_move_mirror_view> view;
        };

        std::string get_data_name()
        {
            return "wf-move-" + output->to_string();
        }

        /* Delete the mirror view on the given output.
         * If the view hasn't been unmapped yet, then do so. */
        void delete_mirror_view_from_output(wf::output_t *wo,
            bool show_animation, bool already_unmapped)
        {
            if (!wo->has_data(get_data_name()))
                return;

            auto view = wo->get_data<wf_move_output_state> (get_data_name())->view;
            /* We erase so early so that in case of already_unmapped == false,
             * we don't do this again for the unmap signal which will be triggered
             * by our view->unmap() call */
            wo->erase_data(get_data_name());

            view->show_animation = show_animation;
            if (!already_unmapped)
                view->close();

            wo->erase_data(get_data_name());
        }

        /* Destroys all mirror views created by this plugin */
        void delete_mirror_views(bool show_animation)
        {
            for (auto& wo : wf::get_core().output_layout->get_outputs())
            {
                delete_mirror_view_from_output(wo,
                    show_animation, false);
            }
        }

        wf::signal_callback_t handle_mirror_view_unmapped =
            [=] (wf::signal_data_t* data)
        {
            auto view = get_signaled_view(data);
            delete_mirror_view_from_output(view->get_output(), true, true);
            view->disconnect_signal("unmap", &handle_mirror_view_unmapped);
        };

        /* Creates a new mirror view on output wo if it doesn't exist already */
        void ensure_mirror_view(wf::output_t *wo)
        {
            if (wo->has_data(get_data_name()))
                return;

            auto base_output = output->get_layout_geometry();
            auto mirror_output = wo->get_layout_geometry();

            auto mirror = new wf_move_mirror_view(view, wo,
                base_output.x - mirror_output.x,
                base_output.y - mirror_output.y);

            wf::get_core().add_view(
                std::unique_ptr<wf::view_interface_t> (mirror));

            auto wo_state = wo->get_data_safe<wf_move_output_state> (get_data_name());
            wo_state->view = nonstd::make_observer(mirror);
            mirror->connect_signal("unmap", &handle_mirror_view_unmapped);
        }

        /* Update the view position, with respect to the multi-output configuration
         *
         * Views in wayfire are visible on only a single output. However, when the user
         * moves the view between outputs, it is desirable to temporarily show the view
         * on all outputs whose boundaries it crosses. We emulate this behavior by creating
         * mirror views of the view being moved, while fading them in and out when needed */
        void update_multi_output()
        {
            /* We are not in the join_view mode, so we can move dialogues
             * independently of their main view. However, we do not support
             * moving dialogues to a different output than their main view. */
            if (this->view && this->view->parent)
                return;

            /* The mouse isn't on our output anymore -> transfer ownership of
             * the move operation to the other output where the input currently is */
            auto global = get_global_input_coords();
            auto target_output =
                wf::get_core().output_layout->get_output_at(global.x, global.y);

            if (target_output != output)
            {
                /* The move plugin on the next output will create new mirror views */
                delete_mirror_views(false);
                move_to_output(target_output);
                return;
            }

            auto current_og = output->get_layout_geometry();
            auto current_geometry =
                view->get_bounding_box() + wf::point_t{current_og.x, current_og.y};

            for (auto& wo : wf::get_core().output_layout->get_outputs())
            {
                if (wo == output) // skip the same output
                    continue;

                auto og = output->get_layout_geometry();
                /* A view is visible on the other output as well */
                if (og & current_geometry)
                    ensure_mirror_view(wo);
            }
        }

        void handle_input_motion()
        {
            auto input = get_input_coords();
            MOVE_HELPER->handle_motion(get_input_coords());

            update_multi_output();
            /* View might get destroyed when updating multi-output */
            if (view)
            {
                if (enable_snap && !MOVE_HELPER->is_view_fixed())
                    update_slot(calc_slot(input.x, input.y));
            } else
            {
                /* View was destroyed, hide slot */
                update_slot(0);
            }
        }

        void fini() override
        {
            if (grab_interface->is_grabbed())
                input_pressed(WLR_BUTTON_RELEASED, false);

            output->rem_binding(&activate_binding);
            output->rem_binding(&touch_activate_binding);
            output->disconnect_signal("move-request", &move_request);
            output->disconnect_signal("detach-view", &view_destroyed);
            output->disconnect_signal("view-disappeared", &view_destroyed);
        }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_move);
