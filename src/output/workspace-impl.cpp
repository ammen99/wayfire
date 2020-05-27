#include <wayfire/view.hpp>
#include <wayfire/output.hpp>
#include <wayfire/core.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/opengl.hpp>
#include <list>
#include <algorithm>
#include <wayfire/nonstd/reverse.hpp>
#include <wayfire/util/log.hpp>

namespace wf
{
/** Find a smart pointer inside a list */
template<class Haystack, class Needle> typename std::list<Haystack>::iterator
    find_in(std::list<Haystack>& hay, const Needle& needle)
{
    return std::find_if(std::begin(hay), std::end(hay), [=] (const auto& elem) {
        return elem.get() == needle.get();
    });
}

/** Bring to front or lower to back inside a container of smart pointers */
template<class Haystack, class Needle> void
    raise_to_front(std::list<Haystack>& hay, const Needle& needle,
        bool reverse = false)
{
    auto it = find_in(hay, needle);
    hay.splice(reverse ? hay.end() : hay.begin(), hay, it);
}

/** Reorder list so that @element is directly above @below. */
template<class Haystack, class Needle> void reorder_above(
    std::list<Haystack>& list, const Needle& element, const Needle& below)
{
    auto element_it = find_in(list, element);
    auto pos = find_in(list, below);
    list.splice(pos, list, element_it);
}

/** Reorder list so that @element is directly below @above. */
template<class Haystack, class Needle> void reorder_below(
    std::list<Haystack>& list, const Needle& element, const Needle& above)
{
    auto element_it = find_in(list, element);
    auto pos = find_in(list, above);
    assert(pos != list.end());
    list.splice(std::next(pos), list, element_it);
}

/**
 * Remove needle from haystack, where both needle and elements in haystack are
 * smart pointers.
 */
template<class Haystack, class Needle>
void remove_from(Haystack& haystack, Needle& n)
{
    auto it = std::remove_if(std::begin(haystack), std::end(haystack),
        [=] (const auto& contained) {
            return contained.get() == n.get();
        });

    haystack.erase(it, std::end(haystack));
}

struct layer_container_t;
/**
 * Implementation of the sublayer struct.
 */
struct sublayer_t
{
    /** A list of the views in the sublayer */
    std::list<wayfire_view> views;

    /** The actual layer this sublayer belongs to */
    nonstd::observer_ptr<layer_container_t> layer;

    /** The sublayer mode */
    sublayer_mode_t mode;

    /**
     * Whether the sublayer is created artificially to hold a single view.
     * In those cases, the sublayer is destroyed as soon as the view is moved
     * elsewhere.
     */
    bool is_single_view;
};

class layer_view_data_t : public custom_data_t
{
  public:
    nonstd::observer_ptr<sublayer_t> sublayer;
    /* Promoted to the fullscreen layer? */
    bool is_promoted = false;
};

/**
 * A container for all the sublayers of a layer.
 */
struct layer_container_t
{
    /** The layer of the container */
    layer_t layer;

    using sublayer_container_t = std::list<std::unique_ptr<sublayer_t>>;
    /** List of sublayers docked below */
    sublayer_container_t below;
    /** List of floating sublayers */
    sublayer_container_t floating;
    /** List of sublayers docked above */
    sublayer_container_t above;

    void remove_sublayer(nonstd::observer_ptr<sublayer_t> sublayer)
    {
        for (auto container : {&below, &floating, &above})
            remove_from(*container, sublayer);
    }
};

/**
 * output_layer_manager_t is a part of the workspace_manager module. It provides
 * the functionality related to layers and sublayers.
 */
class output_layer_manager_t
{
    layer_container_t layers[TOTAL_LAYERS];
  public:
    output_layer_manager_t()
    {
        for (int i = 0; i < TOTAL_LAYERS; i++)
            layers[i].layer = static_cast<layer_t>(1 << i);
    }


    constexpr int layer_index_from_mask(uint32_t layer_mask) const
    {
        return __builtin_ctz(layer_mask);
    }

