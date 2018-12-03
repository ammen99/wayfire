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

static const char* gaussian_fragment_shader =
R"(
#version 100
precision mediump float;

uniform sampler2D bg_texture;
uniform int mode;

varying highp vec2 blurcoord[9];

void main()
{
    vec2 uv = blurcoord[0];

    if (mode == 0) {
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
        gl_FragColor = vec4(bp.rgb, 1.0);
    } else {
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
        gl_FragColor = vec4(bp.rgb, 1.0);
    }
})";

static const wf_blur_default_option_values gaussian_defaults = {
    .algorithm_name = "gaussian",
    .offset = "2",
    .degrade = "1",
    .iterations = "2"
};

class wf_gaussian_blur : public wf_blur_base
{
    GLuint posID, modeID, sizeID, offsetID;

    public:
    wf_gaussian_blur(wayfire_output *output)
        : wf_blur_base(output, gaussian_defaults)
    {
        OpenGL::render_begin();
        program = OpenGL::create_program_from_source(
            gaussian_vertex_shader, gaussian_fragment_shader);

        posID      = GL_CALL(glGetAttribLocation(program, "position"));

        sizeID    = GL_CALL(glGetUniformLocation(program, "size"));
        modeID    = GL_CALL(glGetUniformLocation(program, "mode"));
        offsetID  = GL_CALL(glGetUniformLocation(program, "offset"));
        OpenGL::render_end();
    }

    int blur_fb0(int width, int height)
    {
        int i, iterations = iterations_opt->as_int();
        float offset = offset_opt->as_double();

        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f,  1.0f,
            -1.0f,  1.0f
        };

        /* Enable our shader and pass some data to it. The shader accepts two textures
         * and does gaussian blur on the background texture in two passes, one horizontal
         * and one vertical */
        GL_CALL(glUseProgram(program));
        GL_CALL(glUniform2f(sizeID, width, height));
        GL_CALL(glUniform1f(offsetID, offset));

        GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(posID));

        for (i = 0; i < iterations; i++)
        {
            /* Tell shader to blur horizontally */
            GL_CALL(glUniform1i(modeID, 0));
            render_iteration(fb[0], fb[1], width, height);

            /* Tell shader to blur vertically */
            GL_CALL(glUniform1i(modeID, 1));
            render_iteration(fb[1], fb[0], width, height);
        }

        /* Disable stuff */
        GL_CALL(glUseProgram(0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        GL_CALL(glDisableVertexAttribArray(posID));

        OpenGL::render_end();
        return 0;
    }

    virtual int calculate_blur_radius()
    {
        return 4 * wf_blur_base::calculate_blur_radius();
    }
};

std::unique_ptr<wf_blur_base> create_gaussian_blur(wayfire_output *output)
{
    return nonstd::make_unique<wf_gaussian_blur> (output);
}
