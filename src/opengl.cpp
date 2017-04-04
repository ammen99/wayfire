#include "opengl.hpp"
#include "output.hpp"

/* TODO: do not use GLES 3.2, it is used for debugging, should be removed in production */
#include <GLES3/gl32.h>

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

    debug << "gles: function " << func << " at line " << line << ": \n" << glfunc << " == " << gl_error_string(err) << "\n";
}

namespace OpenGL {
    GLuint compile_shader(const char *src, GLuint type) {
        printf("compile shader\n");

        GLuint shader = GL_CALL(glCreateShader(type));
        GL_CALL(glShaderSource(shader, 1, &src, NULL));

        int s;
        char b1[10000];
        GL_CALL(glCompileShader(shader));
        GL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &s));
        GL_CALL(glGetShaderInfoLog(shader, 10000, NULL, b1));

        if ( s == GL_FALSE ) {

            errio << "shader compilation failed!\n"
                    "src: ***************************\n" <<
                    src <<
                    "********************************\n" <<
                    b1 <<
                    "********************************\n";
            return -1;
        }
        return shader;
    }

    GLuint load_shader(const char *path, GLuint type) {

        std::fstream file(path, std::ios::in);
        if(!file.is_open()) {
            errio << "Cannot open shader file " << path << ". Aborting\n";
            std::exit(1);
        }

        std::string str, line;

        while(std::getline(file, line))
            str += line, str += '\n';

        return compile_shader(str.c_str(), type);
    }

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
    }

    context_t* create_gles_context(wayfire_output *output, const char *shaderSrcPath) {
        context_t *ctx = new context_t;

        if (file_debug == &file_info) { /* we are in debug mode, so add debug output */
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(errorHandler, 0);
        }

        ctx->width = output->handle->width;
        ctx->height = output->handle->height;

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

        ctx->w2ID = GL_CALL(glGetUniformLocation(ctx->program, "w2"));
        ctx->h2ID = GL_CALL(glGetUniformLocation(ctx->program, "h2"));

        glUniform1f(ctx->w2ID, ctx->width / 2.);
        glUniform1f(ctx->h2ID, ctx->height / 2.);

        ctx->position   = GL_CALL(glGetAttribLocation(ctx->program, "position"));
        ctx->uvPosition = GL_CALL(glGetAttribLocation(ctx->program, "uvPosition"));
        return ctx;
    }

    void use_default_program() {
         GL_CALL(glUseProgram(bound->program));
    }

    void bind_context(context_t *ctx) {
        bound = ctx;
    }

    void release_context(context_t *ctx) {
        glDeleteProgram(ctx->program);
        delete ctx;
    }

    void render_texture(GLuint tex, const wayfire_geometry& g, uint32_t bits) {
        if ((bits & DONT_RELOAD_PROGRAM) == 0) {
            GL_CALL(glUseProgram(bound->program));
        }

        float w2 = float(bound->width) / 2.;
        float h2 = float(bound->height) / 2.;

        float tlx = float(g.origin.x) - w2,
              tly = h2 - float(g.origin.y);

        float w = g.size.w;
        float h = g.size.h;

        if(bits & TEXTURE_TRANSFORM_INVERT_Y) {
            h   *= -1;
            tly += h;
        }

        /* TODO: use TRIANGLE_FAN to make less data uploaded */
        GLfloat vertexData[] = {
            tlx    , tly - h, 0.f, // 1
            tlx + w, tly - h, 0.f, // 2
            tlx + w, tly    , 0.f, // 3
            tlx + w, tly    , 0.f, // 3
            tlx    , tly    , 0.f, // 4
            tlx    , tly - h, 0.f, // 1
        };

        GLfloat coordData[] = {
            0.0f, 1.0f,
            1.0f, 1.0f,
            1.0f, 0.0f,
            1.0f, 0.0f,
            0.0f, 0.0f,
            0.0f, 1.0f,
        };

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        GL_CALL(glViewport(0, 0, bound->width, bound->height));

        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
        GL_CALL(glActiveTexture(GL_TEXTURE0));

        GL_CALL(glVertexAttribPointer(bound->position, 3, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(bound->position));

        GL_CALL(glVertexAttribPointer(bound->uvPosition, 2, GL_FLOAT, GL_FALSE, 0, coordData));
        GL_CALL(glEnableVertexAttribArray(bound->uvPosition));

        GL_CALL(glDrawArrays (GL_TRIANGLES, 0, 6));

        GL_CALL(glDisableVertexAttribArray(bound->position));
        GL_CALL(glDisableVertexAttribArray(bound->uvPosition));
    }

    void render_transformed_texture(GLuint tex, const wayfire_geometry& g, glm::mat4 model, glm::vec4 color, uint32_t bits) {
        GL_CALL(glUseProgram(bound->program));

        GL_CALL(glUniformMatrix4fv(bound->mvpID, 1, GL_FALSE, &model[0][0]));
        GL_CALL(glUniform4fv(bound->colorID, 1, &color[0]));

        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        render_texture(tex, g, bits | DONT_RELOAD_PROGRAM);
        GL_CALL(glDisable(GL_BLEND));
    }

    void prepare_framebuffer(GLuint &fbuff, GLuint &texture) {
        if (fbuff != (uint)-1)
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
            GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bound->width, bound->height,
                        0, GL_RGBA, GL_UNSIGNED_BYTE, 0));

        GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                texture, 0));

        auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
        if (status != GL_FRAMEBUFFER_COMPLETE)
            errio << "Error in framebuffer!\n";
    }


    const char *simple_vs =
R"(
#version 100
attribute mediump vec2 position;
attribute highp vec2 uvPosition;

varying highp vec2 uvpos;
uniform int time;

void main() {
    gl_Position = vec4(position, 0.0, 0.0);
    uvpos = uvPosition;
}
)";

const char *simple_fs =
R"(
#version 100
varying highp vec2 uvpos;
uniform sampler2D smp;
void main() {
    gl_FragColor = texture2D(smp, uvpos);
}
)";

    GLuint duplicate_texture(GLuint tex, int w, int h) {
        GLuint dst_tex = -1;

#ifdef USE_GLES3
        GLuint dst_fbuff = -1, src_fbuff = -1;
        prepare_framebuffer(dst_fbuff, dst_tex);
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h,
                0, GL_RGBA, GL_UNSIGNED_BYTE, 0));

        prepare_framebuffer(src_fbuff, tex);

        GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbuff));
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbuff));

//        if (bound->framebuffer_support) {
#ifdef USE_GLES3

            GL_CALL(glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR));
#endif
 //       }

        render_transformed_texture(tex, {{0, 0}, {int32_t(w), int32_t(h)}});
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
        GL_CALL(glDeleteFramebuffers(1, &dst_fbuff));
        GL_CALL(glDeleteFramebuffers(1, &src_fbuff));
#else
//        GL_CALL(glUseProgram(bound->min_program))
    //TODO: implement support for non gles3 systems
#endif
        return dst_tex;
    }

}