    nonstd::observer_ptr<sublayer_t>& get_view_sublayer(wayfire_view view)
    {
        return view->get_data_safe<layer_view_data_t>()->sublayer;
    }

    uint32_t get_view_layer(wayfire_view view)
    {
        if (!view->has_data<layer_view_data_t>())
            return 0;

        /*
         * A view might have layer data set from a previous output.
         * That does not mean it has an assigned layer.
         */
        auto sublayer = get_view_sublayer(view);
        if (!sublayer)
            return 0;

        return sublayer->layer->layer;
    }

    void remove_view(wayfire_view view)
    {
        auto& sublayer = get_view_sublayer(view);
        if (!sublayer)
            return;

        view->damage();

        remove_from(sublayer->views, view);
        if (sublayer->is_single_view)
            sublayer->layer->remove_sublayer(sublayer);

        /* Reset the view's sublayer */
        sublayer = nullptr;
    }

    void add_view_to_sublayer(wayfire_view view,
        nonstd::observer_ptr<sublayer_t> sublayer)
    {
        remove_view(view);
        get_view_sublayer(view) = sublayer;
        sublayer->views.push_front(view);
    }

    nonstd::observer_ptr<sublayer_t> create_sublayer(layer_t layer_mask,
        sublayer_mode_t mode)
    {
        auto sublayer = std::make_unique<sublayer_t> ();
        nonstd::observer_ptr<sublayer_t> ptr{sublayer};

        auto& layer = this->layers[layer_index_from_mask(layer_mask)];
        sublayer->layer = &layer;
        sublayer->mode = mode;
        sublayer->is_single_view = false;

        switch (mode)
        {
            case SUBLAYER_DOCKED_BELOW:
                layer.below.emplace_back(std::move(sublayer));
                break;
            case SUBLAYER_DOCKED_ABOVE:
                layer.above.emplace_front(std::move(sublayer));
                break;
            case SUBLAYER_FLOATING:
                layer.floating.emplace_front(std::move(sublayer));
                break;
        }

        return ptr;
    }

    /** Add or move the view to the given layer */
    void add_view_to_layer(wayfire_view view, layer_t layer)
    {
        view->damage();
        add_view_to_sublayer(view, create_sublayer(layer, SUBLAYER_FLOATING));
        view->damage();
    }

    /** Precondition: view is in some sublayer */
    void bring_to_front(wayfire_view view)
    {
        auto sublayer = get_view_sublayer(view);
        assert(sublayer);
        if (sublayer->mode == SUBLAYER_FLOATING)
            raise_to_front(sublayer->layer->floating, sublayer);

        raise_to_front(sublayer->views, view);
    }

    wayfire_view get_front_view(wf::layer_t layer)
    {
        auto views = get_views_in_layer(layer);
        if (views.size() == 0)
            return nullptr;

        return views.front();
    }

    /** Precondition: view and below are in the same layer */
    void restack_above(wayfire_view view, wayfire_view below)
    {
        view->damage();

        auto view_sublayer = get_view_sublayer(view);
        auto below_sublayer = get_view_sublayer(below);
        assert(view_sublayer->layer == below_sublayer->layer);

        if (view_sublayer == below_sublayer)
        {
            reorder_above(view_sublayer->views, view, below);
            return;
        }

        if (view_sublayer->mode != SUBLAYER_FLOATING ||
            below_sublayer->mode != SUBLAYER_FLOATING)
        {
            return;
        }

        reorder_above(view_sublayer->layer->floating, view_sublayer, below_sublayer);
        raise_to_front(view_sublayer->views, view, true); // bring to back == reverse
    }

    /** Precondition: view and above are in the same layer */
    void restack_below(wayfire_view view, wayfire_view above)
    {
        view->damage();

        auto view_sublayer = get_view_sublayer(view);
        auto above_sublayer = get_view_sublayer(above);
        assert(view_sublayer->layer == above_sublayer->layer);

        if (view_sublayer == above_sublayer)
        {
            reorder_below(view_sublayer->views, view, above);
            return;
        }

        if (view_sublayer->mode != SUBLAYER_FLOATING ||
            above_sublayer->mode != SUBLAYER_FLOATING)
        {
            return;
        }

        reorder_below(view_sublayer->layer->floating, view_sublayer, above_sublayer);
        raise_to_front(view_sublayer->views, view);
    }

