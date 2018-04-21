#include "view.hpp"
#include "view-transform.hpp"
#include "output.hpp"
#include <linux/input.h>

class wf_wrot : public wayfire_plugin_t
{
    button_callback call;
    public:
        void init(wayfire_config *config)
        {
            call = [=] (uint32_t x, int, int)
            {
                output->activate_plugin(grab_interface);
                grab_interface->grab();
            };

            output->add_button(WLR_MODIFIER_ALT, BTN_RIGHT, &call);

            grab_interface->callbacks.pointer.motion = [=] (int, int)
            {
                auto view = output->get_top_view();

                if (!view->get_transformer())
                {
                    auto og = view->get_output()->get_full_geometry();
                    view->set_transformer(std::unique_ptr<wf_2D_view> (new wf_2D_view(og.width, og.height)));
                }

                auto tr = dynamic_cast<wf_2D_view*> (view->get_transformer());
                view->damage();
                tr->angle += 0.02;
                view->damage();

 //               tr->angle += 0.01;
//                tr->angle = M_PI/2;
            };

            grab_interface->callbacks.pointer.button = [=] (uint32_t, uint32_t s)
            {
                if (s == WLR_BUTTON_RELEASED)
                {
                    grab_interface->ungrab();
                    output->deactivate_plugin(grab_interface);
                }
            };
        }
};

extern "C"
{
wayfire_plugin_t *newInstance()
{
    return new wf_wrot;
}
}
