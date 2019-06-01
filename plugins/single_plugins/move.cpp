#include <output.hpp>
#include <core.hpp>
#include <view.hpp>
#include <animation.hpp>
#include <workspace-manager.hpp>
#include <render-manager.hpp>
#include <compositor-view.hpp>
#include <output-layout.hpp>
#include <debug.hpp>

#include <linux/input.h>
#include <signal-definitions.hpp>

#include "snap_signal.hpp"
#include "../wobbly/wobbly-signal.hpp"

class wf_move_mirror_view : public wf::mirror_view_t
{
    int _dx, _dy;
    wf_geometry geometry;
    public:

    wf_move_mirror_view(wayfire_view view, int dx, int dy) :
        wf::mirror_view_t(view), _dx(dx), _dy(dy)
    {
        get_output()->workspace->add_view(self(), wf::LAYER_WORKSPACE);
        emit_map_state_change(this);
    }

    virtual wf_geometry get_output_geometry()
    {
        if (base_view)
            geometry = base_view->get_bounding_box() + wf_point{_dx, _dy};

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
            emit_view_pre_unmap(self());

        wf::mirror_view_t::close();
    }
};

struct move_snap_preview_animation
{
    wf_geometry start_geometry, end_geometry;
    wf_transition alpha;
};

class wf_move_snap_preview : public wf::color_rect_view_t
{
    wf::effect_hook_t pre_paint;

    const wf_color base_color = {0.5, 0.5, 1, 0.5};
    const wf_color base_border = {0.25, 0.25, 0.5, 0.8};
    const int base_border_w = 3;

    public:
    wf_duration duration;
    move_snap_preview_animation animation;

    wf_move_snap_preview(wf::output_t *output, wf_geometry start_geometry)
        : wf::color_rect_view_t()
    {
        set_output(output);

        animation.start_geometry = animation.end_geometry = start_geometry;
        animation.alpha = {0, 1};

        duration = wf_duration{new_static_option("200")};
        pre_paint = [=] () { update_animation(); };
        get_output()->render->add_effect(&pre_paint, wf::OUTPUT_EFFECT_PRE);

        set_color(base_color);
        set_border_color(base_border);
        set_border(base_border_w);

        get_output()->workspace->add_view(self(), wf::LAYER_TOP);
    }

    void set_target_geometry(wf_geometry target, float alpha = -1)
    {
        animation.start_geometry.x = duration.progress(
            animation.start_geometry.x, animation.end_geometry.x);
        animation.start_geometry.y = duration.progress(
            animation.start_geometry.y, animation.end_geometry.y);
        animation.start_geometry.width = duration.progress(
            animation.start_geometry.width, animation.end_geometry.width);
        animation.start_geometry.height = duration.progress(
            animation.start_geometry.height, animation.end_geometry.height);

        if (alpha == -1)
            alpha = animation.alpha.end;
        animation.alpha = {duration.progress(animation.alpha), alpha};

        animation.end_geometry = target;
        duration.start();
    }

    void update_animation()
    {
        wf_geometry current;
        current.x = duration.progress(animation.start_geometry.x,
            animation.end_geometry.x);
        current.y = duration.progress(animation.start_geometry.y,
            animation.end_geometry.y);
        current.width = duration.progress(animation.start_geometry.width,
            animation.end_geometry.width);
        current.height = duration.progress(animation.start_geometry.height,
            animation.end_geometry.height);

        if (current != geometry)
            set_geometry(current);


        auto alpha = duration.progress(animation.alpha);
        if (base_color.a * alpha != _color.a)
        {
            _color.a = alpha * base_color.a;
            _border_color.a = alpha * base_border.a;

            set_color(_color);
            set_border_color(_border_color);
        }

        /* The end of unmap animation, just exit */
        if (!duration.running() && animation.alpha.end <= 0.01)
            close();
    }

    virtual ~wf_move_snap_preview()
    {
        get_output()->render->rem_effect(&pre_paint);
    }
};

class wayfire_move : public wayfire_plugin_t
{
    signal_callback_t move_request, view_destroyed;
    button_callback activate_binding;
    touch_callback touch_activate_binding;
    wayfire_view view;

    wf_option enable_snap, enable_snap_off, snap_threshold, snap_off_threshold;
    bool is_using_touch;
    bool was_client_request;

    bool stuck_in_slot = false;

    struct {
        nonstd::observer_ptr<wf_move_snap_preview> preview;
        int slot_id = 0;
    } slot;

    wf_geometry grabbed_geometry;
    wf_point grab_start;

