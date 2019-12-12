#include "blur.hpp"
#include <debug.hpp>
#include <output.hpp>
#include <workspace-manager.hpp>

static const char* blur_blend_vertex_shader = R"(
#version 100

attribute mediump vec2 position;
varying mediump vec2 uvpos[2];

uniform mat4 mvp;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
    uvpos[0] = (position.xy + vec2(1.0, 1.0)) / 2.0;
    uvpos[1] = vec4(mvp * vec4(uvpos[0] - 0.5, 0.0, 1.0)).xy + 0.5;
})";

static const char* blur_blend_fragment_shader = R"(
#version 100
precision mediump float;

uniform sampler2D window_texture;
uniform sampler2D bg_texture;

varying mediump vec2 uvpos[2];

void main()
{
    vec4 bp = texture2D(bg_texture, uvpos[0]);
    vec4 wp = texture2D(window_texture, uvpos[1]);
    vec4 c = clamp(4.0 * wp.a, 0.0, 1.0) * bp;
    gl_FragColor = wp + (1.0 - wp.a) * c;
})";

wf_blur_base::wf_blur_base(wf::output_t *output,
    const wf_blur_default_option_values& defaults)
{
    this->output = output;
    this->algorithm_name = defaults.algorithm_name;

    auto section = wf::get_core().config->get_section("blur");
    this->offset_opt = section->get_option(algorithm_name + "_offset",
        defaults.offset);
    this->degrade_opt = section->get_option(algorithm_name + "_degrade",
        defaults.degrade);
    this->iterations_opt = section->get_option(algorithm_name + "_iterations",
        defaults.iterations);

    this->options_changed = [=] () { damage_all_workspaces(); };
    this->offset_opt->add_updated_handler(&options_changed);
    this->degrade_opt->add_updated_handler(&options_changed);
    this->iterations_opt->add_updated_handler(&options_changed);

    OpenGL::render_begin();
    blend_program = OpenGL::create_program_from_source(
        blur_blend_vertex_shader, blur_blend_fragment_shader);

    blend_posID    = GL_CALL(glGetAttribLocation(blend_program, "position"));

    blend_mvpID    = GL_CALL(glGetUniformLocation(blend_program, "mvp"));
    blend_texID[0] = GL_CALL(glGetUniformLocation(blend_program, "window_texture"));
    blend_texID[1] = GL_CALL(glGetUniformLocation(blend_program, "bg_texture"));

    OpenGL::render_end();
}

wf_blur_base::~wf_blur_base()
{
    this->offset_opt->rem_updated_handler(&options_changed);
    this->degrade_opt->rem_updated_handler(&options_changed);
    this->iterations_opt->rem_updated_handler(&options_changed);

    OpenGL::render_begin();
    fb[0].release();
    fb[1].release();
    GL_CALL(glDeleteProgram(program[0]));
    GL_CALL(glDeleteProgram(program[1]));
    GL_CALL(glDeleteProgram(blend_program));
    OpenGL::render_end();
}

int wf_blur_base::calculate_blur_radius()
{
    return offset_opt->as_cached_double() * degrade_opt->as_cached_int() * iterations_opt->as_cached_int();
}

void wf_blur_base::damage_all_workspaces()
{
    auto wsize = output->workspace->get_workspace_grid_size();
    for (int vx = 0; vx < wsize.width; vx++)
    {
        for (int vy = 0; vy < wsize.height; vy++)
        {
            output->render->damage(
                output->render->get_ws_box({vx, vy}));
        }
    }
}

void wf_blur_base::render_iteration(wf_framebuffer_base& in,
    wf_framebuffer_base& out, int width, int height)
{
    /* Special case for small regions where we can't really blur, because we
     * simply have too few pixels */
    width = std::max(width, 1);
    height = std::max(height, 1);

    out.allocate(width, height);
    out.bind();

    GL_CALL(glBindTexture(GL_TEXTURE_2D, in.tex));
    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
}

wlr_box wf_blur_base::copy_region(wf_framebuffer_base& result,
    const wf_framebuffer& source, const wf_region& region)
{
    auto subbox = source.framebuffer_box_from_damage_box(
        wlr_box_from_pixman_box(region.get_extents()));

    auto source_box = source.framebuffer_box_from_geometry_box({
        0, 0, source.geometry.width, source.geometry.height});

    /* Scaling down might cause issues like flickering or some discrepancies
     * between the source and final image.
     * To make things a bit more stable, we first blit to a size which
     * is divisble by degrade */
    int degrade = degrade_opt->as_int();
    int rounded_width = std::max(1, subbox.width + subbox.width % degrade);
    int rounded_height = std::max(1, subbox.height + subbox.height % degrade);

    OpenGL::render_begin(source);
    result.allocate(rounded_width, rounded_height);

    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, source.fb));
    GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, result.fb));
    GL_CALL(glBlitFramebuffer(
            subbox.x, source_box.height - subbox.y - subbox.height,
            subbox.x + subbox.width, source_box.height - subbox.y,
            0, 0, rounded_width, rounded_height,
            GL_COLOR_BUFFER_BIT, GL_LINEAR));
    OpenGL::render_end();

    return subbox;
}

