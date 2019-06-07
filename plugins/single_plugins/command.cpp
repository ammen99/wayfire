#include <plugin.hpp>
#include <output.hpp>
#include <core.hpp>
#include <linux/input.h>
#include <linux/input-event-codes.h>

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
 * prefix repeatable_ */

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

    void on_binding(std::string command, bool enable_repeat, wf_activator_source source,
        uint32_t value)
    {
        /* We already have a repeatable command, do not accept further bindings */
        if (repeat.pressed_key || repeat.pressed_button)
            return;

        if (!output->activate_plugin(grab_interface))
            return;

        wf::get_core().run(command.c_str());
        /* No repeat necessary in any of those cases */
        if (!enable_repeat || source == ACTIVATOR_SOURCE_GESTURE || value == 0)
        {
            output->deactivate_plugin(grab_interface);
            return;
        }

        /* Grab if grab wasn't active up to now */
        if (!grab_interface->is_grabbed())
            grab_interface->grab();

        repeat.repeat_command = command;
        if (source == ACTIVATOR_SOURCE_KEYBINDING) {
            repeat.pressed_key = value;
        } else {
            repeat.pressed_button = value;
        }

        repeat_delay_source = wl_event_loop_add_timer(wf::get_core().ev_loop,
            repeat_delay_timeout_handler, &on_repeat_delay_timeout);

        wl_event_source_timer_update(repeat_delay_source,
            wf::get_core().config->get_section("input")
                ->get_option("kb_repeat_delay", "400")->as_int());
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
        uint32_t repeat_rate = wf::get_core().config->get_section("input")
            ->get_option("kb_repeat_rate", "40")->as_int();
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

        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
    }

    std::function<void(uint32_t, uint32_t)> on_button =
        [=] (uint32_t button, uint32_t state)
    {
        if (button == repeat.pressed_button && state == WLR_BUTTON_RELEASED)
            reset_repeat();
    };

    std::function<void(uint32_t, uint32_t)> on_key =
        [=] (uint32_t key, uint32_t state)
    {
        if (key == repeat.pressed_key && state == WLR_KEY_RELEASED)
            reset_repeat();
    };

    public:

    void setup_bindings_from_config(wayfire_config *config)
    {
        auto section = config->get_section("command");

        std::vector<std::string> command_names;
        const std::string exec_prefix = "command_";
        for (auto command : section->options)
        {
            if (begins_with(command->name, exec_prefix))
            {
                command_names.push_back(
                    command->name.substr(exec_prefix.length()));
            }
        }

        bindings.resize(command_names.size());
        const std::string norepeat;

        for (size_t i = 0; i < command_names.size(); i++)
        {
            auto command = exec_prefix + command_names[i];
            auto regular_binding_name = "binding_" + command_names[i];
            auto repeat_binding_name = "repeatable_binding_" + command_names[i];

            auto executable = section->get_option(command, "")->as_string();
            auto repeatable_opt = section->get_option(repeat_binding_name, norepeat);
            auto regular_opt = section->get_option(regular_binding_name, "none");

            using namespace std::placeholders;
            if (repeatable_opt->as_string() != norepeat)
            {
                bindings[i] = std::bind(std::mem_fn(&wayfire_command::on_binding),
                    this, executable, true, _1, _2);
                output->add_activator(repeatable_opt, &bindings[i]);
            }
            else
            {
                bindings[i] = std::bind(std::mem_fn(&wayfire_command::on_binding),
                    this, executable, false, _1, _2);
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

    void init(wayfire_config *config)
    {
        grab_interface->name = "command";
        grab_interface->capabilities = wf::CAPABILITY_GRAB_INPUT;
        grab_interface->callbacks.pointer.button = on_button;
        grab_interface->callbacks.keyboard.key = on_key;
        grab_interface->callbacks.cancel = [=]() {reset_repeat();};

        using namespace std::placeholders;

        setup_bindings_from_config(config);

        reload_config = [=] (wf::signal_data_t*)
        {
            clear_bindings();
            setup_bindings_from_config(wf::get_core().config);
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
