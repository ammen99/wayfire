#include <output.hpp>
#include <core.hpp>
#include <signal_definitions.hpp>
#include <pixman-1/pixman.h>
#include <opengl.hpp>

struct wf_default_workspace_implementation : wf_workspace_implementation
{
    bool view_movable (wayfire_view view)  { return true; }
    bool view_resizable(wayfire_view view) { return true; }
};

class viewport_manager : public workspace_manager
{
    private:
        int vwidth, vheight, vx, vy;
        wayfire_output *output;
        wayfire_view background;

        weston_layer panel_layer, normal_layer, background_layer;
        signal_callback_t adjust_fullscreen_layer, view_detached;

        struct {
            int top_padding;
            int bot_padding;
            int left_padding;
            int right_padding;
        } workarea;

        std::vector<std::vector<wf_workspace_implementation*>> implementation;

        wf_default_workspace_implementation default_implementation;

    public:
        void init(wayfire_output *output);

        void view_bring_to_front(wayfire_view view);
        void view_removed(wayfire_view view);

        void for_each_view(view_callback_proc_t call);
        void for_each_view_reverse(view_callback_proc_t call);

        wf_workspace_implementation* get_implementation(std::tuple<int, int>);
        bool set_implementation(std::tuple<int, int>, wf_workspace_implementation*, bool override = false);

        std::vector<wayfire_view> get_views_on_workspace(std::tuple<int, int>);
        std::vector<wayfire_view>
        get_renderable_views_on_workspace(std::tuple<int, int> ws);

        void set_workspace(std::tuple<int, int>);

        std::tuple<int, int> get_current_workspace();
        std::tuple<int, int> get_workspace_grid_size();

        wayfire_view get_background_view();

        void add_background(wayfire_view background, int x, int y);
        void add_panel(wayfire_view panel);
        void reserve_workarea(wayfire_shell_panel_position position,
             uint32_t width, uint32_t height);
        void configure_panel(wayfire_view view, int x, int y);

        weston_geometry get_workarea();

        void check_lower_panel_layer(int base);
};

/* Start viewport_manager */
void viewport_manager::init(wayfire_output *o)
{
    output = o;
    vx = vy = 0;

    weston_layer_init(&normal_layer,      core->ec);
    weston_layer_init(&panel_layer,       core->ec);
    weston_layer_init(&background_layer,  core->ec);

    weston_layer_set_position(&normal_layer,      WESTON_LAYER_POSITION_NORMAL);
    weston_layer_set_position(&panel_layer,       WESTON_LAYER_POSITION_UI);
    weston_layer_set_position(&background_layer,  WESTON_LAYER_POSITION_BACKGROUND);

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
    o->signal->connect_signal("view-fullscreen-request", &adjust_fullscreen_layer);
    o->signal->connect_signal("attach-view", &view_detached);
    o->signal->connect_signal("detach-view", &view_detached);
}

void viewport_manager::view_bring_to_front(wayfire_view view)
{
    debug << "view bring_to_front" << view->desktop_surface << std::endl;
    if (view->handle->layer_link.layer == NULL)
        weston_layer_entry_insert(&normal_layer.view_list, &view->handle->layer_link);
}

void viewport_manager::view_removed(wayfire_view view)
{
    debug << "view removed" << view->desktop_surface << std::endl;
    if (view->handle->layer_link.layer)
        weston_layer_entry_remove(&view->handle->layer_link);

    if (view == background)
        background = nullptr;
}

void viewport_manager::for_each_view(view_callback_proc_t call)
{
    weston_view *view;
    wayfire_view v;

    wl_list_for_each(view, &normal_layer.view_list.link, layer_link.link) {
        if ((v = core->find_view(view)) && v->is_visible())
            call(v);
    }
}