void wf_blur_base::pre_render(uint32_t src_tex, wlr_box src_box,
    const wf_region& damage, const wf_framebuffer& target_fb)
{
    int degrade = degrade_opt->as_int();
    auto damage_box = copy_region(fb[0], target_fb, damage);
    int scaled_width = std::max(1, damage_box.width / degrade);
    int scaled_height = std::max(1, damage_box.height / degrade);

    int r = blur_fb0(scaled_width, scaled_height);

    /* Make sure the result is always fb[1], because that's what is used in render() */
    if (r != 0)
        std::swap(fb[0], fb[1]);

    /* Support iterations = 0 */
    if (iterations_opt->as_int() == 0 && algorithm_name != "bokeh")
    {
        int rounded_width = std::max(1, damage_box.width + damage_box.width % degrade);
        int rounded_height = std::max(1, damage_box.height + damage_box.height % degrade);
        OpenGL::render_begin();
        fb[1].allocate(scaled_width, scaled_height);
        fb[1].bind();
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0].fb));
        GL_CALL(glBlitFramebuffer(0, 0, rounded_width, rounded_height,
                0, 0, scaled_width, scaled_height,
                GL_COLOR_BUFFER_BIT, GL_LINEAR));
        OpenGL::render_end();
        std::swap(fb[0], fb[1]);
    }

    /* we subtract target_fb's position to so that
     * view box is relative to framebuffer */
    auto view_box = target_fb.framebuffer_box_from_geometry_box(
        src_box + wf_point{-target_fb.geometry.x, -target_fb.geometry.y});

    OpenGL::render_begin();
    fb[1].allocate(view_box.width, view_box.height);
    fb[1].bind();
    GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0].fb));

    /* Blit the blurred texture into an fb which has the size of the view,
     * so that the view texture and the blurred background can be combined
     * together in render()
     *
     * local_geometry is damage_box relative to view box */
    wlr_box local_box = damage_box + wf_point{-view_box.x, -view_box.y};
    GL_CALL(glBlitFramebuffer(0, 0, scaled_width, scaled_height,
            local_box.x,
            view_box.height - local_box.y - local_box.height,
            local_box.x + local_box.width,
            view_box.height - local_box.y,
            GL_COLOR_BUFFER_BIT, GL_LINEAR));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    OpenGL::render_end();
}

void wf_blur_base::render(uint32_t src_tex, wlr_box src_box, wlr_box scissor_box,
    const wf_framebuffer& target_fb)
{
    wlr_box fb_geom = target_fb.framebuffer_box_from_geometry_box(target_fb.geometry);
    auto view_box = target_fb.framebuffer_box_from_geometry_box(src_box);
    view_box.x -= fb_geom.x;
    view_box.y -= fb_geom.y;

    OpenGL::render_begin(target_fb);

    /* Use shader and enable vertex and texcoord data */
    GL_CALL(glUseProgram(blend_program));
    GL_CALL(glEnableVertexAttribArray(blend_posID));
    static const float vertexData[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f,
        -1.0f,  1.0f
    };

    GL_CALL(glVertexAttribPointer(blend_posID, 2, GL_FLOAT, GL_FALSE, 0, vertexData));

    /* Blend blurred background with window texture src_tex */
    GL_CALL(glUniformMatrix4fv(blend_mvpID, 1, GL_FALSE, &glm::inverse(target_fb.transform)[0][0]));
    GL_CALL(glUniform1i(blend_texID[0], 0));
    GL_CALL(glUniform1i(blend_texID[1], 1));

    GL_CALL(glActiveTexture(GL_TEXTURE0 + 0));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, src_tex));
    GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, fb[1].tex));
    /* Render it to target_fb */
    target_fb.bind();
    GL_CALL(glViewport(view_box.x, fb_geom.height - view_box.y - view_box.height,
            view_box.width, view_box.height));
    target_fb.scissor(scissor_box);

    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

    /* Disable stuff */
    GL_CALL(glUseProgram(0));
    /* GL_CALL(glActiveTexture(GL_TEXTURE0 + 1)); */
    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    GL_CALL(glActiveTexture(GL_TEXTURE0));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
    GL_CALL(glDisableVertexAttribArray(blend_posID));

    OpenGL::render_end();
}

std::unique_ptr<wf_blur_base> create_blur_from_name(wf::output_t *output,
    std::string algorithm_name)
{
    if (algorithm_name == "box")
        return create_box_blur(output);
    if (algorithm_name == "bokeh")
        return create_bokeh_blur(output);
    if (algorithm_name == "kawase")
        return create_kawase_blur(output);
    if (algorithm_name == "gaussian")
        return create_gaussian_blur(output);

    LOGE ("Unrecognized blur algorithm %s. Using default kawase blur.",
        algorithm_name.c_str());
    return create_kawase_blur(output);
}
