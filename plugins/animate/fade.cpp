#include "animate.hpp"
#include "fade.hpp"

int fadeDuration;
struct FadeWindowData : public WindowData {
    bool fadeIn = false; // used to prevent activation
    bool fadeOut = false; // of fading multiple times
};

/* FadeIn begin */

template<>
Fade<FadeIn>::Fade (View _win) : win(_win), run(true) {

    /* exit if already running */
    if(ExistsData(win, "fade")) {
        auto data = GetData(FadeWindowData, win, "fade");
        if(data->fadeIn)
            run = false;
    }
    else AllocData(FadeWindowData, win, "fade"),
        GetData(FadeWindowData, win, "fade")->fadeIn = true;

    if(!run) return;

    savetr = win->transparent;
    win->transparent = true;
    win->keepCount++;

    target = maxstep = getSteps(fadeDuration);
    progress = 0;
}

template<> bool Fade<FadeIn>::Step() {
    progress++;
    win->transform.color[3] = (float(progress) / float(maxstep));
    win->addDamage();

    if(progress == target) {
        if(restoretr)
            win->transparent = savetr;

        GetData(FadeWindowData, win, "fade")->fadeIn = false;

        win->transparent = savetr;
        win->keepCount--;

        return false;
    }
    return true;
}

template<> bool Fade<FadeIn>::Run() { return this->run; }
template<> Fade<FadeIn>::~Fade() {}
/* FadeIn  end */

/* FadeOut begin */
template<>
Fade<FadeOut>::Fade (View _win) : win(_win), run(true) {

    /* exit if already running */
    if(ExistsData(win, "fade")) {
        if(GetData(FadeWindowData, win, "fade")->fadeOut)
            run = false;
    }
    else AllocData(FadeWindowData, win, "fade"),
        GetData(FadeWindowData, win, "fade")->fadeOut = true;

    if(!run) return;

    if(GetData(FadeWindowData, win, "fade")->fadeIn)
        restoretr = false;

    savetr = win->transparent;
    win->keepCount++;
    win->transparent = true;

    progress = maxstep = getSteps(fadeDuration);
    target = 0;
}

template<>
bool Fade<FadeOut>::Step() {
    progress--;
    win->transform.color[3] = (float(progress) / float(maxstep));
    win->addDamage();

    if(progress == target) {
        if(restoretr)
            win->transparent = savetr;

        GetData(FadeWindowData, win, "fade")->fadeOut = false;

        win->transparent = savetr;
        win->keepCount--;

        if(!win->keepCount) {

            /* window is now unmapped */
            win->norender = true;
            XFreePixmap(core->d, win->pixmap);

            core->focusWindow(core->getActiveWindow());

            /* there's been DestroyNotify, delete window */
            if(win->destroyed)
                core->removeWindow(win);
        }
        return false;
    }
    return true;
}

template<> bool Fade<FadeOut>::Run() { return this->run; }
template<> Fade<FadeOut>::~Fade() {}
/* FadeOut end */
