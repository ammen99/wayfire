/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Scott Moreau
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
 *
 * Shaders ported from:
 * https://www.shadertoy.com/view/4sd3WB (Buffer A and B)
 * https://www.shadertoy.com/view/Xsd3DB (Image)
 *
 */

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>

static const char* vertex_shader =
R"(
#version 100

attribute mediump vec2 position;
attribute highp vec2 uvPosition;

varying highp vec2 uvpos;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
    uvpos = uvPosition;
}
)";

static const char* fragment_shader_a =
R"(
#version 100
precision mediump float;

uniform int num_points;
uniform vec2 points[64];
uniform int button_down;
varying highp vec2 uvpos;
uniform sampler2D u_texture;

void main()
{
    int i;
    for (i = 0; i < num_points; i++)
    {
        vec2 r = gl_FragCoord.xy - points[i];
        float d = 0.001 * dot(r, r);
        if (button_down == 1 && d < 0.05)
        {
            gl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);
            return;
        }
    }

    gl_FragColor = texture2D(u_texture, uvpos);
}
)";

static const char* fragment_shader_b =
R"(
#version 100
precision mediump float;

uniform vec2 resolution;
varying highp vec2 uvpos;
uniform sampler2D u_texture;

void main()
{
    float dx = resolution.x;
    float dy = resolution.y;
    vec2 uv = uvpos;

    vec2 udu = texture2D(u_texture, uv).xy;
    // old elevation
    float u = udu.x;
    // old velocity
    float du = udu.y;

    // Finite differences
    float ux = texture2D(u_texture, vec2(uv.x + dx, uv.y)).x;
    float umx = texture2D(u_texture, vec2(uv.x - dx, uv.y)).x;
    float uy = texture2D(u_texture, vec2(uv.x, uv.y + dy)).x;
    float umy = texture2D(u_texture, vec2(uv.x, uv.y - dy)).x;

    // new elevation
    float nu = u + du + 0.25 * (umx + ux + umy + uy - 4.0 * u);
    nu *= 0.99;

    // evaporation
    if (nu < 0.025)
    {
        nu *= 0.2;
    }

    // store elevation and velocity
    gl_FragColor = vec4(nu, nu - u, 0.0, 0.0);
}
)";

static const char* fragment_shader_c =
R"(
#version 100
precision mediump float;

#define TEXTURED 1

uniform float fade;
uniform vec2 resolution;
varying highp vec2 uvpos;
uniform sampler2D u_texture;
uniform sampler2D water_texture;

void main()
{
    vec2 uv = uvpos;
#if TEXTURED == 1
    vec3 e = vec3(resolution, 0.);
    float p10 = texture2D(water_texture, uv - e.zy).x;
    float p01 = texture2D(water_texture, uv - e.xz).x;
    float p21 = texture2D(water_texture, uv + e.xz).x;
    float p12 = texture2D(water_texture, uv + e.zy).x;

    vec3 grad = normalize(vec3(p21 - p01, p12 - p10, 1.));
    vec4 c = texture2D(u_texture, uv + grad.xy * .35);
    vec4 color = texture2D(u_texture, uv);
    vec3 light = normalize(vec3(.2, -.5, .7));
    float diffuse = dot(grad, light);
    if (diffuse > 0.75)
    {
        diffuse = 1.0;
    }
    float spec = pow(max(0., -reflect(light, grad).z), 32.);
    c = c * diffuse;

    gl_FragColor = (c + spec) * fade + color * (1. - fade);
#else
    float h = texture2D(water_texture, uv).x;
    float sh = 1.35 - h * 2.;
    vec3 c =
       vec3(exp(pow(sh - .75, 2.) * -10.),
            exp(pow(sh - .50, 2.) * -20.),
            exp(pow(sh - .25, 2.) * -10.));
    vec4 color = texture2D(u_texture, uv);
    gl_FragColor = vec4(c, 1.) * fade + color * (1. - fade);
#endif
}
)";

class wayfire_water_screen : public wf::plugin_interface_t
{
    OpenGL::program_t program[3];
    wf::framebuffer_t buffer[2];
    wf::option_wrapper_t<wf::buttonbinding_t> button{"water/activate"};
    wf::animation::simple_animation_t animation =
        wf::animation::simple_animation_t(wf::create_option<int>(5000));
    bool button_down = false;
    bool hook_set = false;
    wf::pointf_t last_cursor;
    int points_loc;
  public:
    void init() override
    {
        grab_interface->name = "water";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        OpenGL::render_begin();
        program[0].set_simple(
            OpenGL::compile_program(vertex_shader, fragment_shader_a));
        program[1].set_simple(
            OpenGL::compile_program(vertex_shader, fragment_shader_b));
        program[2].set_simple(
            OpenGL::compile_program(vertex_shader, fragment_shader_c));
        points_loc = GL_CALL(glGetUniformLocation(
            program[0].get_program_id(wf::TEXTURE_TYPE_RGBA), "points"));
        OpenGL::render_end();

        output->add_button(button, &activate_binding);

        grab_interface->callbacks.pointer.button = [=] (uint32_t b, uint32_t s)
        {
            if (s == WL_POINTER_BUTTON_STATE_RELEASED)
            {
                output->deactivate_plugin(grab_interface);
                animation.animate(animation, 0);
                grab_interface->ungrab();
                button_down = false;
            }
        };
        animation.set(0, 0);
    }

