/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <plugin.hpp>
#include <output.hpp>
#include <opengl.hpp>
#include <wayfire/util/duration.hpp>
#include <render-manager.hpp>

static const char* vertex_shader =
R"(
#version 100

attribute mediump vec2 position;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
}
)";

static const char* fragment_shader =
R"(
#version 100
precision mediump float;

uniform vec2 u_resolution;
uniform vec2 u_mouse;
uniform float u_radius;
uniform float u_zoom;
uniform sampler2D u_texture;

const float PI = 3.1415926535;

void main()
{
        float radius = u_radius;

        float zoom = u_zoom;
        float pw = 1.0 / u_resolution.x;
        float ph = 1.0 / u_resolution.y;

        vec4 p0 = vec4(u_mouse.x, u_resolution.y - u_mouse.y, 1.0 / radius, 0.0);
        vec4 p1 = vec4(pw, ph, PI / radius, (zoom - 1.0) * zoom);
        vec4 p2 = vec4(0, 0, -PI / 2.0, 0.0);

        vec4 t0, t1, t2, t3;

        vec3 tc = vec3(1.0, 0.0, 0.0);
        vec2 uv = vec2(gl_FragCoord.x, gl_FragCoord.y);

        t1 = p0.xyww - vec4(uv, 0.0, 0.0);
        t2.x = t2.y = t2.z = t2.w = 1.0 / sqrt(dot(t1.xyz, t1.xyz));
        t0 = t2 - p0;

        t3.x = t3.y = t3.z = t3.w = 1.0 / t2.x;
        t3 = t3 * p1.z + p2.z;
        t3.x = t3.y = t3.z = t3.w = cos(t3.x);

        t3 = t3 * p1.w;

        t1 = t2 * t1;
        t1 = t1 * t3 + vec4(uv, 0.0, 0.0);

        if (t0.z < 0.0) {
                t1.x = uv.x;
                t1.y = uv.y;
        }

        t1 = t1 * p1 + p2;

        tc = texture2D(u_texture, t1.xy).rgb;

        gl_FragColor = vec4(tc, 1.0);
}
)";

class wayfire_fisheye : public wf::plugin_interface_t
{
    wf::animation::simple_animation_t progression{wf::create_option<int>(300)};

    float target_zoom;
    bool active, hook_set;

    wf::option_wrapper_t<double> radius{"fisheye/radius"};
    wf::option_wrapper_t<double> zoom{"fisheye/zoom"};

    GLuint program, posID, mouseID, resID, radiusID, zoomID;

    void load_program()
    {
        OpenGL::render_begin();
        program = OpenGL::create_program_from_source(
            vertex_shader, fragment_shader);

        posID = GL_CALL(glGetAttribLocation(program, "position"));
        mouseID  = GL_CALL(glGetUniformLocation(program, "u_mouse"));
        resID  = GL_CALL(glGetUniformLocation(program, "u_resolution"));
        radiusID  = GL_CALL(glGetUniformLocation(program, "u_radius"));
        zoomID  = GL_CALL(glGetUniformLocation(program, "u_zoom"));

        OpenGL::render_end();
    }

    public:
        void init() override
        {
            grab_interface->name = "fisheye";
            grab_interface->capabilities = 0;

            hook_set = active = false;
            output->add_activator(
                wf::option_wrapper_t<wf::activatorbinding_t>{"fisheye/toggle"},
                &toggle_cb);

            target_zoom = zoom;
            zoom.set_callback([=] () {
                if (active)
                    this->progression.animate(zoom);
            });

            load_program();
        }

        activator_callback toggle_cb = [=] (wf_activator_source, uint32_t)
        {
            if (!output->can_activate_plugin(grab_interface))
                return false;

            if (active)
            {
                active = false;
                progression.animate(0);
            } else
            {
                active = true;
                progression.animate(zoom);
                if (!hook_set)
                {
                    hook_set = true;
                    output->render->add_post(&render_hook);
                    output->render->set_redraw_always();
                }
            }

            return true;
        };

        wf::post_hook_t render_hook = [=](const wf_framebuffer_base& source,
            const wf_framebuffer_base& dest)
        {
            auto oc = output->get_cursor_position();
            wlr_box box = {(int)oc.x, (int)oc.y, 1, 1};
            box = output->render->get_target_framebuffer().
                framebuffer_box_from_geometry_box(box);
            oc.x = box.x;
            oc.y = box.y;

            static const float vertexData[] = {
                -1.0f, -1.0f,
                 1.0f, -1.0f,
                 1.0f,  1.0f,
                -1.0f,  1.0f
            };

            OpenGL::render_begin(dest);

            GL_CALL(glUseProgram(program));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, source.tex));
            GL_CALL(glActiveTexture(GL_TEXTURE0));

            GL_CALL(glUniform2f(mouseID, oc.x, oc.y));
            GL_CALL(glUniform2f(resID, dest.viewport_width, dest.viewport_height));
            GL_CALL(glUniform1f(radiusID, radius));
            GL_CALL(glUniform1f(zoomID, progression));

            GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
            GL_CALL(glEnableVertexAttribArray(posID));

            GL_CALL(glDrawArrays (GL_TRIANGLE_FAN, 0, 4));
            GL_CALL(glDisableVertexAttribArray(posID));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));

            OpenGL::render_end();

            if (!active && !progression.running())
                finalize();
        };

        void finalize()
        {
            output->render->rem_post(&render_hook);
            output->render->set_redraw_always(false);
            hook_set = false;
        }

        void fini() override
        {
            if (hook_set)
                finalize();

            OpenGL::render_begin();
            GL_CALL(glDeleteProgram(program));
            OpenGL::render_end();

            output->rem_binding(&toggle_cb);
        }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_fisheye);
