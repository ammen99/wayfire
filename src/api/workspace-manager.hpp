#ifndef WORKSPACE_MANAGER_HPP
#define WORKSPACE_MANAGER_HPP

#include <memory>
#include <functional>
#include <vector>
#include <view.hpp>

class wayfire_view_t;
using wayfire_view = std::shared_ptr<wayfire_view_t>;
using view_callback_proc_t = std::function<void(wayfire_view)>;

struct wf_workspace_implementation
{
    virtual bool view_movable(wayfire_view view) = 0;
    virtual bool view_resizable(wayfire_view view) = 0;
};

enum wf_layer
{
    WF_LAYER_BACKGROUND = (1 << 0),
    WF_LAYER_BOTTOM     = (1 << 1),
    WF_LAYER_WORKSPACE  = (1 << 2),
    WF_LAYER_XWAYLAND   = (1 << 3),
    WF_LAYER_TOP        = (1 << 4),
    WF_LAYER_LOCK       = (1 << 5)
};

#define WF_WM_LAYERS    (WF_LAYER_WORKSPACE  | WF_LAYER_XWAYLAND)
#define WF_ABOVE_LAYERS (WF_LAYER_TOP        | WF_LAYER_LOCK)
#define WF_BELOW_LAYERS (WF_LAYER_BACKGROUND | WF_LAYER_BOTTOM)

#define WF_ALL_LAYERS   (WF_WM_LAYERS | WF_ABOVE_LAYERS | WF_BELOW_LAYERS)

/* workspace manager controls various workspace-related functions.
 * Currently it is implemented as a plugin, see workspace_viewport_implementation plugin */
class workspace_manager
{
    public:
        /* return if the view is visible on the given workspace */
        virtual bool view_visible_on(wayfire_view view, std::tuple<int, int>) = 0;

        virtual std::vector<wayfire_view>
            get_views_on_workspace(std::tuple<int, int> ws, uint32_t layer_mask) = 0;
        virtual void for_each_view(view_callback_proc_t call, uint32_t layers_mask) = 0;
        virtual void for_each_view_reverse(view_callback_proc_t call, uint32_t layers_mask) = 0;

        /* TODO: split this api? */

        /* if layer_mask == 0, then we remove the view from its layer,
         * if layer_mask == -1, then the view will be moved to the top of its layer */
        virtual void add_view_to_layer(wayfire_view view, uint32_t layer) = 0;

        virtual uint32_t get_view_layer(wayfire_view view) = 0;

        /* return the active wf_workspace_implementation for the given workpsace */
        virtual wf_workspace_implementation* get_implementation(std::tuple<int, int>) = 0;

        /* returns true if implementation of workspace has been successfully installed.
         * @param override - override current implementation if it is existing.
         * it must be guaranteed that if override is set, then the functions returns true */
        virtual bool set_implementation(std::tuple<int, int>, wf_workspace_implementation *, bool override = false) = 0;

        virtual void set_workspace(std::tuple<int, int>) = 0;
        virtual std::tuple<int, int> get_current_workspace() = 0;
        virtual std::tuple<int, int> get_workspace_grid_size() = 0;

        /* position is an enum of wayfire-shell-protocol */
        virtual void reserve_workarea(uint32_t position,
                uint32_t width, uint32_t height) = 0;

        /* returns the available area for views, it is basically
         * the output geometry minus the area reserved for panels */
        virtual wf_geometry get_workarea() = 0;

        virtual ~workspace_manager();
};

#endif /* end of include guard: WORKSPACE_MANAGER_HPP */
