#include <output.hpp>
#include <core.hpp>
#include <linux/input-event-codes.h>
#include "../../shared/config.hpp"

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

            auto actkey  = section->get_key("next_output", {MODIFIER_SUPER, KEY_K});
            auto withwin = section->get_key("next_output_with_key",
                    {MODIFIER_SUPER | MODIFIER_SHIFT, KEY_K});

            switch_output = [=] (weston_keyboard *kbd, uint32_t key) {
                /* when we switch the output, the oswitch keybinding
                 * will be activated for the next output, which we don't want,
                 * so we postpone the switch */
                auto next = core->get_next_output(output);

                auto loop = wl_display_get_event_loop(core->ec->wl_display);
                wl_event_loop_add_idle(loop, next_output_idle_cb, next);
            };

            switch_output_with_window = [=] (weston_keyboard *kbd, uint32_t key) {
                auto next = core->get_next_output(output);
                auto view = output->get_top_view();

                auto pg = view->output->get_full_geometry();
                auto ng = next->get_full_geometry();

                view->move(view->geometry.x + ng.x - pg.x,
                           view->geometry.y + ng.y - pg.y);
                core->move_view_to_output(view, next);

                auto loop = wl_display_get_event_loop(core->ec->wl_display);
                wl_event_loop_add_idle(loop, next_output_idle_cb, next);
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
