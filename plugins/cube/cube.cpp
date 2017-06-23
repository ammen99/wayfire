#include <opengl.hpp>
#include <output.hpp>
#include <core.hpp>
#include <libweston-3/compositor.h>
#include <linux/input-event-codes.h>

#if USE_GLES32
#include <GLES3/gl32.h>
#endif

class wayfire_cube : public wayfire_plugin_t {
    button_callback activate;
    wayfire_button act_button;

    std::vector<wf_workspace_stream*> streams;
    int vx, vy;

    float XVelocity = 0.01;
    float YVelocity = 0.01;
    float ZVelocity = 0.05;
    float MaxFactor = 10;

    float angle;      // angle between sides
    float offset;     // horizontal rotation angle
    float offsetVert; // vertical rotation angle
    float zoomFactor = 1.0;

    int px, py;

    render_hook_t renderer;

    struct {
        GLuint id = -1;
        GLuint modelID, vpID;
        GLuint posID, uvID;
    } program;

    glm::mat4 vp, model, view, project;
    float coeff;

#if USE_GLES32
    bool use_light;
    int use_deform;
#endif

    wayfire_color backgroud_color;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "cube";
        grab_interface->compatAll = false;
        grab_interface->compat.insert("screenshot");

        auto section = config->get_section("cube");

        XVelocity  = section->get_double("speed_spin_horiz", 0.01);
        YVelocity  = section->get_double("speed_spin_vert",  0.01);
        ZVelocity  = section->get_double("speed_zoom",       0.05);

        backgroud_color = section->get_color("background", {0, 0, 0, 1});

        act_button = section->get_button("activate",
                {MODIFIER_ALT | MODIFIER_CTRL, BTN_LEFT});

        if (act_button.button == 0)
            return;

        activate = [=] (weston_pointer *ptr, uint32_t) {
            initiate(wl_fixed_to_int(ptr->x), wl_fixed_to_int(ptr->y));
        };

        core->input->add_button(act_button.mod, act_button.button,
                &activate, output);

        grab_interface->callbacks.pointer.button = [=] (weston_pointer*,
                uint32_t b, uint32_t s)
        {
            if (s == WL_POINTER_BUTTON_STATE_RELEASED)
                terminate();
        };

        grab_interface->callbacks.pointer.motion = [=] (weston_pointer* ptr,
                weston_pointer_motion_event*)
        {
            pointer_moved(wl_fixed_to_int(ptr->x), wl_fixed_to_int(ptr->y));
        };

        grab_interface->callbacks.pointer.axis = [=] (weston_pointer*,
                weston_pointer_axis_event *ev) {
            if (ev->axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
                pointer_scrolled(ev->value);
        };


#if USE_GLES32
        use_light = section->get_int("light", 1);
        use_deform = section->get_int("deform", 1);
#endif

        renderer = [=] () {render();};
    }

    void load_program()
    {
#if USE_GLES32
            std::string shaderSrcPath =
                INSTALL_PREFIX "/share/wayfire/cube/shaders_3.2";
#else
            std::string shaderSrcPath =
                INSTALL_PREFIX "/share/wayfire/cube/shaders_2.0";
#endif

            program.id = GL_CALL(glCreateProgram());
            GLuint vss, fss, tcs = -1, tes = -1, gss = -1;

            vss = OpenGL::load_shader(std::string(shaderSrcPath)
                        .append("/vertex.glsl").c_str(), GL_VERTEX_SHADER);

            fss = OpenGL::load_shader(std::string(shaderSrcPath)
                        .append("/frag.glsl").c_str(), GL_FRAGMENT_SHADER);

            GL_CALL(glAttachShader(program.id, vss));
            GL_CALL(glAttachShader(program.id, fss));

#if USE_GLES32
            tcs = OpenGL::load_shader(std::string(shaderSrcPath)
                    .append("/tcs.glsl").c_str(),
                    GL_TESS_CONTROL_SHADER);

            tes = OpenGL::load_shader(std::string(shaderSrcPath)
                    .append("/tes.glsl").c_str(),
                    GL_TESS_EVALUATION_SHADER);

            gss = OpenGL::load_shader(std::string(shaderSrcPath)
                    .append("/geom.glsl").c_str(),
                    GL_GEOMETRY_SHADER);
            GL_CALL(glAttachShader(program.id, tcs));
            GL_CALL(glAttachShader(program.id, tes));
            GL_CALL(glAttachShader(program.id, gss));
#endif

            GL_CALL(glLinkProgram(program.id));
            GL_CALL(glUseProgram(program.id));


            program.vpID = GL_CALL(glGetUniformLocation(program.id, "VP"));
            GL_CALL(glUniformMatrix4fv(program.vpID, 1, GL_FALSE, &vp[0][0]));

            program.uvID = GL_CALL(glGetAttribLocation(program.id, "uvPosition"));
            program.posID = GL_CALL(glGetAttribLocation(program.id, "position"));
            program.modelID = GL_CALL(glGetUniformLocation(program.id, "model"));

#if USE_GLES32
            GLuint defID = GL_CALL(glGetUniformLocation(program.id, "deform"));
            glUniform1i(defID, use_deform);

            GLuint lightID = GL_CALL(glGetUniformLocation(program.id, "light"));
            glUniform1i(lightID, use_light);
#endif

            GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
            vh = 0; /* silence compiler warning */

            streams.resize(vw);

            angle = 2 * M_PI / float(vw);
            coeff = 0.5 / std::tan(angle / 2);

            for(int i = 0; i < vw; i++) {
                streams[i] = new wf_workspace_stream;
                streams[i]->fbuff = streams[i]->tex = -1;
            }

            project = glm::perspective(45.0f, 1.f, 0.1f, 100.f);
    }

