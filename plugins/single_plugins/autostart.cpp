#include <wayfire/singleton-plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/option-wrapper.hpp>
#include <config.h>

class wayfire_autostart
{
    wf::option_wrapper_t<bool> autostart_wf_shell{"autostart/autostart_wf_shell"};

  public:
    wayfire_autostart()
    {
        /* Run only once, at startup */
        auto section = wf::get_core().config.get_section("autostart");

        bool panel_manually_started = false;
        bool background_manually_started = false;

        for (const auto& command : section->get_registered_options())
        {
            auto cmd = command->get_value_str();
            wf::get_core().run(cmd);

            if (cmd.find("wf-panel") != std::string::npos)
            {
                panel_manually_started = true;
            }

            if (cmd.find("wf-background") != std::string::npos)
            {
                background_manually_started = true;
            }
        }

        if (autostart_wf_shell && !panel_manually_started)
        {
            wf::get_core().run("wf-panel");
        }

        if (autostart_wf_shell && !background_manually_started)
        {
            wf::get_core().run("wf-background");
        }
    }
};

DECLARE_WAYFIRE_PLUGIN((wf::singleton_plugin_t<wayfire_autostart, false>));
