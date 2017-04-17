#include <output.hpp>
#include <core.hpp>
#include <linux/input-event-codes.h>

#define MAX_OUTPUT_WIDTH 4096

void next_output_idle_cb(void *data)
{
    auto wo = (wayfire_output*) data;
    core->focus_output(wo);
}

class wayfire_output_manager : public wayfire_plugin_t {
    key_callback switch_output, switch_output_with_window;

    public:
        void init(wayfire_config *config)
        {
            grab_interface->name = "oswitch";
            grab_interface->compatAll = true;

            auto section = config->get_section("oswitch");

            auto actkey  = section->get_key("next_output", {MODIFIER_SUPER, KEY_K});
            auto withwin = section->get_key("next_output_with_key", {MODIFIER_SUPER | MODIFIER_SHIFT, KEY_K});

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

                core->move_view_to_output(view, view->output, next);

                auto loop = wl_display_get_event_loop(core->ec->wl_display);
                wl_event_loop_add_idle(loop, next_output_idle_cb, next);
            };

            core->input->add_key(actkey.mod, actkey.keyval, &switch_output, output);
            core->input->add_key(withwin.mod, withwin.keyval, &switch_output_with_window, output);

            /* we exploit the fact that when we're called a new output is
             * created, so we can configure it here */

            int n = core->get_num_outputs() - 1;
            weston_output_move(output->handle, 6 * n * MAX_OUTPUT_WIDTH, 0);
        }
};

extern "C" {
    wayfire_plugin_t *newInstance()
    {
        return new wayfire_output_manager();
    }
}
