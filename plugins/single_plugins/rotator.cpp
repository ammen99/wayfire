#include <output.hpp>
#include <core.hpp>
#include "../../shared/config.hpp"
#include <linux/input-event-codes.h>
#include <compositor.h>

class wayfire_rotator : public wayfire_plugin_t {
    key_callback up, down, left, right;

    public:
    void init(wayfire_config *config) {
        grab_interface->name = "rotator";
        grab_interface->compatAll = true;

        auto section = config->get_section("rotator");

        wayfire_key up_key    = section->get_key("rotate_up",
                {MODIFIER_ALT|MODIFIER_CTRL, KEY_UP});
        wayfire_key down_key  = section->get_key("rotate_down",
                {MODIFIER_ALT|MODIFIER_CTRL, KEY_DOWN});
        wayfire_key left_key  = section->get_key("rotate_left",
                {MODIFIER_ALT|MODIFIER_CTRL, KEY_LEFT});
        wayfire_key right_key = section->get_key("rotate_right",
                {MODIFIER_ALT|MODIFIER_CTRL, KEY_RIGHT});

        up = [=] (weston_keyboard*, uint32_t) {
            output->set_transform(WL_OUTPUT_TRANSFORM_NORMAL);
        };
        down = [=] (weston_keyboard*, uint32_t) {
            output->set_transform(WL_OUTPUT_TRANSFORM_180);
        };
        left = [=] (weston_keyboard*, uint32_t) {
            output->set_transform(WL_OUTPUT_TRANSFORM_270);
        };
        right = [=] (weston_keyboard*, uint32_t) {
            output->set_transform(WL_OUTPUT_TRANSFORM_90);
        };

        output->add_key((weston_keyboard_modifier)up_key.mod, up_key.keyval, &up);
        output->add_key((weston_keyboard_modifier)down_key.mod, down_key.keyval, &down);
        output->add_key((weston_keyboard_modifier)left_key.mod, left_key.keyval, &left);
        output->add_key((weston_keyboard_modifier)right_key.mod, right_key.keyval, &right);
    }
};

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_rotator();
    }
}
