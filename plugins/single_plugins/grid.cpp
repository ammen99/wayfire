#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <algorithm>
#include <cmath>
#include <linux/input-event-codes.h>
#include "wayfire/signal-definitions.hpp"
#include <wayfire/plugins/common/geometry-animation.hpp>

#include "snap_signal.hpp"
#include <wayfire/plugins/wobbly/wobbly-signal.hpp>

const std::string grid_view_id = "grid-view";

class wayfire_grid_view_cdata : public wf::custom_data_t
{
    bool is_active = true;

    wayfire_view view;
    wf::output_t *output;
    wf::effect_hook_t pre_hook;
    wf::signal_callback_t unmapped;

    int32_t tiled_edges = -1;
    const wf::plugin_grab_interface_uptr& iface;

    wf::option_wrapper_t<std::string> animation_type{"grid/type"};
    wf::option_wrapper_t<int> animation_duration{"grid/duration"};
    wf::geometry_animation_t animation{animation_duration};

  public:

    wayfire_grid_view_cdata(wayfire_view view,
        const wf::plugin_grab_interface_uptr& _iface) :
        iface(_iface)
    {
        this->view   = view;
        this->output = view->get_output();
        this->animation = wf::geometry_animation_t{animation_duration};

        if (!view->get_output()->activate_plugin(iface,
            wf::PLUGIN_ACTIVATE_ALLOW_MULTIPLE))
        {
            is_active = false;

            return;
        }

        pre_hook = [=] ()
        {
            adjust_geometry();
        };
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);

        unmapped = [=] (wf::signal_data_t *data)
        {
            if (get_signaled_view(data) == view)
            {
                destroy();
            }
        };

        output->render->set_redraw_always(true);
        output->connect_signal("view-disappeared", &unmapped);
    }

    void destroy()
    {
        view->erase_data<wayfire_grid_view_cdata>();
    }

    void adjust_target_geometry(wf::geometry_t geometry, int32_t target_edges)
    {
        animation.set_start(view->get_wm_geometry());
        animation.set_end(geometry);

        /* Restore tiled edges if we don't need to set something special when
         * grid is ready */
        if (target_edges < 0)
        {
            this->tiled_edges = view->tiled_edges;
        } else
        {
            this->tiled_edges = target_edges;
        }

        std::string type = animation_type;
        if (view->get_transformer("wobbly") || !is_active)
        {
            type = "wobbly";
        }

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
            activate_wobbly(view);

            return destroy();
        }

        view->set_tiled(wf::TILED_EDGES_ALL);
        view->set_moving(1);
        view->set_resizing(1);
        animation.start();
    }

    void set_end_state(wf::geometry_t geometry, int32_t edges)
    {
        if (edges >= 0)
        {
            view->set_tiled(edges);
        }

        view->set_geometry(geometry);
    }

    void adjust_geometry()
    {
        if (!animation.running())
        {
            set_end_state(animation, tiled_edges);
            view->set_moving(0);
            view->set_resizing(0);

            return destroy();
        }

        view->set_geometry((wf::geometry_t)animation);
    }

    ~wayfire_grid_view_cdata()
    {
        if (!is_active)
        {
            return;
        }

        output->render->rem_effect(&pre_hook);
        output->deactivate_plugin(iface);
        output->render->set_redraw_always(false);
        output->disconnect_signal("view-disappeared", &unmapped);
    }
};

class wf_grid_slot_data : public wf::custom_data_t
{
  public:
    int slot;
};

nonstd::observer_ptr<wayfire_grid_view_cdata> ensure_grid_view(wayfire_view view,
    const wf::plugin_grab_interface_uptr& iface)
{
    if (!view->has_data<wayfire_grid_view_cdata>())
    {
        view->store_data(
            std::make_unique<wayfire_grid_view_cdata>(view, iface));
    }

    return view->get_data<wayfire_grid_view_cdata>();
}

class wf_grid_saved_view_geometry : public wf::custom_data_t
{
  public:
    wf::geometry_t geometry;
    uint32_t tiled_edges;
};

