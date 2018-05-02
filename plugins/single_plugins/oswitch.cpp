#include <output.hpp>
#include <core.hpp>
#include <view.hpp>
#include <linux/input-event-codes.h>
#include <config.hpp>

/* XXX: remove next_output_idle_cb, because if an output is unplugged
 * in between switching outputs(highly unlikely, but still possible),
 * we should remove the idle source */
void next_output_idle_cb(void *data)
{
    auto wo = (wayfire_output*) data;
    core->focus_output(wo);
}

class wayfire_output_manager : public wayfire_plugin_t
{
    key_callback switch_output, switch_output_with_window;

    public:
        void init(wayfire_config *config)
        {
            grab_interface->name = "oswitch";
            grab_interface->abilities_mask = WF_ABILITY_NONE;

            auto section = config->get_section("oswitch");

            auto actkey  = section->get_key("next_output", {WLR_MODIFIER_LOGO, KEY_K});
            auto withwin = section->get_key("next_output_with_win",
                    {WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT, KEY_K});

            switch_output = [=] (uint32_t key) {
                /* when we switch the output, the oswitch keybinding
                 * may be activated for the next output, which we don't want,
                 * so we postpone the switch */
                auto next = core->get_next_output(output);

                wl_event_loop_add_idle(core->ev_loop, next_output_idle_cb, next);
            };

            switch_output_with_window = [=] (uint32_t key) {
                auto next = core->get_next_output(output);
                auto view = output->get_active_view();

                if (!view)
                {
                    switch_output(key);
                    return;
                }

                core->move_view_to_output(view, next);
                wl_event_loop_add_idle(core->ev_loop, next_output_idle_cb, next);
            };

            output->add_key(actkey.mod, actkey.keyval, &switch_output);
            output->add_key(withwin.mod, withwin.keyval, &switch_output_with_window);
        }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_output_manager();
    }
}
