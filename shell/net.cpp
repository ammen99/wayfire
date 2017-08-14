#include "net.hpp"
#include <gio/gio.h>
#include <iostream>

struct network_provider_backend
{
    /* setup backend AND initially populate the connection info, then set updated = true */
    virtual bool create(connection_info *store) = 0;
    /* called by the thread starter, see network_widget source code */
    virtual void thread_loop() = 0;

    virtual ~network_provider_backend() = 0;
};

network_provider_backend::~network_provider_backend()
{
}

static void
on_wifi_properties_changed (GDBusProxy          *proxy,
                            GVariant            *changed_properties,
                            const gchar* const  *invalidated_properties,
                            gpointer             user_data)
{
    connection_info *info = (connection_info*) user_data;
    if (g_variant_n_children(changed_properties) > 0)
    {
        GVariantIter *iter;
        g_variant_get(changed_properties, "a{sv}", &iter);

        const gchar *key;
        GVariant *value;
        while (g_variant_iter_loop(iter, "{&sv}", &key, &value))
        {
            if (std::string(key) == "Strength")
            {
                info->mutex.lock();
                info->strength = g_variant_get_byte(value);
                info->updated = true;
                info->mutex.unlock();
            }
        }

        g_variant_iter_free(iter);
    }
}

using updater_callback = std::function<void()>;

static void
on_nm_properties_changed (GDBusProxy          *proxy,
                            GVariant            *changed_properties,
                            const gchar* const  *invalidated_properties,
                            gpointer             user_data)
{
    updater_callback *callback = (updater_callback*) user_data;
    if (g_variant_n_children(changed_properties) > 0)
    {
        GVariantIter *iter;
        g_variant_get(changed_properties, "a{sv}", &iter);

        const gchar *key;
        GVariant *value;
        while (g_variant_iter_loop(iter, "{&sv}", &key, &value))
        {
            if (std::string(key) == "PrimaryConnection")
                (*callback)();
        }

        g_variant_iter_free(iter);
    }
}

struct network_manager_provider : public network_provider_backend
{
    connection_info *info;
    GDBusConnection *dbus_connection;
    GDBusProxy *nm_proxy;

#define NM_DBUS_NAME "org.freedesktop.NetworkManager"

    bool setup_dbus_connection()
    {
        GError *error = NULL;
        dbus_connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

        if (!dbus_connection)
        {
            std::cerr << "Failed to connect to system bus: "
                      << error->message << std::endl;
            return false;
        }

        nm_proxy = g_dbus_proxy_new_sync(dbus_connection,
                                         G_DBUS_PROXY_FLAGS_NONE, NULL,
                                         NM_DBUS_NAME,
                                         "/org/freedesktop/NetworkManager",
                                         "org.freedesktop.NetworkManager",
                                         NULL, &error);

        return true;
    }

    GDBusProxy* current_specific_proxy = NULL;

    void load_wifi_data(const gchar *ap)
    {
        GError *error = NULL;
        current_specific_proxy =
            g_dbus_proxy_new_sync(dbus_connection, G_DBUS_PROXY_FLAGS_NONE, NULL,
                                  NM_DBUS_NAME,
                                  ap,
                                  "org.freedesktop.NetworkManager.AccessPoint",
                                  NULL, &error);

        if (!current_specific_proxy)
        {
            std::cerr << "Failed to obtain AP info: " << error->message << std::endl;
            return;
        }

        GVariant *gv = g_dbus_proxy_get_cached_property(current_specific_proxy, "Strength");

        info->mutex.lock();
        info->strength = g_variant_get_byte(gv);
        info->updated = true;
        info->mutex.unlock();

        g_signal_connect(current_specific_proxy, "g-properties-changed",
                         G_CALLBACK(on_wifi_properties_changed), info);
    }

    void load_bluetooth_data(const gchar *dev)
    {
        /* TODO: implement */
    }

    void load_ethernet_data(const gchar *dev)
    {
        info->mutex.lock();

        info->icon = "none";
        info->strength = 100;
        info->name = "Ethernet";

        info->updated = true;
        info->mutex.unlock();
    }

