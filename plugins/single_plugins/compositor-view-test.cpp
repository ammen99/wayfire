#include "wayfire/compositor-view.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/output.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"

extern "C"
{
#define static
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_xcursor_manager.h>
#undef static
}

class test_view : public wayfire_compositor_view_t,
    public wayfire_compositor_interactive_view
{
  public:
    virtual void _wlr_render_box(const wf::framebuffer_t& fb, int x, int y,
        const wlr_box& scissor)
    {
        wlr_box g{x, y, geometry.width, geometry.height};
        geometry = fb.damage_box_from_geometry_box(g);

        float projection[9];
        wlr_matrix_projection(projection, fb.viewport_width, fb.viewport_height,
            (wl_output_transform)fb.wl_transform);

        float matrix[9];
        wlr_matrix_project_box(matrix, &g, WL_OUTPUT_TRANSFORM_NORMAL, 0,
            projection);

        OpenGL::render_begin(fb);
        auto sbox = scissor;
        wlr_renderer_scissor(wf::get_core().renderer, &sbox);

        float color[] = {1.0f, 0.0, 1.0f, 1.0f};

        wlr_render_quad_with_matrix(wf::get_core().renderer, color, matrix);
        OpenGL::render_end();
    }

    virtual bool accepts_input(int sx, int sy)
    {
        return 0 <= sx && sx < geometry.width &&
               0 <= sy && sy < geometry.height;
    }
};

class wayfire_cvtest : public wayfire_plugin_t
{
    wf::key_callback binding;

  public:
    void init(wayfire_config *config)
    {
        binding = [=] (uint32_t) {test();};
        output->add_key(new_static_option("<shift> <super> KEY_T"), &binding);
    }

    void test()
    {
        auto cv = new wayfire_mirror_view_t(output->get_top_view());

        auto v = std::unique_ptr<wayfire_view_t>{cv};
        wf::get_core().add_view(std::move(v));
        cv->map();
    }
};

extern "C"
{
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_cvtest;
    }
}