    void initiate(int x, int y)
    {
        if (!output->activate_plugin(grab_interface))
            return;
        grab_interface->grab();

        output->render->set_renderer(renderer);

        offset = 0;
        offsetVert = 0;
        zoomFactor = 1;

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        /* important: core uses vx = col vy = row */
        this->vx = vx, this->vy = vy;

        px = x;
        py = y;
    }

    void render()
    {
        if (program.id == (uint)-1)
            load_program();

        GL_CALL(glClearColor(backgroud_color.r, backgroud_color.g,
                backgroud_color.b + 0.5, backgroud_color.a + 1));
        GL_CALL(glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT));

        for(size_t i = 0; i < streams.size(); i++) {
            if (!streams[i]->running) {
                streams[i]->ws = {i, vy};
                output->render->workspace_stream_start(streams[i]);
            } else {
                output->render->workspace_stream_update(streams[i]);
            }
        }

        GL_CALL(glUseProgram(program.id));
        GL_CALL(glEnable(GL_DEPTH_TEST));
        GL_CALL(glDepthFunc(GL_LESS));

        view = glm::lookAt(glm::vec3(0., 2. + offsetVert, 2),
                glm::vec3(0., 0., 0.),
                glm::vec3(0., 1., 0.));
        vp = project * view;

        GL_CALL(glUniformMatrix4fv(program.vpID, 1, GL_FALSE, &vp[0][0]));

        glm::mat4 base_model = glm::scale(glm::mat4(),
                glm::vec3(1. / zoomFactor, 1. / zoomFactor,
                    1. / zoomFactor));


        GLfloat vertexData[] = {
            -0.5,  0.5,
             0.5,  0.5,
             0.5, -0.5,
            -0.5,  0.5,
             0.5, -0.5,
            -0.5, -0.5
        };

        GLfloat coordData[] = {
            0.0f, 1.0f,
            1.0f, 1.0f,
            1.0f, 0.0f,
            0.0f, 1.0f,
            1.0f, 0.0f,
            0.0f, 0.0f,
        };

        GL_CALL(glVertexAttribPointer(program.posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(program.posID));

        GL_CALL(glVertexAttribPointer(program.uvID, 2, GL_FLOAT, GL_FALSE, 0, coordData));
        GL_CALL(glEnableVertexAttribArray(program.uvID));

        for(size_t i = 0; i < streams.size(); i++) {
            int index = (vx + i) % streams.size();

            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));

            GL_CALL(glBindTexture(GL_TEXTURE_2D, streams[index]->tex));
            GL_CALL(glActiveTexture(GL_TEXTURE0));

            model = glm::rotate(base_model,
                    float(i) * angle + offset, glm::vec3(0, 1, 0));
            model = glm::translate(model, glm::vec3(0, 0, coeff));
           GL_CALL(glUniformMatrix4fv(program.modelID, 1, GL_FALSE, &model[0][0]));

#if USE_GLES32
           GL_CALL(glDrawArrays(GL_PATCHES, 0, 6));
#else
           GL_CALL(glDrawArrays(GL_TRIANGLES, 0, 6));
#endif
        }
        glDisable(GL_DEPTH_TEST);

        GL_CALL(glDisableVertexAttribArray(program.posID));
        GL_CALL(glDisableVertexAttribArray(program.uvID));
    }

    void terminate()
    {
        output->render->reset_renderer();
        output->deactivate_plugin(grab_interface);

        auto size = streams.size();

        float dx = -(offset) / angle;
        int dvx = 0;
        if(dx > -1e-4)
            dvx = std::floor(dx + 0.5);
        else
            dvx = std::floor(dx - 0.5);

        int nvx = (vx + (dvx % size) + size) % size;
        output->workspace->set_workspace(std::make_tuple(nvx, vy));

        for (uint i = 0; i < size; i++)
            output->render->workspace_stream_stop(streams[i]);
    }

    void pointer_moved(int x, int y)
    {
        int xdiff = x - px;
        int ydiff = y - py;
        offset += xdiff * XVelocity;
        offsetVert += ydiff * YVelocity;
        px = x, py = y;
    }

    void pointer_scrolled(double amount)
    {
        zoomFactor += ZVelocity * amount;

        if (zoomFactor > MaxFactor)
            zoomFactor = MaxFactor;

        if (zoomFactor <= 0.1)
            zoomFactor = 0.1;
    }
};

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_cube();
    }

}
