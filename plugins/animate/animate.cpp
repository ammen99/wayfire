#include "fade.hpp"
#include "fire.hpp"
#include <opengl.hpp>
#include <output.hpp>

#define HAS_COMPUTE_SHADER (OpenGL::VersionMajor > 4 ||  \
        (OpenGL::VersionMajor == 4 && OpenGL::VersionMinor >= 3))

bool Animation::step() {return false;}
bool Animation::run() {return true;}
Animation::~Animation() {}

AnimationHook::AnimationHook(Animation *_anim, Output *output, View v) {
    anim = _anim;
    this->output = output;

    if (anim->run()) {
        hook.action = std::bind(std::mem_fn(&AnimationHook::step), this);
        hook.type = (v == nullptr ? EFFECT_OVERLAY : EFFECT_WINDOW);
        hook.win = v;

        output->render->add_effect(&hook);
        hook.enable();
    } else {
        delete anim;
        delete this;
    }
}

void AnimationHook::step() {
    if (!this->anim->step()) {
        output->render->rem_effect(hook.id);
        delete anim;
        delete this;
    }
}

class AnimatePlugin : public Plugin {
    SignalListener create, destroy;

    std::string animation;

    public:
        void initOwnership() {
            owner->name = "animate";
            owner->compatAll = true;
        }

        void updateConfiguration() {
            fadeDuration = options["fade_duration"]->data.ival;
            animation = *options["map_animation"]->data.sval;
#if false
            if(map_animation == "fire") {
                if(!HAS_COMPUTE_SHADER) {
                    std::cout << "[EE] OpenGL version below 4.3," <<
                        " so no support for Fire effect" <<
                        "defaulting to fade effect" << std::endl;
                    map_animation = "fade";
                }
            }
#endif
        }

        void init() {
            options.insert(newIntOption("fade_duration", 150));
            options.insert(newStringOption("map_animation", "fade"));

            using namespace std::placeholders;
            create.action = std::bind(std::mem_fn(&AnimatePlugin::mapWindow),
                        this, _1);
            destroy.action = std::bind(std::mem_fn(&AnimatePlugin::unmapWindow),
                        this, _1);

            output->signal->connect_signal("create-view", &create);
            output->signal->connect_signal("destroy-view", &destroy);
        }

        void mapWindow(SignalListenerData data) {
            auto win = *((View*) data[0]);
            new AnimationHook(new Fade<FadeIn>(win, output), output);
        }

        void unmapWindow(SignalListenerData data) {
            auto win = *((View*) data[0]);
            new AnimationHook(new Fade<FadeOut>(win, output), output);
        }

        void fini() {
            output->signal->disconnect_signal("create-view", create.id);
            output->signal->disconnect_signal("destroy-view", destroy.id);
        }
};

extern "C" {
    Plugin *newInstance() {
        return new AnimatePlugin();
    }
}
