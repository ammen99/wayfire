#include "blur.hpp"
#include "debug.hpp"

static const char* dual_kawase_vertex_shader = R"(
#version 100
attribute mediump vec2 position;

varying mediump vec2 uv;

void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);
    uv = (position.xy + vec2(1.0, 1.0)) / 2.0;
})";

static const char* dual_kawase_fragment_shader_down = R"(
#version 100
precision mediump float;

uniform float offset;
uniform vec2 halfpixel;
uniform sampler2D bg_texture;

varying mediump vec2 uv;

void main()
{
    vec4 sum = texture2D(bg_texture, uv) * 4.0;
    sum += texture2D(bg_texture, uv - halfpixel.xy * offset);
    sum += texture2D(bg_texture, uv + halfpixel.xy * offset);
    sum += texture2D(bg_texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset);
    sum += texture2D(bg_texture, uv - vec2(halfpixel.x, -halfpixel.y) * offset);
    gl_FragColor = sum / 8.0;
})";

static const char* dual_kawase_fragment_shader_down_up = R"(
#version 100
precision mediump float;

uniform float offset;
uniform vec2 halfpixel;
uniform sampler2D bg_texture;

varying mediump vec2 uv;

void main()
{
    vec4 sum = texture2D(bg_texture, uv + vec2(-halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(bg_texture, uv + vec2(-halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(bg_texture, uv + vec2(0.0, halfpixel.y * 2.0) * offset);
    sum += texture2D(bg_texture, uv + vec2(halfpixel.x, halfpixel.y) * offset) * 2.0;
    sum += texture2D(bg_texture, uv + vec2(halfpixel.x * 2.0, 0.0) * offset);
    sum += texture2D(bg_texture, uv + vec2(halfpixel.x, -halfpixel.y) * offset) * 2.0;
    sum += texture2D(bg_texture, uv + vec2(0.0, -halfpixel.y * 2.0) * offset);
    sum += texture2D(bg_texture, uv + vec2(-halfpixel.x, -halfpixel.y) * offset) * 2.0;
    gl_FragColor = sum / 12.0;
})";

static const wf_blur_default_option_values dual_kawase_defaults = {
    .algorithm_name = "dual_kawase",
    .offset = "5",
    .degrade = "1",
    .iterations = "4"
};

class wf_dual_kawase_blur : public wf_blur_base
{
    GLuint posIDDown, offsetIDDown, halfpixelIDDown;
    GLuint posIDUp, offsetIDUp, halfpixelIDUp;
    GLuint programDown, programUp;

    public:
    wf_dual_kawase_blur(wayfire_output *output)
        : wf_blur_base(output, dual_kawase_defaults)
    {
        OpenGL::render_begin();

        programDown = OpenGL::create_program_from_source(dual_kawase_vertex_shader,
            dual_kawase_fragment_shader_down);

        posIDDown       = GL_CALL(glGetAttribLocation( programDown, "position"));
        offsetIDDown    = GL_CALL(glGetUniformLocation(programDown, "offset"));
        halfpixelIDDown = GL_CALL(glGetUniformLocation(programDown, "halfpixel"));

    programUp = OpenGL::create_program_from_source(dual_kawase_vertex_shader,
            dual_kawase_fragment_shader_down_up);

        posIDUp       = GL_CALL(glGetAttribLocation( programUp, "position"));
        offsetIDUp    = GL_CALL(glGetUniformLocation(programUp, "offset"));
        halfpixelIDUp = GL_CALL(glGetUniformLocation(programUp, "halfpixel"));

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

    int sampleWidth, sampleHeight;

        OpenGL::render_begin();

    /* Downsample */
        GL_CALL(glUseProgram(programDown));
        GL_CALL(glVertexAttribPointer(posIDDown, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(posIDDown));

        GL_CALL(glUniform1f(offsetIDDown, offset));

        for (int i = 0; i < iterations; i++){
        sampleWidth = width / (1 << i);
        sampleHeight = height / (1 << i);

        GL_CALL(glUniform2f(halfpixelIDDown, 0.5f / sampleWidth, 0.5f / sampleHeight));
            render_iteration(fb[i % 2], fb[1 - i % 2], sampleWidth, sampleHeight);
    }
    
    GL_CALL(glDisableVertexAttribArray(posIDDown));

        /* Upsample */
    GL_CALL(glUseProgram(programUp));
        GL_CALL(glVertexAttribPointer(posIDUp, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(posIDUp));

        GL_CALL(glUniform1f(offsetIDUp, offset));

        for (int i = iterations - 1; i >= 0; i--) {
        sampleWidth = width / (1 << i);
        sampleHeight = height / (1 << i);

        GL_CALL(glUniform2f(halfpixelIDUp, 0.5f / sampleWidth, 0.5f / sampleHeight));
            render_iteration(fb[1 - i % 2], fb[i % 2], sampleWidth, sampleHeight);
    }

        GL_CALL(glUseProgram(0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        GL_CALL(glDisableVertexAttribArray(posIDUp));

        OpenGL::render_end();

        return 0;
    }
};

std::unique_ptr<wf_blur_base> create_dual_kawase_blur(wayfire_output *output)
{
    return nonstd::make_unique<wf_dual_kawase_blur> (output);
}
