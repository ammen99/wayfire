#ifndef VIEW_TRANSFORM_HPP
#define VIEW_TRANSFORM_HPP

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "view.hpp"

/* all transforms operate with a coordinate system centered at the
 * geometric center of the main surface(the wayfire_view_t) */

/* TODO: adjust to resolution */
class wf_view_transformer_t
{
    public:
        virtual wf_point local_to_transformed_point(wf_point point) = 0;
        virtual wf_point transformed_to_local_point(wf_point point) = 0;

        /* return the boundingbox of region after applying all transformations */
        virtual wlr_box get_bounding_box(wlr_box region);

        /* src_tex      the internal FBO texture,
         * target_fbo   the fbo to render to
         * src_box      the geometry of src_tex in screen-centric coordinates
         * scissor_box  the area in which the transform renderer must update,
         *              drawing outside of it will cause artifacts */
        virtual void render_with_damage(uint32_t src_tex,
                                        uint32_t target_fbo,
                                        wlr_box src_box,
                                        wlr_box scissor_box) = 0;
};

class wf_2D_view : public wf_view_transformer_t
{
    public:
        float angle = 0.0f;
        float scale_x = 1.0f, scale_y = 1.0f;
        float translation_x = 0.0f, translation_y = 0.0f;
        float alpha = 1.0f;

        float m_aspect;

        glm::mat4 ortho;

    public:
        wf_2D_view(float w, float h);

        virtual wf_point local_to_transformed_point(wf_point point);
        virtual wf_point transformed_to_local_point(wf_point point);

        virtual void render_with_damage(uint32_t src_tex,
                                        uint32_t target_fbo,
                                        wlr_box src_box,
                                        wlr_box scissor_box);
};

class wf_3D_view : public wf_view_transformer_t
{
    public:
        glm::mat4 view_proj{1.0}, translation{1.0}, rotation{1.0}, scaling{1.0};
        glm::vec4 color{1, 1, 1, 1};

        glm::mat4 calculate_total_transform();

        float m_width, m_height;
        float m_aspect;

    public:
        wf_3D_view(float width, float height);

        virtual wf_point local_to_transformed_point(wf_point point);
        virtual wf_point transformed_to_local_point(wf_point point);

        virtual void render_with_damage(uint32_t src_tex,
                                        uint32_t target_fbo,
                                        wlr_box src_box,
                                        wlr_box scissor_box);
};

#endif /* end of include guard: VIEW_TRANSFORM_HPP */
