#include <output.hpp>
#include <algorithm>

struct GridWindow {
    View v;
    wlc_geometry initial_geometry, target_geometry;
};

class Grid : public Plugin {

    std::unordered_map<wlc_handle, wlc_geometry> saved_view_geometry;

    KeyBinding keys[10];
    uint32_t codes[10];

    Hook rnd;
    int steps = 1;
    int curstep;
    GridWindow currentWin;

    public:
    Grid() {
    }

    void init() {
        options.insert(newIntOption("duration", 200));

        codes[1] = XKB_KEY_KP_End;
        codes[2] = XKB_KEY_KP_Down;
        codes[3] = XKB_KEY_KP_Page_Down;
        codes[4] = XKB_KEY_KP_Left;
        codes[5] = XKB_KEY_KP_Begin;
        codes[6] = XKB_KEY_KP_Right;
        codes[7] = XKB_KEY_KP_Home;
        codes[8] = XKB_KEY_KP_Up;
        codes[9] = XKB_KEY_KP_Page_Up;

        using namespace std::placeholders;

        for(int i = 1; i < 10; i++) {
            keys[i].key    = codes[i];
            keys[i].mod    = WLC_BIT_MOD_ALT | WLC_BIT_MOD_CTRL;
            keys[i].action = std::bind(std::mem_fn(&Grid::handleKey), this, _1);
            keys[i].type   = BindingTypePress;
            output->hook->add_key(&keys[i], true);
        }

        rnd.action = std::bind(std::mem_fn(&Grid::step), this);
        output->hook->add_hook(&rnd);
    }

    void initOwnership(){
        grab_interface->name = "grid";
        grab_interface->compatAll = false;
    }

    void updateConfiguration() {
        steps = options["duration"]->data.ival;
    }


#define GetProgress(start,end,curstep,steps) ((float(end)*(curstep)+float(start) \
                                            *((steps)-(curstep)))/(steps))

    void step() {
        int cx = GetProgress(currentWin.initial_geometry.origin.x,
                currentWin.target_geometry.origin.x, curstep, steps);
        int cy = GetProgress(currentWin.initial_geometry.origin.y,
                currentWin.target_geometry.origin.y, curstep, steps);
        int cw = GetProgress(currentWin.initial_geometry.size.w,
                currentWin.target_geometry.size.w, curstep, steps);
        int ch = GetProgress(currentWin.initial_geometry.size.h,
                currentWin.target_geometry.size.h, curstep, steps);

        currentWin.v->set_geometry(cx, cy, cw, ch);

        curstep++;
        if (curstep == steps) {
            currentWin.v->set_geometry(currentWin.target_geometry);

            wlc_view_set_state(currentWin.v->get_id(), WLC_BIT_RESIZING, false);
            output->render->set_redraw_everything(false);
            rnd.disable();
        }
    }

    void toggleMaxim(View v, int &x, int &y, int &w, int &h) {
        auto it = saved_view_geometry.find(v->get_id());

        if (it == saved_view_geometry.end()) {
            saved_view_geometry[v->get_id()] = v->attrib;
            GetTuple(sw, sh, output->get_screen_size());
            x = y = 0, w = sw, h = sh;
        } else {
            x = it->second.origin.x;
            y = it->second.origin.y;
            w = it->second.size.w;
            h = it->second.size.h;

            saved_view_geometry.erase(it);
        }
    }

    void getSlot(int n, int &x, int &y, int &w, int &h) {
        GetTuple(width, height, output->get_screen_size());

        int w2 = width  / 2;
        int h2 = height / 2;
        if(n == 7)
            x = 0, y = 0, w = w2, h = h2;
        if(n == 8)
            x = 0, y = 0, w = width, h = h2;
        if(n == 9)
            x = w2, y = 0, w = w2, h = h2;
        if(n == 4)
            x = 0, y = 0, w = w2, h = height;
        if(n == 6)
            x = w2, y = 0, w = w2, h = height;
        if(n == 1)
            x = 0, y = h2, w = w2, h = h2;
        if(n == 2)
            x = 0, y = h2, w = width, h = h2;
        if(n == 3)
            x = w2, y = h2, w = w2, h = h2;
    }

    void handleKey(EventContext ctx) {
        auto view = output->get_active_view();
        if (!view)
            return;

        int x, y, w, h;
        for(int i = 1; i < 10; i++) {
            if (ctx.xev.xkey.key == codes[i]) {
                if (i == 5)
                    toggleMaxim(view, x, y, w, h);
                else
                    getSlot(i, x, y, w, h);
            }
        }

        currentWin = {
            .v = view,
            .initial_geometry = view->attrib,
            .target_geometry = {{x, y}, {(uint)w, (uint)h}}
        };

        curstep = 0;
        rnd.enable();
        wlc_view_set_state(view->get_id(), WLC_BIT_RESIZING, true);
        output->render->set_redraw_everything(true);
    }
};

extern "C" {
    Plugin *newInstance() {
        return new Grid();
    }
}
