#include <output.hpp>
#include <core.hpp>
#include <signal_definitions.hpp>
#include <pixman-1/pixman.h>

class viewport_manager : public workspace_manager {
    private:
        int vwidth, vheight, vx, vy;
        wayfire_output *output;
        wayfire_view background;

        weston_layer panel_layer, normal_layer, background_layer;

        struct {
            int top_padding;
            int bot_padding;
            int left_padding;
            int right_padding;
        } workarea;

    public:
        void init(wayfire_output *output);

        void view_bring_to_front(wayfire_view view);
        void view_removed(wayfire_view view);

        void for_each_view(view_callback_proc_t call);
        void for_each_view_reverse(view_callback_proc_t call);

        std::vector<wayfire_view> get_views_on_workspace(std::tuple<int, int>);
        void set_workspace(std::tuple<int, int>);

        std::tuple<int, int> get_current_workspace();
        std::tuple<int, int> get_workspace_grid_size();

        void texture_from_workspace(std::tuple<int, int> vp,
                GLuint &fbuff, GLuint &tex);

        wayfire_view get_background_view();

        void add_background(wayfire_view background, int x, int y);
        void add_panel(wayfire_view panel);
        void reserve_workarea(wayfire_shell_panel_position position,
                uint32_t width, uint32_t height);
        void configure_panel(wayfire_view view, int x, int y);

        wayfire_geometry get_workarea();
};

/* Start viewport_manager */
void viewport_manager::init(wayfire_output *o)
{
    output = o;
    vx = vy = 0;

    weston_layer_init(&normal_layer, core->ec);
    weston_layer_init(&panel_layer, core->ec);
    weston_layer_init(&background_layer, core->ec);

    weston_layer_set_position(&normal_layer, WESTON_LAYER_POSITION_NORMAL);
    weston_layer_set_position(&panel_layer, WESTON_LAYER_POSITION_TOP_UI);
    weston_layer_set_position(&background_layer, WESTON_LAYER_POSITION_BACKGROUND);

    vwidth = core->vwidth;
    vheight = core->vheight;
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
}

void viewport_manager::for_each_view(view_callback_proc_t call)
{
    weston_view *view;
    wayfire_view v;

    wl_list_for_each(view, &normal_layer.view_list.link, layer_link.link) {
        if ((v = core->find_view(view)))
            call(v);
    }
}

void viewport_manager::for_each_view_reverse(view_callback_proc_t call)
{
    weston_view *view;
    wayfire_view v;

    wl_list_for_each_reverse(view, &normal_layer.view_list.link, layer_link.link) {
        if ((v = core->find_view(view)))
            call(v);
    }
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
        auto views = get_views_on_workspace({vx, vy});
        if (views.size() >= 1)
            output->focus_view(views[0], core->get_current_seat());
        return;
    }

    auto dx = (vx - nx) * output->handle->width;
    auto dy = (vy - ny) * output->handle->height;

    for_each_view([=] (wayfire_view v) {
        v->move(v->geometry.origin.x + dx, v->geometry.origin.y + dy);
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

    output->focus_view(nullptr, core->get_current_seat());
    /* we iterate through views on current viewport from bottom to top
     * that way we ensure that they will be focused befor all others */
    auto views = get_views_on_workspace({vx, vy});
    auto it = views.rbegin();
    while(it != views.rend()) {
        if ((*it)->is_mapped && !(*it)->destroyed)
            output->focus_view(*it, core->get_current_seat());
        ++it;
    }
}

std::vector<wayfire_view> viewport_manager::get_views_on_workspace(std::tuple<int, int> vp)
{
    GetTuple(tx, ty, vp);

    wayfire_geometry g;
    g.origin = {(tx - vx) * output->handle->width, (ty - vy) * (output->handle->height)};
    g.size = {output->handle->width, output->handle->height};

    std::vector<wayfire_view> ret;
    for_each_view([&ret, g] (wayfire_view view) {
        if (rect_inside(g, view->geometry)) {
            ret.push_back(view);
        }
    });

    return ret;
}

void viewport_manager::texture_from_workspace(std::tuple<int, int> vp,
        GLuint &fbuff, GLuint &tex)
{
    OpenGL::bind_context(output->render->ctx);

    if (fbuff == (uint)-1 || tex == (uint)-1)
        OpenGL::prepare_framebuffer(fbuff, tex);

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbuff));

    auto g = output->get_full_geometry();
    if (background)
        background->render(0);

    GetTuple(x, y, vp);
    GetTuple(cx, cy, get_current_workspace());

    int dx = -g.origin.x + (cx - x)  * output->handle->width,
        dy = -g.origin.y + (cy - y)  * output->handle->height;

    wayfire_geometry output_rect = {
        .origin = {-dx, -dy},
        .size = {output->handle->width, output->handle->height}
    };

    for_each_view_reverse([=] (wayfire_view v) {
        if (v->is_visible() && rect_inside(output_rect, v->geometry)) {
            v->geometry.origin.x += dx;
            v->geometry.origin.y += dy;
            v->render(0);
            v->geometry.origin.x -= dx;
            v->geometry.origin.y -= dy;
        }
    });

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
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
    background->move(x + g.origin.x, y + g.origin.y);

    output->detach_view(background);

    weston_layer_entry_insert(&background_layer.view_list, &background->handle->layer_link);

    auto loop = wl_display_get_event_loop(core->ec->wl_display);
    wl_event_loop_add_idle(loop, bg_idle_cb, output->handle);

    this->background = background;

    background->ds_geometry.x += g.origin.x;
    background->ds_geometry.y += g.origin.y;
}

void viewport_manager::add_panel(wayfire_view panel)
{
    /* views have first been created as desktop views,
     * so they are currently in the normal layer, we must remove them first */
    output->detach_view(panel);

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
    view->move(g.origin.x + x, g.origin.y + y);
}

wayfire_geometry viewport_manager::get_workarea()
{
    auto g = output->get_full_geometry();
    return
    {
        .origin = {g.origin.x + workarea.left_padding, g.origin.y + workarea.top_padding},
        .size = {g.size.w - workarea.left_padding - workarea.right_padding,
                 g.size.h - workarea.top_padding - workarea.bot_padding}
    };
}

class viewport_impl_plugin : public wayfire_plugin_t {

    void init(wayfire_config *config)
    {
        output->workspace = new viewport_manager();
        output->workspace->init(output);
    }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new viewport_impl_plugin();
    }
}

