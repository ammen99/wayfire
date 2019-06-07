#include "view-transform.hpp"
#include "opengl.hpp"
#include "debug.hpp"
#include "core.hpp"
#include "output.hpp"
#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#define PI 3.14159265359

wlr_box wf_view_transformer_t::get_bounding_box(wf_geometry view, wlr_box region)
{
    const auto p1 = local_to_transformed_point(view, {region.x,                region.y});
    const auto p2 = local_to_transformed_point(view, {region.x + region.width, region.y});
    const auto p3 = local_to_transformed_point(view, {region.x,                region.y + region.height});
    const auto p4 = local_to_transformed_point(view, {region.x + region.width, region.y + region.height});

    const int x1 = std::min({p1.x, p2.x, p3.x, p4.x});
    const int x2 = std::max({p1.x, p2.x, p3.x, p4.x});
    const int y1 = std::min({p1.y, p2.y, p3.y, p4.y});
    const int y2 = std::max({p1.y, p2.y, p3.y, p4.y});

    return wlr_box{x1, y1, x2 - x1, y2 - y1};
}

void wf_view_transformer_t::render_with_damage(uint32_t src_tex, wlr_box src_box,
            const wf_region& damage, const wf_framebuffer& target_fb)
{
    for (const auto& rect : damage)
    {
        auto box = target_fb.framebuffer_box_from_damage_box(
            wlr_box_from_pixman_box(rect));
        render_box(src_tex, src_box, box, target_fb);
    }
}

struct transformable_quad
{
    gl_geometry geometry;
    float off_x, off_y;
};

static wf_point get_center(wf_geometry view)
{
    return {
        view.x + view.width / 2,
        view.y + view.height/ 2
    };
}

static wf_point get_center_relative_coords(wf_geometry view, wf_point point)
{
    return {
        (point.x - view.x) - view.width / 2,
        view.height / 2 - (point.y - view.y)
    };
}

static wf_point get_absolute_coords_from_relative(wf_geometry view, wf_point point)
{
    return {
        point.x + view.x + view.width / 2,
        (view.height / 2 - point.y) + view.y
    };
}

static transformable_quad center_geometry(wf_geometry output_geometry,
                                          wf_geometry geometry,
                                          wf_point target_center)
{
    transformable_quad quad;

    geometry.x -= output_geometry.x;
    geometry.y -= output_geometry.y;

    target_center.x -= output_geometry.x;
    target_center.y -= output_geometry.y;

    quad.geometry.x1 = -(target_center.x - geometry.x);
    quad.geometry.y1 =  (target_center.y - geometry.y);

    quad.geometry.x2 = quad.geometry.x1 + geometry.width;
    quad.geometry.y2 = quad.geometry.y1 - geometry.height;

    quad.off_x = (geometry.x - output_geometry.width / 2.0)  - quad.geometry.x1;
    quad.off_y = (output_geometry.height / 2.0 - geometry.y) - quad.geometry.y1;

    return quad;
}

wf_2D_view::wf_2D_view(wayfire_view view)
{
    this->view = view;
}

static void rotate_xy(float& x, float& y, float angle)
{
    auto v = glm::vec4{x, y, 0, 1};
    auto rot = glm::rotate(glm::mat4(1.0), angle, {0, 0, 1});
    v = rot * v;
    x = v.x;
    y = v.y;
}

wf_point wf_2D_view::local_to_transformed_point(wf_geometry geometry, wf_point point)
{

    auto p2 = get_center_relative_coords(view->get_wm_geometry(), point);
    float x = p2.x, y = p2.y;

    x *= scale_x; y *= scale_y;
    rotate_xy(x, y, angle);
    x += translation_x;
    y -= translation_y;

    auto r = get_absolute_coords_from_relative(view->get_wm_geometry(),
                                             {(int32_t) x, (int32_t) y});
    return r;
}

