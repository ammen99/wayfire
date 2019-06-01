#include <algorithm>
#include "output.hpp"
#include "core.hpp"
#include "debug.hpp"
#include "workspace-manager.hpp"
#include "output-layout.hpp"
#include "render-manager.hpp"
#include "wayfire-shell.hpp"
#include "wayfire-shell-protocol.h"
#include "signal-definitions.hpp"
#include "../view/view-impl.hpp"

struct wayfire_shell_output
{
    int inhibits = 0;
    std::vector<wl_resource*> resources;
};

struct wayfire_shell_client
{
    std::map<wf::output_t*, wayfire_shell_output> output_resources;
};

struct wayfire_shell
{
    std::map<wl_client*, wayfire_shell_client> clients;
    std::map<wf::output_t*, signal_callback_t> output_autohide_callback;

    signal_callback_t output_added, output_removed;
    static wayfire_shell& get_instance()
    {
        static wayfire_shell shell;
        return shell;
    }

    wayfire_shell(const wayfire_shell& other) = delete;
    void operator = (const wayfire_shell& other) = delete;

    private:
    wayfire_shell() {}
};

static wf::workspace_manager::anchored_edge anchor_edge_to_workspace_edge(uint32_t edge)
{
    if (edge == ZWF_WM_SURFACE_V1_ANCHOR_EDGE_TOP)
        return wf::workspace_manager::ANCHORED_EDGE_TOP;
    if (edge == ZWF_WM_SURFACE_V1_ANCHOR_EDGE_BOTTOM)
        return wf::workspace_manager::ANCHORED_EDGE_BOTTOM;
    if (edge == ZWF_WM_SURFACE_V1_ANCHOR_EDGE_LEFT)
        return wf::workspace_manager::ANCHORED_EDGE_LEFT;
    if (edge == ZWF_WM_SURFACE_V1_ANCHOR_EDGE_RIGHT)
        return wf::workspace_manager::ANCHORED_EDGE_RIGHT;

    log_error ("Unrecognized anchor edge %d", edge);
    return wf::workspace_manager::ANCHORED_EDGE_TOP;
}

class wayfire_shell_wm_surface : public wf::custom_data_t
{
    std::unique_ptr<wf::workspace_manager::anchored_area> area;
    /* output may be null, in which case the wm surface isn't tied to an output */
    wf::output_t *output = nullptr;
    wayfire_view view;

    uint32_t anchors = 0;
    struct {
        int top    = 0;
        int bottom = 0;
        int left   = 0;
        int right  = 0;
        bool margins_set = false;
    } margin;

    uint32_t focus_mode = -1;
    uint32_t exclusive_zone_size = 0;

    /* width/height with which the view position was last calculated */
    int32_t previous_width = 0, previous_height = 0;

    /* Requested layer focus in core */
    int32_t layer_focus_request = -1;
    inline void drop_focus_request()
    {
        wf::get_core().unfocus_layer(layer_focus_request);
        layer_focus_request = -1;
    }

    public:
    wayfire_shell_wm_surface(wf::output_t *output, wayfire_view view)
    {
        this->output = output;
        this->view = view;

        if (output)
        {
            view->connect_signal("geometry-changed", &on_geometry_changed);
            view->connect_signal("set-output", &on_view_output_changed);
        }
    }

    ~wayfire_shell_wm_surface()
    {
        /* Make sure we unfocus the current layer, if it was focused */
        drop_focus_request();

        view->disconnect_signal("geometry-changed", &on_geometry_changed);
        view->disconnect_signal("set-output", &on_view_output_changed);

        /* If the view's output has been reset, we have already reset the needed state
         * in the output changed handler */
        if (output && view->get_output() && area)
        {
            output->workspace->remove_reserved_area(area.get());
            output->workspace->reflow_reserved_areas();
        }
    }

