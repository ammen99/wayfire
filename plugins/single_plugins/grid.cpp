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
#include <wayfire/view-transform.hpp>

const std::string grid_view_id = "grid-view";

/**
 * A transformer used for a simple crossfade + scale animation.
 *
 * It fades out the scaled contents from original_buffer, and fades in the
 * current contents of the view, based on the alpha value in the transformer.
 */
class grid_crossfade_transformer : public wf::view_2D
{
  public:
    grid_crossfade_transformer(wayfire_view view) :
        wf::view_2D(view)
    {
        // Create a copy of the view contents
        original_buffer.geometry = view->get_wm_geometry();
        original_buffer.scale    = view->get_output()->handle->scale;

        auto w = original_buffer.scale * original_buffer.geometry.width;
        auto h = original_buffer.scale * original_buffer.geometry.height;

        OpenGL::render_begin();
        original_buffer.allocate(w, h);
        original_buffer.bind();
        OpenGL::clear({0, 0, 0, 0});
        OpenGL::render_end();

        auto og = view->get_output_geometry();
        for (auto& surface : view->enumerate_surfaces(wf::origin(og)))
        {
            wf::region_t damage = wf::geometry_t{
                surface.position.x,
                surface.position.y,
                surface.surface->get_size().width,
                surface.surface->get_size().height
            };

            damage &= original_buffer.geometry;
            surface.surface->simple_render(original_buffer,
                surface.position.x, surface.position.y, damage);
        }
    }

    void render_box(wf::texture_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf::framebuffer_t& fb) override
    {
        // See the current target geometry
        auto bbox = view->get_wm_geometry();
        bbox = this->get_bounding_box(bbox, bbox);

        double saved = this->alpha;
        this->alpha = 1.0;
        // Now render the real view
        view_2D::render_box(src_tex, src_box, scissor_box, fb);
        this->alpha = saved;

        double ra;
        const double N = 2;
        if (alpha < 0.5)
        {
            ra = std::pow(alpha * 2, 1.0 / N) / 2.0;
        } else
        {
            ra = std::pow((alpha - 0.5) * 2, N) / 2.0 + 0.5;
        }

        // First render the original buffer with corresponding alpha
        OpenGL::render_begin(fb);
        fb.logic_scissor(scissor_box);
        OpenGL::render_texture({original_buffer.tex}, fb, bbox,
            glm::vec4{1.0f, 1.0f, 1.0f, 1.0 - ra});
        OpenGL::render_end();
    }

    ~grid_crossfade_transformer()
    {
        OpenGL::render_begin();
        original_buffer.release();
        OpenGL::render_end();
    }

    // The contents of the view before the change.
    wf::framebuffer_t original_buffer;
};

class wayfire_grid_view_cdata : public wf::custom_data_t
{
    wf::geometry_t original;
    wayfire_view view;
    wf::output_t *output;
    wf::signal_connection_t unmapped = [=] (auto data)
    {
        if (get_signaled_view(data) == view)
        {
            destroy();
        }
    };

    wf::option_wrapper_t<std::string> animation_type{"grid/type"};
    wf::option_wrapper_t<int> animation_duration{"grid/duration"};
    wf::geometry_animation_t animation{animation_duration,
        wf::animation::smoothing::circle};

  public:
    wayfire_grid_view_cdata(wayfire_view view)
    {
        this->view   = view;
        this->output = view->get_output();
        this->animation = wf::geometry_animation_t{animation_duration};

        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->connect_signal("view-disappeared", &unmapped);
    }

    void destroy()
    {
        view->erase_data<wayfire_grid_view_cdata>();
    }

    void adjust_target_geometry(wf::geometry_t geometry, int32_t target_edges)
    {
        // Apply the desired attributes to the view
        const auto& set_state = [=] ()
        {
            if (target_edges >= 0)
            {
                view->set_fullscreen(false);
                view->set_tiled(target_edges);
            }

            view->set_geometry(geometry);
        };

        if (animation_type.value() != "crossfade")
        {
            /* Order is important here: first we set the view geometry, and
             * after that we set the snap request. Otherwise the wobbly plugin
             * will think the view actually moved */
            set_state();
            if (animation_type.value() == "wobbly")
            {
                activate_wobbly(view);
            }

            return destroy();
        }

        // Crossfade animation
        original = view->get_wm_geometry();
        animation.set_start(original);
        animation.set_end(geometry);
        animation.start();

        // Add crossfade transformer
        if (!view->get_transformer("grid-crossfade"))
        {
            view->add_transformer(
                std::make_unique<grid_crossfade_transformer>(view),
                "grid-crossfade");
        }

        // Start the transition
        set_state();
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        if (!animation.running())
        {
            return destroy();
        }

        if (view->get_wm_geometry() != original)
        {
            original = view->get_wm_geometry();
            animation.set_end(original);
        }

        view->damage();

        auto tr_untyped = view->get_transformer("grid-crossfade").get();
        auto tr = dynamic_cast<grid_crossfade_transformer*>(tr_untyped);

        auto geometry = view->get_wm_geometry();

        tr->scale_x = animation.width / geometry.width;
        tr->scale_y = animation.height / geometry.height;

        tr->translation_x = (animation.x + animation.width / 2) -
            (geometry.x + geometry.width / 2.0);
        tr->translation_y = (animation.y + animation.height / 2) -
            (geometry.y + geometry.height / 2.0);

        tr->alpha = animation.progress();
        view->damage();
    };

    ~wayfire_grid_view_cdata()
    {
        view->pop_transformer("grid-crossfade");
        output->render->rem_effect(&pre_hook);
    }
};

class wf_grid_slot_data : public wf::custom_data_t
{
  public:
    int slot;
};

nonstd::observer_ptr<wayfire_grid_view_cdata> ensure_grid_view(wayfire_view view)
{
    if (!view->has_data<wayfire_grid_view_cdata>())
    {
        view->store_data(
            std::make_unique<wayfire_grid_view_cdata>(view));
    }

    return view->get_data<wayfire_grid_view_cdata>();
}

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

                if (!output->can_activate_plugin(wf::CAPABILITY_MANAGE_DESKTOP))
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
