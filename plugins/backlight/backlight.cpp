#include <output.hpp>
#include <core.hpp>
#include <linux/input-event-codes.h>
#include "../../shared/config.hpp"

class backlight_backend
{
    public:
    virtual int get_max() = 0;
    virtual int get_current() = 0;
    virtual void set(int value) = 0;
};

class intel_backend : public backlight_backend
{
    const char *max_path = "/sys/class/backlight/intel_backlight/max_brightness";
    const char *path = "/sys/class/backlight/intel_backlight/brightness";

    public:
        int get_max()
        {
            std::ifstream in(max_path);
            int max_br;
            in >> max_br;
            return max_br;
        }

        int get_current()
        {
            std::ifstream in(path);
            int br;
            in >> br;
            return br;
        }

        /* intel's backlight is usually writable from root, we rely on intel-util */
        void set(int value)
        {
            std::string command = core->plugin_path + "/wayfire/intel-util " + std::to_string(value);
            core->run(command.c_str());
        }
};

class weston_backlight_backend : public backlight_backend
{
    wayfire_output *_output;
    public:
        weston_backlight_backend(wayfire_output *output) : _output(output) {}

        int get_max()
        {
            return 255;
        }

        int get_current()
        {
            return _output->handle->backlight_current;
        }

        void set(int value)
        {
            if (_output->handle->set_backlight) {
                _output->handle->set_backlight(_output->handle, value);
            } else {
                info << "Failed to set backlight using weston backend" << std::endl;
            }
        }
};

class wayfire_backlight : public wayfire_plugin_t {
    key_callback up, down;

    backlight_backend *backend = nullptr;
    int max_brightness;

    public:
        void init(wayfire_config *config)
        {
            grab_interface->name = "backlight";
            grab_interface->compatAll = true;

            auto section = config->get_section("backlight");
            std::string back = section->get_string("backend", "weston");

            if (back == "weston") {
                backend = new weston_backlight_backend(output);
            } else if (back == "intel") {
                backend = new intel_backend();
            } else {
                info << "Unrecognized backlight backend, disabling plugin." << std::endl;
                return;
            }

            max_brightness = backend->get_max();

            wayfire_key br_up = section->get_key("key_up", {0, KEY_BRIGHTNESSUP});
            wayfire_key br_down = section->get_key("key_down", {0, KEY_BRIGHTNESSDOWN});

            up = [=] (weston_keyboard *kbd, uint32_t key)
            {
                int value = backend->get_current();
                value += 0.05 * max_brightness;
                if (value > max_brightness)
                    value = max_brightness;

                backend->set(value);
            };

            down = [=] (weston_keyboard *kbd, uint32_t key)
            {
                int value = backend->get_current();
                value -= 0.05 * max_brightness;
                if (value < 0)
                    value = 0;

                backend->set(value);
            };

            output->add_key(br_up.mod, br_up.keyval, &up);
            output->add_key(br_down.mod, br_down.keyval, &down);
        }
};

extern "C" {
    wayfire_plugin_t* newInstance()
    {
        return new wayfire_backlight();
    }
}
