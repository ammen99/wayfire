#include <plugin.hpp>
#include <opengl.hpp>
#include <output.hpp>
#include <core.hpp>
#include <render-manager.hpp>
#include <workspace-manager.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <img.hpp>

#include "cube.hpp"
#include "simple-background.hpp"
#include "skydome.hpp"
#include "cubemap.hpp"

#define Z_OFFSET_NEAR 0.89567f
#define Z_OFFSET_FAR  2.00000f

#define ZOOM_MAX 10.0f
#define ZOOM_MIN 0.1f

#ifdef USE_GLES32
#include <GLES3/gl32.h>
#endif

class wayfire_cube : public wf::plugin_interface_t
{
    button_callback activate_binding;
    activator_callback rotate_left, rotate_right;
    wf::render_hook_t renderer;

    /* Used to restore the pointer where the grab started */
    wf_point saved_pointer_position;

    std::vector<wf::workspace_stream_t> streams;

    wf_option XVelocity, YVelocity, ZVelocity;
    wf_option zoom_opt;

    /* the Z camera distance so that (-1, 1) is mapped to the whole screen
     * for the given FOV */
    float identity_z_offset;

    struct {
        GLuint id = -1;
        GLuint modelID, vpID;
        GLuint posID, uvID;
        GLuint defID, lightID;
        GLuint easeID;
    } program;

    wf_cube_animation_attribs animation;
    wf_option use_light, use_deform;

    std::string last_background_mode;
    std::unique_ptr<wf_cube_background_base> background;

    wf_option background_mode;

    void reload_background()
    {
        if (background_mode->as_string() == last_background_mode)
            return;

        last_background_mode = background_mode->as_string();

        if (last_background_mode == "simple")
            background = std::make_unique<wf_cube_simple_background> ();
        else if (last_background_mode == "skydome")
            background = std::make_unique<wf_cube_background_skydome> (output);
        else if (last_background_mode == "cubemap")
            background = std::make_unique<wf_cube_background_cubemap> ();
        else
        {
            log_error("cube: Unrecognized background mode %s. Using default \"simple\"",
                last_background_mode.c_str());
            background = std::make_unique<wf_cube_simple_background> ();
        }
    }

    bool tessellation_support;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "cube";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        auto section = config->get_section("cube");

        XVelocity  = section->get_option("speed_spin_horiz", "0.01");
        YVelocity  = section->get_option("speed_spin_vert",  "0.01");
        ZVelocity  = section->get_option("speed_zoom",       "0.05");

        zoom_opt = section->get_option("zoom", "0");

        animation.duration = wf_duration(section->get_option("initial_animation", "350"));
        animation.duration.start();

        background_mode = section->get_option("background_mode", "simple");
        reload_background();

        auto button = section->get_option("activate", "<alt> <ctrl> BTN_LEFT");
        activate_binding = [=] (uint32_t, int32_t, int32_t) {
            input_grabbed();
        };

        auto key_left = section->get_option("rotate_left", "<alt> <ctrl> KEY_LEFT");
        rotate_left = [=] (wf_activator_source, uint32_t) {
            move_vp(-1);
        };

        auto key_right = section->get_option("rotate_right", "<alt> <ctrl> KEY_RIGHT");
        rotate_right = [=] (wf_activator_source, uint32_t) {
            move_vp(1);
        };

        output->add_button(button, &activate_binding);
        output->add_activator(key_left, &rotate_left);
        output->add_activator(key_right, &rotate_right);

        grab_interface->callbacks.pointer.button = [=] (uint32_t b, uint32_t s)
        {
            if (s == WL_POINTER_BUTTON_STATE_RELEASED)
                input_ungrabbed();
        };

        grab_interface->callbacks.pointer.relative_motion = [=] (wlr_event_pointer_motion* ev)
        {
            pointer_moved(ev);
        };

        grab_interface->callbacks.pointer.axis = [=] (
                wlr_event_pointer_axis *ev) {
            if (ev->orientation == WLR_AXIS_ORIENTATION_VERTICAL)
                pointer_scrolled(ev->delta);
        };

