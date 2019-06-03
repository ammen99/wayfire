#include <plugin.hpp>
#include <output.hpp>
#include <core.hpp>
#include <view.hpp>
#include <workspace-manager.hpp>
#include <render-manager.hpp>
#include <algorithm>
#include <linux/input-event-codes.h>
#include "signal-definitions.hpp"
#include <animation.hpp>

#include "snap_signal.hpp"
#include "../wobbly/wobbly-signal.hpp"

extern "C"
{
#include <wlr/util/edges.h>
}

const std::string grid_view_id = "grid-view";

class wayfire_grid_view_cdata : public wf::custom_data_t
{
    wf_duration duration;
    bool is_active = true;

    wayfire_view view;
    wf::output_t *output;
    wf::effect_hook_t pre_hook;
    wf::signal_callback_t unmapped;

    uint32_t tiled_edges;
    wf_geometry target, initial;
    wayfire_grab_interface iface;
    wf_option animation_type;

    public:

    wayfire_grid_view_cdata(wayfire_view view, wayfire_grab_interface iface,
                      wf_option animation_type, wf_option animation_duration)
    {
        this->view = view;
        this->output = view->get_output();
        this->iface = iface;
        this->animation_type = animation_type;
        duration = wf_duration(animation_duration);

        if (!view->get_output()->activate_plugin(iface))
        {
            is_active = false;
            return;
        }

        pre_hook = [=] () {
            adjust_geometry();
        };
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);

        unmapped = [=] (wf::signal_data_t *data)
        {
            if (get_signaled_view(data) == view)
                destroy();
        };

        output->render->set_redraw_always(true);
        output->connect_signal("view-disappeared", &unmapped);
        output->connect_signal("detach-view", &unmapped);
    }

    void destroy()
    {
        view->erase_data<wayfire_grid_view_cdata>();
    }

    void adjust_target_geometry(wf_geometry geometry, uint32_t tiled_edges)
    {
        target = geometry;
        initial = view->get_wm_geometry();
        this->tiled_edges = tiled_edges;

        auto type = animation_type->as_string();
        if (view->get_transformer("wobbly") || !is_active)
            type = "wobbly";

        if (type == "none")
        {
            set_end_state(geometry, tiled_edges);
            return destroy();
        }

        if (type == "wobbly")
        {
            /* Order is important here: first we set the view geometry, and
             * after that we set the snap request. Otherwise the wobbly plugin
             * will think the view actually moved */
            set_end_state(geometry, tiled_edges);
            snap_wobbly(view, geometry);

            if (!tiled_edges) // release snap, so subsequent size changes don't bother us
                snap_wobbly(view, geometry, false);

            return destroy();
        }

        view->set_maximized(1);
        view->set_moving(1);
        view->set_resizing(1);
        duration.start();
    }

    void set_end_state(wf_geometry geometry, uint32_t edges)
    {
        view->set_geometry(geometry);
        if (__builtin_popcount(edges) == 4)
        {
            view->set_maximized(true);
        } else
        {
            view->set_maximized(false);
        }

        view->set_tiled(edges);
    }

    void adjust_geometry()
    {
        if (!duration.running())
        {
            set_end_state(target, tiled_edges);
            view->set_moving(0);
            view->set_resizing(0);

            return destroy();
        }

        int cx = duration.progress(initial.x, target.x);
        int cy = duration.progress(initial.y, target.y);
        int cw = duration.progress(initial.width, target.width);
        int ch = duration.progress(initial.height, target.height);

        view->set_geometry({cx, cy, cw, ch});
    }

    ~wayfire_grid_view_cdata()
    {
        if (!is_active)
            return;

        output->render->rem_effect(&pre_hook);
        output->deactivate_plugin(iface);
        output->render->set_redraw_always(false);
        output->disconnect_signal("view-disappeared", &unmapped);
        output->disconnect_signal("detach-view", &unmapped);
    }
};

class wf_grid_slot_data : public wf::custom_data_t
{
    public:
        int slot;
};

nonstd::observer_ptr<wayfire_grid_view_cdata> ensure_grid_view(wayfire_view view,
        wayfire_grab_interface iface, wf_option animation_type,
        wf_option animation_duration)
{
    if (!view->has_data<wayfire_grid_view_cdata>())
    {
        view->store_data(std::make_unique<wayfire_grid_view_cdata>
            (view, iface, animation_type, animation_duration));
    }

    return view->get_data<wayfire_grid_view_cdata> ();
}

class wf_grid_saved_view_geometry : public wf::custom_data_t
{
    public:
    wf_geometry geometry;
    uint32_t tiled_edges;
};

