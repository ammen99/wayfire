#include "core.hpp"
#include "opengl.hpp"
#include "output.hpp"
#include <glm/glm.hpp>
#include "img.hpp"
#include "signal_definitions.hpp"

#include <libweston-3/xwayland-api.h>

/* misc definitions */

glm::mat4 wayfire_view_transform::global_rotation;
glm::mat4 wayfire_view_transform::global_scale;
glm::mat4 wayfire_view_transform::global_translate;
glm::mat4 wayfire_view_transform::global_view_projection;

glm::mat4 wayfire_view_transform::calculate_total_transform()
{
    return global_view_projection * (global_translate * translation) *
           (global_rotation * rotation) * (global_scale * scale);
}

bool point_inside(wayfire_point point, wayfire_geometry rect)
{
    if(point.x < rect.origin.x || point.y < rect.origin.y)
        return false;

    if(point.x > rect.origin.x + (int32_t)rect.size.w)
        return false;

    if(point.y > rect.origin.y + (int32_t)rect.size.h)
        return false;

    return true;
}

bool rect_inside(wayfire_geometry screen, wayfire_geometry win)
{
    if (win.origin.x + (int32_t)win.size.w <= screen.origin.x ||
            win.origin.y + (int32_t)win.size.h <= screen.origin.y)
        return false;

    if (screen.origin.x + (int32_t)screen.size.w <= win.origin.x ||
            screen.origin.y + (int32_t)screen.size.h <= win.origin.y)
        return false;
    return true;
}


const weston_xwayland_surface_api *xwayland_surface_api = nullptr;

wayfire_view_t::wayfire_view_t(weston_desktop_surface *ds)
{
    output  = core->get_active_output();
    handle = weston_desktop_surface_create_view(ds);

    weston_desktop_surface_set_user_data(ds, NULL);
    weston_desktop_surface_set_activated(ds, true);

    desktop_surface = ds;
    surface = weston_desktop_surface_get_surface(ds);

    geometry.size.w = surface->width;
    geometry.size.h = surface->height;

    transform.color = glm::vec4(1, 1, 1, 1);

    if (!xwayland_surface_api) {
        xwayland_surface_api = weston_xwayland_surface_get_api(core->ec);
    }
}

wayfire_view_t::~wayfire_view_t()
{
}

#define Mod(x,m) (((x)%(m)+(m))%(m))



// TODO: implement is_visible
bool wayfire_view_t::is_visible()
{
    return true;
}

void wayfire_view_t::move(int x, int y)
{
    geometry.origin = {x, y};
    weston_view_set_position(handle, x - ds_geometry.x, y - ds_geometry.y);

    /* TODO: we should check if surface is wayland/xwayland in the beginning, since
     * this won't change, it doesn't make sense to check this every time */
    if (xwayland_surface_api && xwayland_surface_api->is_xwayland_surface(surface))
        xwayland_surface_api->send_position(surface, x - ds_geometry.x, y - ds_geometry.y);
}

void wayfire_view_t::resize(int w, int h)
{
    geometry.size = {w, h};
    weston_desktop_surface_set_size(desktop_surface, w, h);
}

void wayfire_view_t::set_geometry(wayfire_geometry g)
{
    move(g.origin.x, g.origin.y);
    resize(g.size.w, g.size.h);
}

void wayfire_view_t::set_geometry(int x, int y, int w, int h)
{
    geometry = (wayfire_geometry) {
        .origin = {x, y},
         .size = {(int32_t)w, (int32_t)h}
    };

    set_geometry(geometry);

    //wlc_view_set_geometry(view, 0, &attrib);
}

void wayfire_view_t::set_maximized(bool maxim)
{
    if (fullscreen)
        return;

    /* TODO: use the future grid's snap functionality */
    if (maxim && !maximized) {
        saved_geometry = geometry;
        set_geometry(0, 0, output->handle->width, output->handle->height);
    } else if (!maxim && maximized) {
        set_geometry(saved_geometry);
    }
}

void wayfire_view_t::map(int sx, int sy)
{
    if (!weston_surface_is_mapped(surface)) {
        sx += output->handle->x;
        sy += output->handle->y;

        if (xwayland.is_xorg) {
            auto g = weston_desktop_surface_get_geometry(desktop_surface);
            weston_view_set_position(handle, xwayland.x - g.x, xwayland.y - g.y);
        } else {
            weston_view_set_position(handle, sx, sy);
        }

        geometry.size = {surface->width, surface->height};
        geometry.origin = {sx, sy};

        weston_view_update_transform(handle);
        handle->is_mapped  = true;
        surface->is_mapped = true;

        auto sig_data = create_view_signal{core->find_view(handle)};
        output->signal->emit_signal("create-view", &sig_data);

        return;
    }

    int oldx, oldy;
    int newx, newy;

    weston_view_to_global_fixed(handle, 0, 0, &oldx, &oldy);
    weston_view_to_global_fixed(handle, sx, sy, &newx, &newy);

    ds_geometry = weston_desktop_surface_get_geometry(desktop_surface);

    sx = handle->geometry.x + (newx - oldx) + ds_geometry.x;
    sy = handle->geometry.y + (newy - oldy) + ds_geometry.y;

    if (sx != geometry.origin.x || sy != geometry.origin.y)
        move(sx, sy);

    /* TODO: see shell.c#activate() */
}

void render_surface(weston_surface *surface, int x, int y, glm::mat4, glm::vec4);
void wayfire_view_t::render(uint32_t bits)
{
    render_surface(surface, geometry.origin.x - ds_geometry.x, geometry.origin.y - ds_geometry.y,
                   transform.calculate_total_transform(), transform.color);

    std::vector<effect_hook_t*> hooks_to_run;
    for (auto hook : effects) {
        hooks_to_run.push_back(hook);
    }

    for (auto hook : hooks_to_run)
        (*hook)();
}

/* This is a hack, we use it so that we can reach the memory used by
 * the texture pointer in the surface
 * weston has the function surface_copy_content,
 * but that approach would require glReadPixels to copy data from GPU->CPU
 * and then again we upload it CPU->GPU, which is slow */
struct weston_gl_surface_state {
    GLfloat color[4];
    void *shader;
    GLuint textures[3];
};


void render_surface(weston_surface *surface, int x, int y, glm::mat4 transform, glm::vec4 color)
{
    if (!surface->is_mapped || !surface->renderer_state)
        return;

    auto gs = (weston_gl_surface_state *) surface->renderer_state;

    wayfire_geometry geometry;
    geometry.origin = {x, y};
    geometry.size = {surface->width, surface->height};

    for (int i = 0; i < 3 && gs->textures[i]; i++) {
        OpenGL::render_transformed_texture(gs->textures[i], geometry, transform,
                                           color, TEXTURE_TRANSFORM_USE_COLOR);
    }

    weston_subsurface *sub;
    if (!wl_list_empty(&surface->subsurface_list)) {
        wl_list_for_each(sub, &surface->subsurface_list, parent_link) {
            if (sub && sub->surface != surface)
                render_surface(sub->surface, sub->position.x + x, sub->position.y + y, transform, color);
        }
    }
}