    public:
        void init(wayfire_config *config)
        {
            grab_interface->name = "move";
            grab_interface->abilities_mask = WF_ABILITY_CHANGE_VIEW_GEOMETRY | WF_ABILITY_GRAB_INPUT;

            auto section = config->get_section("move");
            wf_option button = section->get_option("activate", "<super> BTN_LEFT");
            activate_binding = [=] (uint32_t, int, int)
            {
                is_using_touch = false;
                was_client_request = false;
                auto view = wf::get_core().get_cursor_focus_view();

                if (view && view->role != wf::VIEW_ROLE_SHELL_VIEW)
                    initiate(view);
            };

            touch_activate_binding = [=] (int32_t sx, int32_t sy)
            {
                is_using_touch = true;
                was_client_request = false;
                auto view = wf::get_core().get_touch_focus_view();

                if (view && view->role != wf::VIEW_ROLE_SHELL_VIEW)
                    initiate(view);
            };

            output->add_button(button, &activate_binding);
            output->add_touch(new_static_option("<super>"), &touch_activate_binding);

            enable_snap = section->get_option("enable_snap", "1");
            enable_snap_off = section->get_option("enable_snap_off", "1");
            snap_threshold = section->get_option("snap_threshold", "2");
            snap_off_threshold = section->get_option("snap_off_threshold", "0");

            using namespace std::placeholders;
            grab_interface->callbacks.pointer.button =
            [=] (uint32_t b, uint32_t state)
            {
                /* the request usually comes with the left button ... */
                if (state == WLR_BUTTON_RELEASED && was_client_request && b == BTN_LEFT)
                    return input_pressed(state);

                if (b != button->as_button().button)
                    return;

                is_using_touch = false;
                input_pressed(state);
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
                    input_pressed(WLR_BUTTON_RELEASED);
            };

            grab_interface->callbacks.cancel = [=] ()
            {
                input_pressed(WLR_BUTTON_RELEASED);
            };

            move_request = std::bind(std::mem_fn(&wayfire_move::move_requested), this, _1);
            output->connect_signal("move-request", &move_request);

            view_destroyed = [=] (signal_data* data)
            {
                if (get_signaled_view(data) == view)
                {
                    view = nullptr;
                    input_pressed(WLR_BUTTON_RELEASED);
                }
            };
            output->connect_signal("detach-view", &view_destroyed);
            output->connect_signal("view-disappeared", &view_destroyed);
        }

        void move_requested(signal_data *data)
        {
            auto view = get_signaled_view(data);
            if (!view)
                return;

            GetTuple(tx, ty, wf::get_core().get_touch_position(0));
            if (tx != wf::compositor_core_t::invalid_coordinate &&
                ty != wf::compositor_core_t::invalid_coordinate)
            {
                is_using_touch = true;
            } else
            {
                is_using_touch = false;
            }

            was_client_request = true;
            initiate(view);
        }

        void initiate(wayfire_view view)
        {
            if (!view || !view->is_mapped())
                return;

            auto current_ws = output->workspace->get_current_workspace();
            auto current_ws_impl =
                output->workspace->get_workspace_implementation(current_ws);
            if (!current_ws_impl->view_movable(view))
                return;

            if (view->get_output() != output)
                return;

            if (!output->activate_plugin(grab_interface))
                return;

            if (!grab_interface->grab()) {
                output->deactivate_plugin(grab_interface);
                return;
            }

            stuck_in_slot = !view->tiled_edges && !view->fullscreen;
            grabbed_geometry = view->get_wm_geometry();

            GetTuple(sx, sy, get_input_coords());
            grab_start = {sx, sy};

            output->workspace->bring_to_front(view);
            if (enable_snap->as_int())
                slot.slot_id = 0;

            this->view = view;
            output->render->set_redraw_always();

            start_wobbly(view, sx, sy);
            if (!stuck_in_slot)
                snap_wobbly(view, view->get_bounding_box());

            update_multi_output();
            view->set_moving(true);
        }

        void input_pressed(uint32_t state)
        {
            if (state != WLR_BUTTON_RELEASED)
                return;

            grab_interface->ungrab();
            output->deactivate_plugin(grab_interface);
            output->render->set_redraw_always(false);

            /* The view was moved to another output or was destroyed,
             * we don't have to do anything more */
            if (!view)
                return;

            end_wobbly(view);
            view->set_moving(false);

            /* Delete any mirrors we have left, showing an animation */
            delete_mirror_views(true);

            /* Don't do snapping, etc for shell views */
            if (view->role == wf::VIEW_ROLE_SHELL_VIEW)
                return;

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

        }