        grab_interface->callbacks.cancel = [=] () {
            deactivate();
        };

        use_light  = section->get_option("light", "1");
        use_deform = section->get_option("deform", "0");

        auto vw = std::get<0>(output->workspace->get_workspace_grid_size());
        animation.side_angle = 2 * M_PI / float(vw);
        identity_z_offset = 0.5 / std::tan(animation.side_angle / 2);
        animation.offset_z = {identity_z_offset + Z_OFFSET_NEAR,
            identity_z_offset + Z_OFFSET_NEAR};

        renderer = [=] (const wf_framebuffer& dest) {render(dest);};

        OpenGL::render_begin(output->render->get_target_framebuffer());
        load_program();
        OpenGL::render_end();
    }

    void schedule_next_frame()
    {
        output->render->schedule_redraw();
        /* Damage a minimal area of the screen so that next frame gets scheduled */
        output->render->damage({0, 0, 1, 1});
    }

    void load_program()
    {

#ifdef USE_GLES32
        std::string ext_string(reinterpret_cast<const char*> (glGetString(GL_EXTENSIONS)));
        tessellation_support =
            ext_string.find(std::string("GL_EXT_tessellation_shader")) != std::string::npos;
        GLuint tcs = -1, tes = -1, gss = -1;
#else
        tessellation_support = false;
#endif

        std::string shaderSrcPath;
        if (tessellation_support) {
            shaderSrcPath = INSTALL_PREFIX "/share/wayfire/cube/shaders_3.2";
        } else {
            shaderSrcPath = INSTALL_PREFIX "/share/wayfire/cube/shaders_2.0";
        }

        program.id = GL_CALL(glCreateProgram());
        GLuint vss, fss;

        /* Vertex and fragment shaders are used in both GLES 2.0 and 3.2 modes */
        vss = OpenGL::load_shader(shaderSrcPath + "/vertex.glsl", GL_VERTEX_SHADER);
        fss = OpenGL::load_shader(shaderSrcPath + "/frag.glsl", GL_FRAGMENT_SHADER);
        GL_CALL(glAttachShader(program.id, vss));
        GL_CALL(glAttachShader(program.id, fss));

        if (tessellation_support)
        {
#ifdef USE_GLES32
            tcs = OpenGL::load_shader(shaderSrcPath + "/tcs.glsl", GL_TESS_CONTROL_SHADER);
            tes = OpenGL::load_shader(shaderSrcPath + "/tes.glsl", GL_TESS_EVALUATION_SHADER);
            gss = OpenGL::load_shader(shaderSrcPath + "/geom.glsl", GL_GEOMETRY_SHADER);

            GL_CALL(glAttachShader(program.id, tcs));
            GL_CALL(glAttachShader(program.id, tes));
            GL_CALL(glAttachShader(program.id, gss));
#endif
        }

        GL_CALL(glLinkProgram(program.id));
        GL_CALL(glUseProgram(program.id));

        GL_CALL(glDeleteShader(vss));
        GL_CALL(glDeleteShader(fss));

        if (tessellation_support)
        {
#ifdef USE_GLES32
            GL_CALL(glDeleteShader(tcs));
            GL_CALL(glDeleteShader(tes));
            GL_CALL(glDeleteShader(gss));
#endif
        }

        program.vpID = GL_CALL(glGetUniformLocation(program.id, "VP"));
        program.uvID = GL_CALL(glGetAttribLocation(program.id, "uvPosition"));
        program.posID = GL_CALL(glGetAttribLocation(program.id, "position"));
        program.modelID = GL_CALL(glGetUniformLocation(program.id, "model"));

        if (tessellation_support)
        {
            program.defID = GL_CALL(glGetUniformLocation(program.id, "deform"));
            program.easeID = GL_CALL(glGetUniformLocation(program.id, "ease"));
            program.lightID = GL_CALL(glGetUniformLocation(program.id, "light"));
        }

        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        (void) vh; // silence compiler warning

        streams.resize(vw);
        animation.projection = glm::perspective(45.0f, 1.f, 0.1f, 100.f);
    }

    /* Tries to initialize renderer, activate plugin, etc. */
    bool activate()
    {
        if (output->is_plugin_active(grab_interface->name))
            return true;

        if (!output->activate_plugin(grab_interface))
            return false;

        output->render->set_renderer(renderer);
        grab_interface->grab();
        return true;
    }

    int calculate_viewport_dx_from_rotation()
    {
        float dx = -animation.duration.progress(animation.rotation) / animation.side_angle;
        return std::floor(dx + 0.5);
    }

    /* Disable custom rendering and deactivate plugin */
    void deactivate()
    {
        output->render->set_renderer(nullptr);

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);

        /* Figure out how much we have rotated and switch workspace */
        int size = streams.size();
        int dvx = calculate_viewport_dx_from_rotation();

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        int nvx = (vx + (dvx % size) + size) % size;
        output->workspace->set_workspace(std::make_tuple(nvx, vy));

        /* We are finished with rotation, make sure the next time cube is used
         * it is properly reset */
        animation.rotation = {0, 0};

        for (auto& stream : streams)
            output->render->workspace_stream_stop(stream);
    }

    /* Sets attributes target to such values that the cube effect isn't visible,
     * i.e towards the starting(or ending) position
     *
     * It doesn't change rotation because that is different in different cases -
     * for example when moved by the keyboard or with a button grab */
    void reset_attribs()
    {
        animation.zoom = {animation.duration.progress(animation.zoom), 1.0};
        animation.offset_z = {animation.duration.progress(animation.offset_z), identity_z_offset + Z_OFFSET_NEAR};
        animation.offset_y = {animation.duration.progress(animation.offset_y), 0};
        animation.ease_deformation = {animation.duration.progress(animation.ease_deformation), 0};
    }

    /* Start moving to a workspace to the left/right using the keyboard */
    void move_vp(int dir)
    {
        if (!activate())
            return;

        /* After the rotation is done, we want to exit cube and focus the target workspace */
        animation.in_exit = true;

        /* Set up rotation target to the next workspace in the given direction,
         * and reset other attribs */
        reset_attribs();
        animation.rotation = {animation.duration.progress(animation.rotation),
            animation.rotation.end - dir * animation.side_angle};

        animation.duration.start();
        update_view_matrix();
        schedule_next_frame();
    }

    /* Initiate with an button grab. */
    void input_grabbed()
    {
        if (!activate())
            return;

        GetTuple(px, py, wf::get_core().get_cursor_position());
        saved_pointer_position = {px, py};
        wf::get_core().hide_cursor();

        /* Rotations, offset_y and zoom stay as they are now, as they have been grabbed.
         * offset_z changes to the default one.
         *
         * We also need to make sure the cube gets deformed */
        animation.in_exit = false;
        float current_rotation = animation.duration.progress(animation.rotation);
        float current_offset_y = animation.duration.progress(animation.offset_y);
        float current_zoom = animation.duration.progress(animation.zoom);

        animation.rotation = {current_rotation, current_rotation};
        animation.offset_y = {current_offset_y, current_offset_y};
        animation.offset_z = {animation.duration.progress(animation.offset_z),
                              zoom_opt->as_double() + identity_z_offset + Z_OFFSET_NEAR};

        animation.zoom = {current_zoom, current_zoom};
        animation.ease_deformation = {animation.duration.progress(animation.ease_deformation), 1};

        animation.duration.start();

        update_view_matrix();
        schedule_next_frame();
    }

    /* Mouse grab was released */
    void input_ungrabbed()
    {
        wf::get_core().set_cursor("default");
        wf::get_core().warp_cursor(
            saved_pointer_position.x, saved_pointer_position.y);

        animation.in_exit = true;

        /* Rotate cube so that selected workspace aligns with the output */
        float current_rotation = animation.duration.progress(animation.rotation);
        int dvx = calculate_viewport_dx_from_rotation();
        animation.rotation = {current_rotation, -dvx * animation.side_angle};
        /* And reset other attributes, again to align the workspace with the output */
        reset_attribs();

        animation.duration.start();

        update_view_matrix();
        schedule_next_frame();
    }

    /* Update the view matrix used in the next frame */
    void update_view_matrix()
    {
        auto zoom_translate = glm::translate(glm::mat4(1.f),
            glm::vec3(0.f, 0.f, -animation.duration.progress(animation.offset_z)));

        auto rotation = glm::rotate(glm::mat4(1.0),
            (float) animation.duration.progress(animation.offset_y),
            glm::vec3(1., 0., 0.));

        auto view = glm::lookAt(glm::vec3(0., 0., 0.),
            glm::vec3(0., 0., -animation.duration.progress(animation.offset_z)),
            glm::vec3(0., 1., 0.));

        animation.view = zoom_translate * rotation * view;
    }

    void update_workspace_streams()
    {
        GetTuple(vx, vy, output->workspace->get_current_workspace());
        (void) vx;

        for(size_t i = 0; i < streams.size(); i++)
        {
            if (!streams[i].running)
            {
                streams[i].ws = std::make_tuple(i, vy);
                output->render->workspace_stream_start(streams[i]);
            } else
            {
                output->render->workspace_stream_update(streams[i]);
            }
        }
    }

    glm::mat4 calculate_vp_matrix(const wf_framebuffer& dest)
    {
        float zoom_factor = animation.duration.progress(animation.zoom);
        auto scale_matrix = glm::scale(glm::mat4(1.0),
            glm::vec3(1. / zoom_factor, 1. / zoom_factor, 1. / zoom_factor));

        return dest.transform * animation.projection * animation.view * scale_matrix;
    }

    /* Calculate the base model matrix for the i-th side of the cube */
    glm::mat4 calculate_model_matrix(int i, glm::mat4 fb_transform)
    {
        auto rotation = glm::rotate(glm::mat4(1.0),
            float(i) * animation.side_angle + float(animation.duration.progress(animation.rotation)),
            glm::vec3(0, 1, 0));

        auto translation = glm::translate(glm::mat4(1.0), glm::vec3(0, 0, identity_z_offset));
        return rotation * translation * glm::inverse(fb_transform);
    }

    /* Render the sides of the cube, using the given culling mode - cw or ccw */
    void render_cube(GLuint front_face, glm::mat4 fb_transform)
    {
        GL_CALL(glFrontFace(front_face));
        static const GLuint indexData[] = { 0, 1, 2, 0, 2, 3 };

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        (void) vy;
        for(size_t i = 0; i < streams.size(); i++)
        {
            int index = (vx + i) % streams.size();
            GL_CALL(glBindTexture(GL_TEXTURE_2D, streams[index].buffer.tex));

            auto model = calculate_model_matrix(i, fb_transform);
            GL_CALL(glUniformMatrix4fv(program.modelID, 1, GL_FALSE, &model[0][0]));

            if (tessellation_support) {
#ifdef USE_GLES32
                GL_CALL(glDrawElements(GL_PATCHES, 6, GL_UNSIGNED_INT, &indexData));
#endif
            } else {
                GL_CALL(glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, &indexData));
            }
        }
    }

    void render(const wf_framebuffer& dest)
    {
        update_workspace_streams();

        if (program.id == (uint32_t)-1)
            load_program();

        OpenGL::render_begin(dest);
        GL_CALL(glClear(GL_DEPTH_BUFFER_BIT));
        OpenGL::render_end();

        reload_background();
        background->render_frame(dest, animation);

        auto vp = calculate_vp_matrix(dest);

        OpenGL::render_begin(dest);
        GL_CALL(glUseProgram(program.id));
        GL_CALL(glEnable(GL_DEPTH_TEST));
        GL_CALL(glDepthFunc(GL_LESS));

        static GLfloat vertexData[] = {
            -0.5,  0.5,
            0.5,  0.5,
            0.5, -0.5,
            -0.5, -0.5
        };

        static GLfloat coordData[] = {
            0.0f, 1.0f,
            1.0f, 1.0f,
            1.0f, 0.0f,
            0.0f, 0.0f
        };

        GL_CALL(glVertexAttribPointer(program.posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glVertexAttribPointer(program.uvID, 2, GL_FLOAT, GL_FALSE, 0, coordData));

        GL_CALL(glEnableVertexAttribArray(program.posID));
        GL_CALL(glEnableVertexAttribArray(program.uvID));

        GL_CALL(glUniformMatrix4fv(program.vpID, 1, GL_FALSE, &vp[0][0]));
        if (tessellation_support)
        {
            GL_CALL(glUniform1i(program.defID, *use_deform));
            GL_CALL(glUniform1i(program.lightID, *use_light));
            GL_CALL(glUniform1f(program.easeID,
                    animation.duration.progress(animation.ease_deformation)));
        }

        /* We render the cube in two stages, based on winding.
         * By using two stages, we ensure that we first render the cube sides
         * that are on the back, and then we render those at the front, so we
         * don't have to use depth testing and we also can support alpha cube. */
        GL_CALL(glEnable(GL_CULL_FACE));
        render_cube(GL_CCW, dest.transform);
        render_cube(GL_CW, dest.transform);
        GL_CALL(glDisable(GL_CULL_FACE));

        GL_CALL(glDisable(GL_DEPTH_TEST));
        GL_CALL(glUseProgram(0));
        GL_CALL(glDisableVertexAttribArray(program.posID));
        GL_CALL(glDisableVertexAttribArray(program.uvID));
        OpenGL::render_end();

        update_view_matrix();
        if (animation.duration.running())
            schedule_next_frame();

        if (animation.in_exit && !animation.duration.running())
            deactivate();
    }

    void pointer_moved(wlr_event_pointer_motion* ev)
    {
        if (animation.in_exit)
            return;

        double xdiff = ev->delta_x;
        double ydiff = ev->delta_y;

        animation.zoom = {animation.duration.progress(animation.zoom), animation.zoom.end};

        double current_off_y = animation.duration.progress(animation.offset_y);
        double off_y = current_off_y + ydiff * YVelocity->as_cached_double();

        off_y = clamp(off_y, -1.5, 1.5);
        animation.offset_y = {current_off_y, off_y};
        animation.offset_z = {animation.duration.progress(animation.offset_z), animation.offset_z.end};

        float current_rotation = animation.duration.progress(animation.rotation);
        animation.rotation = {current_rotation,
            current_rotation + xdiff * XVelocity->as_cached_double()};

        animation.ease_deformation = {animation.duration.progress(animation.ease_deformation),
            animation.ease_deformation.end};

        animation.duration.start();

        schedule_next_frame();
    }

    void pointer_scrolled(double amount)
    {
        if (animation.in_exit)
            return;

        animation.offset_y = {animation.duration.progress(animation.offset_y), animation.offset_y.end};
        animation.offset_z = {animation.duration.progress(animation.offset_z), animation.offset_z.end};
        animation.rotation = {animation.duration.progress(animation.rotation), animation.rotation.end};
        animation.ease_deformation = {animation.duration.progress(animation.ease_deformation), animation.ease_deformation.end};

        float target_zoom = animation.duration.progress(animation.zoom);
        float start_zoom = target_zoom;

        target_zoom +=  std::min(std::pow(target_zoom, 1.5f), ZOOM_MAX) * amount * ZVelocity->as_cached_double();
        target_zoom = std::min(std::max(target_zoom, ZOOM_MIN), ZOOM_MAX);
        animation.zoom = {start_zoom, target_zoom};

        animation.duration.start();

        schedule_next_frame();
    }

    void fini()
    {
        if (output->is_plugin_active(grab_interface->name))
            deactivate();

        OpenGL::render_begin();
        for (size_t i = 0; i < streams.size(); i++)
            streams[i].buffer.release();
        OpenGL::render_end();

        output->rem_binding(&activate_binding);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_cube);
