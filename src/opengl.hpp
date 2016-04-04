#ifndef DRIVER_H
#define DRIVER_H

#include "core.hpp"

void gl_call(const char*, uint32_t, const char*);

#ifndef __STRING
#  define __STRING(x) #x
#endif

/* recommended to use this to make OpenGL calls, since it offers easier debugging */
/* This macro is taken from WLC source code */
#define GL_CALL(x) x; gl_call(__PRETTY_FUNCTION__, __LINE__, __STRING(x))

#define TEXTURE_TRANSFORM_INVERT_Y 1
#define TEXTURE_TRANSFORM_USE_COLOR (1 << 1)

namespace OpenGL {

    /* Different Context is kept for each output */
    /* Each of the following functions uses the currently bound context */
    struct Context {
        GLuint program;
        glm::vec4 color;
        GLuint mvpID, colorID;
        GLuint position, uvPosition;

        int32_t width, height;
    };

    Context* init_opengl(Output *output, const char *shaderSrcPath);
    void bind_context(Context* ctx);
    void release_context(Context *ctx);

    void renderTransformedTexture(GLuint text, const wlc_geometry& g,
            glm::mat4 transform = glm::mat4(), uint32_t bits = 0);
    void renderTexture(GLuint tex, const wlc_geometry& g, uint32_t bits);
    GLuint duplicate_texture(GLuint source_tex, const wlc_geometry& g);

    GLuint loadShader(const char *path, GLuint type);
    GLuint compileShader(const char* src, GLuint type);

    void prepareFramebuffer(GLuint &fbuff, GLuint &texture);

    /* set program to current program */
    void useDefaultProgram();
}


#endif
