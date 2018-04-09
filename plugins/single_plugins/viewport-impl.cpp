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

using wf_layer = std::list<wayfire_view>;

class viewport_manager : public workspace_manager
{
    private:
        int vwidth, vheight, vx, vy;
        wayfire_output *output;

        std::vector<wayfire_view> custom_views;

        wf_layer panel_layer, normal_layer, background_layer;
        signal_callback_t adjust_fullscreen_layer, view_detached;

        struct {
            int top_padding;
            int bot_padding;
            int left_padding;
            int right_padding;
        } workarea;

        std::vector<std::vector<wf_workspace_implementation*>> implementation;
        wf_default_workspace_implementation default_implementation;

        wf_layer* get_layer(wayfire_view view);

    public:
        void init(wayfire_output *output);
        ~viewport_manager();

        void view_bring_to_front(wayfire_view view);
        void view_removed(wayfire_view view);

        bool view_visible_on(wayfire_view, std::tuple<int, int>);

        void for_all_view(view_callback_proc_t call);
        void for_each_view(view_callback_proc_t call);
        void for_each_view_reverse(view_callback_proc_t call);

        wf_workspace_implementation* get_implementation(std::tuple<int, int>);
        bool set_implementation(std::tuple<int, int>, wf_workspace_implementation*, bool override = false);

        std::vector<wayfire_view> get_views_on_workspace(std::tuple<int, int>);
        std::vector<wayfire_view>
        get_renderable_views_on_workspace(std::tuple<int, int> ws);
        std::vector<wayfire_view> get_panels();

        void add_renderable_view(wayfire_view view);
        void rem_renderable_view(wayfire_view view);

        void set_workspace(std::tuple<int, int>);

        std::tuple<int, int> get_current_workspace();
        std::tuple<int, int> get_workspace_grid_size();

        wayfire_view get_background_view();

        void add_background(wayfire_view background, int x, int y);
        void add_panel(wayfire_view panel);
        void reserve_workarea(uint32_t position,
             uint32_t width, uint32_t height);
        void configure_panel(wayfire_view view, int x, int y);

        wf_geometry get_workarea();

        void check_lower_panel_layer(int base);
        bool draw_panel_over_fullscreen_windows;
        bool sent_autohide = false;
};

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

wf_layer* viewport_manager::get_layer(wayfire_view view)
{
    auto it = std::find(background_layer.begin(), background_layer.end(), view);
    if (it != background_layer.end())
        return &background_layer;

    it = std::find(panel_layer.begin(), panel_layer.end(), view);
    if (it != panel_layer.end())
        return &panel_layer;

    return &normal_layer;
}

void viewport_manager::view_bring_to_front(wayfire_view view)
{
    if (get_layer(view) != &normal_layer)
        return;

    view_removed(view);
    normal_layer.insert(normal_layer.begin(), view);
}

void viewport_manager::view_removed(wayfire_view view)
{
    background_layer.remove(view);
    normal_layer.remove(view);
    panel_layer.remove(view);
}

bool viewport_manager::view_visible_on(wayfire_view view, std::tuple<int, int> vp)
{
    GetTuple(tx, ty, vp);

    auto g = output->get_full_geometry();
    g.x += (tx - vx) * output->handle->width;
    g.y += (ty - vy) * (output->handle->height);

    return rect_intersect(g, view->get_wm_geometry());
}

void viewport_manager::for_all_view(view_callback_proc_t call)
{
    std::vector<wayfire_view> views = custom_views;
    for (auto v : panel_layer)
        views.push_back(v);
    for (auto v : normal_layer)
        views.push_back(v);
    for (auto v : background_layer)
        views.push_back(v);

    for (auto v : views)
        call(v);
}

void viewport_manager::for_each_view(view_callback_proc_t call)
{
    std::vector<wayfire_view> views;
    for (auto v : normal_layer)
        if (v->is_visible())
            views.push_back(v);

    for (auto v : views)
        call(v);
}

void viewport_manager::for_each_view_reverse(view_callback_proc_t call)
{
    std::vector<wayfire_view> views;
    for (auto v : normal_layer)
        if (v->is_visible())
            views.push_back(v);

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

    if (nx == vx && ny == vy) {
        auto views = get_views_on_workspace(std::make_tuple(vx, vy));
        if (views.size() >= 1)
            output->focus_view(views[0]);
        return;
    }

    auto dx = (vx - nx) * output->handle->width;
    auto dy = (vy - ny) * output->handle->height;

    for_each_view([=] (wayfire_view v) {
        v->move(v->get_wm_geometry().x + dx, v->get_wm_geometry().y + dy);
    });

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
    auto views = get_views_on_workspace(std::make_tuple(vx, vy));
    auto it = views.rbegin();
    while(it != views.rend()) {
        if ((*it)->is_mapped && !(*it)->destroyed)
            output->focus_view(*it);
        ++it;
    }

    check_lower_panel_layer(0);
}