wf_point wf_2D_view::transformed_to_local_point(wf_geometry geometry, wf_point point)
{
    point = get_center_relative_coords(view->get_wm_geometry(), point);
    float x = point.x, y = point.y;

    x /= scale_x; y /= scale_y;
    rotate_xy(x, y, -angle);
    x -= translation_x;
    y += translation_y;

    return get_absolute_coords_from_relative(view->get_wm_geometry(),
                                             {(int32_t) x, (int32_t) y});
}

void wf_2D_view::render_box(uint32_t src_tex, wlr_box src_box,
    wlr_box scissor_box, const wf_framebuffer& fb)
{
    auto quad = center_geometry(fb.geometry, src_box, get_center(view->get_wm_geometry()));

    quad.geometry.x1 *= scale_x;
    quad.geometry.x2 *= scale_x;
    quad.geometry.y1 *= scale_y;
    quad.geometry.y2 *= scale_y;

    auto rotate = glm::rotate(glm::mat4(1.0), angle, {0, 0, 1});
    auto translate = glm::translate(glm::mat4(1.0),
                                    {quad.off_x + translation_x,
                                     quad.off_y - translation_y, 0});

    auto ortho = glm::ortho(-fb.geometry.width  / 2.0f, fb.geometry.width  / 2.0f,
                            -fb.geometry.height / 2.0f, fb.geometry.height / 2.0f);

    auto transform = fb.transform * ortho * translate * rotate;

    OpenGL::render_begin(fb);
    fb.scissor(scissor_box);
    OpenGL::render_transformed_texture(src_tex, quad.geometry, {},
                                       transform, {1.0f, 1.0f, 1.0f, alpha});
    OpenGL::render_end();
}

const float wf_3D_view::fov = PI/4;
glm::mat4 wf_3D_view::default_view_matrix()
{
    return glm::lookAt(
        glm::vec3(0., 0., 1.0 / std::tan(fov / 2)),
        glm::vec3(0., 0., 0.),
        glm::vec3(0., 1., 0.));
}

glm::mat4 wf_3D_view::default_proj_matrix()
{
    return glm::perspective(fov, 1.0f, .1f, 100.f);
}

wf_3D_view::wf_3D_view(wayfire_view view)
{
    this->view = view;
    view_proj = default_proj_matrix() * default_view_matrix();
}

/* TODO: cache total_transform, because it is often unnecessarily recomputed */
glm::mat4 wf_3D_view::calculate_total_transform()
{
    auto og = view->get_output()->get_relative_geometry();
    glm::mat4 depth_scale = glm::scale(glm::mat4(1.0), {1, 1, 2.0 / std::min(og.width, og.height)});
    return translation * view_proj * depth_scale * rotation * scaling;
}

wf_point wf_3D_view::local_to_transformed_point(wf_geometry geometry, wf_point point)
{
    auto p = get_center_relative_coords(geometry, point);
    glm::vec4 v(1.0f * p.x, 1.0f * p.y, 0, 1);
    v = calculate_total_transform() * v;

    v.x /= v.w;
    v.y /= v.w;

    return get_absolute_coords_from_relative(geometry, {(int32_t) v.x, (int32_t) v.y});
}

/* TODO: is there a way to realiably reverse projective transformations? */
wf_point wf_3D_view::transformed_to_local_point(wf_geometry geometry, wf_point point)
{
    return {WF_INVALID_INPUT_COORDINATES, WF_INVALID_INPUT_COORDINATES};
}

void wf_3D_view::render_box(uint32_t src_tex, wlr_box src_box,
    wlr_box scissor_box, const wf_framebuffer& fb)
{
    auto quad = center_geometry(fb.geometry, src_box, get_center(src_box));

    auto transform = calculate_total_transform();
    auto translate = glm::translate(glm::mat4(1.0), {quad.off_x, quad.off_y, 0});
    auto scale = glm::scale(glm::mat4(1.0), {
                                2.0 / fb.geometry.width,
                                2.0 / fb.geometry.height,
                                1.0
                            });

    transform = fb.transform * scale * translate * transform;

    OpenGL::render_begin(fb);
    fb.scissor(scissor_box);
    OpenGL::render_transformed_texture(src_tex, quad.geometry, {},
                                       transform, color);
    OpenGL::render_end();
}
