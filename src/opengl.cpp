#include "opengl.hpp"

namespace {
    GLuint program;
    GLuint mvpID;
    GLuint depthID, colorID, bgraID;
    glm::mat4 ViewMat;
    glm::mat4 Proj;
    glm::mat4 MVP;

    GLuint framebuffer;
    GLuint framebufferTexture;

    GLuint fullVAO, fullVBO;
}

bool OpenGL::transformed = false;
int  OpenGL::depth;
glm::vec4 OpenGL::color;

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
    int VersionMinor, VersionMajor;
    GLuint position, uvPosition;
#define uchar unsigned char
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

    GLuint getTex() {return framebufferTexture;}

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

//        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, tex));

        GL_CALL(glVertexAttribPointer(position, 3, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glVertexAttribPointer(uvPosition, 2, GL_FLOAT, GL_FALSE, 0, coordData));

        GL_CALL(glDrawArrays (GL_TRIANGLES, 0, 6));
    }

    void renderTransformedTexture(GLuint tex, const wlc_geometry& g, glm::mat4 Model, uint32_t bits) {
        set_transform(Model);
        renderTexture(tex, g, bits);
    }

    void set_transform(glm::mat4 tr) {
         glUniformMatrix4fv(mvpID, 1, GL_FALSE, &tr[0][0]);
    }

    void preStage() {
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, framebuffer));
    }

    void preStage(GLuint fbuff) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbuff);
    }

    void prepareFramebuffer(GLuint &fbuff, GLuint &texture) {
        GetTuple(sw, sh, core->getScreenSize());

        GL_CALL(glGenFramebuffers(1, &fbuff));
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fbuff));

        GL_CALL(glGenTextures(1, &texture));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));

        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sw, sh,
                0, GL_RGBA, GL_UNSIGNED_BYTE, 0));

        GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                texture, 0));

        auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
        if (status != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "Error in framebuffer !!!" << std::endl;
    }

    void useDefaultProgram() {
        GL_CALL(glUseProgram(program));
    }

    void initOpenGL(const char *shaderSrcPath) {
        std::string tmp = shaderSrcPath;

        GLuint vss = loadShader(std::string(shaderSrcPath)
                    .append("/vertex.glsl").c_str(),
                     GL_VERTEX_SHADER);

        GLuint fss = loadShader(std::string(shaderSrcPath)
                    .append("/frag.glsl").c_str(),
                     GL_FRAGMENT_SHADER);

        program = glCreateProgram();

        GL_CALL(glAttachShader (program, vss));
        GL_CALL(glAttachShader (program, fss));
        GL_CALL(glLinkProgram (program));
        useDefaultProgram();

        mvpID   = GL_CALL(glGetUniformLocation(program, "MVP"));
        depthID = GL_CALL(glGetUniformLocation(program, "depth"));
        colorID = GL_CALL(glGetUniformLocation(program, "color"));
        bgraID  = GL_CALL(glGetUniformLocation(program, "bgra"));

        ViewMat = glm::lookAt(glm::vec3(0., 0., 1.67),
                glm::vec3(0., 0., 0.),
                glm::vec3(0., 1., 0.));
        Proj = glm::perspective(45.f, 1.f, .1f, 100.f);
        MVP = glm::mat4();

        GL_CALL(glUniformMatrix4fv(mvpID, 1, GL_FALSE, &MVP[0][0]));

        auto w2ID = GL_CALL(glGetUniformLocation(program, "w2"));
        auto h2ID = GL_CALL(glGetUniformLocation(program, "h2"));

        glUniform1f(w2ID, 1366. / 2);
        glUniform1f(h2ID, 768. / 2);

        position   = GL_CALL(glGetAttribLocation(program, "position"));
        uvPosition = GL_CALL(glGetAttribLocation(program, "uvPosition"));
    }
}
