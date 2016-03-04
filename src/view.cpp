#include "core.hpp"
#include "opengl.hpp"


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

    if(point.x > rect.origin.x + rect.size.w)
        return false;

    if(point.y > rect.origin.y + rect.size.h)
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
    view = _view;
    auto geom = wlc_view_get_geometry(view);
    std::memcpy(&attrib, geom, sizeof(attrib));
}

FireView::~FireView() {
}

#define Mod(x,m) (((x)%(m)+(m))%(m))



bool FireView::is_visible() {
    return wlc_output_get_mask(wlc_get_focused_output()) & default_mask;
}

void FireView::move(int x, int y) {
    attrib.origin = {x, y};
    wlc_view_set_geometry(view, 0, &attrib);
}

void FireView::resize(int w, int h) {
    attrib.size = {(uint32_t)w, uint32_t(h)};

    wlc_view_set_geometry(view, 0, &attrib);
}

void FireView::set_geometry(wlc_geometry g) {
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

#include <wlc/wlc-wayland.h>
#include <wlc/wlc-render.h>

void render_surface(wlc_resource surface, wlc_geometry g, glm::mat4 transform) {
    uint32_t tex[3];
    wlc_surface_get_textures(surface, tex);
    for(int i = 0; i < 3 && tex[i]; i++)
        OpenGL::renderTransformedTexture(tex[i], g, transform);

    size_t num_subsurfaces;

    auto subsurfaces = wlc_surface_get_subsurfaces(surface, &num_subsurfaces);
    if (!subsurfaces) return;

    for(int i = num_subsurfaces - 1; i >= 0; i--) {
        wlc_geometry sub_g;
        wlc_get_subsurface_geometry(subsurfaces[i], &sub_g);

        sub_g.origin.x += g.origin.x;
        sub_g.origin.y += g.origin.y;

        render_surface(subsurfaces[i], sub_g, transform);
    }
}

namespace WinUtil {

    /* ensure that new window is on current viewport */
    bool constrainNewWindowPosition(int &x, int &y) {
        GetTuple(sw, sh, core->getScreenSize());
        auto nx = (x % sw + sw) % sw;
        auto ny = (y % sh + sh) % sh;

        if(nx != x || ny != y){
            x = nx, y = ny;
            return true;
        }
        return false;
    }

}
