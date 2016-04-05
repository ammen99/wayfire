#include "core.hpp"
#include "opengl.hpp"
#include "output.hpp"
#include <wlc/wlc-wayland.h>


/* misc definitions */

glm::mat4 Transform::grot;
glm::mat4 Transform::gscl;
glm::mat4 Transform::gtrs;
glm::mat4 Transform::ViewProj;

bool Transform::has_rotation = false;

Transform::Transform() {
    this->translation = glm::mat4();
    this->rotation = glm::mat4();
    this->scalation = glm::mat4();
}

glm::mat4 Transform::compose() {
    return ViewProj*(gtrs*translation)*(grot*rotation)*(gscl*scalation);
}

bool point_inside(wlc_point point, wlc_geometry rect) {
    if(point.x < rect.origin.x || point.y < rect.origin.y)
        return false;

    if(point.x > rect.origin.x + (int32_t)rect.size.w)
        return false;

    if(point.y > rect.origin.y + (int32_t)rect.size.h)
        return false;

    return true;
}

bool rect_inside(wlc_geometry screen, wlc_geometry win) {
    if (win.origin.x + (int32_t)win.size.w < screen.origin.x ||
        win.origin.y + (int32_t)win.size.h < screen.origin.y)
        return false;

    if (screen.origin.x + (int32_t)screen.size.w < win.origin.x ||
        screen.origin.y + (int32_t)screen.size.h < win.origin.y)
        return false;
    return true;
}

FireView::FireView(wlc_handle _view) {
    view    = _view;
    surface = wlc_view_get_surface(view);
    output  = core->get_active_output();

    auto geom = wlc_view_get_geometry(view);
    attrib = *geom;
}

FireView::~FireView() {
}

#define Mod(x,m) (((x)%(m)+(m))%(m))



bool FireView::is_visible() {
    return wlc_output_get_mask(wlc_get_focused_output()) & default_mask;
}

void FireView::move(int x, int y) {
    auto v = core->find_view(view);
    attrib.origin = {x, y};
    wlc_view_set_geometry(view, 0, &attrib);
}

void FireView::resize(int w, int h) {
    attrib.size = {(uint32_t)w, uint32_t(h)};

    wlc_view_set_geometry(view, 0, &attrib);
}

void FireView::set_geometry(wlc_geometry g) {
    std::cout << g.origin.x << " " << g.origin.y << " " << g.size.w << " " << g.size.h << std::endl;
    attrib = g;
    wlc_view_set_geometry(view, 0, &attrib);
}

void FireView::set_geometry(int x, int y, int w, int h) {
    attrib = (wlc_geometry) {
        .origin = {x, y},
        .size = {(uint32_t)w, (uint32_t)h}
    };

    wlc_view_set_geometry(view, 0, &attrib);
}

void FireView::set_mask(uint32_t mask) {
    std::cout << "set mask " << view << " " << mask << std::endl;
    default_mask = mask;
    if (!has_temporary_mask)
        restore_mask();
}

void FireView::restore_mask() {
    wlc_view_set_mask(view, default_mask);
    has_temporary_mask = false;
}

void FireView::set_temporary_mask(uint32_t tmask) {
    wlc_view_set_mask(view, tmask);
    has_temporary_mask = true;
}

void FireView::render(uint32_t bits) {
    wlc_geometry g;
    wlc_view_get_visible_geometry(get_id(), &g);
    render_surface(surface, g, transform.compose(), bits);

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

void FireView::snapshot(std::vector<Surface> &v) {
    v.clear();
    wlc_geometry vis;
    wlc_view_get_visible_geometry(view, &vis);
    collect_subsurfaces(surface, vis, v);
}

#include <wlc/wlc-wayland.h>
#include <wlc/wlc-render.h>

void collect_subsurfaces(wlc_resource surface, wlc_geometry g, std::vector<Surface>& v) {
    Surface s;
    wlc_surface_get_textures(surface, s.tex, &s.fmt);
    for (int i = 0; i < 3 && s.tex[i]; i++) {
        s.tex[i] = OpenGL::duplicate_texture(s.tex[i], g.size.w, g.size.h);
        std::cout << s.tex[i] << std::endl;
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

void render_surface(wlc_resource surface, wlc_geometry g, glm::mat4 transform, uint32_t bits) {
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
