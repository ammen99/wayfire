#include <config.hpp>
#include <view.hpp>
#include <debug.hpp>
#include <output.hpp>
#include <core.hpp>
#include <workspace-manager.hpp>
#include <render-manager.hpp>
#include <signal-definitions.hpp>
#include <pixman-1/pixman.h>
#include <opengl.hpp>
#include <list>
#include <algorithm>
#include "wayfire-shell-protocol.h"

struct wf_default_workspace_implementation : wf_workspace_implementation
{
    bool view_movable (wayfire_view view)  { return true; }
    bool view_resizable(wayfire_view view) { return true; }
};

using wf_layer_container = std::list<wayfire_view>;

class viewport_manager : public workspace_manager
{
    static const int TOTAL_WF_LAYERS = 6;
    struct custom_layer_data_t : public wf_custom_view_data
    {
        static const std::string name;
        uint32_t layer = 0;
    };

    uint32_t& _get_view_layer(wayfire_view view)
    {
        auto it = view->custom_data.find(custom_layer_data_t::name);
        custom_layer_data_t *layer_data = nullptr;

        if (it == view->custom_data.end())
        {
            layer_data = new custom_layer_data_t;
            view->custom_data[custom_layer_data_t::name] = layer_data;
        } else
        {
            layer_data = dynamic_cast<custom_layer_data_t*> (it->second);
        }

        assert(layer_data);

        return layer_data->layer;
    }


    private:
        int vwidth, vheight, vx, vy;
        wayfire_output *output;

        wf_layer_container layers[TOTAL_WF_LAYERS];

        inline int layer_index_from_mask(uint32_t layer_mask) const
        { return __builtin_ctz(layer_mask); }

        signal_callback_t adjust_fullscreen_layer, view_detached;

        struct {
            int top_padding;
            int bot_padding;
            int left_padding;
            int right_padding;
        } workarea;

        std::vector<std::vector<wf_workspace_implementation*>> implementation;
        wf_default_workspace_implementation default_implementation;

        void check_lower_panel_layer(int base);
        bool draw_panel_over_fullscreen_windows;
        bool sent_autohide = false;

        void remove_from_layer(wayfire_view view, uint32_t layer);

    public:
        void init(wayfire_output *output);
        ~viewport_manager();

        bool view_visible_on(wayfire_view, std::tuple<int, int>);

        std::vector<wayfire_view>
            get_views_on_workspace(std::tuple<int, int> ws, uint32_t layer_mask);
        void for_each_view(view_callback_proc_t call, uint32_t layers_mask);
        void for_each_view_reverse(view_callback_proc_t call, uint32_t layers_mask);

        void add_view_to_layer(wayfire_view view, uint32_t layer);
        uint32_t get_view_layer(wayfire_view view);

        wf_workspace_implementation* get_implementation(std::tuple<int, int>);
        bool set_implementation(std::tuple<int, int>, wf_workspace_implementation*, bool override = false);

        void set_workspace(std::tuple<int, int>);

        std::tuple<int, int> get_current_workspace();
        std::tuple<int, int> get_workspace_grid_size();

        void reserve_workarea(uint32_t position,
             uint32_t width, uint32_t height);

        wf_geometry get_workarea();
};

const std::string viewport_manager::custom_layer_data_t::name = "__layer_data";

/* Start viewport_manager */
void viewport_manager::init(wayfire_output *o)
{
    output = o;
    vx = vy = 0;

    vwidth = core->vwidth;
    vheight = core->vheight;
    implementation.resize(vwidth, std::vector<wf_workspace_implementation*>
            (vheight, &default_implementation));

    adjust_fullscreen_layer = [=] (signal_data *data)
    {
        auto conv = static_cast<view_maximized_signal*> (data);
        assert(conv);

        if (conv->state != conv->view->fullscreen)
            check_lower_panel_layer(conv->state ? 1 : -1);
        else
            check_lower_panel_layer(0);
    };

    view_detached = [=] (signal_data *data)
    {
        check_lower_panel_layer(0);
    };

    o->connect_signal("view-fullscreen-request", &adjust_fullscreen_layer);
    o->connect_signal("attach-view", &view_detached);
    o->connect_signal("detach-view", &view_detached);
}

