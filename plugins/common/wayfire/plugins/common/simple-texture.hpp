#pragma once
#include <wayfire/opengl.hpp>
#include <wayfire/nonstd/noncopyable.hpp>

namespace wf
{
struct simple_texture_t : public noncopyable_t
{
    GLuint tex = -1;
    int width  = 0;
    int height = 0;

    /**
     * Destroy the GL texture.
     * This will call OpenGL::render_begin()/end() internally.
     */
    void release()
    {
        if (this->tex == (GLuint) - 1)
        {
            return;
        }

        OpenGL::render_begin();
        GL_CALL(glDeleteTextures(1, &tex));
        OpenGL::render_end();
        this->tex = -1;
    }

    /** Auto-release the texture when the object is destroyed */
    ~simple_texture_t()
    {
        release();
    }
};
}