    void push_views(std::vector<wayfire_view>& into, layer_t layer_e,
        bool promoted)
    {
        auto& layer = this->layers[layer_index_from_mask(layer_e)];
        for (const auto& sublayers :
            {&layer.above, &layer.floating, &layer.below})
        {
            for (const auto& sublayer : *sublayers)
            {
                auto& container = sublayer->views;
                std::copy_if(container.begin(), container.end(),
                    std::back_inserter(into), [=] (wayfire_view view) {
                        const auto& layer_data =
                            view->get_data_safe<layer_view_data_t>();
                        return layer_data->is_promoted == promoted;
                    });
            }
        }
    }

    std::vector<wayfire_view> get_views_in_layer(uint32_t layers_mask)
    {
        std::vector<wayfire_view> views;
        auto try_push = [&] (layer_t layer, bool promoted = false)
        {
            if (!(layer & layers_mask))
                return;
            push_views(views, layer, promoted);
        };


        /* Above fullscreen views */
        for (auto layer : {LAYER_DESKTOP_WIDGET, LAYER_LOCK, LAYER_UNMANAGED})
            try_push(layer);

        /* Fullscreen */
        try_push(LAYER_WORKSPACE, true);

        /* Below fullscreen */
        for (auto layer :
            {LAYER_TOP, LAYER_WORKSPACE, LAYER_BOTTOM, LAYER_BACKGROUND})
        {
            try_push(layer);
        }

        return views;
    }

    std::vector<wayfire_view> get_promoted_views()
    {
        std::vector<wayfire_view> views;
        push_views(views, LAYER_WORKSPACE, true);
        return views;
    }

    /**
     * @return A list of all views in the given sublayer.
     */
    std::vector<wayfire_view> get_views_in_sublayer(
        nonstd::observer_ptr<sublayer_t> sublayer)
    {
        std::vector<wayfire_view> result;
        std::copy(sublayer->views.begin(), sublayer->views.end(),
            std::back_inserter(result));
        return result;
    }

    void destroy_sublayer(nonstd::observer_ptr<sublayer_t> sublayer)
    {
        for (auto& view : get_views_in_sublayer(sublayer))
            add_view_to_layer(view, sublayer->layer->layer);

        sublayer->layer->remove_sublayer(sublayer);
    }
};

struct default_workspace_implementation_t : public workspace_implementation_t
{
    bool view_movable (wayfire_view view)  { return true; }
    bool view_resizable(wayfire_view view) { return true; }
    virtual ~default_workspace_implementation_t() {}
};

/**
 * The output_viewport_manager_t provides viewport-related functionality in
 * workspace_manager
 */
class output_viewport_manager_t
{
  private:
    int vwidth;
    int vheight;
    int current_vx;
    int current_vy;

    output_t *output;

  public:
    output_viewport_manager_t(output_t *output)
    {
        this->output = output;
        vwidth = wf::option_wrapper_t<int> ("core/vwidth");
        vheight = wf::option_wrapper_t<int> ("core/vheight");

        vwidth = clamp(vwidth, 1, 20);
        vheight = clamp(vheight, 1, 20);

        current_vx = 0;
        current_vy = 0;
    }
    /**
     * @param threshold Threshold of the view to be counted
     *        on that workspace. 1.0 for 100% visible, 0.1 for 10%
     *
     * @return a vector of all the workspaces
     */
    std::vector<wf::point_t> get_view_workspaces(wayfire_view view, double threshold)
    {
        assert(view->get_output() == this->output);
        std::vector<wf::point_t> view_workspaces;
        wf::geometry_t workspace_relative_geometry;
        wlr_box view_bbox = view->get_bounding_box();

        for (int horizontal = 0; horizontal < this->vwidth; horizontal++)
        {
            for (int vertical = 0; vertical < this->vheight; vertical++)
            {
                wf::point_t ws = {horizontal, vertical};
                if (output->workspace->view_visible_on(view, ws))
                {
                    workspace_relative_geometry = output->render->get_ws_box(ws);
                    auto intersection = wf::geometry_intersection(
                        view_bbox, workspace_relative_geometry);
                    double area = 1.0 * intersection.width * intersection.height;
                    area /= 1.0 * view_bbox.width * view_bbox.height;

                    if (area < threshold)
                    {
                        continue;
                    }
                    view_workspaces.push_back(ws);
                }
            }
        }
        return view_workspaces;
    }

