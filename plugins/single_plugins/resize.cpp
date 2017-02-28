#include <wm.hpp>
#include <output.hpp>
#include <core.hpp>
#include <linux/input.h>

enum resize_edges {
    RESIZE_EDGE_TOP = 1 << 0,
    RESIZE_EDGE_BOTTOM = 1 << 1,
    RESIZE_EDGE_LEFT = 1 << 2,
    RESIZE_EDGE_RIGHT = 1 << 3
};

class wayfire_resize : public wayfire_plugin_t {
    button_callback activate_binding;
    wayfire_view view;
    wl_fixed_t initial_x, initial_y;

    uint32_t edges;
    public:
        void init(wayfire_config *config) {
            grab_interface->name = "resize";
            grab_interface->compatAll = true;

            activate_binding = [=] (weston_pointer* ptr, uint32_t) {
                this->initiate(ptr);
            };

            using namespace std::placeholders;
            output->input->add_button(MODIFIER_SUPER, BTN_LEFT, &activate_binding);
            grab_interface->callbacks.pointer.button =
                std::bind(std::mem_fn(&wayfire_resize::button_pressed), this, _1, _2, _3);
            grab_interface->callbacks.pointer.motion =
                std::bind(std::mem_fn(&wayfire_resize::pointer_motion), this, _1, _2);

            /* TODO: resize_request, read binding from config */
        }

        void initiate(weston_pointer *ptr) {
            if (!ptr->focus)
                return;

            view = core->find_view(ptr->focus);
            if (!view)
                return;

            if (!output->input->activate_plugin(grab_interface))
                return;
            if (!grab_interface->grab())
                return;

            weston_view_from_global_fixed(view->handle, ptr->x, ptr->y, &initial_x, &initial_y);

            int pointer_x = wl_fixed_to_int(initial_x);
            int pointer_y = wl_fixed_to_int(initial_y);

            const int32_t halfw = view->geometry.origin.x + view->geometry.size.w / 2;
            const int32_t halfh = view->geometry.origin.y + view->geometry.size.h / 2;

            if (pointer_x < halfw) {
                edges = RESIZE_EDGE_LEFT;
            } else {
                edges = RESIZE_EDGE_RIGHT;
            }

            if (pointer_y < halfh) {
                edges |= RESIZE_EDGE_TOP;
            } else {
                edges |= RESIZE_EDGE_BOTTOM;
            }

            weston_desktop_surface_set_resizing(view->desktop_surface, true);
        }

        void button_pressed(weston_pointer *ptr, uint32_t button, uint32_t state) {
            if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_RELEASED)
                return;

            grab_interface->ungrab();
            output->input->deactivate_plugin(grab_interface);
            weston_desktop_surface_set_resizing(view->desktop_surface, false);
        }

        void pointer_motion(weston_pointer *ptr, weston_pointer_motion_event *ev) {
            auto newg = view->geometry;

            wl_fixed_t current_x, current_y;
            weston_view_from_global_fixed(view->handle, ptr->x, ptr->y, &current_x, &current_y);

            int dx = wl_fixed_to_int(current_x - initial_x);
            int dy = wl_fixed_to_int(current_y - initial_y);

            initial_x = current_x;
            initial_y = current_y;

            if (edges & RESIZE_EDGE_LEFT) {
                newg.origin.x += dx;
                newg.size.w -= dx;
            } else {
                newg.size.w += dx;
            }

            if (edges & RESIZE_EDGE_TOP) {
                newg.origin.y += dy;
                newg.size.h -= dy;
            } else {
                newg.size.h += dy;
            }

            auto max_size = weston_desktop_surface_get_max_size(view->desktop_surface);
            auto min_size = weston_desktop_surface_get_min_size(view->desktop_surface);

            min_size.width = std::max(min_size.width, 10);
            min_size.height = std::max(min_size.height, 10);

            if (max_size.width > 0)
                newg.size.w = std::min(max_size.width, newg.size.w);
            newg.size.w = std::max(min_size.width, newg.size.w);

            if (max_size.height > 0)
                newg.size.h = std::min(max_size.height, newg.size.h);
            newg.size.h = std::max(min_size.height, newg.size.h);

            view->set_geometry(newg);
        }
};

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_resize();
    }
}
