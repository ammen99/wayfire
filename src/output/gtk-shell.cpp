#include "gtk-shell.hpp"
#include "gtk-shell-protocol.h"

#include "view.hpp"
#include "../core/core-impl.hpp"
#include "core.hpp"
#include "debug.hpp"
#include <map>

struct wf_gtk_shell {
    std::map<wl_resource*, std::string> surface_app_id;
};

static void handle_gtk_surface_set_dbus_properties(wl_client *, wl_resource *resource,
    const char *application_id,
    const char *, const char *, const char *, const char *, const char *)
{
    auto surface = static_cast<wl_resource*> (wl_resource_get_user_data(resource));
    if (application_id)
        wf::get_core_impl().gtk_shell->surface_app_id[surface] = application_id;
}

static void handle_gtk_surface_set_modal(wl_client *client, wl_resource *resource) { }
static void handle_gtk_surface_unset_modal(wl_client *client, wl_resource *resource) { }
static void handle_gtk_surface_present(wl_client *client, wl_resource *resource, uint32_t time) { }
static void handle_gtk_surface_destroy(wl_resource *resource) { }

const struct gtk_surface1_interface gtk_surface1_impl = {
	.set_dbus_properties = handle_gtk_surface_set_dbus_properties,
	.set_modal = handle_gtk_surface_set_modal,
	.unset_modal = handle_gtk_surface_unset_modal,
	.present = handle_gtk_surface_present,
};

static void handle_gtk_shell_get_gtk_surface(wl_client *client, wl_resource *resource,
    uint32_t id, wl_resource *surface)
{
    auto res = wl_resource_create(client, &gtk_surface1_interface,
        wl_resource_get_version(resource), id);
    wl_resource_set_implementation(res, &gtk_surface1_impl,
        surface, handle_gtk_surface_destroy);
}

static void handle_gtk_shell_set_startup_id(wl_client *client, wl_resource *resource, const char *startup_id) {}
static void handle_gtk_shell_system_bell(wl_client *client, wl_resource *resource, wl_resource *surface) {}

static const struct gtk_shell1_interface gtk_shell1_impl = {
	.get_gtk_surface = handle_gtk_shell_get_gtk_surface,
	.set_startup_id = handle_gtk_shell_set_startup_id,
	.system_bell = handle_gtk_shell_system_bell,
};

static void handle_gtk_shell1_destroy(wl_resource *resource) {}

void bind_gtk_shell1(wl_client *client, void *data, uint32_t version, uint32_t id)
{
    auto resource = wl_resource_create(client, &gtk_shell1_interface, 1, id);
    wl_resource_set_implementation(resource, &gtk_shell1_impl,
        NULL, handle_gtk_shell1_destroy);
}

wf_gtk_shell* wf_gtk_shell_create(wl_display *display)
{
    if (wl_global_create(display, &gtk_shell1_interface, 1, NULL,
            bind_gtk_shell1) == NULL)
    {
        log_error("Failed to create gtk_shell1");
        return nullptr;
    }

    return new wf_gtk_shell;
}

std::string wf_gtk_shell_get_custom_app_id(wf_gtk_shell *shell,
    wl_resource *surface)
{
    return shell->surface_app_id[surface];
}
