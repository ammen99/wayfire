#include <opengl.hpp>
#include <output.hpp>
#include <core.hpp>
#include <render-manager.hpp>
#include <workspace-manager.hpp>

#include <compositor.h>
#include <linux/input-event-codes.h>
#include <config.hpp>
#include <config.h>

#define COEFF_DELTA_NEAR 0.89567f
#define COEFF_DELTA_FAR  2.00000f

#if USE_GLES32
#include <GLES3/gl32.h>
#endif

class wayfire_cube : public wayfire_plugin_t {
    button_callback activate;
    wayfire_button act_button;

    std::vector<wf_workspace_stream*> streams;

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
#if USE_GLES32
        GLuint easeID;
#endif
    } program;

    struct duple { float start, end; };

    struct
    {
        duple offset_y;
        duple offset_z;
        duple rotation;
        int current_step, max_steps;

#if USE_GLES32
        duple ease_deformation;
#endif

        bool in_exit, active = false;
    } animation;

    glm::mat4 vp, model, view, project;
    float coeff;

#if USE_GLES32
    bool use_light;
    int use_deform;
    float current_ease;
#endif

    wayfire_color backgroud_color;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "cube";
        grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;

        auto section = config->get_section("cube");

        XVelocity  = section->get_double("speed_spin_horiz", 0.01);
        YVelocity  = section->get_double("speed_spin_vert",  0.01);
        ZVelocity  = section->get_double("speed_zoom",       0.05);

        animation.max_steps  = section->get_duration("initial_animation", 30);

        backgroud_color = section->get_color("background", {0, 0, 0, 1});

        act_button = section->get_button("activate",
                {WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL, BTN_LEFT});

        if (act_button.button == 0)
            return;

        activate = [=] (weston_pointer *ptr, uint32_t) {
            initiate(wl_fixed_to_int(ptr->x), wl_fixed_to_int(ptr->y));
        };

        output->add_button(act_button.mod, act_button.button, &activate);

        grab_interface->callbacks.pointer.button = [=] (weston_pointer*,
                uint32_t b, uint32_t s)
        {
            if (s == WL_POINTER_BUTTON_STATE_RELEASED)
                input_released();
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

        auto vw = std::get<0>(output->workspace->get_workspace_grid_size());
        angle = 2 * M_PI / float(vw);
        coeff = 0.5 / std::tan(angle / 2);

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

            program.easeID = GL_CALL(glGetUniformLocation(program.id, "ease"));
#endif

            GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
            vh = 0; /* silence compiler warning */

            streams.resize(vw);


            for(int i = 0; i < vw; i++) {
                streams[i] = new wf_workspace_stream;
                streams[i]->fbuff = streams[i]->tex = -1;
            }

            project = glm::perspective(45.0f, 1.f, 0.1f, 100.f);
    }

    void initiate(int x, int y)
    {
        if (!animation.active)
        {
            if (!output->activate_plugin(grab_interface))
                return;

            grab_interface->grab();
            output->render->set_renderer(renderer);
        }

        animation.in_exit = false;
        animation.active = true;

        offset = 0;
        offsetVert = 0;
        zoomFactor = 1;
        animation.current_step = 0;
        animation.in_exit = false;
        animation.offset_z = {coeff + COEFF_DELTA_NEAR, coeff + COEFF_DELTA_FAR};

#if USE_GLES32
        animation.ease_deformation = {0, 1};
#endif

        if (update_animation())
            output->render->schedule_redraw();

        px = x;
        py = y;
    }

    bool update_animation()
    {
        float z_offset = GetProgress(animation.offset_z.start,
                                     animation.offset_z.end,
                                     animation.current_step,
                                     animation.max_steps);

#if USE_GLES32
        current_ease = GetProgress(animation.ease_deformation.start,
                                   animation.ease_deformation.end,
                                   animation.current_step,
                                   animation.max_steps);
#endif

        /* also update rotation and Y offset */
        if (animation.in_exit)
        {
            offsetVert = GetProgress(animation.offset_y.start,
                                   animation.offset_y.end,
                                   animation.current_step,
                                   animation.max_steps);

            offset = GetProgress(animation.rotation.start,
                                 animation.rotation.end,
                                 animation.current_step,
                                 animation.max_steps);
        }

        view = glm::lookAt(glm::vec3(0., offsetVert, z_offset),
                glm::vec3(0., 0., 0.),
                glm::vec3(0., 1., 0.));

        if (animation.current_step < animation.max_steps)
        {
            ++animation.current_step;
            if (animation.current_step == animation.max_steps)
                return false;
            else
                return true;
        }

        return false;
    }

    void render()
    {
        if (program.id == (uint)-1)
            load_program();

        OpenGL::use_device_viewport();
        GL_CALL(glClearColor(backgroud_color.r, backgroud_color.g,
                backgroud_color.b, backgroud_color.a));

        GL_CALL(glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT));

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        for(size_t i = 0; i < streams.size(); i++) {
            if (!streams[i]->running) {
                streams[i]->ws = std::make_tuple(i, vy);
                output->render->workspace_stream_start(streams[i]);
            } else {
                output->render->workspace_stream_update(streams[i]);
            }
        }

        GL_CALL(glUseProgram(program.id));
        GL_CALL(glEnable(GL_DEPTH_TEST));
        GL_CALL(glDepthFunc(GL_LESS));

        OpenGL::use_device_viewport();
        vp = project * view;
        GL_CALL(glUniformMatrix4fv(program.vpID, 1, GL_FALSE, &vp[0][0]));

#if USE_GLES32
        GL_CALL(glUniform1f(program.easeID, current_ease));
#endif

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

        bool result = update_animation();
        if (result)
            output->render->schedule_redraw();

        if (animation.in_exit && !result)
            terminate();
    }

    void input_released()
    {
        auto size = streams.size();

        float dx = -(offset) / angle;
        int dvx = std::floor(dx + 0.5);

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        int nvx = (vx + (dvx % size) + size) % size;
        output->workspace->set_workspace(std::make_tuple(nvx, vy));

        animation.in_exit = true;
        animation.current_step = 0;
        animation.offset_z = {coeff + COEFF_DELTA_FAR, coeff + COEFF_DELTA_NEAR};
        animation.offset_y = {offsetVert, 0};
        animation.rotation = {offset + 1.0f * dvx * angle, 0};

#if USE_GLES32
        animation.ease_deformation = {1, 0};
#endif

        update_animation();
        output->render->schedule_redraw();
    }

    void terminate()
    {
        animation.active = false;
        animation.in_exit = false;

        output->render->reset_renderer();
        output->deactivate_plugin(grab_interface);

        auto size = streams.size();
        for (uint i = 0; i < size; i++)
            output->render->workspace_stream_stop(streams[i]);
    }

    void pointer_moved(int x, int y)
    {
        if (animation.in_exit)
            return;

        int xdiff = x - px;
        int ydiff = y - py;
        offset += xdiff * XVelocity;
        offsetVert += ydiff * YVelocity;
        px = x, py = y;

        output->render->schedule_redraw();
    }

    void pointer_scrolled(double amount)
    {
        zoomFactor += ZVelocity * amount;

        if (zoomFactor > MaxFactor)
            zoomFactor = MaxFactor;

        if (zoomFactor <= 0.1)
            zoomFactor = 0.1;

        output->render->schedule_redraw();
    }
};

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_cube();
    }

}
