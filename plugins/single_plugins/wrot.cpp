#include <wayfire/plugin.hpp>
#include "wayfire/view.hpp"
#include "wayfire/view-transform.hpp"
#include "wayfire/output.hpp"
#include "wayfire/core.hpp"
#include <linux/input.h>

static double cross(double x1, double y1, double x2, double y2) // cross product
{
    return x1 * y2 - x2 * y1;
}

static double vlen(double x1, double y1) // length of vector centered at the origin
{
    return std::sqrt(x1 * x1 + y1 * y1);
}

class wf_wrot : public wf::plugin_interface_t
{
    wf::button_callback call;
    wf::option_wrapper_t<double> reset_radius{"wrot/reset_radius"};

    wf::pointf_t last_position;
    wayfire_view current_view;

  public:
    void init() override
    {
        grab_interface->name = "wrot";
        grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;

        call = [=] (auto)
        {
            if (!output->activate_plugin(grab_interface))
            {
                return false;
            }

            current_view = wf::get_core().get_cursor_focus_view();
            if (!current_view || (current_view->role != wf::VIEW_ROLE_TOPLEVEL))
            {
                output->deactivate_plugin(grab_interface);

                return false;
            }

            output->focus_view(current_view, true);
            grab_interface->grab();

            last_position = output->get_cursor_position();
            return true;
        };

        output->add_button(
            wf::option_wrapper_t<wf::buttonbinding_t>("wrot/activate"), &call);

        grab_interface->callbacks.pointer.motion = [=] (int x, int y)
        {
            if (!current_view->get_transformer("wrot"))
            {
                current_view->add_transformer(std::make_unique<wf::view_2D>(
                    current_view), "wrot");
            }

            auto tr = dynamic_cast<wf::view_2D*>(current_view->get_transformer(
                "wrot").get());
            assert(tr);

            current_view->damage();

            auto g = current_view->get_wm_geometry();

            double cx = g.x + g.width / 2.0;
            double cy = g.y + g.height / 2.0;

            double x1 = last_position.x - cx, y1 = last_position.y - cy;
            double x2 = x - cx, y2 = y - cy;

            if (vlen(x2, y2) <= reset_radius)
            {
                return current_view->pop_transformer("wrot");
            }

            /* cross(a, b) = |a| * |b| * sin(a, b) */
            tr->angle -= std::asin(cross(x1, y1, x2, y2) / vlen(x1, y1) / vlen(x2,
                y2));

            current_view->damage();

            last_position = {1.0 * x, 1.0 * y};
        };

        grab_interface->callbacks.pointer.button = [=] (uint32_t, uint32_t s)
        {
            if (s == WLR_BUTTON_RELEASED)
            {
                input_released();
            }
        };

        grab_interface->callbacks.cancel = [=] ()
        {
            if (grab_interface->is_grabbed())
            {
                input_released();
            }
        };
    }

    void input_released()
    {
        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
    }

    void fini() override
    {
        if (grab_interface->is_grabbed())
        {
            input_released();
        }

        output->rem_binding(&call);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf_wrot);
