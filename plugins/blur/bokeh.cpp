#include "blur.hpp"

static const char* bokeh_vertex_shader =
R"(
#version 100

attribute mediump vec2 position;
varying mediump vec2 uv;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
    uv = (position.xy + vec2(1.0, 1.0)) / 2.0;
}
)";

static const char* bokeh_fragment_shader =
R"(
#version 100
precision mediump float;

uniform float offset;
uniform int iterations;
uniform vec2 halfpixel;
uniform int mode;

uniform sampler2D bg_texture;
varying mediump vec2 uv;

#define GOLDEN_ANGLE 2.39996

mat2 rot = mat2(cos(GOLDEN_ANGLE), sin(GOLDEN_ANGLE), -sin(GOLDEN_ANGLE), cos(GOLDEN_ANGLE));

void main()
{
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
}
)";

static const wf_blur_default_option_values bokeh_defaults = {
    .algorithm_name = "bokeh",
    .offset = "5",
    .degrade = "1",
    .iterations = "15"
};

class wf_bokeh_blur : public wf_blur_base
{
    GLuint posID, offsetID, iterID, halfpixelID;

    public:
    wf_bokeh_blur(wf::output_t* output) : wf_blur_base(output, bokeh_defaults)
    {

        OpenGL::render_begin();
        program[0] = OpenGL::create_program_from_source(bokeh_vertex_shader,
            bokeh_fragment_shader);
        program[1] = -1;

        posID        = GL_CALL(glGetAttribLocation(program[0], "position"));
        iterID       = GL_CALL(glGetUniformLocation(program[0], "iterations"));
        offsetID     = GL_CALL(glGetUniformLocation(program[0], "offset"));
        halfpixelID  = GL_CALL(glGetUniformLocation(program[0], "halfpixel"));
        OpenGL::render_end();
    }

    int blur_fb0(int width, int height) override
    {
        int iterations = iterations_opt->as_int();
        float offset = offset_opt->as_double();

        static const float vertexData[] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
             1.0f,  1.0f,
            -1.0f,  1.0f
        };

        OpenGL::render_begin();
        /* Upload data to shader */
        GL_CALL(glUseProgram(program[0]));
        GL_CALL(glUniform2f(halfpixelID, 0.5f / width, 0.5f / height));
        GL_CALL(glUniform1f(offsetID, offset));
        GL_CALL(glUniform1i(iterID, iterations));

        GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(posID));
        GL_CALL(glDisable(GL_BLEND));

        render_iteration(fb[0], fb[1], width, height);

        /* Reset gl state */
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

        GL_CALL(glUseProgram(0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        GL_CALL(glDisableVertexAttribArray(posID));
        OpenGL::render_end();

        return 1;
    }

    int calculate_blur_radius() override
    {
        return 100 * wf_blur_base::offset_opt->as_double() * wf_blur_base::degrade_opt->as_int();
    }
};

std::unique_ptr<wf_blur_base> create_bokeh_blur(wf::output_t *output)
{
    return std::make_unique<wf_bokeh_blur> (output);
}