    signal_callback_t on_view_output_changed = [=] (signal_data *data)
    {
        if (margin.margins_set || exclusive_zone_size)
        {
            /* An achored view should never be moved to a different output,
             * except if its output was closed, in which case the output is set
             * to nullptr */
            assert(view->get_output() == nullptr
                || view->get_output() == this->output);

            if (view->get_output() == nullptr && area)
            {
                output->workspace->remove_reserved_area(area.get());
                output->workspace->reflow_reserved_areas();
                area = nullptr;
            }
        }

        /* Don't forget to reset keyboard focus mode if moved to another output */
        if (focus_mode == ZWF_WM_SURFACE_V1_KEYBOARD_FOCUS_MODE_EXCLUSIVE_FOCUS
            && view->get_output() == nullptr)
        {
            drop_focus_request();
            focus_mode = ZWF_WM_SURFACE_V1_KEYBOARD_FOCUS_MODE_CLICK_TO_FOCUS;
        }
    };

    const uint32_t both_horiz =
        ZWF_WM_SURFACE_V1_ANCHOR_EDGE_TOP | ZWF_WM_SURFACE_V1_ANCHOR_EDGE_BOTTOM;
    const uint32_t both_vert =
        ZWF_WM_SURFACE_V1_ANCHOR_EDGE_LEFT | ZWF_WM_SURFACE_V1_ANCHOR_EDGE_RIGHT;

    void set_anchor(uint32_t anchors)
    {
        if (!output)
        {
            log_error("wayfire-shell: attempt to set anchor for an outputless wm-surface");
            return;
        }

        /* Just a warning, the result will be wrong, but won't crash */
        if ((anchors & both_vert) == both_vert || (anchors & both_horiz) == both_horiz)
        {
            log_error("wayfire-shell: Failed to set anchors, \
                opposing edges detected.");
        }

        this->anchors = anchors;
        if (anchors > 0)
        {
            if (margin.margins_set)
                set_margin(margin.top, margin.bottom, margin.left, margin.right);
            set_exclusive_zone(this->exclusive_zone_size);
        }
    }

    std::function<void(wf_geometry, wf_geometry)> on_reflow =
    [=] (wf_geometry _, wf_geometry workarea)
    {
        if (!margin.margins_set)
            return;

        auto surface_geometry = view->get_wm_geometry();

        /* First calculate offsets from edges of the available workarea */
        int x = workarea.x, y = workarea.y;
        if (anchors & ZWF_WM_SURFACE_V1_ANCHOR_EDGE_TOP)
            y += margin.top;
        if (anchors & ZWF_WM_SURFACE_V1_ANCHOR_EDGE_BOTTOM)
            y = workarea.y + workarea.height - surface_geometry.height - margin.bottom;

        if (anchors & ZWF_WM_SURFACE_V1_ANCHOR_EDGE_LEFT)
             x += margin.left;
        if (anchors & ZWF_WM_SURFACE_V1_ANCHOR_EDGE_RIGHT)
            x = workarea.x + workarea.width - surface_geometry.width - margin.right;

        /* Second, if the wm surface is anchored to a single edge,
         * center it on that edge */
        if (__builtin_popcount(anchors) == 1)
        {
            if (anchors & (both_horiz)) {
                x = workarea.x + workarea.width / 2 - surface_geometry.width / 2;
            } else {
                y = workarea.y + workarea.height / 2 - surface_geometry.height / 2;
            }
        }

        previous_width = surface_geometry.width;
        previous_height = surface_geometry.height;

        view->move(x, y);
    };

    signal_callback_t on_geometry_changed = [=] (signal_data *data)
    {
        auto wm = view->get_wm_geometry();

        /* Trigger a reflow to reposition the view according to newer geometry */
        if (wm.width != previous_width || wm.height != previous_height)
            set_exclusive_zone(exclusive_zone_size);
    };

