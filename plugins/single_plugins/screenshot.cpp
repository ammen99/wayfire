#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

#include <linux/input-event-codes.h>
#include <compositor.h>

#include <output.hpp>
#include <img.hpp>
#include <core.hpp>
#include <opengl.hpp>

class wayfire_screenshot : public wayfire_plugin_t {
    key_callback binding;
    effect_hook_t hook;

    std::string path;

    public:
        void init(wayfire_config *config)
        {
            grab_interface->name = "screenshot";
            grab_interface->compatAll = true;

            auto section = config->get_section("screenshot");

            auto key = section->get_key("take", {MODIFIER_SUPER, KEY_S});
            if (key.keyval == 0)
                return;

            auto default_path = std::string(secure_getenv("HOME")) + "/Pictures/";
            path = section->get_string("save_path", default_path);

            hook = std::bind(std::mem_fn(&wayfire_screenshot::save_screenshot), this);
            binding = [=] (weston_keyboard*, uint32_t)
            {
                /* we just see if we will be blocked by already plugin */
                if (!output->activate_plugin(grab_interface))
                    return;
                output->deactivate_plugin(grab_interface);

                output->render->add_output_effect(&hook);
                weston_output_schedule_repaint(output->handle);
            };
            core->input->add_key(key.mod, key.keyval, &binding, output);
        }

        void save_screenshot()
        {
            output->render->rem_effect(&hook);

            auto geometry = output->get_full_geometry();
            uint8_t *pixels = new uint8_t[geometry.size.w * geometry.size.h * 4];

            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
            GL_CALL(glReadPixels(0, 0, 1920, 1080, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

            std::ostringstream out;

            using namespace std::chrono;
            auto time = system_clock::to_time_t(system_clock::now());
            out << std::put_time(std::localtime(&time), "%Y-%m-%d-%X");
            auto fname = path + "screenshot-" + out.str() + ".png";

            image_io::write_to_file(fname, pixels,
                    geometry.size.w, geometry.size.h, "png");
        }
};

extern "C" {
    wayfire_plugin_t* newInstance()
    {
        return new wayfire_screenshot;
    }
}
