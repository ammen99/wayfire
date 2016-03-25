#include "commonincludes.hpp"

#define SizeStates (WindowStateMaxH|WindowStateMaxV|WindowStateFullscreen)

class Transform {
    public: // applied to all windows
        static glm::mat4 grot;
        static glm::mat4 gscl;
        static glm::mat4 gtrs;

        static glm::mat4 ViewProj;
        static bool has_rotation;
    public:
        glm::mat4 rotation;
        glm::mat4 scalation;
        glm::mat4 translation;
        glm::mat4 translateToCenter;

        glm::vec4 color;
    public:
        Transform();
        glm::mat4 compose();
};


struct ViewData {
};

struct EffectHook;
class Output;

bool point_inside(wlc_point point, wlc_geometry rect);
bool rect_inside(wlc_geometry screen, wlc_geometry win);

class FireView {
    wlc_handle view;
    wlc_resource surface;

    public:
        Output *output;

        FireView(wlc_handle);
        ~FireView();
        /* this can be used by plugins to store
         * specific for the plugin data */
        std::unordered_map<std::string, ViewData> data;
        std::unordered_map<uint, EffectHook*> effects;

        wlc_geometry attrib;

        Transform transform;

        bool is_visible();

        /* default_mask is the mask of all viewports the current view is visible on */
        uint32_t default_mask;
        bool has_temporary_mask = false;

        /* vx and vy are the coords of the viewport where the top left corner is located */
        int vx, vy;

        void set_mask(uint32_t mask);
        void restore_mask();
        void set_temporary_mask(uint32_t tmask);

        void move(int x, int y);
        void resize(int w, int h);

        void set_geometry(int x, int y, int w, int h);
        void set_geometry(wlc_geometry g);

        wlc_handle get_id() {return view;}
        wlc_resource get_surface() {return surface;}
};

void render_surface(wlc_resource surface, wlc_geometry g, glm::mat4 transform, uint32_t bits = 0);

typedef std::shared_ptr<FireView> View;
class Core;