    void set_margin(int32_t top, int32_t bottom, int32_t left, int32_t right)
    {
        if (!output)
        {
            log_error("wayfire-shell: attempt to set margin for an outputless wm-surface");
            return;
        }

        margin.top = top;
        margin.bottom = bottom;
        margin.left = left;
        margin.right = right;
        margin.margins_set = true;

        /* set_exclusive_zone will trigger a reflow */
        set_exclusive_zone(exclusive_zone_size);
    }

    void set_keyboard_mode(uint32_t new_mode)
    {
        if (!output && new_mode == ZWF_WM_SURFACE_V1_KEYBOARD_FOCUS_MODE_EXCLUSIVE_FOCUS)
        {
            log_error("wayfire-shell: cannot set exclusive focus for outputless"
                " wm surface");
            return;
        }

        /* Nothing to do */
        if (focus_mode == new_mode)
            return;

        if (this->focus_mode == ZWF_WM_SURFACE_V1_KEYBOARD_FOCUS_MODE_EXCLUSIVE_FOCUS)
            drop_focus_request();

        this->focus_mode = new_mode;
        switch(new_mode)
        {
            case ZWF_WM_SURFACE_V1_KEYBOARD_FOCUS_MODE_NO_FOCUS:
                view->view_impl->keyboard_focus_enabled = false;
                view->get_output()->refocus(nullptr);
                break;

            case ZWF_WM_SURFACE_V1_KEYBOARD_FOCUS_MODE_CLICK_TO_FOCUS:
                view->view_impl->keyboard_focus_enabled = true;
                break;

            case ZWF_WM_SURFACE_V1_KEYBOARD_FOCUS_MODE_EXCLUSIVE_FOCUS:
                /* Notice: using output here is safe, because we do not allow
                 * exclusive focus for outputless surfaces */
                view->view_impl->keyboard_focus_enabled = true;
                layer_focus_request = wf::get_core().focus_layer(
                    output->workspace->get_view_layer(view), layer_focus_request);
                output->focus_view(view);
                break;

            default:
                log_error ("wayfire-shell: Invalid keyboard mode!");
                break;
        };
    }

    /* We keep an exclusive zone even if its size is 0, because
     * margin positioning depends on the reflow callback */
    void set_exclusive_zone(uint32_t size)
    {
        if (!output)
        {
            log_error("wayfire-shell: attempt to set exlusive zone for an "
                " outputless wm-surface");
            return;
        }

        this->exclusive_zone_size = size;

        if (__builtin_popcount(anchors) != 1)
            return;

        bool new_area = false;
        if (!area)
        {
            area = std::make_unique<wf::workspace_manager::anchored_area> ();
            area->reflowed = on_reflow;
            new_area = true;
        }

        area->edge = anchor_edge_to_workspace_edge(anchors);
        area->reserved_size = size;
        area->real_size = size;

        if (new_area)
            output->workspace->add_reserved_area(area.get());

        output->workspace->reflow_reserved_areas();
    }
};

static nonstd::observer_ptr<wayfire_shell_wm_surface>
wm_surface_from_view(wayfire_view view)
{
    return view->get_data<wayfire_shell_wm_surface>();
}

static wayfire_view view_from_resource(wl_resource *resource)
{
    // TODO: assert(wl_resource_instance_of)
    return static_cast<wf::view_interface_t*> (
        wl_resource_get_user_data(resource))->self();
}

static nonstd::observer_ptr<wayfire_shell_wm_surface>
wm_surface_from_resource(wl_resource *resource)
{
    return wm_surface_from_view(view_from_resource(resource));
}

static void handle_wm_surface_configure(wl_client *client, wl_resource *resource,
    int32_t x, int32_t y)
{
    view_from_resource(resource)->move(x, y);
}

static void handle_wm_surface_set_anchor(wl_client *client, wl_resource *resource,
    uint32_t anchors)
{
    wm_surface_from_resource(resource)->set_anchor(anchors);
}

static void handle_wm_surface_set_margin(wl_client *client, wl_resource *resource,
    int32_t top, int32_t bottom, int32_t left, int32_t right)
{
    wm_surface_from_resource(resource)->set_margin(top, bottom, left, right);
}

