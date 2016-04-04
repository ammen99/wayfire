#include "animate.hpp"
#include "fade.hpp"
#include <output.hpp>

int fadeDuration;
/* FadeIn begin */
template<>
Fade<FadeIn>::Fade (View _win, Output *o) : out(o), win(_win) {
    target = maxstep = fadeDuration;
    progress = 0;

    win->is_hidden = true;
    GetTuple(x, y, out->viewport->get_current_viewport());
    out->render->set_renderer(out->viewport->get_mask_for_viewport(x, y));
}

template<> bool Fade<FadeIn>::step() {
    progress++;
    out->render->ctx->color =
        glm::vec4(1, 1, 1, (float(progress) / float(maxstep)));

    win->render(TEXTURE_TRANSFORM_USE_COLOR);
    if(progress == target) {
        out->render->reset_renderer();
        win->is_hidden = false;
        return false;
    }

    return true;
}

template<> bool Fade<FadeIn>::run() { return true; }
template<> Fade<FadeIn>::~Fade() {}
/* FadeIn  end */

/* FadeOut begin */
template<>
Fade<FadeOut>::Fade (View _win, Output *o) : out(o), win(_win) {
    wlc_geometry g;
    wlc_view_get_visible_geometry(win->get_id(), &g);
    collected_surfaces = win->collected_surfaces;

    GetTuple(x, y, out->viewport->get_current_viewport());
    out->render->set_renderer(out->viewport->get_mask_for_viewport(x, y));

    /* exit if already running */
    progress = maxstep = fadeDuration;
    target = 0;
}

template<>
bool Fade<FadeOut>::step() {
    progress--;
    out->render->ctx->color =
        glm::vec4(1, 1, 1, (float(progress) / float(maxstep)));

    for (auto surf : collected_surfaces) {
        for (int i = 0; i < 3 && surf.tex[i]; i++)
            OpenGL::renderTransformedTexture(surf.tex[i], surf.g,
                    glm::mat4(), TEXTURE_TRANSFORM_USE_COLOR);
    }

    if (progress == target) {
        out->render->reset_renderer();
        return false;
    }
    return true;
}

template<> bool Fade<FadeOut>::run() { return true; }
template<> Fade<FadeOut>::~Fade() {}
/* FadeOut end */
