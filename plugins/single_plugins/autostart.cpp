#include <singleton-plugin.hpp>
#include <core.hpp>
#include <wayfire/util/log.hpp>

class wayfire_autostart
{
  public:
    wayfire_autostart()
    {
        /* Run only once, at startup */
        auto section = wf::get_core().config.get_section("autostart");
        for (const auto& command : section->get_registered_options())
            wf::get_core().run(command->get_value_str().c_str());
    }
};

DECLARE_WAYFIRE_PLUGIN((wf::singleton_plugin_t<wayfire_autostart, false>));
