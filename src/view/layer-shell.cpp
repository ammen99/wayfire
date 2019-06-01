#include <algorithm>
#include <cstring>

#include "xdg-shell.hpp"
#include "core.hpp"
#include "debug.hpp"
#include "output.hpp"
#include "workspace-manager.hpp"
#include "output-layout.hpp"
#include "view-impl.hpp"

extern "C"
{
#define namespace namespace_t
#include <wlr/types/wlr_layer_shell_v1.h>
#undef namespace
}

static const uint32_t both_vert =
    ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
static const uint32_t both_horiz =
    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;

static wf::layer_t zwlr_layer_to_wf_layer(zwlr_layer_shell_v1_layer layer)
{
    switch (layer)
    {
        case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
            return wf::LAYER_LOCK;
        case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
            return wf::LAYER_TOP;
        case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
            return wf::LAYER_BOTTOM;
        case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
            return wf::LAYER_BACKGROUND;
        default:
            throw std::domain_error("Invalid layer for layer surface!");
    }
}

class wayfire_layer_shell_view : public wf::wlr_view_t
{
    wf::wl_listener_wrapper on_map, on_unmap, on_destroy, on_new_popup;

  public:
    wlr_layer_surface_v1 *lsurface;
    wlr_layer_surface_v1_state prev_state;

    std::unique_ptr<wf::workspace_manager::anchored_area> anchored_area;

    wayfire_layer_shell_view(wlr_layer_surface_v1 *lsurf);
    virtual ~wayfire_layer_shell_view() {}

    void map(wlr_surface *surface);
    void unmap();
    void commit();
    void close();
    virtual void destroy();

    void configure(wf_geometry geometry);
};

wf::workspace_manager::anchored_edge anchor_to_edge(uint32_t edges)
{
    if (edges == ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)
        return wf::workspace_manager::ANCHORED_EDGE_TOP;
    if (edges == ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)
        return wf::workspace_manager::ANCHORED_EDGE_BOTTOM;
    if (edges == ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)
        return wf::workspace_manager::ANCHORED_EDGE_LEFT;
    if (edges == ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)
        return wf::workspace_manager::ANCHORED_EDGE_RIGHT;

    assert(false);
}

struct wf_layer_shell_manager
{
    using layer_t = std::vector<wayfire_layer_shell_view*>;
    layer_t layers[4];

    void handle_map(wayfire_layer_shell_view *view)
    {
        layers[view->lsurface->layer].push_back(view);
        arrange_layers(view->get_output());
    }

    void handle_unmap(wayfire_layer_shell_view *view)
    {
        auto& cont = layers[view->lsurface->layer];
        auto it = std::find(cont.begin(), cont.end(), view);

        if (view->anchored_area)
            view->get_output()->workspace->remove_reserved_area(view->anchored_area.get());

        cont.erase(it);
        arrange_layers(view->get_output());
    }

    layer_t filter_views(wf::output_t *output, int layer)
    {
        layer_t result;
        for (auto view : layers[layer])
        {
            if (view->get_output() == output)
                result.push_back(view);
        }

        return result;
    }

    layer_t filter_views(wf::output_t *output)
    {
        layer_t result;
        for (int i = 0; i < 4; i++)
        {
            auto layer_result = filter_views(output, i);
            result.insert(result.end(), layer_result.begin(), layer_result.end());
        }

        return result;
    }

    void set_exclusive_zone(wayfire_layer_shell_view *v)
    {
        int edges = v->lsurface->current.anchor;

        /* Special case we support */
        if (__builtin_popcount(edges) == 3)
        {
            if ((edges & both_horiz) == both_horiz)
                edges ^= both_horiz;

            if ((edges & both_vert) == both_vert)
                edges ^= both_vert;
        }

        if (__builtin_popcount(edges) > 1)
        {
            log_error ("Unsupported: layer-shell exclusive zone for surfaces anchored to 0, 2 or 4 edges");
            return;
        }

        if (!v->anchored_area)
        {
            v->anchored_area = std::make_unique<wf::workspace_manager::anchored_area>();
            v->anchored_area->reflowed = [v] (wf_geometry geometry, wf_geometry _)
            { v->configure(geometry); };
            /* Notice that the reflowed areas won't be changed until we call
             * reflow_reserved_areas(). However, by that time the information
             * in anchored_area will have been populated */
            v->get_output()->workspace->add_reserved_area(v->anchored_area.get());
        }

        v->anchored_area->edge = anchor_to_edge(edges);
        v->anchored_area->reserved_size = v->lsurface->current.exclusive_zone;
        v->anchored_area->real_size = v->anchored_area->edge <= wf::workspace_manager::ANCHORED_EDGE_BOTTOM ?
            v->lsurface->current.desired_height : v->lsurface->current.desired_width;
    }