    wf::button_callback activate_binding = [=] (uint32_t, int, int)
    {
        if (!output->is_plugin_active(grab_interface->name))
        {
            if (!output->activate_plugin(grab_interface))
            {
                return false;
            }
        }

        animation.animate(animation, 1);

        if (!hook_set)
        {
            output->render->add_post(&render);
            hook_set = true;
        }
        last_cursor = output->get_cursor_position();
        grab_interface->grab();
        button_down = true;
        return true;
    };

    wf::post_hook_t render = [=] (const wf::framebuffer_base_t& source,
        const wf::framebuffer_base_t& destination)
    {
        auto transform = output->render->get_target_framebuffer().transform;
        auto cursor_position = output->get_cursor_position();
        auto og = output->get_relative_geometry();
        auto fbg = output->render->get_target_framebuffer().
            framebuffer_box_from_geometry_box(output->get_relative_geometry());
        transform = glm::inverse(transform);

        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f,  1.0f,
            -1.0f,  1.0f
        };

        static const float coordData[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f
        };

        wf::pointf_t step;
        std::vector<float> points;
        wf::pointf_t center{0.5, 0.5};
        auto d = glm::distance(glm::vec2(last_cursor.x, last_cursor.y),
            glm::vec2(cursor_position.x, cursor_position.y));

        /* Interpolate between last and current cursor */
        int num_points = std::min(int(d / 5 + 1), 64);
        step.x = (cursor_position.x - last_cursor.x) / num_points;
        step.y = (cursor_position.y - last_cursor.y) / num_points;
        for (int i = 0; i < num_points; i++)
        {
            wf::pointf_t p = wf::pointf_t{
                cursor_position.x - step.x * i,
                cursor_position.y - step.y * i};

            float x = p.x / og.width;
            float y = p.y / og.height;
	    
            /* Apply transform to cursor position */
            glm::vec4 point{x - center.x, y - center.y, 1.0, 1.0};
            glm::vec4 result = transform * point;
            x = (result.x + center.x) * fbg.width;
            y = (result.y + center.y) * fbg.height;
            y = fbg.height - y;
            points.push_back(x);
            points.push_back(y);
        }
        last_cursor = cursor_position;

        /* First pass */
        OpenGL::render_begin();
        if (buffer[0].allocate(fbg.width, fbg.height))
        {
            buffer[0].bind();
            buffer[0].geometry = fbg;
            OpenGL::clear({0, 0, 0, 1});
        }
        if (buffer[1].allocate(fbg.width, fbg.height))
        {
            buffer[1].bind();
            buffer[1].geometry = fbg;
            OpenGL::clear({0, 0, 0, 1});
        }
        buffer[0].bind();
        program[0].use(wf::TEXTURE_TYPE_RGBA);
        program[0].attrib_pointer("position", 2, 0, vertexData);
        program[0].attrib_pointer("uvPosition", 2, 0, coordData);
        GL_CALL(glUniform2fv(points_loc, num_points, points.data()));
        program[0].uniform1i("num_points", num_points);
        program[0].uniform1i("button_down", button_down ? 1 : 0);
        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, buffer[1].tex));

        GL_CALL(glDisable(GL_BLEND));
        GL_CALL(glDrawArrays (GL_TRIANGLE_FAN, 0, 4));
        GL_CALL(glEnable(GL_BLEND));

        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        program[0].deactivate();
        OpenGL::render_end();

        /* Second pass */
        OpenGL::render_begin(buffer[1]);
        program[1].use(wf::TEXTURE_TYPE_RGBA);
        program[1].attrib_pointer("position", 2, 0, vertexData);
        program[1].attrib_pointer("uvPosition", 2, 0, coordData);
        program[1].uniform2f("resolution", 1.0 / fbg.width, 1.0 / fbg.height);
        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, buffer[0].tex));

        GL_CALL(glDisable(GL_BLEND));
        GL_CALL(glDrawArrays (GL_TRIANGLE_FAN, 0, 4));
        GL_CALL(glEnable(GL_BLEND));

        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        program[1].deactivate();
        OpenGL::render_end();

        /* Final pass */
        OpenGL::render_begin(destination);
        program[2].use(wf::TEXTURE_TYPE_RGBA);
        program[2].attrib_pointer("position", 2, 0, vertexData);
        program[2].attrib_pointer("uvPosition", 2, 0, coordData);
        program[2].uniform2f("resolution", 1.0 / fbg.width, 1.0 / fbg.height);
        program[2].uniform1f("fade", animation);
        program[2].uniform1i("water_texture", 1);
        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, source.tex));
        GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, buffer[1].tex));

        GL_CALL(glDisable(GL_BLEND));
        GL_CALL(glDrawArrays (GL_TRIANGLE_FAN, 0, 4));
        GL_CALL(glEnable(GL_BLEND));

        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        GL_CALL(glActiveTexture(GL_TEXTURE0));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
        program[2].deactivate();
        OpenGL::render_end();

        if (!button_down && !animation.running())
        {
            hook_set = false;
            output->render->rem_post(&render);
            OpenGL::render_begin();
            buffer[0].release();
            buffer[1].release();
            OpenGL::render_end();
        }

        output->render->schedule_redraw();
    };

    void fini() override
    {
        output->deactivate_plugin(grab_interface);
        output->rem_binding(&activate_binding);
        grab_interface->ungrab();
        if (hook_set)
        {
            output->render->rem_post(&render);
        }

        OpenGL::render_begin();
        buffer[0].release();
        buffer[1].release();
        program[0].free_resources();
        program[1].free_resources();
        program[2].free_resources();
        OpenGL::render_end();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_water_screen);