    void active_connection_updated()
    {
        GVariant *gv = g_dbus_proxy_get_cached_property(
                nm_proxy, "PrimaryConnection");

        gsize n;
        const gchar *active_connection = g_variant_get_string(gv, &n);

        /* no active connection */
        if (std::string(active_connection) == "/")
        {
            info->mutex.lock();
            info->name = "No network";
            info->updated = true;
            info->strength = 0;
            info->mutex.unlock();

            return;
        }

        GError *error = NULL;
        GDBusProxy *aconn_proxy =
            g_dbus_proxy_new_sync(dbus_connection,
                                  G_DBUS_PROXY_FLAGS_NONE, NULL,
                                  NM_DBUS_NAME,
                                  active_connection,
                                  "org.freedesktop.NetworkManager.Connection.Active",
                                  NULL, &error);

        if (!aconn_proxy)
        {
            std::cerr << "Failed to get active connection: " << error->message << std::endl;
            return;
        }

        gv = g_dbus_proxy_get_cached_property(aconn_proxy, "Type");
        std::string type = g_variant_get_string(gv, &n);

        gv = g_dbus_proxy_get_cached_property(aconn_proxy, "Id");

        info->mutex.lock();
        info->name = g_variant_get_string(gv, &n);
        info->mutex.unlock();

        gv = g_dbus_proxy_get_cached_property(aconn_proxy, "SpecificObject");
        const gchar *object = g_variant_get_string(gv, &n);

        if (current_specific_proxy)
            g_object_unref(current_specific_proxy);

        if (type == "bluetooth")
        {
            load_bluetooth_data(object);
        } else if (type.find("ethernet") != std::string::npos)
        {
            load_ethernet_data(object);
        } else if (type.find("wireless") != std::string::npos)
        {
            load_wifi_data(object);
        }

        g_object_unref(aconn_proxy);
    }

    void load_initial_connection_info()
    {
        /* don't have to lock mutex, as we are still in the main thread */
        info->updated = true;
        info->icon = info->name = "none";
        info->strength = 0;

        active_connection_updated();
    }

    bool create(connection_info *info)
    {
        this->info = info;

        if (!setup_dbus_connection())
            return false;

        load_initial_connection_info();
        return true;
    }

    void thread_loop()
    {
        updater_callback callback =
            std::bind(std::mem_fn(&network_manager_provider::active_connection_updated), this);

        g_signal_connect(nm_proxy, "g-properties-changed",
                         G_CALLBACK(on_nm_properties_changed), &callback);

        GMainLoop *loop = g_main_loop_new(NULL, false);
        g_main_loop_run(loop);

        g_main_loop_unref(loop);
        g_object_unref(nm_proxy);
    }

    ~network_manager_provider()
    {
    }
};

void network_widget::create()
{
    backend = new network_manager_provider();
    if (!backend->create(&connection))
    {
        delete backend;
        backend = nullptr;
        return;
    }

    updater_thread = std::thread([=] () { backend->thread_loop(); });

    cairo_set_font_size(cr, font_size);
    cairo_set_font_face(cr, cairo_font_face);
}

bool network_widget::update()
{
    bool result;
    connection.mutex.lock();
    result = connection.updated;
    connection.mutex.unlock();
    return result;
}

void network_widget::repaint()
{
    constexpr wayfire_color color_good = {0, 1, 0, 1},
                            color_avg  = {1, 1, 0.3, 1},
                            color_bad  = {1, 0, 0, 1};

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    render_rounded_rectangle(cr, center_x - max_w / 2, 0, max_w, panel_h, 7,
            widget::background_color.r, widget::background_color.g,
            widget::background_color.b, widget::background_color.a);

    cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);

    std::string text;
    wayfire_color color;

    connection.mutex.lock();

    text = connection.name;
    if (connection.strength >= 50)
        color = color_good;
    else if (connection.strength > 30)
        color = color_avg;
    else
        color = color_bad;
    connection.updated = false;

    connection.mutex.unlock();


    cairo_text_extents_t te;
    cairo_text_extents(cr, text.c_str(), &te);

    double x = 0, y = font_size;
    cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);

    x = center_x + max_w / 2 - te.width;

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text.c_str());
}