/*
 * 7 8 9
 * 4 5 6
 * 1 2 3
 */
static uint32_t get_tiled_edges_for_slot(uint32_t slot)
{
    if (slot == 0)
    {
        return 0;
    }

    uint32_t edges = wf::TILED_EDGES_ALL;
    if (slot % 3 == 0)
    {
        edges &= ~WLR_EDGE_LEFT;
    }

    if (slot % 3 == 1)
    {
        edges &= ~WLR_EDGE_RIGHT;
    }

    if (slot <= 3)
    {
        edges &= ~WLR_EDGE_TOP;
    }

    if (slot >= 7)
    {
        edges &= ~WLR_EDGE_BOTTOM;
    }

    return edges;
}

static uint32_t get_slot_from_tiled_edges(uint32_t edges)
{
    for (int slot = 0; slot <= 9; slot++)
    {
        if (get_tiled_edges_for_slot(slot) == edges)
        {
            return slot;
        }
    }

    return 0;
}

class wayfire_grid : public wf::plugin_interface_t
{
    std::vector<std::string> slots =
    {"unused", "bl", "b", "br", "l", "c", "r", "tl", "t", "tr"};
    wf::activator_callback bindings[10];
    wf::option_wrapper_t<wf::activatorbinding_t> keys[10];
    wf::option_wrapper_t<wf::activatorbinding_t> restore_opt{"grid/restore"};

    wf::activator_callback restore = [=] (auto)
    {
        if (!output->can_activate_plugin(grab_interface))
        {
            return false;
        }

        auto view = output->get_active_view();
        if (!view || (view->role != wf::VIEW_ROLE_TOPLEVEL))
        {
            return false;
        }

        view->tile_request(0);

        return true;
    };

    nonstd::observer_ptr<wayfire_grid_view_cdata> ensure_grid_view(wayfire_view view)
    {
        return ::ensure_grid_view(view, grab_interface);
    }

  public:
    void init() override
    {
        grab_interface->name = "grid";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_DESKTOP;

        for (int i = 1; i < 10; i++)
        {
            keys[i].load_option("grid/slot_" + slots[i]);
            bindings[i] = [=] (auto)
            {
                auto view = output->get_active_view();
                if (!view || (view->role != wf::VIEW_ROLE_TOPLEVEL))
                {
                    return false;
                }

                handle_slot(view, i);

                return true;
            };

            output->add_activator(keys[i], &bindings[i]);
        }

        output->add_activator(restore_opt, &restore);

        output->connect_signal("workarea-changed", &on_workarea_changed);
        output->connect_signal("view-snap", &on_snap_signal);
        output->connect_signal("query-snap-geometry", &on_snap_query);
        output->connect_signal("view-tile-request", &on_maximize_signal);
        output->connect_signal("view-fullscreen-request", &on_fullscreen_signal);
    }

    bool can_adjust_view(wayfire_view view)
    {
        auto workspace_impl =
            output->workspace->get_workspace_implementation();

        return workspace_impl->view_movable(view) &&
               workspace_impl->view_resizable(view);
    }

    void handle_slot(wayfire_view view, int slot, wf::point_t delta = {0, 0})
    {
        if (!can_adjust_view(view))
        {
            return;
        }

        view->get_data_safe<wf_grid_slot_data>()->slot = slot;
        ensure_grid_view(view)->adjust_target_geometry(
            get_slot_dimensions(slot) + delta,
            get_tiled_edges_for_slot(slot));
    }

    /*
     * 7 8 9
     * 4 5 6
     * 1 2 3
     * */
    wf::geometry_t get_slot_dimensions(int n)
    {
        auto area = output->workspace->get_workarea();
        int w2    = area.width / 2;
        int h2    = area.height / 2;

        if (n % 3 == 1)
        {
            area.width = w2;
        }

        if (n % 3 == 0)
        {
            area.width = w2, area.x += w2;
        }

        if (n >= 7)
        {
            area.height = h2;
        } else if (n <= 3)
        {
            area.height = h2, area.y += h2;
        }

        return area;
    }