    /**
     * @param use_bbox When considering view visibility, whether to use the
     *        bounding box or the wm geometry.
     *
     * @return true if the view is visible on the workspace vp
     */
    bool view_visible_on(wayfire_view view, wf::point_t vp, bool use_bbox)
    {
        auto g = output->get_relative_geometry();
        if (view->role != VIEW_ROLE_DESKTOP_ENVIRONMENT)
        {
            g.x += (vp.x - current_vx) * g.width;
            g.y += (vp.y - current_vy) * g.height;
        }

        if (view->has_transformer() && use_bbox) {
            return view->intersects_region(g);
        } else {
            return g & view->get_wm_geometry();
        }
    }

    /**
     * Moves view geometry so that it is visible on the given workspace
     */
    void move_to_workspace(wayfire_view view, wf::point_t ws)
    {
        if (view->get_output() != output)
        {
            LOGE("Cannot ensure view visibility for a view from a different output!");
            return;
        }

        auto box = view->get_wm_geometry();
        auto visible = output->get_relative_geometry();
        visible.x += (ws.x - current_vx) * visible.width;
        visible.y += (ws.y - current_vy) * visible.height;

        if (!(box & visible))
        {
            /* center of the view */
            int cx = box.x + box.width / 2;
            int cy = box.y + box.height / 2;

            int width = visible.width, height = visible.height;
            /* compute center coordinates when moved to the current workspace */
            int local_cx = (cx % width + width) % width;
            int local_cy = (cy % height + height) % height;

            /* finally, calculate center coordinates in the target workspace */
            int target_cx = local_cx + visible.x;
            int target_cy = local_cy + visible.y;

            view->move(box.x + target_cx - cx, box.y + target_cy - cy);
        }
    }

    std::vector<wayfire_view> get_views_on_workspace(wf::point_t vp,
        uint32_t layers_mask, bool wm_only)
    {
        /* get all views in the given layers */
        std::vector<wayfire_view> views =
            output->workspace->get_views_in_layer(layers_mask);

        /* remove those which aren't visible on the workspace */
        auto it = std::remove_if(views.begin(), views.end(), [&] (wayfire_view view) {
            return !view_visible_on(view, vp, !wm_only);
        });

        views.erase(it, views.end());
        return views;
    }

    std::vector<wayfire_view> get_promoted_views(wf::point_t workspace)
    {
        std::vector<wayfire_view> views =
            output->workspace->get_promoted_views();

        /* remove those which aren't visible on the workspace */
        auto it = std::remove_if(views.begin(), views.end(), [&] (wayfire_view view) {
            return !view_visible_on(view, workspace, true);
        });

        views.erase(it, views.end());
        return views;
    }

    std::vector<wayfire_view> get_views_on_workspace_sublayer(wf::point_t vp,
        nonstd::observer_ptr<sublayer_t> sublayer, bool wm_only)
    {
        std::vector<wayfire_view> views =
            output->workspace->get_views_in_sublayer(sublayer);

        auto it = std::remove_if(views.begin(), views.end(), [&] (wayfire_view view) {
            return !view_visible_on(view, vp, !wm_only);
        });

        views.erase(it, views.end());
        return views;
    }

    wf::point_t get_current_workspace()
    {
        return {current_vx, current_vy};
    }

    wf::dimensions_t get_workspace_grid_size()
    {
        return {vwidth, vheight};
    }

