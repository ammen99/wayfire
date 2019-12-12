#include "core.hpp"
#include "output.hpp"
#include "opengl.hpp"
#include "compositor-view.hpp"
#include "signal-definitions.hpp"
#include "debug.hpp"

#include <glm/gtc/matrix_transform.hpp>

extern "C"
{
#define static
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#undef static
}

/* Implementation of mirror_view_t */
wf::mirror_view_t::mirror_view_t(wayfire_view base_view)
    : wf::view_interface_t()
{
    this->base_view = base_view;

    base_view_unmapped = [=] (wf::signal_data_t*) {
        close();
    };

    base_view->connect_signal("unmap", &base_view_unmapped);

    base_view_damaged = [=] (wf::signal_data_t* ) {
        damage();
    };

    base_view->connect_signal("damaged-region", &base_view_damaged);
}

wf::mirror_view_t::~mirror_view_t() { }

void wf::mirror_view_t::close()
{
    if (!base_view)
        return;

    emit_view_pre_unmap();

    base_view->disconnect_signal("unmap", &base_view_unmapped);
    base_view->disconnect_signal("damaged-region", &base_view_damaged);
    base_view = nullptr;

    emit_map_state_change(this);
    emit_view_unmap();

    unref();
}

bool wf::mirror_view_t::is_mapped() const
{
    return base_view && base_view->is_mapped();
}

wf_size_t wf::mirror_view_t::get_size() const
{
    if (!is_mapped())
        return {0, 0};

    auto box = base_view->get_bounding_box();
    return {box.width, box.height};
}

void wf::mirror_view_t::simple_render(const wf_framebuffer& fb, int x, int y,
    const wf_region& damage)
{
    if (!is_mapped())
        return;

    /* Normally we shouldn't copy framebuffers. But in this case we can assume
     * nothing will break, because the copy will be destroyed immediately */
    wf_framebuffer copy;
    memcpy((void*)&copy, (void*)&fb, sizeof(wf_framebuffer));

    /* The base view is in another coordinate system, we need to calculate the
     * difference between the two, so that it appears at the correct place.
     *
     * Note that this simply means we need to change fb's geometry. Damage is
     * calculated for this mirror view, and needs to stay as it is */
    auto base_bbox = base_view->get_bounding_box();

    copy.geometry.x += base_bbox.x - (x + fb.geometry.x);
    copy.geometry.y += base_bbox.y - (y + fb.geometry.y);

    base_view->render_transformed(copy, damage);
    copy.reset();
}

void wf::mirror_view_t::move(int x, int y)
{
    damage();
    view_geometry_changed_signal data;
    data.old_geometry = get_wm_geometry();

    this->x = x;
    this->y = y;

    damage();
    emit_signal("geometry-changed", &data);
}

wf_geometry wf::mirror_view_t::get_output_geometry()
{
    if (!is_mapped())
        return get_bounding_box();

    wf_geometry geometry;
    geometry.x = this->x;
    geometry.y = this->y;

    auto dims = get_size();
    geometry.width = dims.width;
    geometry.height = dims.height;

    return geometry;
}

wlr_surface *wf::mirror_view_t::get_keyboard_focus_surface() { return nullptr; }
bool wf::mirror_view_t::is_focuseable() const { return false; }
bool wf::mirror_view_t::should_be_decorated() { return false; }

/* Implementation of color_rect_view_t */

wf::color_rect_view_t::color_rect_view_t() : wf::view_interface_t()
{
    this->_color = {0, 0, 0, 1};
    this->border = 0;
    this->_is_mapped = true;
}

void wf::color_rect_view_t::close()
{
    this->_is_mapped = false;

    emit_view_unmap();
    emit_map_state_change(this);

    unref();
}

void wf::color_rect_view_t::set_color(wf::color_t color)
{
    this->_color = color;
    damage();
}

void wf::color_rect_view_t::set_border_color(wf::color_t border)
{
    this->_border_color = border;
    damage();
}

void wf::color_rect_view_t::set_border(int width)
{
    this->border = width;
    damage();
}

bool wf::color_rect_view_t::is_mapped() const
{
    return _is_mapped;
}

wf_size_t wf::color_rect_view_t::get_size() const
{
    return {
        geometry.width,
        geometry.height,
    };
}

static void render_colored_rect(const wf_framebuffer& fb, float projection[9],
    int x, int y, int w, int h, const wf::color_t& color)
{
    wlr_box render_geometry_unscaled {x, y, w, h};
    wlr_box render_geometry = fb.damage_box_from_geometry_box(
        render_geometry_unscaled);

    float matrix[9];
    wlr_matrix_project_box(matrix, &render_geometry,
        WL_OUTPUT_TRANSFORM_NORMAL, 0, projection);

    float a = color.a;
    float col[4] = {(float)color.r * a, (float)color.g * a, (float)color.b * a, a};
    wlr_render_quad_with_matrix(wf::get_core().renderer, col, matrix);
}

void wf::color_rect_view_t::simple_render(const wf_framebuffer& fb, int x, int y,
    const wf_region& damage)
{
    float projection[9];
    wlr_matrix_projection(projection, fb.viewport_width, fb.viewport_height,
        (wl_output_transform)fb.wl_transform);

    OpenGL::render_begin(fb);
    for (const auto& box : damage)
    {
        auto sbox =
            fb.framebuffer_box_from_damage_box(wlr_box_from_pixman_box(box));
        wlr_renderer_scissor(wf::get_core().renderer, &sbox);

        /* Draw the border, making sure border parts don't overlap, otherwise
         * we will get wrong corners if border has alpha != 1.0 */
        // top
        render_colored_rect(fb, projection, x, y, geometry.width, border,
            _border_color);
        // bottom
        render_colored_rect(fb, projection, x, y + geometry.height - border,
            geometry.width, border, _border_color);
        // left
        render_colored_rect(fb, projection, x, y + border, border,
            geometry.height - 2 * border, _border_color);
        // right
        render_colored_rect(fb, projection, x + geometry.width - border,
            y + border, border, geometry.height - 2 * border, _border_color);

        /* Draw the inside of the rect */
        render_colored_rect(fb, projection, x + border, y + border,
            geometry.width - 2 * border, geometry.height - 2 * border,
            _color);
    }
    OpenGL::render_end();
}

void wf::color_rect_view_t::move(int x, int y)
{
    damage();
    view_geometry_changed_signal data;
    data.old_geometry = get_wm_geometry();

    this->geometry.x = x;
    this->geometry.y = y;

    damage();
    emit_signal("geometry-changed", &data);
}

void wf::color_rect_view_t::resize(int w, int h)
{
    damage();
    view_geometry_changed_signal data;
    data.old_geometry = get_wm_geometry();

    this->geometry.width = w;
    this->geometry.height = h;

    damage();
    emit_signal("geometry-changed", &data);
}

wf_geometry wf::color_rect_view_t::get_output_geometry()
{
    return geometry;
}

wlr_surface *wf::color_rect_view_t::get_keyboard_focus_surface() { return nullptr; }
bool wf::color_rect_view_t::is_focuseable() const { return false; }
bool wf::color_rect_view_t::should_be_decorated() { return false; }
