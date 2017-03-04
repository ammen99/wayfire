#ifndef DRIVER_H
#define DRIVER_H

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <map>
#include "view.hpp"

void gl_call(const char*, uint32_t, const char*);

#ifndef __STRING
#  define __STRING(x) #x
#endif

/* recommended to use this to make OpenGL calls, since it offers easier debugging */
/* This macro is taken from WLC source code */
#define GL_CALL(x) x; gl_call(__PRETTY_FUNCTION__, __LINE__, __STRING(x))

#define TEXTURE_TRANSFORM_INVERT_Y  (1 << 0)
#define TEXTURE_TRANSFORM_USE_COLOR (1 << 1)

namespace OpenGL {

    /* Different Context is kept for each output */
    /* Each of the following functions uses the currently bound context */
    struct context_t {
        GLuint program, min_program;

        GLuint mvpID, colorID;
        GLuint position, uvPosition;

        int32_t width, height;
    };

    context_t* create_gles_context(wayfire_output *output, const char *shader_src_path);
    void bind_context(context_t* ctx);
    void release_context(context_t *ctx);

    void render_transformed_texture(GLuint text, const wayfire_geometry& g,
            glm::mat4 transform = glm::mat4(), glm::vec4 color = glm::vec4(1.f),
            uint32_t bits = 0);
    void render_texture(GLuint tex, const wayfire_geometry& g, uint32_t bits);

    GLuint duplicate_texture(GLuint source_tex, int w, int h);

    GLuint load_shader(const char *path, GLuint type);
    GLuint compile_shader(const char *src, GLuint type);

#ifdef USE_GLES3
    void prepare_framebuffer(GLuint& fbuff, GLuint& texture);
#endif

    /* set program to current program */
    void use_default_program();
}

#endif