    void set_workspace(wf::point_t nws)
    {
        if(nws.x >= vwidth || nws.y >= vheight || nws.x < 0 || nws.y < 0)
        {
            LOGE("Attempt to set invalid workspace: ", nws,
                " workspace grid size is ", vwidth, "x", vheight);
            return;
        }

        if (nws.x == current_vx && nws.y == current_vy)
        {
            output->refocus();
            return;
        }

        change_viewport_signal data;
        data.old_viewport = {current_vx, current_vy};
        data.new_viewport = {nws.x, nws.y};

        /* The part below is tricky, because with the current architecture
         * we cannot make the viewport change look atomic, i.e the workspace
         * is changed first, and then all views are moved.
         *
         * We first change the viewport, and then adjust the position of the
         * views. */
        current_vx = nws.x;
        current_vy = nws.y;

        auto screen = output->get_screen_size();
        auto dx = (data.old_viewport.x - nws.x) * screen.width;
        auto dy = (data.old_viewport.y - nws.y) * screen.height;

        for (auto& v : output->workspace->get_views_in_layer(MIDDLE_LAYERS))
        {
            v->move(v->get_wm_geometry().x + dx,
                v->get_wm_geometry().y + dy);
        }

        data.output = output;
        output->emit_signal("viewport-changed", &data);

        /* unfocus view from last workspace */
        output->focus_view(nullptr);
        /* we iterate through views on current viewport from bottom to top
         * that way we ensure that they will be focused before all others */
        auto views = get_views_on_workspace(get_current_workspace(),
            MIDDLE_LAYERS, true);

        for (auto& view : wf::reverse(views))
            output->workspace->bring_to_front(view);

        /* Focus last window */
        auto it = std::find_if(views.begin(), views.end(),
            [] (wayfire_view view) { return view->is_mapped(); });
        if (it != views.end())
            output->focus_view(*it);
    }
};

/**
 * output_workarea_manager_t provides workarea-related functionality from the
 * workspace_manager module
 */
class output_workarea_manager_t
{
    wf::geometry_t current_workarea;
    std::vector<workspace_manager::anchored_area*> anchors;

    output_t *output;
  public:
    output_workarea_manager_t(output_t *output)
    {
        this->output = output;
        this->current_workarea = output->get_relative_geometry();
    }

    wf::geometry_t get_workarea()
    {
        return current_workarea;
    }

    wf::geometry_t calculate_anchored_geometry(const workspace_manager::anchored_area& area)
    {
        auto wa = get_workarea();
        wf::geometry_t target;

        if (area.edge <= workspace_manager::ANCHORED_EDGE_BOTTOM)
        {
            target.width = wa.width;
            target.height = area.real_size;
        } else
        {
            target.height = wa.height;
            target.width = area.real_size;
        }

        target.x = wa.x;
        target.y = wa.y;

        if (area.edge == workspace_manager::ANCHORED_EDGE_RIGHT)
            target.x = wa.x + wa.width - target.width;

        if (area.edge == workspace_manager::ANCHORED_EDGE_BOTTOM)
            target.y = wa.y + wa.height - target.height;

        return target;
    }

    void add_reserved_area(workspace_manager::anchored_area *area)
    {
        anchors.push_back(area);
    }

    void remove_reserved_area(workspace_manager::anchored_area *area)
    {
        auto it = std::remove(anchors.begin(), anchors.end(), area);
        anchors.erase(it, anchors.end());
    }

