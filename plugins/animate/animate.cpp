#include "fade.hpp"
#include "fire.hpp"
#include <opengl.hpp>

#define HAS_COMPUTE_SHADER (OpenGL::VersionMajor > 4 ||  \
        (OpenGL::VersionMajor == 4 && OpenGL::VersionMinor >= 3))

bool Animation::Step() {return false;}
bool Animation::Run() {return true;}
Animation::~Animation() {}

AnimationHook::AnimationHook(Animation *_anim) {
    this->anim = _anim;
    if(anim->Run()) {
        this->hook.action =
            std::bind(std::mem_fn(&AnimationHook::Step), this);
        core->addHook(&hook);
        this->hook.enable();
    }
    else {
        delete anim;
        delete this;
    }
}

void AnimationHook::Step() {
    if(!this->anim->Step()) {
        core->remHook(hook.id);
        delete anim;
        delete this;
    }
}

class AnimatePlugin : public Plugin {
    SignalListener map, unmap;

    std::string map_animation;

    public:
        void initOwnership() {
            owner->name = "animate";
            owner->compatAll = true;
        }

        void updateConfiguration() {
            fadeDuration = options["fade_duration"]->data.ival;
            map_animation = *options["map_animation"]->data.sval;
            if(map_animation == "fire") {
                if(!HAS_COMPUTE_SHADER) {
                    std::cout << "[EE] OpenGL version below 4.3," <<
                        " so no support for Fire effect" <<
                        "defaulting to fade effect" << std::endl;
                    map_animation = "fade";
                }
            }
        }

        void init() {
            options.insert(newIntOption("fade_duration", 150));
            options.insert(newStringOption("map_animation", "fade"));

            using namespace std::placeholders;
            map.action = std::bind(std::mem_fn(&AnimatePlugin::mapWindow),
                        this, _1);
            unmap.action = std::bind(std::mem_fn(&AnimatePlugin::unmapWindow),
                        this, _1);

            core->connect_signal("map-window", &map);
            core->connect_signal("unmap-window", &unmap);
        }

        void mapWindow(SignalListenerData data) {
            auto win = *((View*) data[0]);
            if(map_animation == "fire")
                new Fire(win);
            else
                new AnimationHook(new Fade<FadeIn>(win));
        }

        void unmapWindow(SignalListenerData data) {
            auto win = *((View*) data[0]);
            new AnimationHook(new Fade<FadeOut>(win));
        }

        void fini() {
            core->disconnect_signal("map-window", map.id);
            core->disconnect_signal("unmap-window", unmap.id);
        }
};

extern "C" {
    Plugin *newInstance() {
        return new AnimatePlugin();
    }
}
