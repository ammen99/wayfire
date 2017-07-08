#include "core.hpp"
#include "opengl.hpp"
#include "output.hpp"
#include <glm/glm.hpp>
#include "signal_definitions.hpp"

#include <xwayland-api.h>
#include <libweston-desktop.h>


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

bool operator == (const wayfire_geometry& a, const wayfire_geometry& b)
{
    return a.origin.x == b.origin.x && a.origin.y == b.origin.y &&
        a.size.w == b.size.w && a.size.h == b.size.h;
}

bool operator != (const wayfire_geometry& a, const wayfire_geometry& b)
{
    return !(a == b);
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
    ds_geometry = {{0, 0}, {0, 0}};
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
    weston_view_set_position(handle, x - ds_geometry.origin.x,
            y - ds_geometry.origin.y);

    /* TODO: we should check if surface is wayland/xwayland in the beginning, since
     * this won't change, it doesn't make sense to check this every time */
    if (xwayland_surface_api && xwayland_surface_api->is_xwayland_surface(surface))
        xwayland_surface_api->send_position(surface, x - ds_geometry.origin.x,
                y - ds_geometry.origin.y);
}

void wayfire_view_t::resize(int w, int h)
{
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
}

void wayfire_view_t::set_maximized(bool maxim)
{
    maximized = maxim;
    weston_desktop_surface_set_maximized(desktop_surface, maximized);
}

void wayfire_view_t::set_fullscreen(bool full)
{
    fullscreen = full;
    weston_desktop_surface_set_fullscreen(desktop_surface, fullscreen);
}

void wayfire_view_t::map(int sx, int sy)
{
    if (!weston_surface_is_mapped(surface)) {
        /* special views are panels/backgrounds, workspace_manager handles their position */
        if (!is_special) {
            sx += output->handle->x;
            sy += output->handle->y;

            auto g = weston_desktop_surface_get_geometry(desktop_surface);

            if (xwayland_surface_api && xwayland_surface_api->is_xwayland_surface(surface)) {
                ds_geometry.origin = {0, 0};
            } else {
                ds_geometry.origin = {g.x, g.y};
            }

            ds_geometry.size = {g.width, g.height};

            if (xwayland.is_xorg) {
                sx = xwayland.x;
                sy = xwayland.y;
            }

            move(sx, sy);
            geometry.origin = {sx, sy};
        }

        weston_view_update_transform(handle);
        handle->is_mapped  = true;
        surface->is_mapped = true;
        is_mapped = true;

        auto sig_data = create_view_signal{core->find_view(handle)};
        output->signal->emit_signal("create-view", &sig_data);

        return;
    }

    auto new_ds_g = weston_desktop_surface_get_geometry(desktop_surface);
    if (new_ds_g.x != ds_geometry.origin.x || new_ds_g.y != ds_geometry.origin.y) {
        ds_geometry.origin = {new_ds_g.x, new_ds_g.y};
        move(geometry.origin.x, geometry.origin.y);
    }

    geometry.size = {new_ds_g.width, new_ds_g.height};
}

static void render_surface(weston_surface *surface, pixman_region32_t *damage,
        int x, int y, glm::mat4, glm::vec4, uint32_t bits);

/* TODO: use bits */
void wayfire_view_t::render(uint32_t bits, pixman_region32_t *damage)
{
    pixman_region32_t our_damage;
    bool free_damage = false;

    if (damage == nullptr) {
        pixman_region32_init(&our_damage);
        pixman_region32_copy(&our_damage, &output->handle->region);
        damage = &our_damage;
        free_damage = true;
    }

    render_surface(surface, damage,
            geometry.origin.x - ds_geometry.origin.x, geometry.origin.y - ds_geometry.origin.y,
            transform.calculate_total_transform(), transform.color, bits);

    if (free_damage)
        pixman_region32_fini(&our_damage);

    std::vector<effect_hook_t*> hooks_to_run;
    for (auto hook : effects)
        hooks_to_run.push_back(hook);

    for (auto hook : hooks_to_run)
        (*hook)();
}

static inline void render_surface_box(GLuint tex[3], const pixman_box32_t& surface_box,
        const pixman_box32_t& subbox, glm::mat4 transform,
        glm::vec4 color, uint32_t bits)
{
    OpenGL::texture_geometry texg = {
        1.0f * (subbox.x1 - surface_box.x1) / (surface_box.x2 - surface_box.x1),
        1.0f * (subbox.y1 - surface_box.y1) / (surface_box.y2 - surface_box.y1),
        1.0f * (subbox.x2 - surface_box.x1) / (surface_box.x2 - surface_box.x1),
        1.0f * (subbox.y2 - surface_box.y1) / (surface_box.y2 - surface_box.y1),
    };

    wayfire_geometry geometry =
    {.origin = {subbox.x1, subbox.y1},
     .size = {subbox.x2 - subbox.x1, subbox.y2 - subbox.y1}
    };

    for (int i = 0; i < 3 && tex[i]; i++) {
        OpenGL::render_transformed_texture(tex[i], geometry, texg, transform,
                                           color, bits);
    }
}

static void render_surface(weston_surface *surface, pixman_region32_t *damage,
        int x, int y, glm::mat4 transform, glm::vec4 color, uint32_t bits)
{
    if (!surface->is_mapped || !surface->renderer_state ||
            surface->width * surface->height == 0)
        return;

    pixman_region32_t damaged_region;
    pixman_region32_init_rect(&damaged_region, x, y,
            surface->width, surface->height);
    pixman_region32_intersect(&damaged_region, &damaged_region, damage);

    pixman_box32_t surface_box;
    surface_box.x1 = x; surface_box.y1 = y;
    surface_box.x2 = x + surface->width; surface_box.y2 = y + surface->height;

    int n = 0;
    pixman_box32_t *boxes = pixman_region32_rectangles(&damaged_region, &n);

    GLuint *tex = (GLuint*)core->ec->renderer->get_gl_surface_contents(surface);

    for (int i = 0; i < n; i++) {
        render_surface_box(tex, surface_box, boxes[i],
                transform, color, bits | TEXTURE_USE_TEX_GEOMETRY);
    }
    pixman_region32_fini(&damaged_region);

    weston_subsurface *sub;
    if (!wl_list_empty(&surface->subsurface_list)) {
        wl_list_for_each(sub, &surface->subsurface_list, parent_link) {
            if (sub && sub->surface != surface) {
                render_surface(sub->surface, damage,
                        sub->position.x + x, sub->position.y + y,
                        transform, color, bits);
            }
        }
    }
}