    void reflow_reserved_areas()
    {
        auto old_workarea = current_workarea;

        current_workarea = output->get_relative_geometry();
        for (auto a : anchors)
        {
            auto anchor_area = calculate_anchored_geometry(*a);

            if (a->reflowed)
                a->reflowed(anchor_area, current_workarea);

            switch(a->edge)
            {
                case workspace_manager::ANCHORED_EDGE_TOP:
                    current_workarea.y += a->reserved_size;
                    // fallthrough
                case workspace_manager::ANCHORED_EDGE_BOTTOM:
                    current_workarea.height -= a->reserved_size;
                    break;

                case workspace_manager::ANCHORED_EDGE_LEFT:
                    current_workarea.x += a->reserved_size;
                    // fallthrough
                case workspace_manager::ANCHORED_EDGE_RIGHT:
                    current_workarea.width -= a->reserved_size;
                    break;
            }
        }

        reserved_workarea_signal data;
        data.old_workarea = old_workarea;
        data.new_workarea = current_workarea;

        if (data.old_workarea != data.new_workarea)
            output->emit_signal("reserved-workarea", &data);
    }
};

class workspace_manager::impl
{
    wf::output_t *output;
    wf::geometry_t output_geometry;

    signal_callback_t output_geometry_changed = [&] (void*)
    {
        auto old_w = output_geometry.width, old_h = output_geometry.height;
        auto new_size = output->get_screen_size();

        for (auto& view : layer_manager.get_views_in_layer(MIDDLE_LAYERS))
        {
            if (!view->is_mapped())
                continue;

            auto wm = view->get_wm_geometry();
            float px = 1. * wm.x / old_w;
            float py = 1. * wm.y / old_h;
            float pw = 1. * wm.width / old_w;
            float ph = 1. * wm.height / old_h;

            view->set_geometry({
                int(px * new_size.width), int(py * new_size.height),
                int(pw * new_size.width), int(ph * new_size.height)
            });
        }

        output_geometry = output->get_relative_geometry();
        workarea_manager.reflow_reserved_areas();
    };

    signal_callback_t view_changed_viewport = [=] (signal_data_t *data)
    {
        check_autohide_panels();
    };

    signal_connection_t on_view_state_updated = {[=] (signal_data_t*)
    {
        update_promoted_views();
    }};

    bool sent_autohide = false;

    std::unique_ptr<workspace_implementation_t> workspace_impl;

  public:
    output_layer_manager_t layer_manager;
    output_viewport_manager_t viewport_manager;
    output_workarea_manager_t workarea_manager;

    impl(output_t *o) :
        layer_manager(),
        viewport_manager(o),
        workarea_manager(o)
    {
        output = o;
        output_geometry = output->get_relative_geometry();

        o->connect_signal("view-change-viewport", &view_changed_viewport);
        o->connect_signal("output-configuration-changed", &output_geometry_changed);
        o->connect_signal("view-fullscreen", &on_view_state_updated);
        o->connect_signal("unmap-view", &on_view_state_updated);
    }

    workspace_implementation_t* get_implementation()
    {
        static default_workspace_implementation_t default_impl;
        return workspace_impl ?  workspace_impl.get() : &default_impl;
    }

    bool set_implementation(std::unique_ptr<workspace_implementation_t> impl,
        bool overwrite)
    {
        bool replace = overwrite || !workspace_impl;

        if (replace)
            workspace_impl = std::move(impl);

        return replace;
    }

    void check_autohide_panels()
    {
        auto fs_views = viewport_manager.get_promoted_views(
            viewport_manager.get_current_workspace());

        if (fs_views.size() && !sent_autohide)
        {
            sent_autohide = 1;
            output->emit_signal("fullscreen-layer-focused", reinterpret_cast<signal_data_t*> (1));
            LOGD("autohide panels");
        }
        else if (fs_views.empty() && sent_autohide)
        {
            sent_autohide = 0;
            output->emit_signal("fullscreen-layer-focused", reinterpret_cast<signal_data_t*> (0));
            LOGD("restore panels");
        }
    }

    void set_workspace(wf::point_t ws)
    {
        viewport_manager.set_workspace(ws);
        check_autohide_panels();
    }

    void request_workspace(wf::point_t ws)
    {
        if (ws == viewport_manager.get_current_workspace())
            return;

        change_viewport_signal data;
        data.carried_out = false;
        data.old_viewport = viewport_manager.get_current_workspace();
        data.new_viewport = ws;
        data.output = output;
        output->emit_signal("set-workspace-request", &data);

        if (!data.carried_out)
            set_workspace(ws);
    }

