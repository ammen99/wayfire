#include <output.hpp>
#include <img.hpp>

struct File {
    std::string name;
};

class Screenshot : public Plugin {
    KeyBinding binding;
    EffectHook hook;
    std::string path;
    public:
        void initOwnership() {
            owner->name = "screenshot";
            owner->compatAll = true;
        }

        void updateConfiguration() {
            auto key = *options["activate"]->data.key;
            binding.key = key.key;
            binding.mod = key.mod;
            binding.type = BindingTypePress;
            binding.action = std::bind(std::mem_fn(&Screenshot::initiate),
                    this, std::placeholders::_1);
            output->hook->add_key(&binding, true);

            hook.type = EFFECT_OVERLAY;
            hook.action = std::bind(std::mem_fn(&Screenshot::save_screenshot), this);
            output->render->add_effect(&hook);
            hook.disable();

            path = *options["path"]->data.sval;
        }

        void init() {
            options.insert(newKeyOption("activate", Key{0, 0}));
            options.insert(newStringOption("path", "none"));
        }

        void initiate(EventContext ctx) {
            hook.enable();
        }

        void save_screenshot() {
            hook.disable();
            wlc_geometry in = {
                {0, 0},
                {(uint32_t)output->screen_width,
                 (uint32_t)output->screen_height}
            }, out;

            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
            uint8_t pixels[output->screen_width * output->screen_height * 4];
            wlc_pixels_read(WLC_RGBA8888, &in, &out, pixels);

            /* TODO: write to home */
            image_io::write_to_file("/home/ilex/Scr.png", pixels,
                    output->screen_width, output->screen_height, "png");
        }
};

extern "C" {
    Plugin* newInstance() {
        return new Screenshot;
    }
}
