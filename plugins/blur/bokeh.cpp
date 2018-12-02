#include "blur.hpp"

static const char* bokeh_vertex_shader =
R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 texcoord;

varying mediump vec2 uvpos[2];

uniform mat4 mvp;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
    uvpos[0] = texcoord;
    uvpos[1] = vec4(mvp * vec4(texcoord - 0.5, 0.0, 1.0)).xy + 0.5;
}
)";

static const char* bokeh_fragment_shader =
R"(
#version 100
precision mediump float;

uniform float offset;
uniform int iterations;
uniform vec2 halfpixel;
uniform sampler2D window_texture;
uniform sampler2D bg_texture;
uniform int mode;

varying mediump vec2 uvpos[2];

#define GOLDEN_ANGLE 2.39996

mat2 rot = mat2(cos(GOLDEN_ANGLE), sin(GOLDEN_ANGLE), -sin(GOLDEN_ANGLE), cos(GOLDEN_ANGLE));

void main()
{
    vec2 uv = uvpos[0];

    if (mode == 0) {
        float radius = offset;
        vec3 acc = vec3(0), div = acc;
        float r = 1.0;
        vec2 vangle = vec2(radius / sqrt(float(iterations)), radius / sqrt(float(iterations)));
	for (int j = 0; j < iterations; j++)
        {
            r += 1.0 / r;
	    vangle = rot * vangle;
            vec3 col = texture2D(bg_texture, uv + (r - 1.0) * vangle * halfpixel * 2.0).rgb;
            vec3 bokeh = pow(col, vec3(4.0));
            acc += col * bokeh;
            div += bokeh;
	}
	if (iterations == 0)
            gl_FragColor = texture2D(bg_texture, uv);
        else
            gl_FragColor = vec4(acc / div, 1.0);
    } else {
        vec4 wp = texture2D(window_texture, uvpos[1]);
        vec4 bp = texture2D(bg_texture, uv);
        vec4 c = clamp(4.0 * wp.a, 0.0, 1.0) * bp;
        gl_FragColor = wp + (1.0 - wp.a) * c;
    }
}
)";

static const wf_blur_default_option_values bokeh_defaults = {
    .algorithm_name = "bokeh",
    .offset = "1",
    .degrade = "1",
    .iterations = "15"
};

class wf_bokeh_blur : public wf_blur_base
{
    GLuint modeID, posID, mvpID, texcoordID,
           offsetID, iterID, halfpixelID, texID[2];

    public:
    wf_bokeh_blur(wayfire_output* output) : wf_blur_base(output, bokeh_defaults)
    {

        OpenGL::render_begin();
        program = OpenGL::create_program_from_source(bokeh_vertex_shader,
            bokeh_fragment_shader);

        posID      = GL_CALL(glGetAttribLocation(program, "position"));
        texcoordID = GL_CALL(glGetAttribLocation(program, "texcoord"));

        mvpID        = GL_CALL(glGetUniformLocation(program, "mvp"));
        iterID       = GL_CALL(glGetUniformLocation(program, "iterations"));
        modeID       = GL_CALL(glGetUniformLocation(program, "mode"));
        texID[0]     = GL_CALL(glGetUniformLocation(program, "window_texture"));
        texID[1]     = GL_CALL(glGetUniformLocation(program, "bg_texture"));
        offsetID     = GL_CALL(glGetUniformLocation(program, "offset"));
        halfpixelID  = GL_CALL(glGetUniformLocation(program, "halfpixel"));
        OpenGL::render_end();
    }

    void pre_render(uint32_t src_tex, wlr_box _src_box, const wf_region& damage,
        const wf_framebuffer& target_fb)
    {
        int iterations = iterations_opt->as_int();
        float offset = offset_opt->as_double();

        wlr_box fb_geom = target_fb.framebuffer_box_from_geometry_box(target_fb.geometry);

        wlr_box b = wlr_box_from_pixman_box(damage.get_extents());
        b = target_fb.framebuffer_box_from_damage_box(b);

        auto src_box = target_fb.framebuffer_box_from_geometry_box(_src_box);
        int fb_h = fb_geom.height;

        src_box.x -= fb_geom.x;
        src_box.y -= fb_geom.y;

        int x = src_box.x, y = src_box.y, w = src_box.width, h = src_box.height;
        int bx = b.x, by = b.y, bw = b.width, bh = b.height;

        int sw = bw * (1.0 / degrade_opt->as_int());
        int sh = bh * (1.0 / degrade_opt->as_int());

        int pw = sw * degrade_opt->as_int();
        int ph = sh * degrade_opt->as_int();

        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f,  1.0f,
            -1.0f,  1.0f
        };
        static const float texCoords[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f
        };

