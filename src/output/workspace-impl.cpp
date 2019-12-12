#include <view.hpp>
#include <debug.hpp>
#include <output.hpp>
#include <core.hpp>
#include <workspace-manager.hpp>
#include <render-manager.hpp>
#include <signal-definitions.hpp>
#include <opengl.hpp>
#include <list>
#include <algorithm>
#include <nonstd/reverse.hpp>
#include <wayfire/util/log.hpp>

namespace wf
{
/**
 * output_layer_manager_t is a part of the workspace_manager module. It provides
 * the functionality related to layers.
 */
class output_layer_manager_t
{
    struct view_layer_data_t : public wf::custom_data_t
    {
        uint32_t layer = 0;
    };

    using layer_container = std::list<wayfire_view>;
    layer_container layers[TOTAL_LAYERS];

  public:
    constexpr int layer_index_from_mask(uint32_t layer_mask) const
    {
        return __builtin_ctz(layer_mask);
    }

    uint32_t& get_view_layer(wayfire_view view)
    {
        return view->get_data_safe<view_layer_data_t>()->layer;
    }

    void remove_view(wayfire_view view)
    {
        auto& view_layer = get_view_layer(view);
        if (!view_layer)
            return;

        view->damage();
        auto& layer_container = layers[layer_index_from_mask(view_layer)];

        auto it = std::remove(
            layer_container.begin(), layer_container.end(), view);
        layer_container.erase(it, layer_container.end());

        view_layer = 0;
    }

    /**
     * Add or move the view to the given layer
     */
    void add_view_to_layer(wayfire_view view, layer_t layer)
    {
        view->damage();
        auto& current_layer = get_view_layer(view);

        if (current_layer)
            remove_view(view);

        auto& layer_container = layers[layer_index_from_mask(layer)];
        layer_container.push_front(view);
        current_layer = layer;
        view->damage();
    }

    void bring_to_front(wayfire_view view)
    {
        uint32_t view_layer = get_view_layer(view);
        assert(view_layer > 0); // checked in workspace_manager::impl

        remove_view(view);
        add_view_to_layer(view, static_cast<layer_t>(view_layer));
    }

    wayfire_view get_front_view(wf::layer_t layer)
    {
        auto& container = layers[layer_index_from_mask(layer)];
        if (container.empty())
            return nullptr;
        return container.front();
    }

    void restack_above(wayfire_view view, wayfire_view below)
    {
        remove_view(view);
        auto layer = get_view_layer(below);
        auto& container = layers[layer_index_from_mask(layer)];
        auto it = std::find(container.begin(), container.end(), below);

        container.insert(it, view);
        get_view_layer(view) = layer;
    }

    void restack_below(wayfire_view view, wayfire_view above)
    {
        remove_view(view);
        auto layer = get_view_layer(above);
        auto& container = layers[layer_index_from_mask(layer)];
        auto it = std::find(container.begin(), container.end(), above);
        assert(it != container.end());

        container.insert(std::next(it), view);
        get_view_layer(view) = layer;
    }

