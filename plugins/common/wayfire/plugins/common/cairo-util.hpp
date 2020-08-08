#pragma once

#include <wayfire/plugins/common/simple-texture.hpp>
#include <cairo.h>

namespace wf
{
struct simple_texture_t;
}

/**
 * Upload the data from the cairo surface to the OpenGL texture.
 *
 * @param surface The source cairo surface.
 * @param buffer  The buffer to upload data to.
 */
static void cairo_surface_upload_to_texture(
    cairo_surface_t *surface, wf::simple_texture_t& buffer)
{
    buffer.width  = cairo_image_surface_get_width(surface);
    buffer.height = cairo_image_surface_get_height(surface);
    if (buffer.tex == (GLuint) - 1)
    {
        GL_CALL(glGenTextures(1, &buffer.tex));
    }

    auto src = cairo_image_surface_get_data(surface);

    GL_CALL(glBindTexture(GL_TEXTURE_2D, buffer.tex));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
    GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
        buffer.width, buffer.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, src));
}
