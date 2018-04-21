#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

#include <linux/input-event-codes.h>
#include <compositor.h>

#include <output.hpp>
#include <img.hpp>
#include <opengl.hpp>
#include <config.hpp>
#include <render-manager.hpp>

class wayfire_screenshot : public wayfire_plugin_t {
    key_callback screenshot, record;
    effect_hook_t hook;

    weston_recorder *w_recorder = NULL;

    std::string path;


    std::string get_current_name(std::string prefix, std::string suffix)
    {
        std::ostringstream out;

        using namespace std::chrono;
        auto time = system_clock::to_time_t(system_clock::now());
        out << std::put_time(std::localtime(&time), "%Y-%m-%d-%X");
        auto fname = path + prefix + "-" + out.str() + "." + suffix;

        return fname;
    }

    public:
        void init(wayfire_config *config)
        {
            grab_interface->name = "screenshot";
            grab_interface->abilities_mask = WF_ABILITY_RECORD_SCREEN;

            auto section = config->get_section("screenshot");

            auto key = section->get_key("take", {WLR_MODIFIER_SUPER, KEY_S});
            if (key.keyval == 0)
                return;

            auto default_path = std::string(secure_getenv("HOME")) + "/Pictures/";
            path = section->get_string("save_path", default_path);

            hook = std::bind(std::mem_fn(&wayfire_screenshot::save_screenshot), this);
            screenshot = [=] (weston_keyboard*, uint32_t)
            {
                /* we just see if we will be blocked by already plugin */
                if (!output->activate_plugin(grab_interface))
                    return;
                output->deactivate_plugin(grab_interface);

                output->render->add_output_effect(&hook);
                weston_output_schedule_repaint(output->handle);
            };
            output->add_key(key.mod, key.keyval, &screenshot);

            key = section->get_key("record", {WLR_MODIFIER_SUPER, KEY_R});
            if (key.keyval == 0)
                return;

            record = [=] (weston_keyboard*, uint32_t)
            {
                if (w_recorder)
                {
                    weston_recorder_stop(w_recorder);
                    w_recorder = NULL;
                } else
                {
                    w_recorder =
                        weston_recorder_start(output->handle,
                                              get_current_name("record", "wcap").c_str());
                }
            };
            output->add_key(key.mod, key.keyval, &record);

        }

        void save_screenshot()
        {
            output->render->rem_effect(&hook);

            auto geometry = output->get_full_geometry();
            uint8_t *pixels = new uint8_t[geometry.width * geometry.height * 4];

            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
            GL_CALL(glReadPixels(0, 0, geometry.width, geometry.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels));

            image_io::write_to_file(get_current_name("screenshot", "png"), pixels,
                    geometry.width, geometry.height, "png");
        }
};

extern "C" {
    wayfire_plugin_t* newInstance()
    {
        return new wayfire_screenshot;
    }
}
