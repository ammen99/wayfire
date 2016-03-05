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

namespace OpenGL {
    extern bool      transformed;
    extern int       depth;
    extern glm::vec4 color;

    extern int VersionMinor, VersionMajor;

    void initOpenGL(const char *shaderSrcPath);

    void renderTransformedTexture(GLuint text, const wlc_geometry& g, glm::mat4 transform = glm::mat4(), uint32_t bits = 0);
    void renderTexture(GLuint tex, const wlc_geometry& g, uint32_t bits);
    void set_transform(glm::mat4 tr);

    void preStage();
    void preStage(GLuint fbuff);
    void endStage();

    GLuint loadShader(const char *path, GLuint type);
    GLuint compileShader(const char* src, GLuint type);

    void prepareFramebuffer(GLuint &fbuff, GLuint &texture);
    GLuint getTex();

    /* set program to current program */
    void useDefaultProgram();

    /* reset OpenGL state */
    void reset_gl();
}


#endif
