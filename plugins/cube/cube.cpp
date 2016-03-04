#include <core.hpp>
#include <opengl.hpp>
class Cube : public Plugin {
    ButtonBinding activate;
    ButtonBinding deactiv;
    ButtonBinding zoomIn, zoomOut;

    Hook mouse;
    std::vector<GLuint> sides;
    std::vector<GLuint> sideFBuffs;
    int vx, vy;

    float Velocity = 0.01;
    float VVelocity = 0.01;
    float ZVelocity = 0.05;
    float MaxFactor = 10;

    float angle;      // angle between sides
    float offset;     // horizontal rotation angle
    float offsetVert; // vertical rotation angle
    float zoomFactor = 1.0;

    int px, py;

    RenderHook renderer;

    GLuint program;
    GLuint vao, vbo;

    GLuint vpID;
    GLuint initialModel;
    GLuint nmID;

    glm::mat4 vp, model, view;
    float coeff;

    Button actButton;
    Color bg;

    public:
        void initOwnership() {
            owner->name = "cube";
            owner->compatAll = false;
        }

        void updateConfiguration() {
            Velocity  = options["velocity" ]->data.fval;
            VVelocity = options["vvelocity"]->data.fval;
            ZVelocity = options["zvelocity"]->data.fval;

            if(OpenGL::VersionMajor >= 4) {
                int val = options["deform"]->data.ival;
                glUseProgram(program);
                GLuint defID = glGetUniformLocation(program, "deform");
                glUniform1i(defID, val);

                val = options["light"]->data.ival ? 1 : 0;
                GLuint lightID = glGetUniformLocation(program, "light");
                glUniform1i(lightID, val);

                OpenGL::useDefaultProgram();
            }

            actButton = *options["activate"]->data.but;
            bg = *options["bg"]->data.color;

            if(actButton.button == 0)
                return;

            using namespace std::placeholders;

            zoomOut.button = Button4;
            zoomIn.button  = Button5;
            zoomOut.type   = zoomIn.type   = BindingTypePress;
            zoomOut.mod    = zoomIn.mod    = AnyModifier;
            zoomOut.action = zoomIn.action =
                std::bind(std::mem_fn(&Cube::onScrollEvent), this, _1);

            core->addBut(&zoomOut, false);
            core->addBut(&zoomIn , false);


            activate.button = actButton.button;
            activate.type = BindingTypePress;
            activate.mod = actButton.mod;
            activate.action =
                std::bind(std::mem_fn(&Cube::Initiate), this, _1);
            core->addBut(&activate, true);

            deactiv.button = actButton.button;
            deactiv.mod    = AnyModifier;
            deactiv.type   = BindingTypeRelease;
            deactiv.action =
                std::bind(std::mem_fn(&Cube::Terminate), this, _1);
            core->addBut(&deactiv, false);

        }

        void init() {
            options.insert(newFloatOption("velocity",  0.01));
            options.insert(newFloatOption("vvelocity", 0.01));
            options.insert(newFloatOption("zvelocity", 0.05));

            options.insert(newColorOption("bg", Color{0, 0, 0}));
            options.insert(newButtonOption("activate", Button{0, 0}));

            /* these features require tesselation,
             * so if OpenGL version < 4 do not expose
             * such capabilities */
            if(OpenGL::VersionMajor >= 4) {
                options.insert(newIntOption  ("deform",    0));
                options.insert(newIntOption  ("light",     false));
            }

            std::string shaderSrcPath =
                "/usr/local/share/fireman/cube/s4.0";
            if(OpenGL::VersionMajor < 4)
                shaderSrcPath = "/usr/local/share/fireman/cube/s3.3";


            program = glCreateProgram();
            GLuint vss, fss, tcs = -1, tes = -1, gss = -1;

            vss = GLXUtils::loadShader(std::string(shaderSrcPath)
                        .append("/vertex.glsl").c_str(), GL_VERTEX_SHADER);

            fss = GLXUtils::loadShader(std::string(shaderSrcPath)
                        .append("/frag.glsl").c_str(), GL_FRAGMENT_SHADER);

            glAttachShader (program, vss);
            glAttachShader (program, fss);

            if(OpenGL::VersionMajor >= 4) {
                tcs = GLXUtils::loadShader(std::string(shaderSrcPath)
                            .append("/tcs.glsl").c_str(),
                            GL_TESS_CONTROL_SHADER);

                tes = GLXUtils::loadShader(std::string(shaderSrcPath)
                            .append("/tes.glsl").c_str(),
                            GL_TESS_EVALUATION_SHADER);

                gss = GLXUtils::loadShader(std::string(shaderSrcPath)
                            .append("/geom.glsl").c_str(),
                            GL_GEOMETRY_SHADER);
                glAttachShader (program, tcs);
                glAttachShader (program, tes);
                glAttachShader (program, gss);
            }

            glBindFragDataLocation (program, 0, "outColor");
            glLinkProgram (program);
            glUseProgram(program);

            vpID = glGetUniformLocation(program, "VP");
            initialModel = glGetUniformLocation(program, "initialModel");
            if(OpenGL::VersionMajor >= 4)
                nmID = glGetUniformLocation(program, "NM");

            auto proj = glm::perspective(45.0f, 1.f, 0.1f, 100.f);
            view = glm::lookAt(glm::vec3(0., 2., 2),
                    glm::vec3(0., 0., 0.),
                    glm::vec3(0., 1., 0.));
            vp = proj * view;

            glGenVertexArrays (1, &vao);
            glBindVertexArray (vao);

            GLfloat vertices[] = {
                -0.5f, -0.5f, 0.f, 0.0f, 0.0f,
                0.5f, -0.5f, 0.f, 1.0f, 0.0f,
                0.5f,  0.5f, 0.f, 1.0f, 1.0f,
                0.5f,  0.5f, 0.f, 1.0f, 1.0f,
                -0.5f,  0.5f, 0.f, 0.0f, 1.0f,
                -0.5f, -0.5f, 0.f, 0.0f, 0.0f,
            };
            glGenBuffers (1, &vbo);
            glBindBuffer (GL_ARRAY_BUFFER, vbo);
            glBufferData (GL_ARRAY_BUFFER, sizeof (vertices),
                    vertices, GL_STATIC_DRAW );

            GLint position = glGetAttribLocation (program, "position");
            GLint uvPosition = glGetAttribLocation (program, "uvPosition");

            glVertexAttribPointer (position, 3,
                    GL_FLOAT, GL_FALSE,
                    5 * sizeof (GL_FLOAT), 0);
            glVertexAttribPointer (uvPosition, 2,
                    GL_FLOAT, GL_FALSE,
                    5 * sizeof (GL_FLOAT), (void*)(3 * sizeof (float)));

            glEnableVertexAttribArray (position);
            glEnableVertexAttribArray (uvPosition);

            OpenGL::useDefaultProgram();

            GetTuple(vw, vh, core->get_viewport_grid_size());
            vh = 0;

            sides.resize(vw);
            sideFBuffs.resize(vw);

            angle = 2 * M_PI / float(vw);
            coeff = 0.5 / std::tan(angle / 2);

            for(int i = 0; i < vw; i++)
                sides[i] = sideFBuffs[i] = -1,
                OpenGL::prepareFramebuffer(sideFBuffs[i], sides[i]);
            mouse.action = std::bind(std::mem_fn(&Cube::mouseMoved), this);
            core->addHook(&mouse);

            renderer = std::bind(std::mem_fn(&Cube::Render), this);
        }

