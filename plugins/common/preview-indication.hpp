#include <geometry.hpp>
#include <config.hpp>
#include <animation.hpp>
#include <render-manager.hpp>
#include <workspace-manager.hpp>
#include <compositor-view.hpp>

namespace wf
{

/**
 * A view which can be used to show previews for different actions on the
 * screen, for ex. when snapping a view
 */
class preview_indication_view_t : public wf::color_rect_view_t
{
    wf::effect_hook_t pre_paint;

    /* Default colors */
    const wf::color_t base_color = {0.5, 0.5, 1, 0.5};
    const wf::color_t base_border = {0.25, 0.25, 0.5, 0.8};
    const int base_border_w = 3;

    wf_duration duration;

    /* The animation running */
    struct state_t
    {
        wf_geometry start_geometry, end_geometry;
        wf_transition alpha;
    } animation;

    bool should_close = false;

  public:

    /**
     * Create a new indication preview on the indicated output.
     *
     * @param start_geometry The geometry the preview should have, relative to
     *                       the output
     */
    preview_indication_view_t(wf::output_t *output, wf_geometry start_geometry)
        : wf::color_rect_view_t()
    {
        set_output(output);

        animation.start_geometry = animation.end_geometry = start_geometry;
        animation.alpha = {0, 1};

        duration = wf_duration{new_static_option("200")};
        pre_paint = [=] () { update_animation(); };
        get_output()->render->add_effect(&pre_paint, wf::OUTPUT_EFFECT_PRE);

        set_color(base_color);
        set_border_color(base_border);
        set_border(base_border_w);

        this->role = VIEW_ROLE_COMPOSITOR_VIEW;
        get_output()->workspace->add_view(self(), wf::LAYER_TOP);
    }

    /** A convenience wrapper around the full version */
    preview_indication_view_t(wf::output_t *output, wf_point start)
        :preview_indication_view_t(output, wf_geometry{start.x, start.y, 1, 1})
    { }

    /**
     * Animate the preview to the given target geometry and alpha.
     *
     * @param close Whether the view should be closed when the target is
     *              reached.
     */
    void set_target_geometry(wf_geometry target, float alpha, bool close = false)
    {
        animation.start_geometry.x = duration.progress(
            animation.start_geometry.x, animation.end_geometry.x);
        animation.start_geometry.y = duration.progress(
            animation.start_geometry.y, animation.end_geometry.y);
        animation.start_geometry.width = duration.progress(
            animation.start_geometry.width, animation.end_geometry.width);
        animation.start_geometry.height = duration.progress(
            animation.start_geometry.height, animation.end_geometry.height);

        animation.alpha = {duration.progress(animation.alpha), alpha};

        animation.end_geometry = target;
        duration.start();

        this->should_close = close;
    }

    /**
     * A wrapper around set_target_geometry(wf_geometry, double, bool)
     */
    void set_target_geometry(wf_point point, double alpha,
        bool should_close = false)
    {
        return set_target_geometry({point.x, point.y, 1, 1},
            alpha, should_close);
    }

    virtual ~preview_indication_view_t()
    {
        get_output()->render->rem_effect(&pre_paint);
    }

  protected:
    /** Update the current state */
    void update_animation()
    {
        wf_geometry current;
        current.x = duration.progress(animation.start_geometry.x,
            animation.end_geometry.x);
        current.y = duration.progress(animation.start_geometry.y,
            animation.end_geometry.y);
        current.width = duration.progress(animation.start_geometry.width,
            animation.end_geometry.width);
        current.height = duration.progress(animation.start_geometry.height,
            animation.end_geometry.height);

        if (current != geometry)
            set_geometry(current);

        auto alpha = duration.progress(animation.alpha);
        if (base_color.a * alpha != _color.a)
        {
            _color.a = alpha * base_color.a;
            _border_color.a = alpha * base_border.a;

            set_color(_color);
            set_border_color(_border_color);
        }

        /* The end of unmap animation, just exit */
        if (!duration.running() && should_close)
            close();
    }
};
}