    void pin_view(wayfire_layer_shell_view *v, wf_geometry usable_workarea)
    {
        auto state = &v->lsurface->current;
        auto bounds = v->lsurface->current.exclusive_zone < 0 ?
            v->get_output()->get_relative_geometry() : usable_workarea;

        wf_geometry box;
        box.x = box.y = 0;
        box.width = state->desired_width;
        box.height = state->desired_height;

		if ((state->anchor & both_horiz) && box.width == 0)
        {
			box.x = bounds.x;
			box.width = bounds.width;
		}
        else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT))
        {
			box.x = bounds.x;
		}
        else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
        {
			box.x = bounds.x + (bounds.width - box.width);
		} else
        {
			box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
		}

		if ((state->anchor & both_vert) && box.height == 0)
        {
			box.y = bounds.y;
			box.height = bounds.height;
		}
        else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP))
        {
			box.y = bounds.y;
		}
        else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))
        {
			box.y = bounds.y + (bounds.height - box.height);
		} else
        {
			box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
		}

        v->configure(box);
    }

    uint32_t arrange_layer(wf::output_t *output, int layer)
    {
        uint32_t focus_mask = 0;
        auto views = filter_views(output, layer);

        /* First we need to put all views that have exclusive zone set.
         * The rest are then placed into the free area */
        for (auto v : views)
        {
            if (v->lsurface->client_pending.keyboard_interactive && v->is_mapped())
                focus_mask = zwlr_layer_to_wf_layer(v->lsurface->layer);

            if (v->lsurface->client_pending.exclusive_zone > 0)
            {
                set_exclusive_zone(v);
            }
            else if (v->anchored_area)
            {
                /* Make sure the view doesn't have a reserved area anymore */
                output->workspace->remove_reserved_area(v->anchored_area.get());
                v->anchored_area = nullptr;
            }
        }

        auto usable_workarea = output->workspace->get_workarea();
        for (auto v : views)
        {
            /* The protocol dictates that the values -1 and 0 for exclusive zone
             * mean that it doesn't have one */
            if (v->lsurface->client_pending.exclusive_zone < 1)
                pin_view(v, usable_workarea);
        }

        return focus_mask;
    }

    void arrange_unmapped_view(wayfire_layer_shell_view* view)
    {
        if (view->lsurface->client_pending.exclusive_zone < 1)
            return pin_view(view, view->get_output()->workspace->get_workarea());

        set_exclusive_zone(view);
        view->get_output()->workspace->reflow_reserved_areas();
    }

    uint32_t focused_layer_request_uid = -1;
    void arrange_layers(wf::output_t *output)
    {
        auto views = filter_views(output);

        uint32_t focus1 = arrange_layer(output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);
        uint32_t focus2 = arrange_layer(output, ZWLR_LAYER_SHELL_V1_LAYER_TOP);
        uint32_t focus3 = arrange_layer(output, ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM);
        uint32_t focus4 = arrange_layer(output, ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND);

        auto focus_mask = std::max({focus1, focus2, focus3, focus4});
        focused_layer_request_uid = wf::get_core().focus_layer(focus_mask,
            focused_layer_request_uid);
        output->workspace->reflow_reserved_areas();
    }
};

