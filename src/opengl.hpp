#ifndef DRIVER_H
#define DRIVER_H

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <map>
#include "output.hpp"

void gl_call(const char*, uint32_t, const char*);

#ifndef __STRING
#  define __STRING(x) #x
#endif

/* recommended to use this to make OpenGL calls, since it offers easier debugging */
/* This macro is taken from WLC source code */
#define GL_CALL(x) x; gl_call(__PRETTY_FUNCTION__, __LINE__, __STRING(x))

#define TEXTURE_TRANSFORM_INVERT_X     (1 << 0)
#define TEXTURE_TRANSFORM_INVERT_Y     (1 << 1)
#define TEXTURE_TRANSFORM_USE_COLOR    (1 << 2)
#define TEXTURE_TRANSFORM_USE_DEVCOORD (1 << 3)
#define TEXTURE_USE_TEX_GEOMETRY       (1 << 4)
#define DONT_RELOAD_PROGRAM            (1 << 5)

namespace OpenGL {

    /* all are relative coordinates scaled to [0, 1] */
    struct texture_geometry {
        float x1, y1, x2, y2;
    };

    /* Different Context is kept for each output */
    /* Each of the following functions uses the currently bound context */
    struct context_t {
        GLuint program, min_program;

        GLuint mvpID, colorID;
        GLuint position, uvPosition;

        GLuint w2ID, h2ID;

        wayfire_output *output;
        int32_t width, height;
        int32_t device_width, device_height;
    };

    context_t* create_gles_context(wayfire_output *output, const char *shader_src_path);
    void bind_context(context_t* ctx);
    void release_context(context_t *ctx);

    /* texg arguments are used only when bits has USE_TEX_GEOMETRY
     * if you don't wish to use them, simply pass {} as argument */
    void render_transformed_texture(GLuint text, const weston_geometry& g,
            const texture_geometry& texg, glm::mat4 transform = glm::mat4(),
            glm::vec4 color = glm::vec4(1.f), uint32_t bits = 0);
    void render_texture(GLuint tex, const weston_geometry& g,
            const texture_geometry& texg, uint32_t bits);

    GLuint duplicate_texture(GLuint source_tex, int w, int h);

    GLuint load_shader(const char *path, GLuint type);
    GLuint compile_shader(const char *src, GLuint type);

    void prepare_framebuffer(GLuint& fbuff, GLuint& texture,
            float scale_x = 1, float scale_y = 1);

    /* set program to current program */
    void use_default_program();
}

#endif
