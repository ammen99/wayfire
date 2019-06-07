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
#include <debug.hpp>
#include <animation.hpp>
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
    wf::post_hook_t hook;
    activator_callback toggle_cb;
    wf_duration duration;
    float target_zoom;
    bool active, hook_set;
    wf_option radius, zoom;

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
        void init(wayfire_config *config)
        {
            auto section = config->get_section("fisheye");
            auto toggle_key = section->get_option("toggle", "<super> KEY_F");
            radius = section->get_option("radius", "300");
            zoom = section->get_option("zoom", "7");

            target_zoom = zoom->as_double();

            hook = [=] (const wf_framebuffer_base& source, const wf_framebuffer_base& dest) {
                render(source, dest);
            };

            toggle_cb = [=] (wf_activator_source, uint32_t)
            {
                    if (active)
                    {
                        active = false;
                        duration.start(duration.progress(), 0);
                    } else
                    {
                        active = true;
                        duration.start(duration.progress(), target_zoom);

                        if (!hook_set)
                        {
                            hook_set = true;
                            output->render->add_post(&hook);
                            output->render->set_redraw_always();
                        }
                    }
            };
            output->add_activator(toggle_key, &toggle_cb);

            load_program();

            duration = wf_duration(new_static_option("700"));
            duration.start(0, 0); // so that the first value we get is correct
        }

        void render(const wf_framebuffer_base& source,
            const wf_framebuffer_base& dest)
        {
            GetTuple(x, y, output->get_cursor_position());
            wlr_box box = {x, y, 1, 1};
            box = output->render->get_target_framebuffer().
                framebuffer_box_from_geometry_box(box);
            x = box.x;
            y = box.y;

            static const float vertexData[] = {
                -1.0f, -1.0f,
                 1.0f, -1.0f,
                 1.0f,  1.0f,
                -1.0f,  1.0f
            };

            auto current_zoom = duration.progress();
            target_zoom = zoom->as_double();

            OpenGL::render_begin(dest);

            GL_CALL(glUseProgram(program));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, source.tex));
            GL_CALL(glActiveTexture(GL_TEXTURE0));

            GL_CALL(glUniform2f(mouseID, x, y));
            GL_CALL(glUniform2f(resID, dest.viewport_width, dest.viewport_height));
            GL_CALL(glUniform1f(radiusID, radius->as_double()));
            GL_CALL(glUniform1f(zoomID, current_zoom));

            GL_CALL(glVertexAttribPointer(posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));
            GL_CALL(glEnableVertexAttribArray(posID));

            GL_CALL(glDrawArrays (GL_TRIANGLE_FAN, 0, 4));
            GL_CALL(glDisableVertexAttribArray(posID));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));

            OpenGL::render_end();

            if (active)
            {
                /* Reset animation in case target_zoom
                 * was changed via config */
                duration.start(current_zoom, target_zoom);
            } else if (!duration.running())
            {
                finalize();
            }
        }

        void finalize()
        {
            output->render->rem_post(&hook);
            output->render->set_redraw_always(false);
            hook_set = false;
        }

        void fini()
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
