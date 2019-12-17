#include <plugin.hpp>
#include <output.hpp>
#include <core.hpp>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <signal-definitions.hpp>
#include <wayfire/util/log.hpp>

static bool begins_with(std::string word, std::string prefix)
{
    if (word.length() < prefix.length())
        return false;

    return word.substr(0, prefix.length()) == prefix;
}

/* Initial repeat delay passed */
static int repeat_delay_timeout_handler(void *callback)
{
    (*reinterpret_cast<std::function<void()>*> (callback)) ();
    return 1; // disconnect
};

/* Between each repeat */
static int repeat_once_handler(void *callback)
{
    (*reinterpret_cast<std::function<void()>*> (callback)) ();
    return 1; // continue timer
}

/* Provides a way to bind specific commands to activator bindings.
 *
 * It supports 2 modes:
 *
 * 1. Regular bindings
 * 2. Repeatable bindings - for example, if the user binds a keybinding, then
 * after a specific delay the command begins to be executed repeatedly, until
 * the user released the key. In the config file, repeatable bindings have the
 * prefix repeatable_
 * 3. Always bindings - bindings that can be executed even if a plugin is already
 * active, or if the screen is locked. They have a prefix always_
 * */

class wayfire_command : public wf::plugin_interface_t
{
    std::vector<activator_callback> bindings;

    struct
    {
        uint32_t pressed_button = 0;
        uint32_t pressed_key = 0;
        std::string repeat_command;
    } repeat;

    wl_event_source *repeat_source = NULL, *repeat_delay_source = NULL;

    enum binding_mode {
        BINDING_NORMAL,
        BINDING_REPEAT,
        BINDING_ALWAYS,
    };
    bool on_binding(std::string command, binding_mode mode, wf_activator_source source,
        uint32_t value)
    {
        /* We already have a repeatable command, do not accept further bindings */
        if (repeat.pressed_key || repeat.pressed_button)
            return false;

        if (!output->activate_plugin(grab_interface, mode == BINDING_ALWAYS))
            return false;

        wf::get_core().run(command.c_str());

        /* No repeat necessary in any of those cases */
        if (mode != BINDING_REPEAT || source == ACTIVATOR_SOURCE_GESTURE ||
            value == 0)
        {
            output->deactivate_plugin(grab_interface);
            return true;
        }

        repeat.repeat_command = command;
        if (source == ACTIVATOR_SOURCE_KEYBINDING) {
            repeat.pressed_key = value;
        } else {
            repeat.pressed_button = value;
        }

        repeat_delay_source = wl_event_loop_add_timer(wf::get_core().ev_loop,
            repeat_delay_timeout_handler, &on_repeat_delay_timeout);

        wl_event_source_timer_update(repeat_delay_source,
            wf::option_wrapper_t<int>("input/kb_repeat_delay"));

        wf::get_core().connect_signal("pointer_button", &on_button_event);
        wf::get_core().connect_signal("keyboard_key", &on_key_event);

        return true;
    }

    std::function<void()> on_repeat_delay_timeout = [=] ()
    {
        repeat_delay_source = NULL;
        repeat_source = wl_event_loop_add_timer(wf::get_core().ev_loop,
            repeat_once_handler, &on_repeat_once);
        on_repeat_once();
    };

    std::function<void()> on_repeat_once = [=] ()
    {
        uint32_t repeat_rate = wf::option_wrapper_t<int> ("input/kb_repeat_rate");
        if (repeat_rate <= 0 || repeat_rate > 1000)
            return reset_repeat();

        wl_event_source_timer_update(repeat_source, 1000 / repeat_rate);
        wf::get_core().run(repeat.repeat_command.c_str());
    };

    void reset_repeat()
    {
        if (repeat_delay_source)
        {
            wl_event_source_remove(repeat_delay_source);
            repeat_delay_source = NULL;
        }

        if (repeat_source)
        {
            wl_event_source_remove(repeat_source);
            repeat_source = NULL;
        }

        repeat.pressed_key = repeat.pressed_button = 0;
        output->deactivate_plugin(grab_interface);

        wf::get_core().disconnect_signal("pointer_button", &on_button_event);
        wf::get_core().disconnect_signal("keyboard_key", &on_key_event);
    }

    wf::signal_callback_t on_button_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_event_pointer_button>*>(data);
        if (ev->event->button == repeat.pressed_button &&
            ev->event->state == WLR_BUTTON_RELEASED)
        {
            reset_repeat();
        }
    };

    wf::signal_callback_t on_key_event = [=] (wf::signal_data_t *data)
    {
        auto ev = static_cast<
            wf::input_event_signal<wlr_event_keyboard_key>*>(data);
        if (ev->event->keycode == repeat.pressed_key &&
            ev->event->state == WLR_KEY_RELEASED)
        {
            reset_repeat();
        }
    };

    public:

    void setup_bindings_from_config()
    {
        auto section = wf::get_core().config.get_section("command");

        std::vector<std::string> command_names;
        const std::string exec_prefix = "command_";
        for (auto command : section->get_registered_options())
        {
            if (begins_with(command->get_name(), exec_prefix))
            {
                command_names.push_back(
                    command->get_name().substr(exec_prefix.length()));
            }
        }

        bindings.resize(command_names.size());
        const std::string norepeat = "...norepeat...";
        const std::string noalways = "...noalways...";

        for (size_t i = 0; i < command_names.size(); i++)
        {
            auto command = exec_prefix + command_names[i];
            auto regular_binding_name = "binding_" + command_names[i];
            auto repeat_binding_name = "repeatable_binding_" + command_names[i];
            auto always_binding_name = "always_binding_" + command_names[i];

            auto check_activator = [&] (const std::string& name)
            {
                auto opt = section->get_option_or(name);
                if (opt)
                {
                    auto value = wf::option_type::from_string<
                        wf::activatorbinding_t> (opt->get_value_str());
                    if (value) return wf::create_option(value.value());
                }

                return wf::option_sptr_t<wf::activatorbinding_t>{};
            };

            auto executable = section->get_option(command)->get_value_str();
            auto repeatable_opt = check_activator(repeat_binding_name);
            auto regular_opt = check_activator(regular_binding_name);
            auto always_opt = check_activator(always_binding_name);

            using namespace std::placeholders;
            if (repeatable_opt)
            {
                bindings[i] = std::bind(std::mem_fn(&wayfire_command::on_binding),
                    this, executable, BINDING_REPEAT, _1, _2);
                output->add_activator(repeatable_opt, &bindings[i]);
            }
            else if (always_opt)
            {
                bindings[i] = std::bind(std::mem_fn(&wayfire_command::on_binding),
                    this, executable, BINDING_ALWAYS, _1, _2);
                output->add_activator(always_opt, &bindings[i]);
            }
            else if (regular_opt)
            {
                bindings[i] = std::bind(std::mem_fn(&wayfire_command::on_binding),
                    this, executable, BINDING_NORMAL, _1, _2);
                output->add_activator(regular_opt, &bindings[i]);
            }
        }
    }

    void clear_bindings()
    {
        for (auto& binding : bindings)
            output->rem_binding(&binding);

        bindings.clear();
    }

    wf::signal_callback_t reload_config;

    void init()
    {
        grab_interface->name = "command";
        grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;

        using namespace std::placeholders;

        setup_bindings_from_config();
        reload_config = [=] (wf::signal_data_t*)
        {
            clear_bindings();
            setup_bindings_from_config();
        };

        wf::get_core().connect_signal("reload-config", &reload_config);
    }

    void fini()
    {
        wf::get_core().disconnect_signal("reload-config", &reload_config);
        clear_bindings();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_command);