        /* Calculate the slot to which the view would be snapped if the input
         * is released at output-local coordinates (x, y) */
        int calc_slot(int x, int y)
        {
            auto g = output->workspace->get_workarea();
            if (!(output->get_relative_geometry() & wf_point{x, y}))
                return 0;

            if (view && output->workspace->get_view_layer(view) != wf::LAYER_WORKSPACE)
                return 0;

            int threshold = snap_threshold->as_cached_int();

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
                GetTuple(ix, iy, get_input_coords());
                slot.preview->set_target_geometry({ix, iy, 1, 1}, 0);
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

                GetTuple(ix, iy, get_input_coords());
                auto preview = new wf_move_snap_preview(output, {ix, iy, 1, 1});

                wf::get_core().add_view(
                    std::unique_ptr<wf::view_interface_t> (preview));

                preview->set_output(output);
                preview->set_target_geometry(query.out_geometry, 1);
                slot.preview = nonstd::make_observer(preview);
            }
        }

        /* The input has moved enough so we remove the view from its slot */
        void unsnap()
        {
            stuck_in_slot = 1;
            if (view->fullscreen)
                view->fullscreen_request(view->get_output(), false);

            if (view->tiled_edges)
            {
                snap_signal data;
                data.view = view;
                data.slot = 0;
                output->emit_signal("view-snap", &data);
            }

            /* view geometry might change after unmaximize/unfullscreen, so update position */
            grabbed_geometry = view->get_wm_geometry();

            snap_wobbly(view, {}, false);
        }

        /* Returns the currently used input coordinates in global compositor space */
        std::tuple<int, int> get_global_input_coords()
        {
            if (is_using_touch) {
                return wf::get_core().get_touch_position(0);
            } else {
                return wf::get_core().get_cursor_position();
            }
        }

        /* Returns the currently used input coordinates in output-local space */
        std::tuple<int, int> get_input_coords()
        {
            GetTuple(gx, gy, get_global_input_coords());
            auto og = output->get_layout_geometry();

            return std::tuple<int, int> {gx - og.x, gy - og.y};
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

            view->move(wm_g.x + dx, wm_g.y + dy);
            translate_wobbly(view, dx, dy);

            view->set_moving(false);

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

        signal_callback_t handle_mirror_view_unmapped = [=] (signal_data* data)
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

            auto mirror = new wf_move_mirror_view(view, base_output.x - mirror_output.x,
                base_output.y - mirror_output.y);

            wf::get_core().add_view(
                std::unique_ptr<wf::view_interface_t> (mirror));

            auto wo_state = wo->get_data_safe<wf_move_output_state> (get_data_name());
            wo_state->view = nonstd::make_observer(mirror);

            mirror->set_output(wo);
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
            /* The mouse isn't on our output anymore -> transfer ownership of
             * the move operation to the other output where the input currently is */
            GetTuple(global_x, global_y, get_global_input_coords());
            auto target_output =
                wf::get_core().output_layout->get_output_at(global_x, global_y);
            if (target_output != output)
            {
                /* The move plugin on the next output will create new mirror views */
                delete_mirror_views(false);
                move_to_output(target_output);
                return;
            }

            auto current_og = output->get_layout_geometry();
            auto current_geometry =
                view->get_bounding_box() + wf_point{current_og.x, current_og.y};

            for (auto& wo : wf::get_core().output_layout->get_outputs())
            {
                if (wo == output) // skip the same output
                    return;

                auto og = output->get_layout_geometry();

                /* A view is visible on the other output as well */
                if (og & current_geometry)
                    ensure_mirror_view(wo);
            }
        }

        void handle_input_motion()
        {
            GetTuple(x, y, get_input_coords());

            move_wobbly(view, x, y);

            int dx = x - grab_start.x;
            int dy = y - grab_start.y;

            if (std::sqrt(dx * dx + dy * dy) >= snap_off_threshold->as_cached_int() &&
                !stuck_in_slot && enable_snap_off->as_int())
            {
                unsnap();
            }

            if (!stuck_in_slot)
                return;

            view->move(grabbed_geometry.x + dx, grabbed_geometry.y + dy);
            update_multi_output();

            /* TODO: possibly show some visual indication */
            if (enable_snap->as_cached_int())
                update_slot(calc_slot(x, y));
        }

        void fini()
        {
            if (grab_interface->is_grabbed())
                input_pressed(WLR_BUTTON_RELEASED);

            output->rem_binding(&activate_binding);
            output->rem_binding(&touch_activate_binding);
            output->disconnect_signal("move-request", &move_request);
            output->disconnect_signal("detach-view", &view_destroyed);
            output->disconnect_signal("view-disappeared", &view_destroyed);
        }
};

extern "C" {
    wayfire_plugin_t* newInstance()
    {
        return new wayfire_move();
    }
}