static void handle_wm_surface_set_keyboard_mode(wl_client *client,
    wl_resource *resource, uint32_t mode)
{
    wm_surface_from_resource(resource)->set_keyboard_mode(mode);
}

static void handle_wm_surface_set_exclusive_zone(wl_client *client,
    wl_resource *resource, uint32_t size)
{
    wm_surface_from_resource(resource)->set_exclusive_zone(size);
}

const struct zwf_wm_surface_v1_interface zwf_wm_surface_v1_implementation = {
    .configure = handle_wm_surface_configure,
    .set_anchor = handle_wm_surface_set_anchor,
    .set_margin = handle_wm_surface_set_margin,
    .set_keyboard_mode = handle_wm_surface_set_keyboard_mode,
    .set_exclusive_zone = handle_wm_surface_set_exclusive_zone
};

static void zwf_shell_manager_get_wm_surface(struct wl_client *client,
    struct wl_resource *resource, struct wl_resource *surface,
    uint32_t role, struct wl_resource *output, uint32_t id)
{
    wf::output_t *wo = output ?
        wf::get_core().output_layout->find_output(
            wlr_output_from_resource(output)) : nullptr;

    auto view = wf::wl_surface_to_wayfire_view(surface);

    if (!view)
    {
        log_error ("wayfire_shell: get_wm_surface() for invalid surface!");
        return;
    }

    auto wf_wm_surface = std::make_unique<wayfire_shell_wm_surface> (wo, view);
    view->store_data<wayfire_shell_wm_surface> (std::move(wf_wm_surface));

    auto wfo = wl_resource_create(client, &zwf_wm_surface_v1_interface, 1, id);
    wl_resource_set_implementation(wfo, &zwf_wm_surface_v1_implementation, view.get(), NULL);

    view->set_role(wf::VIEW_ROLE_SHELL_VIEW);
    if (wo)
    {
        view->get_output()->workspace->remove_view(view);
        view->set_output(wo);
    }

    uint32_t layer = 0;
    switch(role)
    {
        case ZWF_WM_SURFACE_V1_ROLE_BACKGROUND:
            layer = wf::LAYER_BACKGROUND;
            break;
        case ZWF_WM_SURFACE_V1_ROLE_BOTTOM:
            layer = wf::LAYER_BOTTOM;
            break;
        case ZWF_WM_SURFACE_V1_ROLE_PANEL:
            layer = wf::LAYER_TOP;
            break;
        case ZWF_WM_SURFACE_V1_ROLE_OVERLAY:
            layer = wf::LAYER_LOCK;
            break;

        default:
            log_error ("Invalid role for shell view");
    }

    view->get_output()->workspace->add_view(view, (wf::layer_t)layer);
    view->set_activated(true);
}

static void zwf_output_inhibit_output(struct wl_client *client,
                                      struct wl_resource *resource)
{
    auto wo = (wf::output_t*)wl_resource_get_user_data(resource);
    wo->render->add_inhibit(true);

    auto& cl = wayfire_shell::get_instance().clients[client];
    auto& out = cl.output_resources[wo];
    ++out.inhibits;
}

static void zwf_output_inhibit_output_done(struct wl_client *client,
                                           struct wl_resource *resource)
{
    auto wo = (wf::output_t*)wl_resource_get_user_data(resource);
    auto& cl = wayfire_shell::get_instance().clients[client];
    auto& out = cl.output_resources[wo];

    if (out.inhibits <= 0)
    {
        log_error ("wayfire-shell: inhibit_output_done but no active inhibits?");
        return;
    }

    --out.inhibits;
    wo->render->add_inhibit(false);
}

const struct zwf_output_v1_interface zwf_output_v1_implementation =
{
    zwf_output_inhibit_output,
    zwf_output_inhibit_output_done,
};

