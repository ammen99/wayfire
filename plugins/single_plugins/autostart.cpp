#include <plugin.hpp>
#include <core.hpp>

class wayfire_autostart_core_data : public wf::custom_data_t { };

class wayfire_autostart : public wf::plugin_interface_t
{
    public:
    void init(wayfire_config *config)
    {
        /* Run only once, at startup */
        if (wf::get_core().has_data<wayfire_autostart_core_data> ())
            return;

        auto section = config->get_section("autostart");
        for (const auto& command : section->options)
            wf::get_core().run(command->as_string().c_str());

        wf::get_core().get_data_safe<wayfire_autostart_core_data>();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_autostart);