static wf_layer_shell_manager layer_shell_manager;
wayfire_layer_shell_view::wayfire_layer_shell_view(wlr_layer_surface_v1 *lsurf)
    : wf::wlr_view_t(), lsurface(lsurf)
{
    log_debug("Create a layer surface: namespace %s layer %d anchor %d,"
              "size %dx%d, margin top:%d, down:%d, left:%d, right:%d",
              lsurf->namespace_t, lsurf->layer, lsurf->client_pending.anchor,
              lsurf->client_pending.desired_width, lsurf->client_pending.desired_height,
              lsurf->client_pending.margin.top,
              lsurf->client_pending.margin.bottom,
              lsurf->client_pending.margin.left,
              lsurf->client_pending.margin.right);

    if (lsurf->output)
    {
        auto wo = wf::get_core().output_layout->find_output(lsurf->output);
        set_output(wo);
    }

    if (!get_output())
    {
        log_error ("Couldn't find output for the layer surface");
        close();
        return;
    }

    lsurf->output = get_output()->handle;

    role = wf::VIEW_ROLE_SHELL_VIEW;
    lsurface->data = dynamic_cast<wf::view_interface_t*> (this);

    on_map.set_callback([&] (void*) { map(lsurface->surface); });
    on_unmap.set_callback([&] (void*) { unmap(); });
    on_destroy.set_callback([&] (void*) { destroy(); });
    on_new_popup.set_callback([&] (void *data) {
        create_xdg_popup((wlr_xdg_popup*) data);
    });

    on_map.connect(&lsurface->events.map);
    on_unmap.connect(&lsurface->events.unmap);
    on_destroy.connect(&lsurface->events.destroy);
    on_new_popup.connect(&lsurface->events.new_popup);

    layer_shell_manager.arrange_unmapped_view(this);
}

void wayfire_layer_shell_view::destroy()
{
    on_map.disconnect();
    on_unmap.disconnect();
    on_destroy.disconnect();
    on_new_popup.disconnect();

    wf::wlr_view_t::destroy();
}

void wayfire_layer_shell_view::map(wlr_surface *surface)
{
    /* Read initial data */
    view_impl->keyboard_focus_enabled = lsurface->current.keyboard_interactive;
    handle_app_id_changed(nonull(lsurface->namespace_t));

    get_output()->workspace->add_view(self(),
        zwlr_layer_to_wf_layer(lsurface->layer));

    wf::wlr_view_t::map(surface);
    layer_shell_manager.handle_map(this);
}

void wayfire_layer_shell_view::unmap()
{
    wf::wlr_view_t::unmap();
    layer_shell_manager.handle_unmap(this);
}

void wayfire_layer_shell_view::commit()
{
    wf::wlr_view_t::commit();

    auto state = &lsurface->current;
    /* Update the keyboard focus enabled state. If a refocusing is needed, i.e
     * the view state changed, then this will happen when arranging layers */
    view_impl->keyboard_focus_enabled = state->keyboard_interactive;

    if (std::memcmp(state, &prev_state, sizeof(*state)))
    {
        layer_shell_manager.arrange_layers(get_output());
        prev_state = *state;
    }
}

void wayfire_layer_shell_view::close()
{
    wf::wlr_view_t::close();
    wlr_layer_surface_v1_close(lsurface);
}

void wayfire_layer_shell_view::configure(wf_geometry box)
{
    auto state = &lsurface->current;
    if ((state->anchor & both_horiz) == both_horiz)
    {
        box.x += state->margin.left;
        box.width -= state->margin.left + state->margin.right;
    }
    else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT))
    {
        box.x += state->margin.left;
    }
    else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))
    {
        box.x -= state->margin.right;
    }

    if ((state->anchor & both_vert) == both_vert)
    {
        box.y += state->margin.top;
        box.height -= state->margin.top + state->margin.bottom;
    }
    else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP))
    {
        box.y += state->margin.top;
    }
    else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))
    {
        box.y -= state->margin.bottom;
    }

    if (box.width < 0 || box.height < 0) {
        log_error ("layer-surface has calculated width and height < 0");
        close();
    }

    wf::wlr_view_t::move(box.x, box.y);
    wlr_layer_surface_v1_configure(lsurface, box.width, box.height);
}

static wlr_layer_shell_v1 *layer_shell_handle;
void wf::init_layer_shell()
{
    static wf::wl_listener_wrapper on_created;

    layer_shell_handle = wlr_layer_shell_v1_create(wf::get_core().display);
    if (layer_shell_handle)
    {
        on_created.set_callback([] (void *data) {
            auto lsurf = static_cast<wlr_layer_surface_v1*> (data);
            wf::get_core().add_view(
                std::make_unique<wayfire_layer_shell_view> (lsurf));
        });

        on_created.connect(&layer_shell_handle->events.new_surface);
    }
}