    void update_promoted_views()
    {
        auto vp = viewport_manager.get_current_workspace();
        auto already_promoted = viewport_manager.get_promoted_views(vp);
        for (auto& view : already_promoted)
            view->get_data_safe<layer_view_data_t>()->is_promoted = false;

        auto views = viewport_manager.get_views_on_workspace(
            vp, LAYER_WORKSPACE, true);

        /* Do not consider unmapped views or views which are not visible */
        auto it = std::remove_if(views.begin(), views.end(),
            [] (wayfire_view view) -> bool {
                return !view->is_mapped() || !view->is_visible();
            });
        views.erase(it, views.end());

        if (!views.empty() && views.front()->fullscreen)
            views.front()->get_data_safe<layer_view_data_t>()->is_promoted = true;

        check_autohide_panels();
    }

    void handle_view_first_add(wayfire_view view)
    {
        attach_view_signal data;
        data.view = view;
        output->emit_signal("layer-attach-view", &data);
    }

    void add_view_to_layer(wayfire_view view, layer_t layer)
    {
        bool first_add = layer_manager.get_view_layer(view) == 0;
        layer_manager.add_view_to_layer(view, layer);
        update_promoted_views();

        if (first_add)
            handle_view_first_add(view);
    }

    void add_view_to_sublayer(wayfire_view view,
        nonstd::observer_ptr<sublayer_t> sublayer)
    {
        bool first_add = layer_manager.get_view_layer(view) == 0;
        layer_manager.add_view_to_sublayer(view, sublayer);
        update_promoted_views();
        if (first_add)
            handle_view_first_add(view);
    }

    void bring_to_front(wayfire_view view)
    {
        if (!layer_manager.get_view_sublayer(view))
        {
            LOGE ("trying to bring_to_front a view without a layer!");
            return;
        }

        layer_manager.bring_to_front(view);
        update_promoted_views();
    }

    void restack_above(wayfire_view view, wayfire_view below)
    {
        if (!view || !below || view == below)
        {
            LOGE("Cannot restack a view on top of itself");
            return;
        }

        uint32_t view_layer = layer_manager.get_view_layer(view);
        uint32_t below_layer = layer_manager.get_view_layer(below);
        if (view_layer == 0 || below_layer == 0 || view_layer != below_layer)
        {
            LOGE("restacking views from different layers(",
                view_layer, " vs ", below_layer, ")!");
            return;
        }

        LOGD("restack ", view->get_title(), " on top of ", below->get_title());

        layer_manager.restack_above(view, below);
        update_promoted_views();
    }

    void restack_below(wayfire_view view, wayfire_view above)
    {
        if (!view || !above || view == above)
        {
            LOGE("Cannot restack a view on top of itself");
            return;
        }

        uint32_t view_layer = layer_manager.get_view_layer(view);
        uint32_t below_layer = layer_manager.get_view_layer(above);
        if (view_layer == 0 || below_layer == 0 || view_layer != below_layer)
        {
            LOGE("restacking views from different layers(",
                view_layer, " vs ", below_layer, "!)");
            return;
        }

        layer_manager.restack_below(view, above);
        update_promoted_views();
    }