std::vector<wayfire_view> viewport_manager::get_views_on_workspace(std::tuple<int, int> vp)
{
    GetTuple(tx, ty, vp);

    wf_geometry g = output->get_full_geometry();
    g.x += (tx - vx) * output->handle->width;
    g.y += (ty - vy) * (output->handle->height);

    std::vector<wayfire_view> ret;
    for_each_view([&ret, g] (wayfire_view view) {
        if (rect_intersect(g, view->get_wm_geometry())) {
            ret.push_back(view);
        }
    });

    return ret;
}

std::vector<wayfire_view> viewport_manager::get_renderable_views_on_workspace(
        std::tuple<int, int> ws)
{
    std::vector<wayfire_view> ret = custom_views;
    for (auto v : panel_layer)
        ret.push_back(v);

    GetTuple(tx, ty, ws);

    wf_geometry g = output->get_full_geometry();
    g.x += (tx - vx) * output->handle->width;
    g.y += (ty - vy) * (output->handle->height);

    for (auto v : normal_layer)
            ret.push_back(v);

    auto bg = get_background_view();
    if (bg) ret.push_back(bg);

    return ret;
}

void viewport_manager::add_renderable_view(wayfire_view v)
{
    custom_views.push_back(v);
}

void viewport_manager::rem_renderable_view(wayfire_view v)
{
    auto it = custom_views.begin();
    while(it != custom_views.end())
    {
        if (*it == v)
            it = custom_views.erase(it);
    }
}

std::vector<wayfire_view> viewport_manager::get_panels()
{
    std::vector<wayfire_view> ret;

    auto g = output->get_full_geometry();
    for (auto v : panel_layer)
        if (rect_intersect(g, v->get_wm_geometry()))
            ret.push_back(v);

    return ret;
}


wayfire_view viewport_manager::get_background_view()
{
    if (background_layer.empty())
        return nullptr;
    return *background_layer.begin();
}

void viewport_manager::add_background(wayfire_view background, int x, int y)
{
    background->is_special = true;

    auto g = output->get_full_geometry();
    background->move(x + g.x, y + g.y);

    background->get_output()->detach_view(background);
    background->set_output(output);

    background_layer.push_front(background);
}

void viewport_manager::add_panel(wayfire_view panel)
{
    panel->is_special = true;
    /* views have first been created as desktop views,
     * so they are currently in the normal layer, we must remove them first */
    panel->get_output()->detach_view(panel);
    panel->set_output(output);

    panel_layer.push_front(panel);
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

void viewport_manager::configure_panel(wayfire_view view, int x, int y)
{
    auto g = output->get_full_geometry();
    view->move(g.x + x, g.y + y);
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
    auto views = get_views_on_workspace(get_current_workspace());

    int cnt_fullscreen = base;
    for (auto v : views)
        cnt_fullscreen += (v->fullscreen ? 1 : 0);

    if (cnt_fullscreen)
    {
        if (draw_panel_over_fullscreen_windows)
        {
            if (!sent_autohide)
            {
                sent_autohide = 1;

                for (auto res : core->shell_clients)
                    wayfire_shell_send_output_autohide_panels(res, output->id, 1);
            }
        } else
        {
            /* TODO: Implement hiding of layers, possibly custom struct */
            //weston_layer_unset_position(&panel_layer);
        }
    } else
    {
        // TODO: imlement showing
        /*
        weston_layer_set_position(&panel_layer, WESTON_LAYER_POSITION_UI);

        if (sent_autohide)
        {
            sent_autohide = 0;
            for (auto res : core->shell_clients)
                wayfire_shell_send_output_autohide_panels(res, output->handle->id, 0);
        } */
    }
}

class viewport_impl_plugin : public wayfire_plugin_t {

    void init(wayfire_config *config)
    {
        auto vp = new viewport_manager();
        vp->init(output);
        vp->draw_panel_over_fullscreen_windows =
            config->get_section("core")->get_int("draw_panel_over_fullscreen_windows", 0);

        output->workspace = vp;
    }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new viewport_impl_plugin();
    }
}