static uint32_t get_tiled_edges_for_slot(uint32_t slot)
{
    if (slot == 0)
        return 0;
    if (slot == SLOT_CENTER)
        return WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT;

    uint32_t edges = 0;
    if (slot < 4)
        edges |= WLR_EDGE_BOTTOM;
    if (slot >= 7)
        edges |= WLR_EDGE_TOP;
    if (slot % 3 == 1)
        edges |= WLR_EDGE_LEFT;
    if (slot % 3 == 0)
        edges |= WLR_EDGE_RIGHT;

    return edges;
}

class wayfire_grid : public wayfire_plugin_t
{
    std::vector<std::string> slots = {"unused", "bl", "b", "br", "l", "c", "r", "tl", "t", "tr"};
    std::vector<std::string> default_keys = {
        "none",
        "<alt> <ctrl> KEY_KP1",
        "<alt> <ctrl> KEY_KP2",
        "<alt> <ctrl> KEY_KP3",
        "<alt> <ctrl> KEY_KP4",
        "<alt> <ctrl> KEY_KP5",
        "<alt> <ctrl> KEY_KP6",
        "<alt> <ctrl> KEY_KP7",
        "<alt> <ctrl> KEY_KP8",
        "<alt> <ctrl> KEY_KP9",
    };
    activator_callback bindings[10];
    wf_option keys[10];

    wf_option animation_duration, animation_type;

    nonstd::observer_ptr<wayfire_grid_view_cdata>
        ensure_grid_view(wayfire_view view)
    {
        return ::ensure_grid_view(view, grab_interface,
            animation_type, animation_duration);
    }

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "grid";
        grab_interface->abilities_mask = WF_ABILITY_CHANGE_VIEW_GEOMETRY;

        auto section = config->get_section("grid");
        animation_duration = section->get_option("duration", "300");
        animation_type = section->get_option("type", "simple");

        for (int i = 1; i < 10; i++)
        {
            keys[i] = section->get_option("slot_" + slots[i], default_keys[i]);

            bindings[i] = [=] (wf_activator_source, uint32_t)
            {
                auto view = output->get_active_view();
                if (!view || view->role != wf::VIEW_ROLE_TOPLEVEL)
                    return;

                handle_slot(view, i, false);
            };

            output->add_activator(keys[i], &bindings[i]);
        }

