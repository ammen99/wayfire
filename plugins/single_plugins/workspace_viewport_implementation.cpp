#include <output.hpp>
#include <core.hpp>
#include <signal_definitions.hpp>
#include <pixman-1/pixman.h>

class viewport_manager : public workspace_manager {
    private:
        int vwidth, vheight, vx, vy;
        wayfire_output *output;

    public:
        void init(wayfire_output *output);

        std::vector<wayfire_view> get_views_on_workspace(std::tuple<int, int>);
        void set_workspace(std::tuple<int, int>);

        std::tuple<int, int> get_current_workspace();
        std::tuple<int, int> get_workspace_grid_size();

        void texture_from_workspace(std::tuple<int, int> vp,
                GLuint &fbuff, GLuint &tex);
};

/* Start viewport_manager */
void viewport_manager::init(wayfire_output *o)
{
    output = o;
    vx = vy = 0;

    vwidth = core->vwidth;
    vheight = core->vheight;
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

    output->for_each_view([=] (wayfire_view v) {
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
    output->for_each_view([&ret, g] (wayfire_view view) {
        if (rect_inside(g, view->geometry)) {
            ret.push_back(view);
        }
    });

    return ret;
}
/* End viewport_manager */


void viewport_manager::texture_from_workspace(std::tuple<int, int> vp,
        GLuint &fbuff, GLuint &tex)
{
    OpenGL::bind_context(output->render->ctx);

    if (fbuff == (uint)-1 || tex == (uint)-1)
        OpenGL::prepare_framebuffer(fbuff, tex);

    pixman_region32_t full_region;
    pixman_region32_init_rect(&full_region, 0, 0, output->handle->width, output->handle->height);
    output->render->blit_background(fbuff, &full_region);

    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbuff));

    GetTuple(x, y, vp);
    GetTuple(cx, cy, get_current_workspace());

    int dx = (cx - x)  * output->handle->width,
        dy = (cy - y)  * output->handle->height;

    wayfire_geometry output_rect = {
        .origin = {-dx, -dy},
        .size = {output->handle->width, output->handle->height}
    };

    output->for_each_view_reverse([=] (wayfire_view v) {
        /* TODO: check if it really is visible */
        if (rect_inside(output_rect, v->geometry)) {
            v->geometry.origin.x += dx;
            v->geometry.origin.y += dy;
            v->render(0);
            v->geometry.origin.x -= dx;
            v->geometry.origin.y -= dy;
        }
    });

    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
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

