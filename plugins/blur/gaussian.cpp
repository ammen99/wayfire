#include "blur.hpp"

static const char* gaussian_vertex_shader =
R"(
#version 100

attribute mediump vec2 position;
uniform vec2 size;
uniform float offset;

varying highp vec2 blurcoord[9];

void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);

    vec2 texcoord = (position.xy + vec2(1.0, 1.0)) / 2.0;

    blurcoord[0] = texcoord;
    blurcoord[1] = texcoord + vec2(1.0 * offset) / size;
    blurcoord[2] = texcoord - vec2(1.0 * offset) / size;
    blurcoord[3] = texcoord + vec2(2.0 * offset) / size;
    blurcoord[4] = texcoord - vec2(2.0 * offset) / size;
    blurcoord[5] = texcoord + vec2(3.0 * offset) / size;
    blurcoord[6] = texcoord - vec2(3.0 * offset) / size;
    blurcoord[7] = texcoord + vec2(4.0 * offset) / size;
    blurcoord[8] = texcoord - vec2(4.0 * offset) / size;
}
)";

static const char* gaussian_fragment_shader_horz =
R"(
#version 100
precision mediump float;

uniform sampler2D bg_texture;
uniform int mode;

varying highp vec2 blurcoord[9];

void main()
{
    vec2 uv = blurcoord[0];
    vec4 bp = vec4(0.0);
    bp += texture2D(bg_texture, vec2(blurcoord[0].x, uv.y)) * 0.2270270270;
    bp += texture2D(bg_texture, vec2(blurcoord[1].x, uv.y)) * 0.1945945946;
    bp += texture2D(bg_texture, vec2(blurcoord[2].x, uv.y)) * 0.1945945946;
    bp += texture2D(bg_texture, vec2(blurcoord[3].x, uv.y)) * 0.1216216216;
    bp += texture2D(bg_texture, vec2(blurcoord[4].x, uv.y)) * 0.1216216216;
    bp += texture2D(bg_texture, vec2(blurcoord[5].x, uv.y)) * 0.0540540541;
    bp += texture2D(bg_texture, vec2(blurcoord[6].x, uv.y)) * 0.0540540541;
    bp += texture2D(bg_texture, vec2(blurcoord[7].x, uv.y)) * 0.0162162162;
    bp += texture2D(bg_texture, vec2(blurcoord[8].x, uv.y)) * 0.0162162162;
    gl_FragColor = bp;
})";

static const char* gaussian_fragment_shader_vert =
R"(
#version 100
precision mediump float;

uniform sampler2D bg_texture;
uniform int mode;

varying highp vec2 blurcoord[9];

void main()
{
    vec2 uv = blurcoord[0];
    vec4 bp = vec4(0.0);
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[0].y)) * 0.2270270270;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[1].y)) * 0.1945945946;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[2].y)) * 0.1945945946;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[3].y)) * 0.1216216216;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[4].y)) * 0.1216216216;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[5].y)) * 0.0540540541;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[6].y)) * 0.0540540541;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[7].y)) * 0.0162162162;
    bp += texture2D(bg_texture, vec2(uv.x, blurcoord[8].y)) * 0.0162162162;
    gl_FragColor = bp;
})";

static const wf_blur_default_option_values gaussian_defaults = {
    .algorithm_name = "gaussian",
    .offset = "2",
    .degrade = "1",
    .iterations = "2"
};

class wf_gaussian_blur : public wf_blur_base
{
    GLuint posID[2], sizeID[2], offsetID[2];

    public:
    void get_id_locations(int i)
    {
        posID[i]    = GL_CALL(glGetAttribLocation(program[i], "position"));
        sizeID[i]   = GL_CALL(glGetUniformLocation(program[i], "size"));
        offsetID[i] = GL_CALL(glGetUniformLocation(program[i], "offset"));
    }

    wf_gaussian_blur(wf::output_t *output) : wf_blur_base(output, gaussian_defaults)
    {
        OpenGL::render_begin();
        program[0] = OpenGL::create_program_from_source(
            gaussian_vertex_shader, gaussian_fragment_shader_horz);
        program[1] = OpenGL::create_program_from_source(
            gaussian_vertex_shader, gaussian_fragment_shader_vert);
        get_id_locations(0);
        get_id_locations(1);
        OpenGL::render_end();
    }

    void upload_data(int i, int width, int height)
    {
        float offset = offset_opt;
        static const float vertexData[] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
             1.0f,  1.0f,
            -1.0f,  1.0f
        };

        GL_CALL(glUseProgram(program[i]));
        GL_CALL(glUniform2f(sizeID[i], width, height));
        GL_CALL(glUniform1f(offsetID[i], offset));
        GL_CALL(glVertexAttribPointer(posID[i], 2, GL_FLOAT, GL_FALSE, 0, vertexData));
    }

    void blur(int i, int width, int height)
    {
        GL_CALL(glUseProgram(program[i]));
        GL_CALL(glEnableVertexAttribArray(posID[i]));
        render_iteration(fb[i], fb[!i], width, height);
        GL_CALL(glDisableVertexAttribArray(posID[i]));
    }

    int blur_fb0(int width, int height) override
    {
        int i, iterations = iterations_opt;

        OpenGL::render_begin();
        GL_CALL(glDisable(GL_BLEND));
        /* Enable our shader and pass some data to it. The shader
         * does gaussian blur on the background texture in two passes,
         * one horizontal and one vertical */
        upload_data(0, width, height);
        upload_data(1, width, height);

        for (i = 0; i < iterations; i++) {
            /* Blur horizontally */
            blur(0, width, height);

            /* Blur vertically */
            blur(1, width, height);
        }

        /* Reset gl state */
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

        GL_CALL(glUseProgram(0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        OpenGL::render_end();

        return 0;
    }

    int calculate_blur_radius() override
    {
        return 4 * wf_blur_base::calculate_blur_radius();
    }
};

std::unique_ptr<wf_blur_base> create_gaussian_blur(wf::output_t *output)
{
    return std::make_unique<wf_gaussian_blur> (output);
}
