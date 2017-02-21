#include "core.hpp"
#include "opengl.hpp"
#include "output.hpp"
#include <glm/glm.hpp>


/* misc definitions */

glm::mat4 wayfire_view_transform::global_rotation;
glm::mat4 wayfire_view_transform::global_scale;
glm::mat4 wayfire_view_transform::global_translate;
glm::mat4 wayfire_view_transform::global_view_projection;

glm::mat4 wayfire_view_transform::calculate_total_transform() {
    return global_view_projection * (global_translate * translation) *
        (global_rotation * rotation) * (global_scale *scale);
}

bool point_inside(wayfire_point point, wayfire_geometry rect) {
    if(point.x < rect.origin.x || point.y < rect.origin.y)
        return false;

    if(point.x > rect.origin.x + (int32_t)rect.size.w)
        return false;

    if(point.y > rect.origin.y + (int32_t)rect.size.h)
        return false;

    return true;
}

bool rect_inside(wayfire_geometry screen, wayfire_geometry win) {
    if (win.origin.x + (int32_t)win.size.w < screen.origin.x ||
        win.origin.y + (int32_t)win.size.h < screen.origin.y)
        return false;

    if (screen.origin.x + (int32_t)screen.size.w < win.origin.x ||
        screen.origin.y + (int32_t)screen.size.h < win.origin.y)
        return false;
    return true;
}

wayfire_view_t::wayfire_view_t(weston_view *_view) {
    //view    = _view;
    output  = core->get_active_output();

    /// FIXME : get geometry
    //geometry.origin.x = view->geometry.xjko;
    //auto geom = wlc_view_get_geometry(view);
    //attrib = *geom;
}

wayfire_view_t::~wayfire_view_t() {
}

#define Mod(x,m) (((x)%(m)+(m))%(m))



// TODO: implement is_visible
bool wayfire_view_t::is_visible() {
    return true;
    //return wlc_output_get_mask(wlc_get_focused_output()) & default_mask;
}

void wayfire_view_t::move(int x, int y) {
    //auto v = core->find_view(view);
    //attrib.origin = {x, y};
    //wlc_view_set_geometry(view, 0, &attrib);
}

void wayfire_view_t::resize(int w, int h) {
    //attrib.size = {(uint32_t)w, uint32_t(h)};
    //wlc_view_set_geometry(view, 0, &attrib);
}

void wayfire_view_t::set_geometry(wayfire_geometry g) {
    //attrib = g;
    //wlc_view_set_geometry(view, 0, &attrib);
}

void wayfire_view_t::set_geometry(int x, int y, int w, int h) {
    geometry = (wayfire_geometry) {
        .origin = {x, y},
        .size = {(int32_t)w, (int32_t)h}
    };

    //wlc_view_set_geometry(view, 0, &attrib);
}

void wayfire_view_t::set_maximized(bool maxim) {
    if (fullscreen)
        return;

    if (maxim && !maximized) {
        saved_geometry = geometry;
        set_geometry(0, 0, output->handle->width, output->handle->height);
    } else if (!maxim && maximized) {
        set_geometry(saved_geometry);
    }
}

void wayfire_view_t::map(int sx, int sy) {

    if (xwayland.is_xorg) {
        /* TODO: position xorg views, see weston shell.c#2432 */
    } else {
        weston_view_set_position(handle, 0, 0);
    }

    weston_view_update_transform(handle);
    handle->is_mapped = true;

    /* TODO: see shell.c#activate() */
}

void wayfire_view_t::set_mask(uint32_t mask) {
    default_mask = mask;
    if (!has_temporary_mask)
        restore_mask();
}

void wayfire_view_t::restore_mask() {
    //wlc_view_set_mask(view, default_mask);
    //has_temporary_mask = false;
}

void wayfire_view_t::set_temporary_mask(uint32_t tmask) {
    //wlc_view_set_mask(view, tmask);
    has_temporary_mask = true;
}

void wayfire_view_t::render(uint32_t bits) {
    /*
    wlc_geometry g;
    wlc_view_get_visible_geometry(get_id(), &g);
    render_surface(surface, g, transform.compose(), bits);

    */
    /*
    std::vector<EffectHook*> hooks_to_run;
    for (auto hook : effects) {
        if (hook.second->getState()) {
            hooks_to_run.push_back(hook.second);
        }
    }

    for (auto hook : hooks_to_run)
        hook->action();
    */
}

/*
void wayfire_view_t::snapshot() {
    v.clear();
    wlc_geometry vis;
    wlc_view_get_visible_geometry(view, &vis);
    collect_subsurfaces(surface, vis, v);
}
*/


/*
void collect_subsurfaces(wlc_resource surface, wlc_geometry g, std::vector<Surface>& v) {
    Surface s;
    wlc_surface_get_textures(surface, s.tex, &s.fmt);
    for (int i = 0; i < 3 && s.tex[i]; i++) {
        s.tex[i] = OpenGL::duplicate_texture(s.tex[i], g.size.w, g.size.h);
    }

    s.g = g;
    v.push_back(s);

    size_t num_subsurfaces;
    auto subsurfaces = wlc_surface_get_subsurfaces(surface, &num_subsurfaces);
    if (!subsurfaces || !num_subsurfaces)
        return;

    for (int i = num_subsurfaces - 1; i >= 0; i--) {
        wlc_geometry sub_g;
        wlc_get_subsurface_geometry(subsurfaces[i], &sub_g);

        sub_g.origin.x += g.origin.x;
        sub_g.origin.y += g.origin.y;

        collect_subsurfaces(subsurfaces[i], sub_g, v);
    }
}

static void render_surface(wlc_resource surface, wlc_geometry g, glm::mat4 transform, uint32_t bits) {
    Surface surf;
    wlc_surface_get_textures(surface, surf.tex, &surf.fmt);
    for(int i = 0; i < 3 && surf.tex[i]; i++)
        OpenGL::renderTransformedTexture(surf.tex[i], g, transform, bits);

    size_t num_subsurfaces;
    auto subsurfaces = wlc_surface_get_subsurfaces(surface, &num_subsurfaces);
    if (!subsurfaces) return;

    for(int i = num_subsurfaces - 1; i >= 0; i--) {
        wlc_geometry sub_g;
        wlc_get_subsurface_geometry(subsurfaces[i], &sub_g);

        sub_g.origin.x += g.origin.x;
        sub_g.origin.y += g.origin.y;

        render_surface(subsurfaces[i], sub_g, transform, bits);
    }
}
*/