    void remove_view(wayfire_view view)
    {
        uint32_t view_layer = layer_manager.get_view_layer(view);
        layer_manager.remove_view(view);

        detach_view_signal data;
        data.view = view;
        output->emit_signal("layer-detach-view", &data);

        /* Check if the next focused view is fullscreen. If so, then we need
         * to make sure it is in the fullscreen layer */
        if (view_layer & MIDDLE_LAYERS)
            update_promoted_views();
    }
};

workspace_manager::workspace_manager(output_t *wo) : pimpl(new impl(wo)) {}
workspace_manager::~workspace_manager() = default;

/* Just pass to the appropriate function from above */
std::vector<wf::point_t> workspace_manager::get_view_workspaces(wayfire_view view, double threshold)
{ return pimpl->viewport_manager.get_view_workspaces(view, threshold); }
bool workspace_manager::view_visible_on(wayfire_view view, wf::point_t ws) { return pimpl->viewport_manager.view_visible_on(view, ws, true); }
std::vector<wayfire_view> workspace_manager::get_views_on_workspace(wf::point_t ws, uint32_t layer_mask, bool wm_only)
{ return pimpl->viewport_manager.get_views_on_workspace(ws, layer_mask, wm_only); }
std::vector<wayfire_view> workspace_manager::get_views_on_workspace_sublayer(wf::point_t ws, nonstd::observer_ptr<sublayer_t> sublayer, bool wm_only)
{ return pimpl->viewport_manager.get_views_on_workspace_sublayer(ws, sublayer, wm_only); }

nonstd::observer_ptr<sublayer_t> workspace_manager::create_sublayer(layer_t layer, sublayer_mode_t mode)
{ return pimpl->layer_manager.create_sublayer(layer, mode); }
void workspace_manager::destroy_sublayer(nonstd::observer_ptr<sublayer_t> sublayer)
{ return pimpl->layer_manager.destroy_sublayer(sublayer); }
void workspace_manager::add_view_to_sublayer(wayfire_view view, nonstd::observer_ptr<sublayer_t> sublayer)
{ return pimpl->layer_manager.add_view_to_sublayer(view, sublayer); }

void workspace_manager::move_to_workspace(wayfire_view view, wf::point_t ws) { return pimpl->viewport_manager.move_to_workspace(view, ws); }

void workspace_manager::add_view(wayfire_view view, layer_t layer) { return pimpl->add_view_to_layer(view, layer); }
void workspace_manager::bring_to_front(wayfire_view view) { return pimpl->bring_to_front(view); }
void workspace_manager::restack_above(wayfire_view view, wayfire_view below) { return pimpl->restack_above(view, below); }
void workspace_manager::restack_below(wayfire_view view, wayfire_view below) { return pimpl->restack_below(view, below); }
void workspace_manager::remove_view(wayfire_view view) { return pimpl->remove_view(view); }
uint32_t workspace_manager::get_view_layer(wayfire_view view) { return pimpl->layer_manager.get_view_layer(view); }
std::vector<wayfire_view> workspace_manager::get_views_in_layer(uint32_t layers_mask) { return pimpl->layer_manager.get_views_in_layer(layers_mask); }
std::vector<wayfire_view> workspace_manager::get_views_in_sublayer(nonstd::observer_ptr<sublayer_t> sublayer)
{ return pimpl->layer_manager.get_views_in_sublayer(sublayer); }
std::vector<wayfire_view> workspace_manager::get_promoted_views()
{ return pimpl->layer_manager.get_promoted_views(); }
std::vector<wayfire_view> workspace_manager::get_promoted_views(wf::point_t workspace)
{ return pimpl->viewport_manager.get_promoted_views(workspace); }

workspace_implementation_t* workspace_manager::get_workspace_implementation() { return pimpl->get_implementation(); }
bool workspace_manager::set_workspace_implementation(std::unique_ptr<workspace_implementation_t> impl, bool overwrite)
{ return pimpl->set_implementation(std::move(impl), overwrite); }

void workspace_manager::set_workspace(wf::point_t ws) { return pimpl->set_workspace(ws); }
void workspace_manager::request_workspace(wf::point_t ws) { return pimpl->request_workspace(ws); }
wf::point_t workspace_manager::get_current_workspace() { return pimpl->viewport_manager.get_current_workspace(); }
wf::dimensions_t workspace_manager::get_workspace_grid_size() { return pimpl->viewport_manager.get_workspace_grid_size(); }

void workspace_manager::add_reserved_area(anchored_area *area) { return pimpl->workarea_manager.add_reserved_area(area); }
void workspace_manager::remove_reserved_area(anchored_area *area) { return pimpl->workarea_manager.remove_reserved_area(area); }
void workspace_manager::reflow_reserved_areas() { return pimpl->workarea_manager.reflow_reserved_areas(); }
wf::geometry_t workspace_manager::get_workarea() { return pimpl->workarea_manager.get_workarea(); }
} // namespace wf