static void destroy_zwf_output(wl_resource *resource)
{
    auto client = wl_resource_get_client(resource);
    auto wo = (wf::output_t*) wl_resource_get_user_data(resource);
    if (wayfire_shell::get_instance().clients.count(client) == 0)
        return;

    auto& shell_client = wayfire_shell::get_instance().clients[client];
    if (shell_client.output_resources.count(wo) == 0)
        return;

    auto& client_output = shell_client.output_resources[wo];
    auto it = std::find(client_output.resources.begin(), client_output.resources.end(),
                        resource);

    while(client_output.inhibits--)
        wo->render->add_inhibit(false);

    client_output.resources.erase(it);
}

void zwf_shell_manager_get_wf_output(struct wl_client *client,
                                     struct wl_resource *resource,
                                     struct wl_resource *output,
                                     uint32_t id)
{
    auto wlr_out = (wlr_output*) wl_resource_get_user_data(output);
    auto wo = wf::get_core().output_layout->find_output(wlr_out);

    auto wfo = wl_resource_create(client, &zwf_output_v1_interface, 1, id);
    wl_resource_set_implementation(wfo, &zwf_output_v1_implementation, wo, destroy_zwf_output);

    auto& shell_client = wayfire_shell::get_instance().clients[client];
    auto& client_output = shell_client.output_resources[wo];

    client_output.resources.push_back(wfo);
}

const struct zwf_shell_manager_v1_interface zwf_shell_manager_v1_implementation =
{
    zwf_shell_manager_get_wf_output,
    zwf_shell_manager_get_wm_surface,
};

static void destroy_zwf_shell_manager(wl_resource *resource)
{
    auto client = wl_resource_get_client(resource);

    for (auto& out : wayfire_shell::get_instance().clients[client].output_resources)
    {
        while(out.second.inhibits > 0)
            out.first->render->add_inhibit(false);
    }

    wayfire_shell::get_instance().clients.erase(client);
}

void bind_zwf_shell_manager(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    auto resource = wl_resource_create(client, &zwf_shell_manager_v1_interface, 1, id);
    wl_resource_set_implementation(resource, &zwf_shell_manager_v1_implementation, NULL, destroy_zwf_shell_manager);
}


void zwf_output_send_autohide(wayfire_shell *shell, wf::output_t *output, int value)
{
    for (auto& client : shell->clients)
    {
        if (client.second.output_resources.count(output))
        {
            for (auto resource : client.second.output_resources[output].resources)
                zwf_output_v1_send_output_hide_panels(resource, value);
        }
    }
}

static void wayfire_shell_handle_output_created(wf::output_t *output)
{
    auto& shell = wayfire_shell::get_instance();
    shell.output_autohide_callback[output] = [=] (signal_data *flag)
    {
        zwf_output_send_autohide(&wayfire_shell::get_instance(), output, bool(flag));
    };

    /* std::map is guaranteed to not invalidate references to elements in it,
     * so passing such a pointer is safe */
    output->connect_signal("autohide-panels", &shell.output_autohide_callback[output]);
}

static void wayfire_shell_handle_output_destroyed(wf::output_t *output)
{
}

wayfire_shell* wayfire_shell_create(wl_display *display)
{
    if (wl_global_create(display, &zwf_shell_manager_v1_interface,
                         1, NULL, bind_zwf_shell_manager) == NULL)
    {
        log_error("Failed to create wayfire_shell interface");
    }

    auto& shell = wayfire_shell::get_instance();
    shell.output_added = [=] (signal_data *data) {
        wayfire_shell_handle_output_created(get_signaled_output(data));
    };
    shell.output_removed = [=] (signal_data *data) {
        wayfire_shell_handle_output_destroyed( get_signaled_output(data));
    };

    wf::get_core().output_layout->connect_signal("output-added", &shell.output_added);
    wf::get_core().output_layout->connect_signal("output-removed", &shell.output_removed);
    return &shell;
}
