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

    uint32_t edges;
    public:
        void init(weston_config *config) {
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

            const int32_t halfw = view->geometry.origin.x + view->geometry.size.w / 2;
            const int32_t halfh = view->geometry.origin.y + view->geometry.size.h / 2;

            if (ptr->x < halfw) {
                edges = RESIZE_EDGE_LEFT;
            } else {
                edges = RESIZE_EDGE_RIGHT;
            }

            if (ptr->y < halfh) {
                edges |= RESIZE_EDGE_TOP;
            } else {
                edges |= RESIZE_EDGE_BOTTOM;
            }
        }

        void button_pressed(weston_pointer *ptr, uint32_t button, uint32_t state) {
            if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_RELEASED)
                return;

            grab_interface->ungrab();
            output->input->deactivate_plugin(grab_interface);
        }

        void pointer_motion(weston_pointer *ptr, weston_pointer_motion_event *ev) {
            auto newg = view->geometry;
            if (edges & RESIZE_EDGE_LEFT) {
                newg.origin.x += ev->dx;
                newg.size.w -= ev->dx;
            } else {
                newg.size.w += ev->dx;
            }

            if (edges & RESIZE_EDGE_TOP) {
                newg.origin.y += ev->dy;
                newg.size.h -= ev->dy;
            } else {
                newg.size.h += ev->dy;
            }

            view->set_geometry(newg);
        }
};

extern "C" {
    wayfire_plugin_t *newInstance() {
        return new wayfire_resize();
    }
}
