#include <core.hpp>
#include <opengl.hpp>
#include <render-manager.hpp>

/* The MIT License (MIT)
 *
 * Copyright (c) 2018 Ilia Bozhinov
 * Copyright (c) 2018 Scott Moreau
 *
 * The design of blur takes extra consideration due to the fact that
 * the results of blurred pixels rely on surrounding pixel values.
 * This means that when damage happens for only part of the scene (1),
 * blurring this area can result to artifacts because of sampling
 * beyond the edges of the area. To work around this issue, wayfire
 * issues two signals - workspace-stream-pre and workspace-stream-post.
 * workspace-stream-pre gives plugins an opportunity to pad the rects
 * of the damage region (2) and save a snap-shot of the padded area from
 * the buffer containing the last frame. This will be used to redraw
 * the area that will contain artifacts after rendering. This is ok
 * because this area is outside of the original damage area, so the
 * pixels wont be changing in this region of the scene. pre_render is
 * called with the padded damage region as an argument (2). The padded
 * damage extents (3) are used for blitting from the framebuffer, which
 * contains the scene rendered up until the view for which pre_render
 * is called. The padded damage extents rect is blurred with artifacts
 * in pre_render, after which it is then alpha blended with the window
 * and rendered to the framebuffer. Finally, workspace-stream-post
 * allows a chance to redraw the padded area with the saved pixels,
 * before swapping buffers. As long as the padding is enough to cover
 * the maximum sample offset that the shader uses, there should be a
 * seamless experience.
 *
 * 1)
 * .................................................................
 * |                                                               |
 * |                                                               |
 * |           ..................................                  |
 * |           |                                |..                |
 * |           |                                | |                |
 * |           |         Damage region          | |                |
 * |           |                                | |                |
 * |           |                                | |                |
 * |           |                                | |                |
 * |           |                                | |                |
 * |           ```|```````````````````````````````|                |
 * |              `````````````````````````````````                |
 * |                                                               |
 * |                                                               |
 * `````````````````````````````````````````````````````````````````
 *
 * 2)
 * .................................................................
 * |                                                               |
 * |         ......................................                |
 * |         | .................................. |..<-- Padding   |
 * |         | |                                |.. |              |
 * |         | |                                | | |              |
 * |         | |            Padded              | | |              |
 * |         | |         Damage region          | | |              |
 * |         | |                                | | |              |
 * |         | |                                | | |              |
 * |         | |                                | | |              |
 * |         | |                                | | |              |
 * |         | ```|```````````````````````````````| |              |
 * |         ```| ````````````````````````````````` |<-- Padding   |
 * |            `````````````````````````````````````              |
 * `````````````````````````````````````````````````````````````````
 *
 * 3)
 * .................................................................
 * |                                                               |
 * |       x1|                                      |x2            |
 * |   y1__  ...................................... .              |
 * |         |                                    |..              |
 * |         |                                      |              |
 * |         |                   ^                  |              |
 * |         |                                      |              |
 * |         |         <- Padded extents ->         |              |
 * |         |                                      |              |
 * |         |                   v                  |              |
 * |         |                                      |              |
 * |   y2__  ```|                                   |              |
 * |         `  `````````````````````````````````````              |
 * |                                                               |
 * `````````````````````````````````````````````````````````````````
 */
struct wf_blur_default_option_values
{
    std::string algorithm_name;
    std::string offset, degrade, iterations;
};

class wf_blur_base
{
    protected:
    /* used to store temporary results in blur algorithms, cleaned up in base
     * destructor */
    wf_framebuffer_base fb[2];
    /* the program created by the given algorithm, cleaned up in base destructor */
    GLuint program[2];
    /* the program used by wf_blur_base to combine the blurred, unblurred and
     * view texture */
    GLuint blend_program;
    GLuint blend_posID, blend_mvpID, blend_texID[2];

    /* used to get individual algorithm options from config
     * should be set by the constructor */
    std::string algorithm_name;

    wf_option offset_opt, degrade_opt, iterations_opt;
    wf_option_callback options_changed;

    wf::output_t *output;

    /* renders the in texture to the out framebuffer.
     * assumes a properly bound and initialized GL program */
    void render_iteration(wf_framebuffer_base& in, wf_framebuffer_base& out,
        int width, int height);

    /* copy the source pixels from region, storing into result
     * returns the result geometry, in framebuffer coords */
    wlr_box copy_region(wf_framebuffer_base& result,
        const wf_framebuffer& source, const wf_region& region);

    /* blur fb[0]
     * width and height are the scaled dimensions of the buffer
     * returns the index of the fb where the result is stored (0 or 1) */
    virtual int blur_fb0(int width, int height) = 0;

    public:
    wf_blur_base(wf::output_t *output,
        const wf_blur_default_option_values& values);
    virtual ~wf_blur_base();

    virtual int calculate_blur_radius();
    void damage_all_workspaces();

    virtual void pre_render(uint32_t src_tex, wlr_box src_box,
        const wf_region& damage, const wf_framebuffer& target_fb);

    virtual void render(uint32_t src_tex, wlr_box src_box, wlr_box scissor_box,
        const wf_framebuffer& target_fb);
};

std::unique_ptr<wf_blur_base> create_box_blur(wf::output_t *output);
std::unique_ptr<wf_blur_base> create_bokeh_blur(wf::output_t *output);
std::unique_ptr<wf_blur_base> create_kawase_blur(wf::output_t *output);
std::unique_ptr<wf_blur_base> create_gaussian_blur(wf::output_t *output);

std::unique_ptr<wf_blur_base> create_blur_from_name(wf::output_t *output,
    std::string algorithm_name);
