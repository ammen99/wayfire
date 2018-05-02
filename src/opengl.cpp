#include <fstream>
#include "opengl.hpp"
#include "debug.hpp"
#include "output.hpp"
#include "render-manager.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace {
    OpenGL::context_t *bound;
}

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

    log_error("gles2: function %s in %s line %u: %s", glfunc, func, line, gl_error_string(glGetError()));
}

namespace OpenGL
{
    GLuint compile_shader_from_file(const char *path, const char *src, GLuint type)
    {
        GLuint shader = GL_CALL(glCreateShader(type));
        GL_CALL(glShaderSource(shader, 1, &src, NULL));

        int s;
        char b1[10000];
        GL_CALL(glCompileShader(shader));
        GL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &s));
        GL_CALL(glGetShaderInfoLog(shader, 10000, NULL, b1));

        if (s == GL_FALSE)
        {
            log_error("Failed to load shader from %s\n; Errors:\n%s", path, b1);
            return -1;
        }

        return shader;
    }

    GLuint compile_shader(const char *src, GLuint type)
    {
        return compile_shader_from_file("internal", src, type);
    }

    GLuint load_shader(const char *path, GLuint type) {

        std::fstream file(path, std::ios::in);

        if(!file.is_open())
        {
            log_error("cannot open shader file %s", path);
            std::exit(1);
        }

        std::string str, line;
        while(std::getline(file, line))
            str += line, str += '\n';

        return compile_shader(str.c_str(), type);
    }

    /*
    const char *getStrSrc(GLenum src) {
        if(src == GL_DEBUG_SOURCE_API            )return "API";
        if(src == GL_DEBUG_SOURCE_WINDOW_SYSTEM  )return "WINDOW_SYSTEM";
        if(src == GL_DEBUG_SOURCE_SHADER_COMPILER)return "SHADER_COMPILER";
        if(src == GL_DEBUG_SOURCE_THIRD_PARTY    )return "THIRD_PARTYB";
        if(src == GL_DEBUG_SOURCE_APPLICATION    )return "APPLICATIONB";
        if(src == GL_DEBUG_SOURCE_OTHER          )return "OTHER";
        else return "UNKNOWN";
    }

    const char *getStrType(GLenum type) {
        if(type==GL_DEBUG_TYPE_ERROR              )return "ERROR";
        if(type==GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR)return "DEPRECATED_BEHAVIOR";
        if(type==GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR )return "UNDEFINED_BEHAVIOR";
        if(type==GL_DEBUG_TYPE_PORTABILITY        )return "PORTABILITY";
        if(type==GL_DEBUG_TYPE_PERFORMANCE        )return "PERFORMANCE";
        if(type==GL_DEBUG_TYPE_OTHER              )return "OTHER";
        return "UNKNOWN";
    }

    const char *getStrSeverity(GLenum severity) {
        if(severity == GL_DEBUG_SEVERITY_HIGH  )return "HIGH";
        if(severity == GL_DEBUG_SEVERITY_MEDIUM)return "MEDIUM";
        if(severity == GL_DEBUG_SEVERITY_LOW   )return "LOW";
        if(severity == GL_DEBUG_SEVERITY_NOTIFICATION) return "NOTIFICATION";
        return "UNKNOWN";
    }

    void errorHandler(GLenum src, GLenum type,
            GLuint id, GLenum severity,
            GLsizei len, const GLchar *msg,
            const void *dummy) {
        // ignore notifications
        if(severity == GL_DEBUG_SEVERITY_NOTIFICATION)
            return;
        debug << "_______________________________________________\n";
        debug << "GLES debug: \n";
        debug << "Source: " << getStrSrc(src) << std::endl;
        debug << "Type: " << getStrType(type) << std::endl;
        debug << "ID: " << id << std::endl;
        debug << "Severity: " << getStrSeverity(severity) << std::endl;
        debug << "Msg: " << msg << std::endl;;
        debug << "_______________________________________________\n";
    } */

    context_t* create_gles_context(wayfire_output *output, const char *shaderSrcPath)
    {
        context_t *ctx = new context_t;
        ctx->output = output;

        /*
        if (file_debug == &file_info) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(errorHandler, 0);
        } */

        GLuint vss = load_shader(std::string(shaderSrcPath)
                    .append("/vertex.glsl").c_str(),
                     GL_VERTEX_SHADER);

        GLuint fss = load_shader(std::string(shaderSrcPath)
                    .append("/frag.glsl").c_str(),
                     GL_FRAGMENT_SHADER);

        ctx->program = GL_CALL(glCreateProgram());

        GL_CALL(glAttachShader(ctx->program, vss));
        GL_CALL(glAttachShader(ctx->program, fss));
        GL_CALL(glLinkProgram(ctx->program));
        GL_CALL(glUseProgram(ctx->program));

        ctx->mvpID   = GL_CALL(glGetUniformLocation(ctx->program, "MVP"));
        ctx->colorID = GL_CALL(glGetUniformLocation(ctx->program, "color"));

        glm::mat4 identity;
        GL_CALL(glUniformMatrix4fv(ctx->mvpID, 1, GL_FALSE, &identity[0][0]));

        ctx->position   = GL_CALL(glGetAttribLocation(ctx->program, "position"));
        ctx->uvPosition = GL_CALL(glGetAttribLocation(ctx->program, "uvPosition"));
        return ctx;
    }

    void use_default_program() {
         GL_CALL(glUseProgram(bound->program));
    }

    void bind_context(context_t *ctx) {
        bound = ctx;

        bound->width  = ctx->output->handle->width;
        bound->height = ctx->output->handle->height;
    }

    wf_geometry get_device_viewport()
    {
        return {0, 0, bound->width, bound->height};
    }

    void use_device_viewport()
    {
        const auto vp = get_device_viewport();
        GL_CALL(glViewport(vp.x, vp.y, vp.width, vp.height));
    }

    void release_context(context_t *ctx)
    {
        delete ctx;
    }

    void render_texture(GLuint tex, const gl_geometry& g,
            const gl_geometry& texg, uint32_t bits)
    {
        render_transformed_texture(tex, g, texg, glm::mat4(1.0f), glm::vec4(1.0), bits);
    }

    void render_transformed_texture(GLuint tex,
                                    const gl_geometry& g,
                                    const gl_geometry& texg,
                                    glm::mat4 model,
                                    glm::vec4 color,
                                    uint32_t bits)
    {
        /* TODO: perhaps clean many of the flags, as we won't use them anymore in wlroots */
        if ((bits & DONT_RELOAD_PROGRAM) == 0)
            GL_CALL(glUseProgram(bound->program));

        if ((bits & TEXTURE_TRANSFORM_USE_DEVCOORD))
            use_device_viewport();

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
            0.0f, 1.0f,
            1.0f, 1.0f,
            1.0f, 0.0f,
            0.0f, 0.0f,
        };

        if (bits & TEXTURE_USE_TEX_GEOMETRY) {
            coordData[0] = texg.x1; coordData[1] = texg.y2;
            coordData[2] = texg.x2; coordData[3] = texg.y2;
            coordData[4] = texg.x2; coordData[5] = texg.y1;
            coordData[6] = texg.x1; coordData[7] = texg.y1;
        }

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
        GL_CALL(glActiveTexture(GL_TEXTURE0));

        GL_CALL(glVertexAttribPointer(bound->position, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(bound->position));

        GL_CALL(glVertexAttribPointer(bound->uvPosition, 2, GL_FLOAT, GL_FALSE, 0, coordData));
        GL_CALL(glEnableVertexAttribArray(bound->uvPosition));

        GL_CALL(glUniformMatrix4fv(bound->mvpID, 1, GL_FALSE, &model[0][0]));
        GL_CALL(glUniform4fv(bound->colorID, 1, &color[0]));
        GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        GL_CALL(glDrawArrays (GL_TRIANGLE_FAN, 0, 4));

        GL_CALL(glDisableVertexAttribArray(bound->position));
        GL_CALL(glDisableVertexAttribArray(bound->uvPosition));
    }

    void prepare_framebuffer(GLuint &fbuff, GLuint &texture,
                             float scale_x, float scale_y)
    {
        if (fbuff == (uint)-1)
            GL_CALL(glGenFramebuffers(1, &fbuff));
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fbuff));

        bool existing_texture = (texture != (uint)-1);
        if (!existing_texture)
            GL_CALL(glGenTextures(1, &texture));

        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));

        if (!existing_texture)
            GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                        bound->width * scale_x, bound->height * scale_y,
                        0, GL_RGBA, GL_UNSIGNED_BYTE, 0));

        GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                texture, 0));

        auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
        if (status != GL_FRAMEBUFFER_COMPLETE)
            log_error("failed to initialize framebuffer");
    }

    void prepare_framebuffer_size(int w, int h,
                                  GLuint &fbuff, GLuint &texture,
                                  float scale_x, float scale_y)
    {
        log_info("new fb %d %d %u %u", w, h, fbuff, texture);
        GL_CALL(glGenFramebuffers(1, &fbuff));
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fbuff));

        GL_CALL(glGenTextures(1, &texture));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));

        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, 0));

        GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                texture, 0));

        auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));

        log_info("gl::: !!!! %d %d",
                 glCheckFramebufferStatus(GL_FRAMEBUFFER), GL_FRAMEBUFFER_COMPLETE);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            log_error("failed to initialize framebuffer");
        else
            log_error("initialized framebuffer %u with attachment %u", fbuff, texture);
    }


    GLuint duplicate_texture(GLuint tex, int w, int h)
    {
        GLuint dst_tex = -1;

        GLuint dst_fbuff = -1, src_fbuff = -1;

        prepare_framebuffer(dst_fbuff, dst_tex);
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, 0));

        prepare_framebuffer(src_fbuff, tex);

        GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbuff));
        GL_CALL(glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR));

        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

        GL_CALL(glDeleteFramebuffers(1, &dst_fbuff));
        GL_CALL(glDeleteFramebuffers(1, &src_fbuff));
        return dst_tex;
    }
}
