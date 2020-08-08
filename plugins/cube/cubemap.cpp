#include "cubemap.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <config.h>
#include <wayfire/core.hpp>
#include <wayfire/img.hpp>

#include "cubemap-shaders.tpp"

wf_cube_background_cubemap::wf_cube_background_cubemap()
{
    create_program();
    reload_texture();
}

wf_cube_background_cubemap::~wf_cube_background_cubemap()
{
    OpenGL::render_begin();
    program.free_resources();
    OpenGL::render_end();
}

void wf_cube_background_cubemap::create_program()
{
    OpenGL::render_begin();
    program.set_simple(
        OpenGL::compile_program(cubemap_vertex, cubemap_fragment));
    OpenGL::render_end();
}

void wf_cube_background_cubemap::reload_texture()
{
    if (!last_background_image.compare(background_image))
    {
        return;
    }

    last_background_image = background_image;

    OpenGL::render_begin();
    if (tex == (uint32_t)-1)
    {
        GL_CALL(glGenTextures(1, &tex));
    }

    GL_CALL(glBindTexture(GL_TEXTURE_CUBE_MAP, tex));
    for (int i = 0; i < 6; i++)
    {
        if (!image_io::load_from_file(last_background_image,
            GL_TEXTURE_CUBE_MAP_POSITIVE_X + i))
        {
            LOGE("Failed to load cubemap background image from \"%s\".",
                last_background_image.c_str());

            GL_CALL(glDeleteTextures(1, &tex));
            tex = -1;
            break;
        }
    }

    if (tex != (uint32_t)-1)
    {
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
            GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER,
            GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S,
            GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T,
            GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R,
            GL_CLAMP_TO_EDGE));
    }

    GL_CALL(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
    OpenGL::render_end();
}

#include "cubemap-vertex-data.hpp"

void wf_cube_background_cubemap::render_frame(const wf::framebuffer_t& fb,
    wf_cube_animation_attribs& attribs)
{
    reload_texture();

    OpenGL::render_begin(fb);
    if (tex == (uint32_t)-1)
    {
        GL_CALL(glClearColor(TEX_ERROR_FLAG_COLOR));
        GL_CALL(glClear(GL_COLOR_BUFFER_BIT));
        OpenGL::render_end();

        return;
    }

    program.use(wf::TEXTURE_TYPE_RGBA);
    GL_CALL(glDepthMask(GL_FALSE));

    GL_CALL(glBindTexture(GL_TEXTURE_CUBE_MAP, tex));
    program.attrib_pointer("position", 3, 0, skyboxVertices);

    auto model = glm::rotate(glm::mat4(1.0),
        float(attribs.cube_animation.rotation * 0.7f),
        glm::vec3(0, 1, 0));

    glm::vec3 look_at{0.,
        (double)-attribs.cube_animation.offset_y,
        (double)attribs.cube_animation.offset_z};

    auto view = glm::lookAt(glm::vec3(0., 0., 0.), look_at, glm::vec3(0., 1., 0.));
    auto vp   = fb.transform * attribs.projection * view;

    model = vp * model;
    program.uniformMatrix4f("cubeMapMatrix", model);

    GL_CALL(glDrawArrays(GL_TRIANGLES, 0, 6 * 6));

    program.deactivate();
    GL_CALL(glDepthMask(GL_TRUE));
    OpenGL::render_end();
}