        output->connect_signal("reserved-workarea", &on_workarea_changed);
        output->connect_signal("view-snap", &on_snap_signal);
        output->connect_signal("query-snap-geometry", &on_snap_query);
        output->connect_signal("view-maximized-request", &on_maximize_signal);
        output->connect_signal("view-fullscreen-request", &on_fullscreen_signal);
    }

    void handle_slot(wayfire_view view, const wf_geometry workarea, int slot, bool comes_from_maximize_request)
    {
        wf_geometry target = get_slot_dimensions(slot, workarea);
        if (view->maximized && view->get_wm_geometry() == target)
        {
            return;
        }
        else if (!view->has_data<wf_grid_saved_view_geometry>() && slot)
        {
            view->get_data_safe<wf_grid_saved_view_geometry>()
                ->geometry = view->get_wm_geometry();
        }
        else if (view->has_data<wf_grid_saved_view_geometry>() && slot == 0)
        {
            target = view->get_data_safe<wf_grid_saved_view_geometry>()->geometry;
            /* If we have a request from another plugin, for ex. move, we want
             * to make sure the view stays under the pointer. However, when it was
             * a normal restore caused by a client request, we want to restore the
             * view to where it was */
            if (!comes_from_maximize_request)
                target = calculate_restored_geometry(target);

            view->erase_data<wf_grid_saved_view_geometry>();
        }
        else if (!view->has_data<wf_grid_saved_view_geometry>())
        {
            view->set_maximized(slot > 0 ? true : false);
            view->set_tiled(0);
            view->request_native_size();
            return;
        }

        view->get_data_safe<wf_grid_slot_data>()->slot = slot;
        ensure_grid_view(view)->adjust_target_geometry(target,
            get_tiled_edges_for_slot(slot));
    }

    void handle_slot(wayfire_view view, int slot, bool comes_from_maximize_request)
    {
        handle_slot(view, output->workspace->get_workarea(), slot, comes_from_maximize_request);
    }

    /* calculates the target geometry so that it is centered around the pointer */
    wf_geometry calculate_restored_geometry(wf_geometry base_restored)
    {
        GetTuple(cx, cy, output->get_cursor_position());
        base_restored.x = cx - base_restored.width / 2;
        base_restored.y = cy - base_restored.height / 2;

        /* if the view goes outside of the workarea, try to move it back inside */
        auto wa = output->workspace->get_workarea();
        if (base_restored.x + base_restored.width > wa.x + wa.width)
            base_restored.x = wa.x + wa.width - base_restored.width;
        if (base_restored.y + base_restored.height > wa.y + wa.width)
            base_restored.y = wa.y + wa.height - base_restored.width;
        if (base_restored.x < wa.x)
            base_restored.x = wa.x;
        if (base_restored.y < wa.y)
            base_restored.y = wa.y;


        return base_restored;
    }

    /*
     * 7 8 9
     * 4 5 6
     * 1 2 3
     * */
    wf_geometry get_slot_dimensions(int n, wf_geometry area)
    {
        if (n == 0) // toggle off slot
            return {0, 0, 0, 0};

        int w2 = area.width / 2;
        int h2 = area.height / 2;

        if (n % 3 == 1)
            area.width = w2;
        if (n % 3 == 0)
            area.width = w2, area.x += w2;

        if (n >= 7)
            area.height = h2;
        else if (n <= 3)
            area.height = h2, area.y += h2;

        return area;
    }

    wf::signal_callback_t on_workarea_changed = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<reserved_workarea_signal*> (data);
        for (auto& view : output->workspace->get_views_in_layer(wf::LAYER_WORKSPACE))
        {
            if (!view->is_mapped())
                continue;

            auto data = view->get_data_safe<wf_grid_slot_data>();

            /* Detect if the view was maximized outside of the grid plugin */
            auto wm = view->get_wm_geometry();
            if (view->tiled_edges && wm.width == ev->old_workarea.width
                && wm.height == ev->old_workarea.height)
            {
                data->slot = SLOT_CENTER;
            }

            if (!data->slot)
                continue;

            /* Workarea changed, and we have a view which is tiled into some slot.
             * We need to make sure it remains in its slot. So we calculate the
             * viewport of the view, and tile it there */
            auto workarea = ev->new_workarea;
            auto output_geometry = output->get_relative_geometry();

            int vx = std::floor(1.0 * wm.x / output_geometry.width);
            int vy = std::floor(1.0 * wm.y / output_geometry.height);

            workarea.x += vx * output_geometry.width;
            workarea.y += vy * output_geometry.height;

            handle_slot(view, workarea, data->slot, false);
        }
    };

    wf::signal_callback_t on_snap_query = [=] (wf::signal_data_t *data)
    {
        auto query = dynamic_cast<snap_query_signal*> (data);
        assert(query);

        query->out_geometry = get_slot_dimensions(query->slot,
            output->workspace->get_workarea());
    };

    wf::signal_callback_t on_snap_signal = [=] (wf::signal_data_t *ddata)
    {
        snap_signal *data = dynamic_cast<snap_signal*>(ddata);
        handle_slot(data->view, data->slot, false);
    };

    wf::signal_callback_t on_maximize_signal = [=] (wf::signal_data_t *ddata)
    {
        auto data = static_cast<view_maximized_signal*> (ddata);
        handle_slot(data->view, data->state ? 5 : 0, true);
    };

    wf::signal_callback_t on_fullscreen_signal = [=] (wf::signal_data_t *ev)
    {
        auto data = static_cast<view_fullscreen_signal*> (ev);
        static const std::string fs_data_name = "grid-saved-fs";

        if (data->state)
        {
            if (!data->view->has_data(fs_data_name))
            {
                auto sg = data->view->get_data_safe<wf_grid_saved_view_geometry>
                    (fs_data_name);

                sg->geometry = data->view->get_wm_geometry();
                sg->tiled_edges = data->view->tiled_edges;
            }

            ensure_grid_view(data->view)->adjust_target_geometry(
                output->get_relative_geometry(), 0);
            data->view->set_fullscreen(true);
        } else
        {
            if (data->view->has_data(fs_data_name))
            {
                auto sg = data->view->get_data_safe<wf_grid_saved_view_geometry>
                    (fs_data_name);

                auto target_geometry = sg->geometry;
                auto edges = sg->tiled_edges;
                data->view->erase_data(fs_data_name);

                ensure_grid_view(data->view)->adjust_target_geometry(
                    target_geometry, edges);
            }

            data->view->set_fullscreen(false);
        }
    };

    void fini()
    {
        for (int i = 1; i < 10; i++)
            output->rem_binding(&bindings[i]);

        output->disconnect_signal("reserved-workarea", &on_workarea_changed);
        output->disconnect_signal("view-snap", &on_snap_signal);
        output->disconnect_signal("query-snap-geometry", &on_snap_query);
        output->disconnect_signal("view-maximized-request", &on_maximize_signal);
        output->disconnect_signal("view-fullscreen-request", &on_fullscreen_signal);
    }
};

extern "C"
{
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_grid;
    }
}
