#include <output.hpp>
#include <core.hpp>
#include <linux/input.h>
#include <signal_definitions.hpp>
#include "snap_signal.hpp"
#include <libweston-3/libweston-desktop.h>

class wayfire_move : public wayfire_plugin_t {
    signal_callback_t move_request;
    button_callback activate_binding;
    wayfire_view view;

    bool enable_snap;
    int slot;
    int snap_pixels;

    int prev_x, prev_y;

    public:
        void init(wayfire_config *config) {
            grab_interface->name = "move";
            grab_interface->compatAll = true;

            activate_binding = [=] (weston_pointer* ptr, uint32_t) {
                this->initiate(ptr);
            };

            auto section = config->get_section("move");
            wayfire_button button = section->get_button("activate", {MODIFIER_ALT, BTN_LEFT});
            core->input->add_button(button.mod, button.button, &activate_binding, output);

            enable_snap = section->get_int("enable_snap", 1);
            snap_pixels = section->get_int("snap_threshold", 2);

            using namespace std::placeholders;
            grab_interface->callbacks.pointer.button =
                std::bind(std::mem_fn(&wayfire_move::button_pressed), this, _1, _2, _3);
            grab_interface->callbacks.pointer.motion =
                std::bind(std::mem_fn(&wayfire_move::pointer_motion), this, _1, _2);

            move_request = std::bind(std::mem_fn(&wayfire_move::move_requested), this, _1);
            output->signal->connect_signal("move-request", &move_request);
        }

        void move_requested(signal_data *data) {
            auto converted = static_cast<move_request_signal*> (data);
            if(converted)
                initiate(converted->ptr);
        }

        void initiate(weston_pointer *ptr) {
            if (!ptr->focus)
                return;

            view = core->find_view(ptr->focus);
            if (!view || view->is_special)
                return;

            if (!output->activate_plugin(grab_interface))
                return;

            weston_seat_break_desktop_grabs(ptr->seat);
            if (!grab_interface->grab())
                return;

            prev_x = wl_fixed_to_int(ptr->x);
            prev_y = wl_fixed_to_int(ptr->y);

            view->output->focus_view(nullptr, core->get_current_seat());
            if (enable_snap)
                slot = 0;
        }

        void button_pressed(weston_pointer *ptr, uint32_t button, uint32_t state) {
            if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_RELEASED)
                return;

            if (enable_snap && slot != 0) {
                snap_signal data;
                data.view = view;
                data.tslot = (slot_type)slot;

                output->signal->emit_signal("view-snap", &data);
            }

            grab_interface->ungrab();
            output->deactivate_plugin(grab_interface);
            view->output->focus_view(view, core->get_current_seat());
        }

        int calc_slot() {
            auto g = output->get_full_geometry();

            bool is_left = std::abs(prev_x - g.origin.x) <= snap_pixels;
            bool is_right = std::abs(g.origin.x + g.size.w - prev_x) <= snap_pixels;
            bool is_top = std::abs(prev_y - g.origin.y) < snap_pixels;
            bool is_bottom = std::abs(g.origin.y + g.size.h - prev_y) < snap_pixels;

            if (is_left && is_top)
                return SLOT_TL;
            else if (is_left && is_bottom)
                return SLOT_BL;
            else if (is_left)
                return SLOT_LEFT;
            else if (is_right && is_top)
                return SLOT_TR;
            else if (is_right && is_bottom)
                return SLOT_BR;
            else if (is_right)
                return SLOT_RIGHT;
            else if (is_top)
                return SLOT_CENTER;
            else if (is_bottom)
                return SLOT_BOTTOM;
            else
                return 0;
        }

        void pointer_motion(weston_pointer *ptr, weston_pointer_motion_event *ev) {
            int nx = wl_fixed_to_int(ptr->x);
            int ny = wl_fixed_to_int(ptr->y);

            view->move(view->geometry.origin.x + nx - prev_x,
                    view->geometry.origin.y + ny - prev_y);
            prev_x = nx;
            prev_y = ny;

            /* TODO: possibly show some visual indication */
            if (enable_snap)
                slot = calc_slot();
        }
};

extern "C" {
    wayfire_plugin_t* newInstance() {
        return new wayfire_move();
    }
}