viewport_manager::~viewport_manager()
{
}

void viewport_manager::remove_from_layer(wayfire_view view, uint32_t layer)
{
    if (layer == 0)
        return;

    auto& layer_container = layers[layer];

    auto it = std::remove(layer_container.begin(), layer_container.end(), view);
    layer_container.erase(it, layer_container.end());
}

void viewport_manager::add_view_to_layer(wayfire_view view, uint32_t layer)
{
    /* make sure it is a valid layer */
    assert(layer == 0 || layer == uint32_t(-1) || (__builtin_popcount(layer) == 1 && layer <= WF_LAYER_LOCK));
    log_info("add to layer %d", layer);

    auto& current_layer = _get_view_layer(view);
    if (layer == 0)
    {
        if (current_layer)
            remove_from_layer(view, layer_index_from_mask(current_layer));

        current_layer = 0;
        return;
    }

    if (current_layer == layer)
        return;

    if (layer == (uint32_t)-1)
        layer = current_layer;

    if (current_layer)
        remove_from_layer(view, layer_index_from_mask(current_layer));

    auto& layer_container = layers[layer_index_from_mask(layer)];
    log_info("add to layer %d", layer_index_from_mask(layer));
    layer_container.push_front(view);
    current_layer = layer;
}

uint32_t viewport_manager::get_view_layer(wayfire_view view)
{
    return _get_view_layer(view);
}

bool viewport_manager::view_visible_on(wayfire_view view, std::tuple<int, int> vp)
{
    GetTuple(tx, ty, vp);

    auto g = output->get_full_geometry();

    if (!view->is_special)
    {
        g.x += (tx - vx) * g.width;
        g.y += (ty - vy) * g.height;
    }

    if (view->get_transformer())
        return rect_intersect(g, view->get_bounding_box());
    else
        return rect_intersect(g, view->get_wm_geometry());
}

void viewport_manager::for_each_view(view_callback_proc_t call, uint32_t layers_mask)
{
    std::vector<wayfire_view> views;

    for (int i = TOTAL_WF_LAYERS - 1; i >= 0; i--)
    {
        if ((1 << i) & layers_mask)
            for (auto v : layers[i])
                views.push_back(v);
    }

    for (auto v : views)
        call(v);
}

void viewport_manager::for_each_view_reverse(view_callback_proc_t call, uint32_t layers_mask)
{
    std::vector<wayfire_view> views;

    for (int i = TOTAL_WF_LAYERS - 1; i >= 0; i--)
    {
        if ((1 << i) & layers_mask)
            for (auto v : layers[i])
                views.push_back(v);
    }

    auto it = views.rbegin();
    while(it != views.rend())
        call(*it++);
}

wf_workspace_implementation* viewport_manager::get_implementation(std::tuple<int, int> vt)
{
    GetTuple(x, y, vt);
    return implementation[x][y];
}

bool viewport_manager::set_implementation(std::tuple<int, int> vt, wf_workspace_implementation* impl, bool override)
{
    GetTuple(x, y, vt);
    bool replace = override || implementation[x][y] == nullptr;

    if (replace)
        implementation[x][y] = impl;

    return replace;
}

std::tuple<int, int> viewport_manager::get_current_workspace()
{
    return std::make_tuple(vx, vy);
}

std::tuple<int, int> viewport_manager::get_workspace_grid_size()
{
    return std::make_tuple(vwidth, vheight);
}

