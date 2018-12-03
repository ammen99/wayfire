#include "blur.hpp"
#include "debug.hpp"

static const char* kawase_vertex_shader = R"(
#version 100
attribute mediump vec2 position;

varying mediump vec2 uv;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
    uv = (position.xy + vec2(1.0, 1.0)) / 2.0;
})";

static const char* kawase_fragment_shader = R"(
#version 100
precision mediump float;

uniform float offset;
uniform vec2 halfpixel;
uniform sampler2D bg_texture;
uniform int mode;

varying mediump vec2 uv;

void main()
{
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
    } else {
        vec4 sum = texture2D(bg_texture, uv) * 4.0;
        sum += texture2D(bg_texture, uv - halfpixel.xy * offset);
        sum += texture2D(bg_texture, uv + halfpixel.xy * offset);
        sum += texture2D(bg_texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset);
        sum += texture2D(bg_texture, uv - vec2(halfpixel.x, -halfpixel.y) * offset);
        gl_FragColor = sum / 8.0;
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
    GLuint posID, modeID, offsetID, halfpixelID;

    public:
    wf_dual_kawase_blur(wayfire_output *output)
        : wf_blur_base(output, kawase_defaults)
    {
        OpenGL::render_begin();
        program = OpenGL::create_program_from_source(kawase_vertex_shader,
            kawase_fragment_shader);

        posID        = GL_CALL(glGetAttribLocation(program, "position"));
        modeID       = GL_CALL(glGetUniformLocation(program, "mode"));
        offsetID     = GL_CALL(glGetUniformLocation(program, "offset"));
        halfpixelID  = GL_CALL(glGetUniformLocation(program, "halfpixel"));

        OpenGL::render_end();
    }

    int blur_fb0(int width, int height)
    {
        int iterations = iterations_opt->as_int();
        float offset = offset_opt->as_double();

        /* Upload data to shader */
        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f,  1.0f,
            -1.0f,  1.0f
        };

        OpenGL::render_begin();
        GL_CALL(glUseProgram(program));
        GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(posID));

        GL_CALL(glUniform2f(halfpixelID, 0.5f / width, 0.5f / height));
        GL_CALL(glUniform1f(offsetID, offset));

        /* Downsample */
        GL_CALL(glUniform1i(modeID, 0));
        for (int i = 0; i < iterations; i++)
            render_iteration(fb[i % 2], fb[1 - i % 2], width, height);

        /* Upsample */
        GL_CALL(glUniform1i(modeID, 1));
        for (int i = iterations - 1; i >= 0; i--)
            render_iteration(fb[1 - i % 2], fb[i % 2], width, height);

        GL_CALL(glUseProgram(0));
        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        GL_CALL(glDisableVertexAttribArray(posID));
        OpenGL::render_end();
        return 0;
    }
};

std::unique_ptr<wf_blur_base> create_kawase_blur(wayfire_output *output)
{
    return nonstd::make_unique<wf_dual_kawase_blur> (output);
}
