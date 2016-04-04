#include <opengl.hpp>
#include <output.hpp>
//#include <GLES3/gl31.h>

/* TODO: create a tessellation engine and use it in cube deformation
 * currently deformation and lighting is disabled */
class Cube : public Plugin {
    ButtonBinding activate;
    ButtonBinding deactiv;
    ButtonBinding zoom;

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
            owner->compat.insert("screenshot");
        }

        void updateConfiguration() {
            Velocity  = options["velocity" ]->data.fval;
            VVelocity = options["vvelocity"]->data.fval;
            ZVelocity = options["zvelocity"]->data.fval;

#define TESSELATION_SUPPORTED false
            if(TESSELATION_SUPPORTED) {
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
            zoom.button = BTN_SCROLL;
            zoom.type   = BindingTypePress;
            zoom.mod = 0;
            zoom.action = std::bind(std::mem_fn(&Cube::onScrollEvent), this, _1);

            output->hook->add_but(&zoom, false);

            activate.button = actButton.button;
            activate.type = BindingTypePress;
            activate.mod = actButton.mod;
            activate.action =
                std::bind(std::mem_fn(&Cube::Initiate), this, _1);
            output->hook->add_but(&activate, true);

            deactiv.button = actButton.button;
            deactiv.mod    = 0;
            deactiv.type   = BindingTypeRelease;
            deactiv.action =
                std::bind(std::mem_fn(&Cube::Terminate), this, _1);
            output->hook->add_but(&deactiv, false);
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
            if(TESSELATION_SUPPORTED) {
                options.insert(newIntOption  ("deform",    0));
                options.insert(newIntOption  ("light",     false));
            }

            /* TODO: make a better way to determine shader path */
//            std::string shaderSrcPath =
//                "/usr/share/wayfire/cube/s4.0";
//            if(!TESSELATION_SUPPORTED)
//                shaderSrcPath = "/usr/share/wayfire/cube/s3.3";
//

//            program = glCreateProgram();
//            GLuint vss, fss, tcs = -1, tes = -1, gss = -1;
//
//            vss = OpenGL::loadShader(std::string(shaderSrcPath)
//                        .append("/vertex.glsl").c_str(), GL_VERTEX_SHADER);
//
//            fss = OpenGL::loadShader(std::string(shaderSrcPath)
//                        .append("/frag.glsl").c_str(), GL_FRAGMENT_SHADER);
//
//            glAttachShader (program, vss);
//            glAttachShader (program, fss);
//
//            if(TESSELATION_SUPPORTED) {
//                tcs = OpenGL::loadShader(std::string(shaderSrcPath)
//                            .append("/tcs.glsl").c_str(),
//                            GL_TESS_CONTROL_SHADER);
//
//                tes = GLXUtils::loadShader(std::string(shaderSrcPath)
//                            .append("/tes.glsl").c_str(),
//                            GL_TESS_EVALUATION_SHADER);
//
//                gss = GLXUtils::loadShader(std::string(shaderSrcPath)
//                            .append("/geom.glsl").c_str(),
//                            GL_GEOMETRY_SHADER);
//                glAttachShader (program, tcs);
//                glAttachShader (program, tes);
//                glAttachShader (program, gss);
//            }

//            glLinkProgram (program);
//            glUseProgram(program);

            auto proj = glm::perspective(45.0f, 1.f, 0.1f, 100.f);
            view = glm::lookAt(glm::vec3(0., 2., 2),
                    glm::vec3(0., 0., 0.),
                    glm::vec3(0., 1., 0.));
            vp = proj * view;

            GetTuple(vw, vh, output->viewport->get_viewport_grid_size());
            vh = 0;

            sides.resize(vw);
            sideFBuffs.resize(vw);

            angle = 2 * M_PI / float(vw);
            coeff = 0.5 / std::tan(angle / 2);

            for(int i = 0; i < vw; i++)
                sides[i] = sideFBuffs[i] = -1,
                OpenGL::prepareFramebuffer(sideFBuffs[i], sides[i]);
            mouse.action = std::bind(std::mem_fn(&Cube::mouseMoved), this);
            output->hook->add_hook(&mouse);

            renderer = std::bind(std::mem_fn(&Cube::Render), this);
        }

        void Initiate(EventContext ctx) {
            if(!output->input->activate_owner(owner))
                return;
            owner->grab();

            output->render->set_renderer(0, renderer);
            GetTuple(vx, vy, output->viewport->get_current_viewport());

            /* important: core uses vx = col vy = row */
            this->vx = vx, this->vy = vy;

            GetTuple(mx, my, output->input->get_pointer_position());
            px = mx, py = my;

            mouse.enable();
            deactiv.enable();
            zoom.enable();

            offset = 0;
            offsetVert = 0;
            zoomFactor = 1;
        }

        void Render() {
            glClearColor(bg.r, bg.g, bg.b, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

            for(int i = 0; i < sides.size(); i++) {
                output->render->texture_from_viewport(std::make_tuple(i, vy),
                        sideFBuffs[i], sides[i]);
            }

   //         glUseProgram(program);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);

    //        glBindVertexArray(vao);
     //       glBindBuffer(GL_ARRAY_BUFFER, vbo);

            glm::mat4 verticalRotation = glm::rotate(glm::mat4(),
                    offsetVert, glm::vec3(1, 0, 0));
            glm::mat4 scale = glm::scale(glm::mat4(),
                    glm::vec3(1. / zoomFactor, 1. / zoomFactor,
                        1. / zoomFactor));

            glm::mat4 addedS = scale * verticalRotation;
            glm::mat4 vpUpload = vp * addedS;
      //      glUniformMatrix4fv(vpID, 1, GL_FALSE, &vpUpload[0][0]);

            GetTuple(sw, sh, output->get_screen_size());
            wlc_geometry g;
            g.origin = {sw / 4, sh / 4};
            g.size.w = sw / 2;
            g.size.h = sh / 2;

            for(int i = 0; i < sides.size(); i++) {
                int index = (vx + i) % sides.size();

       //         glBindTexture(GL_TEXTURE_2D, sides[index]);

                model = glm::rotate(glm::mat4(),
                        float(i) * angle + offset, glm::vec3(0, 1, 0));
                model = glm::translate(model, glm::vec3(0, 0, coeff));

//                auto nm =
//                    glm::inverse(glm::transpose(glm::mat3(view *  addedS)));

                OpenGL::renderTransformedTexture(sides[index], g,
                        vpUpload * model, TEXTURE_TRANSFORM_INVERT_Y);
                //glUniformMatrix4fv(initialModel, 1, GL_FALSE, &model[0][0]);

//                if(OpenGL::VersionMajor >= 4) {
//                    glUniformMatrix3fv(nmID, 1, GL_FALSE, &nm[0][0]);
//
//                    glPatchParameteri(GL_PATCH_VERTICES, 3);
//                    glDrawArrays (GL_PATCHES, 0, 6);
//                }
//                else
                 //   glDrawArrays(GL_TRIANGLES, 0, 6);
            }
//            glXSwapBuffers(core->d, core->outputwin);
            glDisable(GL_DEPTH_TEST);
        }

        void Terminate(EventContext ctx) {
            output->render->reset_renderer();

            mouse.disable();
            deactiv.disable();
            zoom.disable();

            output->input->deactivate_owner(owner);

            auto size = sides.size();

            float dx = -(offset) / angle;
            int dvx = 0;
            if(dx > -1e-4)
                dvx = std::floor(dx + 0.5);
            else
                dvx = std::floor(dx - 0.5);

            int nvx = (vx + (dvx % size) + size) % size;
            output->viewport->switch_workspace(std::make_tuple(nvx, vy));
        }

        void mouseMoved() {
            GetTuple(mx, my, output->input->get_pointer_position());
            int xdiff = mx - px;
            int ydiff = my - py;
            offset += xdiff * Velocity;
            offsetVert += ydiff * VVelocity;
            px = mx, py = my;
        }

        void onScrollEvent(EventContext ctx) {
            std::cout << "Scroll event" << std::endl;
            zoomFactor += ZVelocity * ctx.amount[0];

            if (zoomFactor > MaxFactor)
                zoomFactor = MaxFactor;

            if (zoomFactor <= 0.1)
                zoomFactor = 0.1;
        }
};

extern "C" {
    Plugin *newInstance() {
        return new Cube();
    }

}
