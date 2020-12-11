#include <cctype>
#include <string>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/plugins/common/scale-signal.hpp>
#include "scale_keys.h"


struct scale_title_filter : public wf::plugin_interface_t
{
    wf::option_wrapper_t<bool> case_sensitive{"scale_title_filter/case_sensitive"};
    std::string title_filter;
    bool scale_running = false;

    bool should_show_view(wayfire_view view)
    {
        if (title_filter.empty())
        {
            return true;
        }

        std::string title = view->get_title();
        if (!case_sensitive)
        {
            std::transform(title.begin(), title.end(), title.begin(),
                [] (unsigned char c)
            {
                if (std::isspace(c))
                {
                    return ' ';
                }

                return (char)std::tolower(c);
            });
        }

        return title.find(title_filter) != std::string::npos;
    }

  public:
    void init() override
    {
        grab_interface->name = "scale_title_filter";
        grab_interface->capabilities = 0;

        output->connect_signal("scale-filter", &view_filter);
        output->connect_signal("scale-end", &scale_end);
    }

    void fini() override
    {
        output->disconnect_signal(&view_filter);
        wf::get_core().disconnect_signal(&scale_key);
        output->disconnect_signal(&scale_end);
    }

    wf::signal_connection_t view_filter{[this] (wf::signal_data_t *data)
        {
            if (!scale_running)
            {
                wf::get_core().connect_signal("keyboard_key", &scale_key);
                scale_running = true;
            }

            auto v   = static_cast<scale_filter_signal*>(data);
            v->hide |= !should_show_view(v->view);
        }
    };

    wf::signal_connection_t scale_key{[this] (wf::signal_data_t *data)
        {
            auto k =
                static_cast<wf::input_event_signal<wlr_event_keyboard_key>*>(data);
            if (k->event->state != WL_KEYBOARD_KEY_STATE_PRESSED)
            {
                return;
            }

            if (k->event->keycode == KEY_BACKSPACE)
            {
                if (!title_filter.empty())
                {
                    title_filter.pop_back();
                    LOGI("Title filter changed: ", title_filter);
                    wf::activator_data_t data{wf::activator_source_t::PLUGIN, 0};
                    output->call_plugin("scale/activate", data);
                }
            } else
            {
                char c = char_from_keycode(k->event->keycode);
                if (c != (char)-1)
                {
                    if ((c == '-') && wf::get_core().get_keyboard_modifiers() &
                        WLR_MODIFIER_SHIFT)
                    {
                        c = '_';
                    }

                    title_filter.push_back(c);
                    LOGI("Title filter changed: ", title_filter);
                    wf::activator_data_t data{wf::activator_source_t::PLUGIN, 0};
                    output->call_plugin("scale/activate", data);
                }
            }
        }
    };

    wf::signal_connection_t scale_end{[this] (wf::signal_data_t *data)
        {
            wf::get_core().disconnect_signal(&scale_key);
            title_filter.clear();
            scale_running = false;
        }
    };
};

DECLARE_WAYFIRE_PLUGIN(scale_title_filter);