void viewport_manager::for_each_view_reverse(view_callback_proc_t call)
{
    weston_view *view;
    wayfire_view v;

    wl_list_for_each_reverse(view, &normal_layer.view_list.link, layer_link.link) {
        if ((v = core->find_view(view)) && v->is_visible())
            call(v);
    }
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
        v->move(v->geometry.x + dx, v->geometry.y + dy);
    });


    weston_output_schedule_repaint(output->handle);

    change_viewport_signal data;
    data.old_vx = vx;
    data.old_vy = vy;

    data.new_vx = nx;
    data.new_vy = ny;

    vx = nx;
    vy = ny;
    output->signal->emit_signal("viewport-changed", &data);

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

    weston_geometry g = output->get_full_geometry();
    g.x += (tx - vx) * output->handle->width;
    g.y += (ty - vy) * (output->handle->height);

    std::vector<wayfire_view> ret;
    for_each_view([&ret, g] (wayfire_view view) {
        if (rect_intersect(g, view->geometry)) {
            ret.push_back(view);
        }
    });

    return ret;
}

std::vector<wayfire_view> viewport_manager::get_renderable_views_on_workspace(
        std::tuple<int, int> ws)
{
    std::vector<wayfire_view> ret;
    weston_view *view;
    wayfire_view v;

    GetTuple(tx, ty, ws);

    weston_geometry g = output->get_full_geometry();
    g.x += (tx - vx) * output->handle->width;
    g.y += (ty - vy) * (output->handle->height);

    if (tx == vx && ty == vy)
    {
        wl_list_for_each(view, &panel_layer.view_list.link, layer_link.link)
        {
            if ((v = core->find_view(view)) && rect_intersect(g, v->geometry))
                ret.push_back(v);
        }
    }

    wl_list_for_each(view, &normal_layer.view_list.link, layer_link.link)
    {
        if ((v = core->find_view(view)) && rect_intersect(g, v->geometry))
            ret.push_back(v);
    }

    auto bg = get_background_view();
    if (bg) ret.push_back(bg);


    return ret;
}

wayfire_view viewport_manager::get_background_view()
{
    return background;
}

void bg_idle_cb(void *data)
{
    auto output = (weston_output*) data;
    weston_output_damage(output);
    weston_output_schedule_repaint(output);
}

void viewport_manager::add_background(wayfire_view background, int x, int y)
{
    background->is_special = true;

    auto g = output->get_full_geometry();
    background->move(x + g.x, y + g.y);

    background->output->detach_view(background);
    background->output = output;

    pixman_region32_copy(&background->handle->damage_clip_region, &output->handle->region);

    weston_layer_entry_insert(&background_layer.view_list, &background->handle->layer_link);

    auto loop = wl_display_get_event_loop(core->ec->wl_display);
    wl_event_loop_add_idle(loop, bg_idle_cb, output->handle);

    this->background = background;
}

void viewport_manager::add_panel(wayfire_view panel)
{
    /* views have first been created as desktop views,
     * so they are currently in the normal layer, we must remove them first */
    panel->output->detach_view(panel);
    panel->output = output;

    pixman_region32_copy(&panel->handle->damage_clip_region, &output->handle->region);

    weston_layer_entry_insert(&panel_layer.view_list, &panel->handle->layer_link);
    panel->is_special = true;
}

void viewport_manager::reserve_workarea(wayfire_shell_panel_position position,
        uint32_t width, uint32_t height)
{
    switch(position) {
        case WAYFIRE_SHELL_PANEL_POSITION_LEFT:
            workarea.left_padding = width;
            break;
        case WAYFIRE_SHELL_PANEL_POSITION_RIGHT:
            workarea.right_padding = width;
            break;
        case WAYFIRE_SHELL_PANEL_POSITION_UP:
            workarea.top_padding = height;
            break;
        case WAYFIRE_SHELL_PANEL_POSITION_DOWN:
            workarea.bot_padding = height;
            break;
    }
}

void viewport_manager::configure_panel(wayfire_view view, int x, int y)
{
    auto g = output->get_full_geometry();
    view->move(g.x + x, g.y + y);
}

weston_geometry viewport_manager::get_workarea()
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
        weston_layer_unset_position(&panel_layer);
    } else
    {
        weston_layer_set_position(&panel_layer, WESTON_LAYER_POSITION_UI);
    }
}

class viewport_impl_plugin : public wayfire_plugin_t {

    void init(wayfire_config *config)
    {
        auto vp = new viewport_manager();
        vp->init(output);

        output->workspace = vp;
    }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new viewport_impl_plugin();
    }
}

