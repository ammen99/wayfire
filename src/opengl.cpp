#include "opengl.hpp"
#include "output.hpp"


namespace {
    OpenGL::Context *bound;
}

const char* gl_error_string(const GLenum error) {
    switch (error) {
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
    GLenum error;
    if ((error = glGetError()) == GL_NO_ERROR)
        return;

    printf("gles2: function %s at line %u: %s == %s\n", func, line, glfunc, gl_error_string(error));
}


namespace OpenGL {
    GLuint compileShader(const char *src, GLuint type) {
        printf("compile shader\n");

        GLuint shader = GL_CALL(glCreateShader(type));
        GL_CALL(glShaderSource(shader, 1, &src, NULL));

        int s;
        char b1[10000];
        GL_CALL(glCompileShader(shader));
        GL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &s));
        GL_CALL(glGetShaderInfoLog(shader, 10000, NULL, b1));


        if ( s == GL_FALSE ) {
            printf("shader compilation failed!\n"
                    "src: ***************************\n"
                    "%s\n"
                    "********************************\n"
                    "%s\n"
                    "********************************\n", src, b1);
            return -1;
        }
        return shader;
    }

    GLuint loadShader(const char *path, GLuint type) {

        std::fstream file(path, std::ios::in);
        if(!file.is_open())
            printf("Cannot open shader file %s. Aborting\n", path),
                std::exit(1);

        std::string str, line;

        while(std::getline(file, line))
            str += line, str += '\n';

        return compileShader(str.c_str(), type);
    }

    void renderTexture(GLuint tex, const wlc_geometry& g, uint32_t bits) {
        float w2 = float(1366) / 2.;
        float h2 = float(768) / 2.;

        float tlx = float(g.origin.x) - w2,
              tly = h2 - float(g.origin.y);

        float w = g.size.w;
        float h = g.size.h;

        if(bits & TEXTURE_TRANSFORM_INVERT_Y) {
            h   *= -1;
            tly += h;
        }

        auto no_color_vector = glm::vec4(1, 1, 1, 1);
        if(bits & TEXTURE_TRANSFORM_USE_COLOR) {
            GL_CALL(glUniform4fv(bound->colorID, 1, &bound->color[0]));
            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        } else {
            GL_CALL(glUniform4fv(bound->colorID, 1, &no_color_vector[0]));
        }

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

        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
        GL_CALL(glVertexAttribPointer(bound->position, 3, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glVertexAttribPointer(bound->uvPosition, 2, GL_FLOAT, GL_FALSE, 0, coordData));
        GL_CALL(glDrawArrays (GL_TRIANGLES, 0, 6));
    }

    void renderTransformedTexture(GLuint tex, const wlc_geometry& g, glm::mat4 Model, uint32_t bits) {
        GL_CALL(glUniformMatrix4fv(bound->mvpID, 1, GL_FALSE, &Model[0][0]));
        renderTexture(tex, g, bits);
    }

    void prepareFramebuffer(GLuint &fbuff, GLuint &texture) {
        GL_CALL(glGenFramebuffers(1, &fbuff));
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fbuff));

        bool existing_texture = texture != (uint)-1;
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
            std::cout << "Error in framebuffer !!!" << std::endl;
    }

    void useDefaultProgram() {
         GL_CALL(glUseProgram(bound->program));
    }

    void bind_context(Context *ctx) {
        bound = ctx;
        GL_CALL(glUseProgram(ctx->program));
    }

    void release_context(Context *ctx) {
        delete ctx;
    }

    const char *simple_vs =
R"(#version 100
attribute mediump vec2 position;
attribute highp vec2 uvPosition;

varying highp vec2 uvpos;

uniform int time;

void main() {

    gl_Position = vec4(position, 0.0, 0.0);
    uvpos = uvPosition;
})";

const char *simple_fs =
R"(#version 100
        varying highp vec2 uvpos;
        uniform sampler2D smp;
        void main() {
            gl_FragColor = texture2D(smp, uvpos);
        }
)";

    GLuint duplicate_texture(GLuint tex, int w, int h) {
        GLuint dst_fbuff = -1, src_fbuff = -1, dst_tex = -1;

        prepareFramebuffer(dst_fbuff, dst_tex);
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h,
                0, GL_RGBA, GL_UNSIGNED_BYTE, 0));

        prepareFramebuffer(src_fbuff, tex);

        GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbuff));
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fbuff));
        GL_CALL(glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR));

//        GL_CALL(glUseProgram(bound->min_program));
//
//        GLfloat vertexData[] = {
//            -1,  1,
//            -1, -1,
//             1, -1,
//             1, -1,
//             1,  1,
//            -1,  1
//        };
//
//        GLfloat coordData[] = {
//            0.0f, 1.0f,
//            1.0f, 1.0f,
//            1.0f, 0.0f,
//            1.0f, 0.0f,
//            0.0f, 0.0f,
//            0.0f, 1.0f,
//        };
//
//        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));
//        GL_CALL(glVertexAttribPointer(bound->position, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
//        GL_CALL(glVertexAttribPointer(bound->uvPosition, 2, GL_FLOAT, GL_FALSE, 0, coordData));
//        GL_CALL(glDrawArrays (GL_TRIANGLES, 0, 6));
//

        renderTransformedTexture(tex, {{0, 0}, {uint32_t(w), uint32_t(h)}});
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
//        GL_CALL(glDeleteFramebuffers(1, &dst_fbuff));
//        GL_CALL(glDeleteFramebuffers(1, &src_fbuff));
        return dst_tex;
    }

    Context* init_opengl(Output *output, const char *shaderSrcPath) {
        Context *ctx = new Context;

        ctx->width = output->screen_width;
        ctx->height = output->screen_height;

        GLuint minvss = compileShader(simple_vs, GL_VERTEX_SHADER);
        GLuint minfss = compileShader(simple_fs, GL_FRAGMENT_SHADER);
        ctx->min_program = GL_CALL(glCreateProgram());
        GL_CALL(glAttachShader(ctx->min_program, minvss));
        GL_CALL(glAttachShader(ctx->min_program, minfss));
        GL_CALL(glLinkProgram(ctx->min_program));

        std::string tmp = shaderSrcPath;

        GLuint vss = loadShader(std::string(shaderSrcPath)
                    .append("/vertex.glsl").c_str(),
                     GL_VERTEX_SHADER);

        GLuint fss = loadShader(std::string(shaderSrcPath)
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

        auto w2ID = GL_CALL(glGetUniformLocation(ctx->program, "w2"));
        auto h2ID = GL_CALL(glGetUniformLocation(ctx->program, "h2"));

        glUniform1f(w2ID, output->screen_width / 2.);
        glUniform1f(h2ID, output->screen_height / 2.);

        ctx->position   = GL_CALL(glGetAttribLocation(ctx->program, "position"));
        ctx->uvPosition = GL_CALL(glGetAttribLocation(ctx->program, "uvPosition"));
        return ctx;
    }
}
