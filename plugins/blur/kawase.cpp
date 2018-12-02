#include "blur.hpp"
#include "debug.hpp"

static const char* kawase_vertex_shader = R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 texcoord;

varying mediump vec2 uvpos[2];

uniform mat4 mvp;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
    uvpos[0] = texcoord;
    uvpos[1] = vec4(mvp * vec4(texcoord - 0.5, 0.0, 1.0)).xy + 0.5;
})";

static const char* kawase_fragment_shader = R"(
#version 100
precision mediump float;

uniform float offset;
uniform vec2 halfpixel;
uniform sampler2D window_texture;
uniform sampler2D bg_texture;
uniform int mode;

varying mediump vec2 uvpos[2];

void main()
{
    vec2 uv = uvpos[0];

    if (mode == 0) {
        vec4 sum = texture2D(bg_texture, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
        sum += texture2D(bg_texture, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
        sum += texture2D(bg_texture, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
        sum += texture2D(bg_texture, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
        sum += texture2D(bg_texture, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
        sum += texture2D(bg_texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
        sum += texture2D(bg_texture, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
        sum += texture2D(bg_texture, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;
        gl_FragColor = sum / 12.0;
    } else if (mode == 1) {
        vec4 sum = texture2D(bg_texture, uv) * 4.0;
        sum += texture2D(bg_texture, uv - halfpixel.xy * offset);
        sum += texture2D(bg_texture, uv + halfpixel.xy * offset);
        sum += texture2D(bg_texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset);
        sum += texture2D(bg_texture, uv - vec2(halfpixel.x, -halfpixel.y) * offset);
        gl_FragColor = sum / 8.0;
    } else {
        vec4 wp = texture2D(window_texture, uvpos[1]);
        vec4 bp = texture2D(bg_texture, uv);
        vec4 c = clamp(4.0 * wp.a, 0.0, 1.0) * bp;
        gl_FragColor = wp + (1.0 - wp.a) * c;
    }
})";

static const wf_blur_default_option_values kawase_defaults = {
    .algorithm_name = "kawase",
    .offset = "2",
    .degrade = "1",
    .iterations = "2"
};

class wf_dual_kawase_blur : public wf_blur_base
{
    GLuint posID, mvpID, texcoordID,
           modeID, offsetID, halfpixelID, texID[2];

    public:
    wf_dual_kawase_blur(wayfire_output *output)
        : wf_blur_base(output, kawase_defaults)
    {
        OpenGL::render_begin();
        program = OpenGL::create_program_from_source(kawase_vertex_shader,
            kawase_fragment_shader);

        posID      = GL_CALL(glGetAttribLocation(program, "position"));
        texcoordID = GL_CALL(glGetAttribLocation(program, "texcoord"));

        mvpID        = GL_CALL(glGetUniformLocation(program, "mvp"));
        modeID       = GL_CALL(glGetUniformLocation(program, "mode"));
        texID[0]     = GL_CALL(glGetUniformLocation(program, "window_texture"));
        texID[1]     = GL_CALL(glGetUniformLocation(program, "bg_texture"));
        offsetID     = GL_CALL(glGetUniformLocation(program, "offset"));
        halfpixelID  = GL_CALL(glGetUniformLocation(program, "halfpixel"));
        OpenGL::render_end();
    }

    void pre_render(uint32_t src_tex, wlr_box src_box, const wf_region& damage,
        const wf_framebuffer& target_fb)
    {
        auto damage_box = copy_region(fb[0], target_fb, damage);
        int scaled_width = damage_box.width / degrade_opt->as_int();
        int scaled_height = damage_box.height / degrade_opt->as_int();

        /* we subtract target_fb's position to so that
         * view box is relative to framebuffer */
        auto view_box = target_fb.framebuffer_box_from_geometry_box(
            src_box + wf_point{-target_fb.geometry.x, -target_fb.geometry.y});

        int iterations = iterations_opt->as_int();
        float offset = offset_opt->as_double();

        /* Upload data to shader */
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

        OpenGL::render_begin();
        GL_CALL(glUseProgram(program));
        GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &glm::mat4(1.0)[0][0]));
        GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glVertexAttribPointer(texcoordID, 2, GL_FLOAT, GL_FALSE, 0, texCoords));
        GL_CALL(glEnableVertexAttribArray(posID));
        GL_CALL(glEnableVertexAttribArray(texcoordID));

        GL_CALL(glUniform2f(halfpixelID, 0.5f / scaled_width, 0.5f / scaled_height));
        GL_CALL(glUniform1f(offsetID, offset));
        GL_CALL(glUniform1i(texID[0], 0));
        GL_CALL(glUniform1i(texID[1], 1));

        /* Downsample */
        GL_CALL(glUniform1i(modeID, 0));
        for (int i = 0; i < iterations; i++)
            render_iteration(fb[i % 2], fb[1 - i % 2], scaled_width, scaled_height);

        /* Upsample */
        GL_CALL(glUniform1i(modeID, 1));
        for (int i = iterations - 1; i >= 0; i--)
            render_iteration(fb[1 - i % 2], fb[i % 2], scaled_width, scaled_height);

        fb[1].allocate(view_box.width, view_box.height);
        fb[1].bind();
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0].fb));

        /* Blit the blurred texture into an fb which has the size of the view,
         * so that the view texture and the blurred background can be combined
         * together in render()
         *
         * local_geometry is damage_box relative to view box */
        wlr_box local_box = damage_box + wf_point{-view_box.x, -view_box.y};
        GL_CALL(glBlitFramebuffer(0, 0, scaled_width, scaled_height,
                local_box.x,
                view_box.height - local_box.y - local_box.height,
                local_box.x + local_box.width,
                view_box.height - local_box.y,
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
        GL_CALL(glUniform1i(modeID, 2));
        GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &glm::inverse(target_fb.transform)[0][0]));
        GL_CALL(glActiveTexture(GL_TEXTURE0 + 0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, src_tex));
        GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, fb[1].tex));
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

std::unique_ptr<wf_blur_base> create_kawase_blur(wayfire_output *output)
{
    return nonstd::make_unique<wf_dual_kawase_blur> (output);
}