        void Initiate(Context *ctx) {
            if(!core->activate_owner(owner))
                return;
            owner->grab();

            if(!core->set_renderer(renderer)) {
                owner->ungrab();
                core->deactivate_owner(owner);
                return;
            }

            GetTuple(vx, vy, core->get_current_viewport());

            /* important: core uses vx = col vy = row */
            this->vx = vx, this->vy = vy;

            GetTuple(mx, my, core->get_pointer_position());
            px = mx, py = my;

            mouse.enable();
            deactiv.enable();
            zoomIn.enable();
            zoomOut.enable();

            offset = 0;
            offsetVert = 0;
            zoomFactor = 1;
        }

        void Render() {
            glClearColor(bg.r, bg.g, bg.b, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

            for(int i = 0; i < sides.size(); i++) {
                core->texture_from_viewport(std::make_tuple(i, vy),
                        sideFBuffs[i], sides[i]);
            }

            glUseProgram(program);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);

            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);

            glm::mat4 verticalRotation = glm::rotate(glm::mat4(),
                    offsetVert, glm::vec3(1, 0, 0));
            glm::mat4 scale = glm::scale(glm::mat4(),
                    glm::vec3(1. / zoomFactor, 1. / zoomFactor,
                        1. / zoomFactor));

            glm::mat4 addedS = scale * verticalRotation;
            glm::mat4 vpUpload = vp * addedS;
            glUniformMatrix4fv(vpID, 1, GL_FALSE, &vpUpload[0][0]);

            for(int i = 0; i < sides.size(); i++) {
                int index = (vx + i) % sides.size();

                glBindTexture(GL_TEXTURE_2D, sides[index]);

                model = glm::rotate(glm::mat4(),
                        float(i) * angle + offset, glm::vec3(0, 1, 0));
                model = glm::translate(model, glm::vec3(0, 0, coeff));

                auto nm =
                    glm::inverse(glm::transpose(glm::mat3(view *  addedS)));

                glUniformMatrix4fv(initialModel, 1, GL_FALSE, &model[0][0]);

                if(OpenGL::VersionMajor >= 4) {
                    glUniformMatrix3fv(nmID, 1, GL_FALSE, &nm[0][0]);

                    glPatchParameteri(GL_PATCH_VERTICES, 3);
                    glDrawArrays (GL_PATCHES, 0, 6);
                }
                else
                    glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            glXSwapBuffers(core->d, core->outputwin);
        }

        void Terminate(Context *ctx) {
            OpenGL::useDefaultProgram();
            core->reset_renderer();
            mouse.disable();
            deactiv.disable();
            zoomIn.disable();
            zoomOut.disable();
            core->deactivate_owner(owner);

            auto size = sides.size();

            float dx = -(offset) / angle;
            int dvx = 0;
            if(dx > -1e-4)
                dvx = std::floor(dx + 0.5);
            else
                dvx = std::floor(dx - 0.5);

            int nvx = (vx + (dvx % size) + size) % size;
            core->switch_workspace(std::make_tuple(nvx, vy));
        }

        void mouseMoved() {
            GetTuple(mx, my, core->get_pointer_position());
            int xdiff = mx - px;
            int ydiff = my - py;
            offset += xdiff * Velocity;
            offsetVert += ydiff * VVelocity;
            px = mx, py = my;
        }

        void onScrollEvent(Context *ctx) {
            auto xev = ctx->xev;
            if(xev.xbutton.button == zoomIn.button)
                zoomFactor -= ZVelocity;
            if(xev.xbutton.button == zoomOut.button)
                zoomFactor += ZVelocity;

            if(zoomFactor <= ZVelocity)
                zoomFactor = ZVelocity;

            if(zoomFactor > MaxFactor)
                zoomFactor = MaxFactor;
        }
};

extern "C" {
    Plugin *newInstance() {
        return new Cube();
    }

}
