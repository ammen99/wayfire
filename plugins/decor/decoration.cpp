#include <plugin.hpp>
#include <view.hpp>
#include <workspace-manager.hpp>
#include <output.hpp>
#include <debug.hpp>
#include <signal-definitions.hpp>

#include "deco-subsurface.hpp"
class wayfire_decoration : public wf::plugin_interface_t
{
    wf_option font;
    wf::signal_callback_t view_created;

    public:
    void init(wayfire_config *config)
    {
        grab_interface->name = "simple-decoration";
        grab_interface->capabilities = wf::CAPABILITY_VIEW_DECORATOR;

        font = config->get_section("decoration")->get_option("font", "serif");

        view_created = [=] (wf::signal_data_t *data)
        {
            new_view(get_signaled_view(data));
        };

        output->connect_signal("map-view", &view_created);
    }

    wf::wl_idle_call idle_deactivate;
    void new_view(wayfire_view view)
    {
        if (view->should_be_decorated())
        {
            if (output->activate_plugin(grab_interface))
            {
                init_view(view, font);
                idle_deactivate.run_once([this] () {
                    output->deactivate_plugin(grab_interface);
                });
            }
        }
    }

    void fini()
    {
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
            view->set_decoration(nullptr);

        output->disconnect_signal("map-view", &view_created);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_decoration);
