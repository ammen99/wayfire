#ifndef WORKSPACE_MANAGER_HPP
#define WORKSPACE_MANAGER_HPP

#include <memory>
#include <functional>
#include <vector>
#include <view.hpp>
#include "../../proto/wayfire-shell-server.h"

class wayfire_view_t;
using wayfire_view = std::shared_ptr<wayfire_view_t>;
using view_callback_proc_t = std::function<void(wayfire_view)>;

struct wf_workspace_implementation
{
    virtual bool view_movable(wayfire_view view) = 0;
    virtual bool view_resizable(wayfire_view view) = 0;
};

/* workspace manager controls various workspace-related functions.
 * Currently it is implemented as a plugin, see workspace_viewport_implementation plugin */
class workspace_manager
{
    public:
        /* we could actually attach signal listeners, but this is easier */
        virtual void view_bring_to_front(wayfire_view view) = 0;
        virtual void view_removed(wayfire_view view) = 0;

        /* return if the view is visible on the given workspace */
        virtual bool view_visible_on(wayfire_view view, std::tuple<int, int>) = 0;

        /* executes call for each view managed by the workspace manager
         * includes background and panels */
        virtual void for_all_view(view_callback_proc_t call) = 0;

        /* executes call for each desktop view(~user apps) in the workspace manager */
        virtual void for_each_view(view_callback_proc_t call) = 0;
        virtual void for_each_view_reverse(view_callback_proc_t call) = 0;

        /* return the active wf_workspace_implementation for the given workpsace */
        virtual wf_workspace_implementation* get_implementation(std::tuple<int, int>) = 0;
        /* returns true if implementation of workspace has been successfully installed.
         * @param override - override current implementation if it is existing.
         * it must be guaranteed that if override is set, then the functions returns true */
        virtual bool set_implementation(std::tuple<int, int>, wf_workspace_implementation *, bool override = false) = 0;

        /* toplevel views (i.e windows) on the given workspace */
        virtual std::vector<wayfire_view>
            get_views_on_workspace(std::tuple<int, int>) = 0;

        virtual void set_workspace(std::tuple<int, int>) = 0;
        virtual std::tuple<int, int> get_current_workspace() = 0;
        virtual std::tuple<int, int> get_workspace_grid_size() = 0;

        virtual wayfire_view get_background_view() = 0;

        /* returns a list of all views on workspace that are visible on the current
         * workspace except panels(but should include background)
         * The list must be returned from top to bottom(i.e the last is background) */
        virtual std::vector<wayfire_view>
            get_renderable_views_on_workspace(std::tuple<int, int> ws) = 0;

        /* add/remove a custom view which should be rendered on the current output
         * useful for overlays like on-screen keyboard or similar */
        virtual void add_renderable_view(wayfire_view view) = 0;
        virtual void rem_renderable_view(wayfire_view view) = 0;

        /* panels are the same on each workspace */
        virtual std::vector<wayfire_view> get_panels() = 0;

        /* wayfire_shell implementation */
        virtual void add_background(wayfire_view background, int x, int y) = 0;
        virtual void add_panel(wayfire_view panel) = 0;
        virtual void reserve_workarea(wayfire_shell_panel_position position,
                uint32_t width, uint32_t height) = 0;
        virtual void configure_panel(wayfire_view view, int x, int y) = 0;

        /* returns the available area for views, it is basically
         * the output geometry minus the area reserved for panels */
        virtual wf_geometry get_workarea() = 0;

        virtual ~workspace_manager();
};

#endif /* end of include guard: WORKSPACE_MANAGER_HPP */
