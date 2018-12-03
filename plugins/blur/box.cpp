#include "blur.hpp"
#include "debug.hpp"

static const char* box_vertex_shader =
R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 texcoord;
uniform vec2 size;
uniform float offset;

varying highp vec2 blurcoord[10];

uniform mat4 mvp;

void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);

    blurcoord[0] = texcoord;
    blurcoord[1] = texcoord + vec2(1.0 * offset) / size;
    blurcoord[2] = texcoord - vec2(1.0 * offset) / size;
    blurcoord[3] = texcoord + vec2(2.0 * offset) / size;
    blurcoord[4] = texcoord - vec2(2.0 * offset) / size;
    blurcoord[5] = texcoord + vec2(3.0 * offset) / size;
    blurcoord[6] = texcoord - vec2(3.0 * offset) / size;
    blurcoord[7] = texcoord + vec2(4.0 * offset) / size;
    blurcoord[8] = texcoord - vec2(4.0 * offset) / size;
    blurcoord[9] = vec4(mvp * vec4(texcoord - 0.5, 0.0, 1.0)).xy + 0.5;
}
)";

static const char* box_fragment_shader =
R"(
#version 100
precision mediump float;

uniform sampler2D window_texture;
uniform sampler2D bg_texture;
uniform int mode;

varying highp vec2 blurcoord[10];

void main()
{
    vec2 uv = blurcoord[0];

    if (mode == 0) {
        vec4 bp = vec4(0.0);
        for(int i = 0; i < 9; i++) {
            vec2 uv = vec2(blurcoord[i].x, uv.y);
            bp += texture2D(bg_texture, uv);
        }
        gl_FragColor = vec4(bp.rgb / 9.0, 1.0);
    } else if (mode == 1) {
        vec4 bp = vec4(0.0);
        for(int i = 0; i < 9; i++) {
            vec2 uv = vec2(uv.x, blurcoord[i].y);
            bp += texture2D(bg_texture, uv);
        }
        gl_FragColor = vec4(bp.rgb / 9.0, 1.0);
    } else {
        vec4 wp = texture2D(window_texture, blurcoord[9]);
        vec4 bp = texture2D(bg_texture, uv);
        vec4 c = clamp(4.0 * wp.a, 0.0, 1.0) * bp;
        gl_FragColor = wp + (1.0 - wp.a) * c;
    }
}
)";

static const wf_blur_default_option_values box_defaults = {
    .algorithm_name = "box",
    .offset = "2",
    .degrade = "1",
    .iterations = "2"
};

class wf_box_blur : public wf_blur_base
{
    GLuint posID, mvpID, texcoordID,
           modeID, sizeID, offsetID, texID[2];

    public:
    wf_box_blur(wayfire_output *output) : wf_blur_base(output, box_defaults)
    {
        OpenGL::render_begin();
        program = OpenGL::create_program_from_source(
            box_vertex_shader, box_fragment_shader);

        posID      = GL_CALL(glGetAttribLocation(program, "position"));
        texcoordID = GL_CALL(glGetAttribLocation(program, "texcoord"));

        mvpID    = GL_CALL(glGetUniformLocation(program, "mvp"));
        sizeID   = GL_CALL(glGetUniformLocation(program, "size"));
        modeID   = GL_CALL(glGetUniformLocation(program, "mode"));
        texID[0] = GL_CALL(glGetUniformLocation(program, "window_texture"));
        texID[1] = GL_CALL(glGetUniformLocation(program, "bg_texture"));
        offsetID = GL_CALL(glGetUniformLocation(program, "offset"));
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
        static const float texCoords[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f
        };

        OpenGL::render_begin();
        /* Enable our shader and pass some data to it. The shader accepts two textures
         * and does box blur on the background texture in two passes, one horizontal
         * and one vertical */
        GL_CALL(glUseProgram(program));
        GL_CALL(glUniform1i(texID[0], 0));
        GL_CALL(glUniform1i(texID[1], 1));
        GL_CALL(glUniform2f(sizeID, width, height));
        GL_CALL(glUniform1f(offsetID, offset));

        GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &glm::mat4(1.0)[0][0]));
        GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glVertexAttribPointer(texcoordID, 2, GL_FLOAT, GL_FALSE, 0, texCoords));
        GL_CALL(glEnableVertexAttribArray(texcoordID));
        GL_CALL(glEnableVertexAttribArray(posID));

        for (i = 0; i < iterations; i++) {
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
        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glDisableVertexAttribArray(posID));
        GL_CALL(glDisableVertexAttribArray(texcoordID));

        OpenGL::render_end();
        return 0;
    }

    virtual int calculate_blur_radius()
    {
        return 4 * wf_blur_base::calculate_blur_radius();
    }
};

std::unique_ptr<wf_blur_base> create_box_blur(wayfire_output *output)
{
    return nonstd::make_unique<wf_box_blur> (output);
}
