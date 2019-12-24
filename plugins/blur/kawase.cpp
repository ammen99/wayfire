#include "blur.hpp"

static const char* kawase_vertex_shader = R"(
#version 100
attribute mediump vec2 position;

varying mediump vec2 uv;

void main() {
    gl_Position = vec4(position.xy, 0.0, 1.0);
    uv = (position.xy + vec2(1.0, 1.0)) / 2.0;
})";

static const char* kawase_fragment_shader_down = R"(
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

static const char* kawase_fragment_shader_down_up = R"(
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

static const wf_blur_default_option_values kawase_defaults = {
    .algorithm_name = "kawase",
    .offset = "5",
    .degrade = "1",
    .iterations = "2"
};

class wf_kawase_blur : public wf_blur_base
{
    GLuint posID[2], offsetID[2], halfpixelID[2];

    public:
    void get_id_locations(int i)
    {
        posID[i]    = GL_CALL(glGetAttribLocation(program[i], "position"));
        offsetID[i] = GL_CALL(glGetUniformLocation(program[i], "offset"));
        halfpixelID[i]   = GL_CALL(glGetUniformLocation(program[i], "halfpixel"));
    }

    wf_kawase_blur(wf::output_t *output)
        : wf_blur_base(output, kawase_defaults)
    {
        OpenGL::render_begin();
        program[0] = OpenGL::create_program_from_source(kawase_vertex_shader,
            kawase_fragment_shader_down);
        program[1] = OpenGL::create_program_from_source(kawase_vertex_shader,
            kawase_fragment_shader_down_up);
        get_id_locations(0);
        get_id_locations(1);
        OpenGL::render_end();
    }

    int blur_fb0(int width, int height) override
    {
        int iterations = iterations_opt;
        float offset = offset_opt;
        int sampleWidth, sampleHeight;

        /* Upload data to shader */
        static const float vertexData[] = {
            -1.0f, -1.0f,
             1.0f, -1.0f,
             1.0f,  1.0f,
            -1.0f,  1.0f
        };

        OpenGL::render_begin();

        /* Downsample */
        GL_CALL(glUseProgram(program[0]));
        GL_CALL(glVertexAttribPointer(posID[0], 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(posID[0]));
        /* Disable blending, because we may have transparent background, which
         * we want to render on uncleared framebuffer */
        GL_CALL(glDisable(GL_BLEND));

        GL_CALL(glUniform1f(offsetID[0], offset));

        for (int i = 0; i < iterations; i++){
            sampleWidth = width / (1 << i);
            sampleHeight = height / (1 << i);

            GL_CALL(glUniform2f(halfpixelID[0], 0.5f / sampleWidth, 0.5f / sampleHeight));
            render_iteration(fb[i % 2], fb[1 - i % 2], sampleWidth, sampleHeight);
        }

        GL_CALL(glDisableVertexAttribArray(posID[0]));

        /* Upsample */
        GL_CALL(glUseProgram(program[1]));
        GL_CALL(glVertexAttribPointer(posID[1], 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(posID[1]));

        GL_CALL(glUniform1f(offsetID[1], offset));

        for (int i = iterations - 1; i >= 0; i--) {
            sampleWidth = width / (1 << i);
            sampleHeight = height / (1 << i);

            GL_CALL(glUniform2f(halfpixelID[1], 0.5f / sampleWidth, 0.5f / sampleHeight));
            render_iteration(fb[1 - i % 2], fb[i % 2], sampleWidth, sampleHeight);
        }

        /* Reset gl state */
        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

        GL_CALL(glUseProgram(0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        GL_CALL(glDisableVertexAttribArray(posID[1]));
        OpenGL::render_end();

        return 0;
    }

    int calculate_blur_radius() override
    {
        return pow(2, iterations_opt + 1) * offset_opt * degrade_opt;
    }
};

std::unique_ptr<wf_blur_base> create_kawase_blur(wf::output_t *output)
{
    return std::make_unique<wf_kawase_blur> (output);
}