    std::vector<wayfire_view> get_views_in_layer(uint32_t layers_mask)
    {
        std::vector<wayfire_view> views;
        for (int i = TOTAL_LAYERS - 1; i >= 0; i--)
        {
            if ((1 << i) & layers_mask)
            {
                views.insert(views.end(),
                    layers[i].begin(), layers[i].end());
            }
        }

        return views;
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
     * @param use_bbox When considering view visibility, whether to use the
     *        bounding box or the wm geometry.
     *
     * @return true if the view is visible on the workspace vp
     */
    bool view_visible_on(wayfire_view view, wf_point vp, bool use_bbox)
    {
        auto g = output->get_relative_geometry();
        if (view->role != VIEW_ROLE_SHELL_VIEW)
        {
            g.x += (vp.x - current_vx) * g.width;
            g.y += (vp.y - current_vy) * g.height;
        }

        if (view->has_transformer() & use_bbox) {
            return view->intersects_region(g);
        } else {
            return g & view->get_wm_geometry();
        }
    }

    /**
     * Moves view geometry so that it is visible on the given workspace
     */
    void move_to_workspace(wayfire_view view, wf_point ws)
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

    std::vector<wayfire_view> get_views_on_workspace(wf_point vp,
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

    wf_point get_current_workspace()
    {
        return {current_vx, current_vy};
    }

    wf_size_t get_workspace_grid_size()
    {
        return {vwidth, vheight};
    }

    void set_workspace(wf_point nws)
    {
        if(nws.x >= vwidth || nws.y >= vheight || nws.x < 0 || nws.y < 0)
        {
            LOGE("Attempt to set invalid workspace: %d,%d,"
                " workspace grid size is %dx%d", nws.x, nws.y, vwidth, vheight);
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

        output->emit_signal("viewport-changed", &data);

        /* unfocus view from last workspace */
        output->focus_view(nullptr);
        /* we iterate through views on current viewport from bottom to top
         * that way we ensure that they will be focused before all others */
        auto views = get_views_on_workspace(get_current_workspace(),
            MIDDLE_LAYERS, true);

        for (auto& view : wf::reverse(views))
        {
            if (view->is_mapped())
                output->workspace->bring_to_front(view);
        }

        /* Focus last window */
        if (!views.empty())
            output->focus_view(views.front());
    }
};

/**
 * output_workarea_manager_t provides workarea-related functionality from the
 * workspace_manager module
 */
class output_workarea_manager_t
{
    wf_geometry current_workarea;
    std::vector<workspace_manager::anchored_area*> anchors;

    output_t *output;
  public:
    output_workarea_manager_t(output_t *output)
    {
        this->output = output;
        this->current_workarea = output->get_relative_geometry();
    }

    wf_geometry get_workarea()
    {
        return current_workarea;
    }

    wf_geometry calculate_anchored_geometry(const workspace_manager::anchored_area& area)
    {
        auto wa = get_workarea();
        wf_geometry target;

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
    wf_geometry output_geometry;

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
        auto fs_views = viewport_manager.get_views_on_workspace(
            viewport_manager.get_current_workspace(), wf::LAYER_FULLSCREEN, true);

        if (fs_views.size() && !sent_autohide)
        {
            sent_autohide = 1;
            output->emit_signal("autohide-panels", reinterpret_cast<signal_data_t*> (1));
            LOGD("autohide panels");
        }
        else if (fs_views.empty() && sent_autohide)
        {
            sent_autohide = 0;
            output->emit_signal("autohide-panels", reinterpret_cast<signal_data_t*> (0));
            LOGD("restore panels");
        }
    }

    void set_workspace(wf_point ws)
    {
        viewport_manager.set_workspace(ws);
        check_autohide_panels();
    }

    layer_t get_target_layer(wayfire_view view, layer_t current_target)
    {
        /* A view which is fullscreen should go directly to the fullscreen layer
         * when it is raised */
        if ((current_target & MIDDLE_LAYERS) && view->fullscreen) {
            return LAYER_FULLSCREEN;
        } else {
            return current_target;
        }
    }

    void check_lower_fullscreen_layer(wayfire_view top_view, uint32_t top_view_layer)
    {
        /* We need to lower fullscreen layer only if we are focusing a regular
         * view, which isn't fullscreen */
        if (top_view_layer != LAYER_WORKSPACE || top_view->fullscreen)
            return;

        auto views = viewport_manager.get_views_on_workspace(
            viewport_manager.get_current_workspace(), LAYER_FULLSCREEN, true);

        for (auto& v : wf::reverse(views))
            layer_manager.add_view_to_layer(v, LAYER_WORKSPACE);
    }

    void add_view_to_layer(wayfire_view view, layer_t layer)
    {
        uint32_t view_layer_before = layer_manager.get_view_layer(view);
        layer_t target_layer = get_target_layer(view, layer);

        /* First lower fullscreen layer, then make the view on top of all lowered
         * fullscreen views */
        check_lower_fullscreen_layer(view, target_layer);
        layer_manager.add_view_to_layer(view, target_layer);

        if (view_layer_before == 0)
        {
            _view_signal data;
            data.view = view;
            output->emit_signal("attach-view", &data);
        }

        check_autohide_panels();
    }

    void bring_to_front(wayfire_view view)
    {
        uint32_t view_layer = layer_manager.get_view_layer(view);
        if (!view_layer)
        {
            LOGE ("trying to bring_to_front a view without a layer!");
            return;
        }

        uint32_t target_layer = get_target_layer(view,
            static_cast<layer_t>(view_layer));

        /* First lower fullscreen layer, then make the view on top of all lowered
         * fullscreen views */
        check_lower_fullscreen_layer(view, target_layer);
        if (view_layer == target_layer) {
            layer_manager.bring_to_front(view);
        } else {
            layer_manager.add_view_to_layer(view,
                static_cast<layer_t>(target_layer));
        }

        check_autohide_panels();
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
            LOGE("restacking views from different layers(%d vs %d!)",
                view_layer, below_layer);
            return;
        }

        LOGI("restack %s on top of %s", view->get_title().c_str(),
            below->get_title().c_str());

        /* If we restack on top of the front-most view, then this can
         * potentially change fullscreen state. So in this case use the
         * bring_to_front() path */
        auto front_view =
            layer_manager.get_front_view(static_cast<wf::layer_t>(below_layer));
        if (front_view == below) {
            bring_to_front(view);
        } else {
            layer_manager.restack_above(view, below);
        }
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
            LOGE("restacking views from different layers(%d vs %d!)",
                view_layer, below_layer);
            return;
        }

        layer_manager.restack_below(view, above);
    }

    void remove_view(wayfire_view view)
    {
        uint32_t view_layer = layer_manager.get_view_layer(view);
        layer_manager.remove_view(view);

        _view_signal data;
        data.view = view;
        output->emit_signal("detach-view", &data);

        /* Check if the next focused view is fullscreen. If so, then we need
         * to make sure it is in the fullscreen layer */
        if (view_layer & MIDDLE_LAYERS)
        {
            auto views = viewport_manager.get_views_on_workspace(
                viewport_manager.get_current_workspace(),
                LAYER_WORKSPACE | LAYER_FULLSCREEN, true);

            if (views.size() && views[0]->fullscreen)
                layer_manager.add_view_to_layer(views[0], LAYER_FULLSCREEN);
        }

        check_autohide_panels();
    }
};

workspace_manager::workspace_manager(output_t *wo) : pimpl(new impl(wo)) {}
workspace_manager::~workspace_manager() = default;

/* Just pass to the appropriate function from above */
bool workspace_manager::view_visible_on(wayfire_view view, wf_point ws) { return pimpl->viewport_manager.view_visible_on(view, ws, true); }
std::vector<wayfire_view> workspace_manager::get_views_on_workspace(wf_point ws, uint32_t layer_mask, bool wm_only)
{ return pimpl->viewport_manager.get_views_on_workspace(ws, layer_mask, wm_only); }

void workspace_manager::move_to_workspace(wayfire_view view, wf_point ws) { return pimpl->viewport_manager.move_to_workspace(view, ws); }

void workspace_manager::add_view(wayfire_view view, layer_t layer) { return pimpl->add_view_to_layer(view, layer); }
void workspace_manager::bring_to_front(wayfire_view view) { return pimpl->bring_to_front(view); }
void workspace_manager::restack_above(wayfire_view view, wayfire_view below) { return pimpl->restack_above(view, below); }
void workspace_manager::restack_below(wayfire_view view, wayfire_view below) { return pimpl->restack_below(view, below); }
void workspace_manager::remove_view(wayfire_view view) { return pimpl->remove_view(view); }
uint32_t workspace_manager::get_view_layer(wayfire_view view) { return pimpl->layer_manager.get_view_layer(view); }
std::vector<wayfire_view> workspace_manager::get_views_in_layer(uint32_t layers_mask) { return pimpl->layer_manager.get_views_in_layer(layers_mask); }

workspace_implementation_t* workspace_manager::get_workspace_implementation() { return pimpl->get_implementation(); }
bool workspace_manager::set_workspace_implementation(std::unique_ptr<workspace_implementation_t> impl, bool overwrite)
{ return pimpl->set_implementation(std::move(impl), overwrite); }

void workspace_manager::set_workspace(wf_point ws) { return pimpl->set_workspace(ws); }
wf_point workspace_manager::get_current_workspace() { return pimpl->viewport_manager.get_current_workspace(); }
wf_size_t workspace_manager::get_workspace_grid_size() { return pimpl->viewport_manager.get_workspace_grid_size(); }

void workspace_manager::add_reserved_area(anchored_area *area) { return pimpl->workarea_manager.add_reserved_area(area); }
void workspace_manager::remove_reserved_area(anchored_area *area) { return pimpl->workarea_manager.remove_reserved_area(area); }
void workspace_manager::reflow_reserved_areas() { return pimpl->workarea_manager.reflow_reserved_areas(); }
wf_geometry workspace_manager::get_workarea() { return pimpl->workarea_manager.get_workarea(); }
} // namespace wf