    wf::signal_callback_t on_workarea_changed = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<wf::workarea_changed_signal*>(data);
        for (auto& view : output->workspace->get_views_in_layer(wf::LAYER_WORKSPACE))
        {
            if (!view->is_mapped())
            {
                continue;
            }

            auto data = view->get_data_safe<wf_grid_slot_data>();

            /* Detect if the view was maximized outside of the grid plugin */
            auto wm = view->get_wm_geometry();
            if (view->tiled_edges && (wm.width == ev->old_workarea.width) &&
                (wm.height == ev->old_workarea.height))
            {
                data->slot = SLOT_CENTER;
            }

            if (!data->slot)
            {
                continue;
            }

            /* Workarea changed, and we have a view which is tiled into some slot.
             * We need to make sure it remains in its slot. So we calculate the
             * viewport of the view, and tile it there */
            auto output_geometry = output->get_relative_geometry();

            int vx = std::floor(1.0 * wm.x / output_geometry.width);
            int vy = std::floor(1.0 * wm.y / output_geometry.height);

            handle_slot(view, data->slot,
                {vx *output_geometry.width, vy * output_geometry.height});
        }
    };

    wf::signal_callback_t on_snap_query = [=] (wf::signal_data_t *data)
    {
        auto query = dynamic_cast<snap_query_signal*>(data);
        assert(query);
        query->out_geometry = get_slot_dimensions(query->slot);
    };

    wf::signal_callback_t on_snap_signal = [=] (wf::signal_data_t *ddata)
    {
        snap_signal *data = dynamic_cast<snap_signal*>(ddata);
        handle_slot(data->view, data->slot);
    };

    wf::geometry_t adjust_for_workspace(wf::geometry_t geometry,
        wf::point_t workspace)
    {
        auto delta_ws = workspace - output->workspace->get_current_workspace();
        auto scr_size = output->get_screen_size();
        geometry.x += delta_ws.x * scr_size.width;
        geometry.y += delta_ws.y * scr_size.height;
        return geometry;
    }

    wf::signal_callback_t on_maximize_signal = [=] (wf::signal_data_t *ddata)
    {
        auto data = static_cast<wf::view_tile_request_signal*>(ddata);

        if (data->carried_out || (data->desired_size.width <= 0) ||
            !can_adjust_view(data->view))
        {
            return;
        }

        data->carried_out = true;
        uint32_t slot = get_slot_from_tiled_edges(data->edges);
        if (slot > 0)
        {
            data->desired_size = get_slot_dimensions(slot);
        }

        data->view->get_data_safe<wf_grid_slot_data>()->slot = slot;
        ensure_grid_view(data->view)->adjust_target_geometry(
            adjust_for_workspace(data->desired_size, data->workspace),
            get_tiled_edges_for_slot(slot));
    };

    wf::signal_callback_t on_fullscreen_signal = [=] (wf::signal_data_t *ev)
    {
        auto data = static_cast<wf::view_fullscreen_signal*>(ev);
        static const std::string fs_data_name = "grid-saved-fs";

        if (data->carried_out || (data->desired_size.width <= 0) ||
            !can_adjust_view(data->view))
        {
            return;
        }

        data->carried_out = true;
        ensure_grid_view(data->view)->adjust_target_geometry(
            adjust_for_workspace(data->desired_size, data->workspace), -1);
    };

    void fini() override
    {
        for (int i = 1; i < 10; i++)
        {
            output->rem_binding(&bindings[i]);
        }

        output->rem_binding(&restore);

        output->disconnect_signal("workarea-changed", &on_workarea_changed);
        output->disconnect_signal("view-snap", &on_snap_signal);
        output->disconnect_signal("query-snap-geometry", &on_snap_query);
        output->disconnect_signal("view-tile-request", &on_maximize_signal);
        output->disconnect_signal("view-fullscreen-request", &on_fullscreen_signal);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_grid);