        OpenGL::render_begin(target_fb);

        /* The damage region we recieve as an argument to this function
         * contains last and current damage. We take the bounding box
         * of this region for blurring. At this point, target_fb contains
         * the scene rendered up until the view for which this function is
         * called. To save resources, the texture can be blurred at a
         * smaller size and then scaled back up. This causes discrepancies
         * between odd and even sizes so to even things out, we upscale
         * by one pixel in the odd size case when doing the initial blit. */
        fb[0].allocate(pw, ph);
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fb.fb));
        GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[0].fb));
        /* The target_fb origin is at bottom left and the y is flipped so we have
         * to take these into account when blitting */
        GL_CALL(glBlitFramebuffer(bx, fb_h - by - bh, bx + bw, fb_h - by, 0, 0, pw, ph, GL_COLOR_BUFFER_BIT, GL_LINEAR));

        /* Upload data to shader */
        GL_CALL(glUseProgram(program));
        GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &glm::mat4(1.0)[0][0]));
        GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glVertexAttribPointer(texcoordID, 2, GL_FLOAT, GL_FALSE, 0, texCoords));
        GL_CALL(glEnableVertexAttribArray(posID));
        GL_CALL(glEnableVertexAttribArray(texcoordID));
        GL_CALL(glUniform2f(halfpixelID, 0.5f / sw, 0.5f / sh));
        GL_CALL(glUniform1f(offsetID, offset));
        GL_CALL(glUniform1i(iterID, iterations));
        GL_CALL(glUniform1i(texID[0], 0));
        GL_CALL(glUniform1i(texID[1], 1));

        GL_CALL(glUniform1i(modeID, 0));

        render_iteration(fb[0], fb[1], sw, sh);

        fb[0].allocate(w, h);
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[1].fb));
        GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[0].fb));
        GL_CALL(glBlitFramebuffer(0, 0, sw, sh,
                bx - x,
                h - (by - y) - bh,
                (bx + bw) - x,
                h - (by - y),
                GL_COLOR_BUFFER_BIT, GL_LINEAR));

        /* Disable stuff */
        GL_CALL(glUseProgram(0));
        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        GL_CALL(glDisableVertexAttribArray(posID));
        GL_CALL(glDisableVertexAttribArray(texcoordID));

        OpenGL::render_end();
    }

    void render(uint32_t src_tex, wlr_box _src_box, wlr_box scissor_box,
        const wf_framebuffer& target_fb)
    {
        wlr_box fb_geom = target_fb.framebuffer_box_from_geometry_box(target_fb.geometry);
        auto src_box = target_fb.framebuffer_box_from_geometry_box(_src_box);
        int fb_h = fb_geom.height;
        src_box.x -= fb_geom.x;
        src_box.y -= fb_geom.y;

        int x = src_box.x, y = src_box.y, w = src_box.width, h = src_box.height;

        OpenGL::render_begin(target_fb);

        /* Use shader and enable vertex and texcoord data */
        GL_CALL(glUseProgram(program));
        GL_CALL(glEnableVertexAttribArray(posID));
        GL_CALL(glEnableVertexAttribArray(texcoordID));

        /* Blend blurred background with window texture src_tex */
        GL_CALL(glUniform1i(modeID, 1));
        GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &glm::inverse(target_fb.transform)[0][0]));
        GL_CALL(glActiveTexture(GL_TEXTURE0 + 0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, src_tex));
        GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, fb[0].tex));
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

        /* Render it to target_fb */
        target_fb.bind();

        GL_CALL(glViewport(x, fb_h - y - h, w, h));
        target_fb.scissor(scissor_box);

        GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

        /* Disable stuff */
        GL_CALL(glUseProgram(0));
        GL_CALL(glDisable(GL_BLEND));
        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        GL_CALL(glDisableVertexAttribArray(posID));
        GL_CALL(glDisableVertexAttribArray(texcoordID));

        OpenGL::render_end();
    }
};

std::unique_ptr<wf_blur_base> create_bokeh_blur(wayfire_output *output)
{
    return nonstd::make_unique<wf_bokeh_blur> (output);
}