void viewport_manager::set_workspace(std::tuple<int, int> nPos)
{
    GetTuple(nx, ny, nPos);
    if(nx >= vwidth || ny >= vheight || nx < 0 || ny < 0)
        return;

    if (nx == vx && ny == vy)
    {
        auto views = get_views_on_workspace(std::make_tuple(vx, vy), WF_WM_LAYERS);
        if (views.size() >= 1)
            output->focus_view(views[0]);
        return;
    }

    GetTuple(sw, sh, output->get_screen_size());
    auto dx = (vx - nx) * sw;
    auto dy = (vy - ny) * sh;

    for_each_view([=] (wayfire_view v) {
        v->move(v->get_wm_geometry().x + dx, v->get_wm_geometry().y + dy);
    }, WF_WM_LAYERS);

    output->render->schedule_redraw();

    change_viewport_signal data;
    data.old_vx = vx;
    data.old_vy = vy;

    data.new_vx = nx;
    data.new_vy = ny;

    vx = nx;
    vy = ny;
    output->emit_signal("viewport-changed", &data);

    output->focus_view(nullptr);
    /* we iterate through views on current viewport from bottom to top
     * that way we ensure that they will be focused befor all others */
    auto views = get_views_on_workspace(std::make_tuple(vx, vy), WF_WM_LAYERS);
    auto it = views.rbegin();
    while(it != views.rend()) {
        if ((*it)->is_mapped() && !(*it)->destroyed)
            output->focus_view(*it);
        ++it;
    }

    check_lower_panel_layer(0);
}

std::vector<wayfire_view>
viewport_manager::get_views_on_workspace(std::tuple<int, int> vp,
                                         uint32_t layers_mask)
{

    std::vector<wayfire_view> views;

    for (int i = 5; i >= 0; i--)
    {
        if ((1 << i) & layers_mask)
        {
            for (auto v : layers[i])
                if (view_visible_on(v, vp))
                    views.push_back(v);
        }
    }

    return views;
}

void viewport_manager::reserve_workarea(uint32_t position,
        uint32_t width, uint32_t height)
{
    GetTuple(sw, sh, output->get_screen_size());
    switch(position) {
        case WAYFIRE_SHELL_PANEL_POSITION_LEFT:
            workarea.left_padding = width;
            height = sh;
            break;
        case WAYFIRE_SHELL_PANEL_POSITION_RIGHT:
            workarea.right_padding = width;
            height = sh;
            break;
        case WAYFIRE_SHELL_PANEL_POSITION_UP:
            workarea.top_padding = height;
            width = sw;
            break;
        case WAYFIRE_SHELL_PANEL_POSITION_DOWN:
            workarea.bot_padding = height;
            width = sw;
            break;
        default:
            log_error("bad reserve_workarea!");
    }

    reserved_workarea_signal data;
    data.width = width;
    data.height = height;
    data.position = position;
    output->emit_signal("reserved-workarea", &data);
}

wf_geometry viewport_manager::get_workarea()
{
    auto g = output->get_full_geometry();
    return
    {
        g.x + workarea.left_padding,
        g.y + workarea.top_padding,
        g.width - workarea.left_padding - workarea.right_padding,
        g.height - workarea.top_padding - workarea.bot_padding
    };
}

void viewport_manager::check_lower_panel_layer(int base)
{
    auto views = get_views_on_workspace(get_current_workspace(), WF_WM_LAYERS);

    int cnt_fullscreen = base;
    for (auto v : views)
        cnt_fullscreen += (v->fullscreen ? 1 : 0);

    log_info("send autohide %d", base);
    if (cnt_fullscreen)
    {
        if (!sent_autohide)
        {
            sent_autohide = 1;

            for (auto res : core->shell_clients)
                wayfire_shell_send_output_autohide_panels(res, output->id, 1);
        }
    } else
    {
        // TODO: imlement showing
        //weston_layer_set_position(&panel_layer, WESTON_LAYER_POSITION_UI);

        if (sent_autohide)
        {
            sent_autohide = 0;
            for (auto res : core->shell_clients)
                wayfire_shell_send_output_autohide_panels(res, output->id, 0);
        }
    }
}

class viewport_impl_plugin : public wayfire_plugin_t {

    void init(wayfire_config *config)
    {
        auto vp = new viewport_manager();
        vp->init(output);

        /* TODO: fix draw_panel_over_fullscreen_windows or update protocol */
        /*
        vp->draw_panel_over_fullscreen_windows =
            config->get_section("core")->get_int("draw_panel_over_fullscreen_windows", 0);
            */

        output->workspace = vp;
    }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new viewport_impl_plugin();
    }
}

