#include <wayfire/util/log.hpp>
#include <fstream>
#include "opengl-priv.hpp"
#include "output.hpp"
#include "core-impl.hpp"
#include "config.h"

extern "C"
{
#define static
#include <wlr/render/egl.h>
#include <wlr/render/wlr_renderer.h>
#undef static
#include <wlr/types/wlr_output.h>
}

#include <glm/gtc/matrix_transform.hpp>

const char* gl_error_string(const GLenum err) {
    switch (err) {
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";
    }
    return "UNKNOWN GL ERROR";
}

void gl_call(const char *func, uint32_t line, const char *glfunc) {
    GLenum err;
    if ((err = glGetError()) == GL_NO_ERROR)
        return;

    LOGE("gles2: function ", glfunc, " in ", func, " line ", line, ": ",
        gl_error_string(glGetError()));
}

namespace OpenGL
{
    /* Different Context is kept for each output */
    /* Each of the following functions uses the currently bound context */
    struct
    {
        GLuint id;

        GLuint mvpID, colorID;
        GLuint position, uvPosition;
    } program;

    GLuint compile_shader_from_file(std::string path, std::string source, GLuint type)
    {
        GLuint shader = GL_CALL(glCreateShader(type));

        const char *c_src = source.c_str();
        GL_CALL(glShaderSource(shader, 1, &c_src, NULL));

        int s;
#define LENGTH 1024 * 128
        char b1[LENGTH];
        GL_CALL(glCompileShader(shader));
        GL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &s));
        GL_CALL(glGetShaderInfoLog(shader, LENGTH, NULL, b1));

        if (s == GL_FALSE)
        {
            LOGE("Failed to load shader from ", path, "\n; Errors:\n", b1);
            return -1;
        }

        return shader;
    }

    GLuint compile_shader(std::string source, GLuint type)
    {
        return compile_shader_from_file("internal", source, type);
    }

    GLuint load_shader(std::string path, GLuint type)
    {
        std::fstream file(path, std::ios::in);
        if(!file.is_open())
        {
            LOGE("cannot open shader file ", path);
            return -1;
        }

        std::string str, line;
        while(std::getline(file, line))
            str += line, str += '\n';

        return compile_shader(str.c_str(), type);
    }

    GLuint create_program_from_shaders(GLuint vertex_shader,
        GLuint fragment_shader)
    {
        auto result_program = GL_CALL(glCreateProgram());
        GL_CALL(glAttachShader(result_program, vertex_shader));
        GL_CALL(glAttachShader(result_program, fragment_shader));
        GL_CALL(glLinkProgram(result_program));

        /* won't be really deleted until program is deleted as well */
        GL_CALL(glDeleteShader(vertex_shader));
        GL_CALL(glDeleteShader(fragment_shader));

        return result_program;
    }

    GLuint create_program_from_source(std::string vertex_source,
        std::string frag_source)
    {
        return create_program_from_shaders(
            compile_shader(vertex_source, GL_VERTEX_SHADER),
            compile_shader(frag_source, GL_FRAGMENT_SHADER));
    }

    GLuint create_program(std::string vertex_path, std::string frag_path)
    {
        return create_program_from_shaders(
            load_shader(vertex_path, GL_VERTEX_SHADER),
            load_shader(frag_path, GL_FRAGMENT_SHADER));
    }

    void init()
    {
        render_begin();

        // enable_gl_synchronuous_debug()
        std::string shader_path = INSTALL_PREFIX "/share/wayfire/shaders";
        program.id = create_program(
            shader_path + "/vertex.glsl", shader_path + "/frag.glsl");

        program.mvpID      = GL_CALL(glGetUniformLocation(program.id, "MVP"));
        program.colorID    = GL_CALL(glGetUniformLocation(program.id, "color"));
        program.position   = GL_CALL(glGetAttribLocation(program.id, "position"));
        program.uvPosition = GL_CALL(glGetAttribLocation(program.id, "uvPosition"));

        render_end();
    }

    void fini()
    {
        render_begin();
        GL_CALL(glDeleteProgram(program.id));
        render_end();
    }

    namespace
    {
        wf::output_t *current_output = NULL;
    }

    void bind_output(wf::output_t *output)
    {
        current_output = output;
    }

    void unbind_output(wf::output_t *output)
    {
        current_output = NULL;
    }

    void render_transformed_texture(GLuint tex,
        const gl_geometry& g, const gl_geometry& texg,
        glm::mat4 model, glm::vec4 color, uint32_t bits)
    {
        GL_CALL(glUseProgram(program.id));

        gl_geometry final_g = g;
        if (bits & TEXTURE_TRANSFORM_INVERT_Y)
            std::swap(final_g.y1, final_g.y2);
        if (bits & TEXTURE_TRANSFORM_INVERT_X)
            std::swap(final_g.x1, final_g.x2);

        GLfloat vertexData[] = {
            final_g.x1, final_g.y2,
            final_g.x2, final_g.y2,
            final_g.x2, final_g.y1,
            final_g.x1, final_g.y1,
        };

        GLfloat coordData[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f,
        };

        if (bits & TEXTURE_USE_TEX_GEOMETRY) {
            coordData[0] = texg.x1; coordData[1] = texg.y2;
            coordData[2] = texg.x2; coordData[3] = texg.y2;
            coordData[4] = texg.x2; coordData[5] = texg.y1;
            coordData[6] = texg.x1; coordData[7] = texg.y1;
        }

        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
        GL_CALL(glActiveTexture(GL_TEXTURE0));

        GL_CALL(glVertexAttribPointer(program.position, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(program.position));

        GL_CALL(glVertexAttribPointer(program.uvPosition, 2, GL_FLOAT, GL_FALSE, 0, coordData));
        GL_CALL(glEnableVertexAttribArray(program.uvPosition));

        GL_CALL(glUniformMatrix4fv(program.mvpID, 1, GL_FALSE, &model[0][0]));
        GL_CALL(glUniform4fv(program.colorID, 1, &color[0]));

        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
        GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

        GL_CALL(glDisableVertexAttribArray(program.uvPosition));
        GL_CALL(glDisableVertexAttribArray(program.position));
    }

    void render_begin()
    {
        /* No real reason for 10, 10, 0 but it doesn't matter */
        render_begin(10, 10, 0);
    }

    void render_begin(const wf_framebuffer_base& fb)
    {
        render_begin(fb.viewport_width, fb.viewport_height, fb.fb);
    }

    void render_begin(int32_t viewport_width, int32_t viewport_height, uint32_t fb)
    {
        if (!current_output && !wlr_egl_is_current(wf::get_core_impl().egl))
            wlr_egl_make_current(wf::get_core_impl().egl, EGL_NO_SURFACE, NULL);

        wlr_renderer_begin(wf::get_core_impl().renderer,
            viewport_width, viewport_height);
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fb));
    }

    void clear(wf::color_t col, uint32_t mask)
    {
        GL_CALL(glClearColor(col.r, col.g, col.b, col.a));
        GL_CALL(glClear(mask));
    }

    void render_end()
    {
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        wlr_renderer_scissor(wf::get_core().renderer, NULL);
        wlr_renderer_end(wf::get_core().renderer);
    }
}

