#include "blur.hpp"
#include <debug.hpp>
#include <output.hpp>
#include <workspace-manager.hpp>

wf_blur_base::wf_blur_base(wayfire_output *output,
    const wf_blur_default_option_values& defaults)
{
    this->output = output;
    this->algorithm_name = defaults.algorithm_name;

    auto section = core->config->get_section("blur");
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
}

wf_blur_base::~wf_blur_base()
{
    this->offset_opt->rem_updated_handler(&options_changed);
    this->degrade_opt->rem_updated_handler(&options_changed);
    this->iterations_opt->rem_updated_handler(&options_changed);

    OpenGL::render_begin();
    fb[0].release();
    fb[1].release();
    GL_CALL(glDeleteProgram(program));
    OpenGL::render_end();
}

int wf_blur_base::calculate_blur_radius()
{
    log_info("got %d %d %d", offset_opt->as_cached_int() , degrade_opt->as_cached_int()
        , iterations_opt->as_cached_int());
    return offset_opt->as_double() * degrade_opt->as_int() * iterations_opt->as_int();
}

void wf_blur_base::damage_all_workspaces()
{
    GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
    for (int vx = 0; vx < vw; vx++)
    {
        for (int vy = 0; vy < vh; vy++)
        {
            output->render->damage(
                output->render->get_ws_box(std::make_tuple(vx, vy)));
        }
    }
}

void wf_blur_base::render_iteration(wf_framebuffer_base& in,
    wf_framebuffer_base& out, int width, int height)
{
    out.allocate(width, height);
    out.bind();

    GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
    GL_CALL(glBindTexture(GL_TEXTURE_2D, in.tex));
    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
}

std::unique_ptr<wf_blur_base> create_blur_from_name(wayfire_output *output,
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

    log_error ("Unrecognized blur algorithm %s. Using default kawase blur.",
        algorithm_name.c_str());
    return create_kawase_blur(output);
}
