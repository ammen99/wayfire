#include "animate.hpp"

extern int fadeDuration;
enum FadeMode { FadeIn = 1, FadeOut = -1 };

template<FadeMode mode>
class Fade : public Animation {
    Output *out;
    std::vector<Surface> collected_surfaces;

    View win;
    uint32_t saved_mask;

    int progress = 0;
    int maxstep = 0;
    int target = 0;

    public:
    Fade(View w, Output *out);
    ~Fade();

    bool step();
    bool run();
};