bool wf_framebuffer_base::allocate(int width, int height)
{
    bool first_allocate = false;
    if (fb == (uint32_t)-1)
    {
        first_allocate = true;
        GL_CALL(glGenFramebuffers(1, &fb));
    }

    if (tex == (uint32_t)-1)
    {

        first_allocate = true;
        GL_CALL(glGenTextures(1, &tex));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    }

    bool is_resize = false;
    /* Special case: fb = 0. This occurs in the default workspace streams, we don't resize anything */
    if (fb != 0)
    {
        if (first_allocate || width != viewport_width || height != viewport_height)
        {
            is_resize = true;
            GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
            GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, 0));
        }
    }

    if (first_allocate)
    {
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fb));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
        GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, tex, 0));
    }

    if (is_resize || first_allocate)
    {
        auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            LOGE("Failed to initialize framebuffer");
            return false;
        }
    }

    viewport_width = width;
    viewport_height = height;

    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

    return is_resize || first_allocate;
}

void wf_framebuffer_base::copy_state(wf_framebuffer_base&& other)
{
    this->viewport_width = other.viewport_width;
    this->viewport_height = other.viewport_height;

    this->fb = other.fb;
    this->tex = other.tex;

    other.reset();
}

wf_framebuffer_base::wf_framebuffer_base(wf_framebuffer_base&& other)
{
    copy_state(std::move(other));
}

