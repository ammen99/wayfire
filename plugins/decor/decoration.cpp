#include <view.hpp>
#include <workspace-manager.hpp>
#include <output.hpp>
#include <debug.hpp>
#include <signal-definitions.hpp>

#include "deco-subsurface.hpp"
class wayfire_decoration : public wayfire_plugin_t
{
    wf_option font;
    wf::signal_callback_t view_created;

    public:
    void init(wayfire_config *config)
    {
        font = config->get_section("decoration")->get_option("font", "serif");

        view_created = [=] (wf::signal_data_t *data)
        {
            new_view(get_signaled_view(data));
        };

        output->connect_signal("map-view", &view_created);
    }

    void new_view(wayfire_view view)
    {
        if (view->should_be_decorated())
            init_view(view, font);
    }

    void fini()
    {
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
            view->set_decoration(nullptr);

        output->disconnect_signal("map-view", &view_created);
    }
};

extern "C"
{
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_decoration;
    }
}

