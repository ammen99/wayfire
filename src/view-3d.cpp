#include "view-transform.hpp"
#include "opengl.hpp"
#include "debug.hpp"
#include "core.hpp"
#include <algorithm>
#include <cmath>

#define PI 3.14159265359

wlr_box wf_view_transformer_t::get_bounding_box(wlr_box region)
{
    const auto p1 = local_to_transformed_point({region.x,                region.y});
    const auto p2 = local_to_transformed_point({region.x + region.width, region.y});
    const auto p3 = local_to_transformed_point({region.x,                region.y - region.height});
    const auto p4 = local_to_transformed_point({region.x + region.width, region.y - region.height});

    const int x1 = std::min({p1.x, p2.x, p3.x, p4.x});
    const int x2 = std::max({p1.x, p2.x, p3.x, p4.x});
    const int y1 = std::max({p1.y, p2.y, p3.y, p4.y});
    const int y2 = std::min({p1.y, p2.y, p3.y, p4.y});

    return {x1, y1, x2 - x1, y1 - y2};
}

wf_2D_view::wf_2D_view(float w, float h)
{
    ortho = glm::ortho(-w/2, w/2, h/2, -h/2);
    m_aspect = w / h;
}

static void rotate_xy(float& x, float& y, float angle)
{
    auto v = glm::vec4{x, y, 0, 1};
    auto rot = glm::rotate(glm::mat4(1.0), angle, {0, 0, 1});
    v = rot * v;
    x = v.x;
    y = v.y;
}

wf_point wf_2D_view::local_to_transformed_point(wf_point point)
{
    float x = point.x, y = point.y;

    x *= scale_x; y *= scale_y;
    rotate_xy(x, y, -angle);
    x += translation_x;
    y += translation_y;

    return {(int32_t) x, (int32_t) y};
}

wf_point wf_2D_view::transformed_to_local_point(wf_point point)
{
    float x = point.x, y = point.y;

    x /= scale_x; y /= scale_y;
    rotate_xy(x, y, angle);
    x -= translation_x;
    y -= translation_y;

    return {(int32_t) x, (int32_t) y};
}

void wf_2D_view::render_with_damage(uint32_t src_tex,
                                    uint32_t target_fbo,
                                    wlr_box src_box,
                                    wlr_box scissor_box)
{
    const float w = src_box.width * scale_x;
    const float h = src_box.height * scale_y;

    const float render_tlx = -src_box.width;
    const float render_tly =  src_box.height;

    const float tlx = -w;
    const float tly =  h;

    const float brx = tlx + w * 2;
    const float bry = tly - h * 2;

    glm::mat4 transform{1.0};
    float off_x = translation_x + src_box.x - render_tlx / 2.0;
    float off_y = translation_y + render_tly / 2.0 - src_box.y;

    auto rotate = glm::rotate(transform, angle, {0, 0, 1});
    auto scale = glm::scale(glm::mat4(1.0), {0.5, 0.5, 1});
    auto translate = glm::translate(transform, {off_x, off_y, 0});

    transform = ortho * translate * scale * rotate;

    wlr_renderer_scissor(core->renderer, &scissor_box);
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, target_fbo));
    OpenGL::use_device_viewport();

    OpenGL::render_transformed_texture(src_tex, {tlx, tly, brx, bry},{}, transform);
}

wf_3D_view::wf_3D_view(float w, float h)
    : m_width(w), m_height(h), m_aspect(w/h)
{
    const float fov = PI / 8; // 45 degrees
    auto view = glm::lookAt(glm::vec3(0., 0., 1.0 / std::tan(fov / 2)),
                            glm::vec3(0., 0., 0.),
                            glm::vec3(0., 1., 0.));
    auto proj = glm::perspective(fov, 1.0f, .1f, 100.f);
    view_proj = proj * view;
}

/* TODO: cache total_transform, because it is often unnecessarily recomputed */
glm::mat4 wf_3D_view::calculate_total_transform()
{
    glm::mat4 depth_scale = glm::scale(glm::mat4(1.0), {1, 1, 2.0 / std::min(m_width, m_height)});
    return view_proj * depth_scale * translation * rotation * scaling;
}

wf_point wf_3D_view::local_to_transformed_point(wf_point point)
{
    glm::vec4 v(1.0f * point.x, 1.0f * point.y, 0, 1);
    v = calculate_total_transform() * v;

    v.x /= v.w;
    v.y /= v.w;

    return {static_cast<int>(v.x), static_cast<int>(v.y)};
}

/* TODO: is there a way to realiably reverse projective transformations? */
wf_point wf_3D_view::transformed_to_local_point(wf_point point)
{
    return {-1, -1};
}


void wf_3D_view::render_with_damage(uint32_t src_tex,
                                uint32_t target_fbo,
                                wlr_box src_box,
                                wlr_box scissor_box)
{
    wlr_renderer_scissor(core->renderer, &scissor_box);
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, target_fbo));
    OpenGL::use_device_viewport();

    const float tlx = -src_box.width / 2.0;
    const float tly =  src_box.height / 2.0;

    const float brx = tlx + src_box.width;
    const float bry = tly - src_box.height;

    float off_x = src_box.x - tlx;
    float off_y = src_box.y - tly;

    auto transform = calculate_total_transform();
    auto translate = glm::translate(glm::mat4(1.0), {off_x, off_y, 0});
    auto scale = glm::scale(glm::mat4(1.0), {
                                2.0 / m_width,
                                2.0 / m_height,
                                1.0
                            });

  //  log_info("render@ %f@%f %f@%f, scale %f@%f", tlx, tly, brx, bry, m_width / 2.0, m_height / 2.0);

    transform = scale * translate * transform;
    OpenGL::render_transformed_texture(src_tex, {tlx, tly, brx, bry}, {},
                                       transform, color, TEXTURE_TRANSFORM_INVERT_Y);
}