wf_framebuffer_base& wf_framebuffer_base::operator = (wf_framebuffer_base&& other)
{
    if (this == &other)
        return *this;

    release();
    copy_state(std::move(other));

    return *this;
}

void wf_framebuffer_base::bind() const
{
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb));
    GL_CALL(glViewport(0, 0, viewport_width, viewport_height));
}

void wf_framebuffer_base::scissor(wlr_box box) const
{
    GL_CALL(glEnable(GL_SCISSOR_TEST));
    GL_CALL(glScissor(box.x, viewport_height - box.y - box.height,
                      box.width, box.height));
}

void wf_framebuffer_base::release()
{
    if (fb != uint32_t(-1) && fb != 0)
    {
        GL_CALL(glDeleteFramebuffers(1, &fb));
    }

    if (tex != uint32_t(-1) && (fb != 0 || tex != 0))
    {
        GL_CALL(glDeleteTextures(1, &tex));
    }

    reset();
}

void wf_framebuffer_base::reset()
{
    fb = -1;
    tex = -1;
    viewport_width = viewport_height = 0;
}

wlr_box wf_framebuffer::framebuffer_box_from_damage_box(wlr_box box) const
{
    if (has_nonstandard_transform)
    {
        // TODO: unimplemented, but also unused for now
        LOGE("unimplemented reached: framebuffer_box_from_geometry_box"
            " with has_nonstandard_transform");
        return {0, 0, 0, 0};
    }

    int width = viewport_width, height = viewport_height;
    if (wl_transform & 1)
        std::swap(width, height);

    wlr_box result = box;
    wl_output_transform transform =
        wlr_output_transform_invert((wl_output_transform)wl_transform);

   // LOGI("got %d,%d %dx%d, %d", box.x, box.y, box.width, box.height, wl_transform);
    wlr_box_transform(&result, &box, transform, width, height);
  //  LOGI("tr %d,%d %dx%d", box.x, box.y, box.width, box.height);
    return result;
}

wlr_box wf_framebuffer::damage_box_from_geometry_box(wlr_box box) const
{
    box.x = std::floor(box.x * scale);
    box.y = std::floor(box.y * scale);
    box.width = std::ceil(box.width * scale);
    box.height = std::ceil(box.height * scale);

    return box;
}

wlr_box wf_framebuffer::framebuffer_box_from_geometry_box(wlr_box box) const
{
    return framebuffer_box_from_damage_box(damage_box_from_geometry_box(box));
}

wf_region wf_framebuffer::get_damage_region() const
{
    return damage_box_from_geometry_box({0, 0, geometry.width, geometry.height});
}

glm::mat4 wf_framebuffer::get_orthographic_projection() const
{
    auto ortho = glm::ortho(1.0f * geometry.x,
        1.0f * geometry.x + 1.0f * geometry.width,
        1.0f * geometry.y + 1.0f * geometry.height,
        1.0f * geometry.y);

    return this->transform * ortho;
}

#define WF_PI 3.141592f

/* look up the actual values of wl_output_transform enum
 * All _flipped transforms have values (regular_transfrom + 4) */
glm::mat4 get_output_matrix_from_transform(wl_output_transform transform)
{
    glm::mat4 scale = glm::mat4(1.0);

    if (transform >= 4)
        scale = glm::scale(scale, {-1, 1, 0});

    /* remove the third bit if it's set */
    uint32_t rotation = transform & (~4);
    glm::mat4 rotation_matrix(1.0);

    if (rotation == WL_OUTPUT_TRANSFORM_90)
        rotation_matrix = glm::rotate(rotation_matrix, -WF_PI / 2.0f, {0, 0, 1});
    if (rotation == WL_OUTPUT_TRANSFORM_180)
        rotation_matrix = glm::rotate(rotation_matrix,  WF_PI,        {0, 0, 1});
    if (rotation == WL_OUTPUT_TRANSFORM_270)
        rotation_matrix = glm::rotate(rotation_matrix,  WF_PI / 2.0f, {0, 0, 1});

    return rotation_matrix * scale;
}
