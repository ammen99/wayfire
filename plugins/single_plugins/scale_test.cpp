#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugins/common/scale-signal.hpp>

class wayfire_scale_test : public wf::plugin_interface_t
{
    wf::option_wrapper_t<std::string> app_id_filter{"scale_test/app_id"};
    wf::option_wrapper_t<bool> case_sensitive{"scale_test/case_sensitive"};
    wf::option_wrapper_t<bool> all_workspaces{"scale_test/all_workspaces"};
    bool active = false;

    bool should_show_view(wayfire_view view) const
    {
        const std::string& app_id_str = app_id_filter;
        if (app_id_str.empty())
        {
            return true;
        }

        if (!case_sensitive)
        {
            std::string app_id = view->get_app_id();
            std::transform(app_id.begin(), app_id.end(), app_id.begin(),
                [] (unsigned char c)
            {
                return (char)std::tolower(c);
            });
            return app_id == app_id_str;
        } else
        {
            return view->get_app_id() == app_id_str;
        }
    }

  public:
    void init() override
    {
        grab_interface->name = "scale_test";
        grab_interface->capabilities = 0;

        output->connect_signal("scale-filter", &view_filter);
        output->connect_signal("scale-end", &scale_end);

        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"scale_test/activate"},
            &activate);
    }

    void fini() override
    {
        output->rem_binding(&activate);
        output->disconnect_signal(&view_filter);
        output->disconnect_signal(&scale_end);
    }

    wf::activator_callback activate = [=] (auto)
    {
        active = true;
        scale_activate_signal data;
        data.all_workspaces = all_workspaces;
        output->emit_signal("scale-activate", &data);
        return true;
    };

    wf::signal_connection_t view_filter{[this] (wf::signal_data_t *data)
        {
            if (active)
            {
                auto v   = static_cast<scale_filter_signal*>(data);
                v->hide |= !should_show_view(v->view);
            }
        }
    };

    wf::signal_connection_t scale_end{[this] (wf::signal_data_t *data)
        {
            active = false;
        }
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_scale_test);
