#include "widgets.hpp"

#include <iostream>
#include <chrono>
#include <ctime>
#include <unistd.h>
#include <sys/wait.h>
#include <cmath>
#include <wayland-client.h>
#include <linux/input-event-codes.h>
#include <gio/gio.h>
#include <mutex>
#include <thread>

#include <sys/dir.h>
#include <dirent.h>

#include <freetype2/ft2build.h>

wayfire_color widget::background_color;
int32_t widget::font_size;
std::string widget::font_face;

cairo_font_face_t *cairo_font_face = nullptr;

void load_default_font()
{
    if (cairo_font_face)
        return;

    FT_Library value;
    auto status = FT_Init_FreeType(&value);
    if (status != 0) {
        std::cerr << "failed to open freetype library" << std::endl;
        exit (EXIT_FAILURE);
    }

    FT_Face face;
    status = FT_New_Face (value, widget::font_face.c_str(), 0, &face);
    if (status != 0) {
        std::cerr << "Error opening font file " << widget::font_face << std::endl;
	    exit (EXIT_FAILURE);
    }

    cairo_font_face = cairo_ft_font_face_create_for_ft_face (face, 0);
}

void render_rounded_rectangle(cairo_t *cr, int x, int y, int width, int height,
        double radius, double r, double g, double b, double a)
{
    double degrees = M_PI / 180.0;

    cairo_new_sub_path (cr);
    cairo_arc(cr, x + width - radius, y + radius, radius, -90 * degrees, 0 * degrees);
    cairo_arc(cr, x + width - radius, y + height - radius, radius, 0 * degrees, 90 * degrees);
    cairo_arc(cr, x + radius, y + height - radius, radius, 90 * degrees, 180 * degrees);
    cairo_arc(cr, x + radius, y + radius, radius, 180 * degrees, 270 * degrees);
    cairo_close_path (cr);

    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_fill_preserve(cr);
}

/* -------------------- Clock widget ----------------- */
void clock_widget::create()
{
    load_default_font();

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); /* blank to white */
    cairo_set_font_size(cr, font_size);
    cairo_set_font_face(cr, cairo_font_face);

    width = font_size * 18;
}

const std::string months[] = {
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

std::string format(int x)
{
    if (x < 10) {
        return "0" + std::to_string(x);
    } else {
        return std::to_string(x);
    }
}

bool clock_widget::update()
{
    using std::chrono::system_clock;

    time_t now = system_clock::to_time_t(system_clock::now());
    auto time = std::localtime(&now);

    std::string time_string = std::to_string(time->tm_mday) + " " +
        months[time->tm_mon] + " " + format(time->tm_hour) +
        ":" + format(time->tm_min);

    if (time_string != this->current_text)
    {
        current_text = time_string;

        cairo_text_extents_t te;
        cairo_text_extents(cr, current_text.c_str(), &te);

        width = te.width;

        return true;
    } else
    {
        return false;
    }
}

void clock_widget::repaint()
{
    cairo_set_source_rgb(cr, 0.91, 0.918, 0.965);

    cairo_move_to(cr, x, font_size);
    cairo_show_text(cr, current_text.c_str());
}

/* --------------- Battery widget ---------------------- */
std::string battery_options::icon_path_prefix;
bool battery_options::invert_icons;
float battery_options::text_scale;

struct battery_info
{
    std::string icon;
    int percentage;

    /* currently unused, once popups have been implemented,
     * it will be used to show time to fill */
    bool charging;

    bool percentage_updated, icon_updated;
    std::mutex mutex;
};

static std::string
find_battery_icon_path(std::string icon_name)
{
    DIR *dp = opendir(battery_options::icon_path_prefix.c_str());
    if (dp == NULL)
    {
        std::cerr << "Failed to open icon directory " << battery_options::icon_path_prefix
                  << ". Not using status icons" << std::endl;
        return "none";
    }

    dirent *dir_entry;
    while((dir_entry = readdir(dp)) != NULL)
    {
        std::string file = dir_entry->d_name;

        if (file.find(icon_name) != std::string::npos)
        {
            file = battery_options::icon_path_prefix + "/" + file;
            closedir(dp);
            return file;
        }
    }

    closedir(dp);
    return "none";
}

static void
on_battery_changed (GDBusProxy          *proxy,
                            GVariant            *changed_properties,
                            const gchar* const  *invalidated_properties,
                            gpointer             user_data)
{
    battery_info *info = (battery_info*) user_data;
    if (g_variant_n_children(changed_properties) > 0)
    {
        GVariantIter *iter;
        g_variant_get(changed_properties, "a{sv}", &iter);

        const gchar *key;
        GVariant *value;
        while (g_variant_iter_loop(iter, "{&sv}", &key, &value))
        {
            if (std::string(key) == "Percentage")
            {
                info->mutex.lock();
                info->percentage = g_variant_get_double(value);
                info->percentage_updated = true;
                info->mutex.unlock();
            } else if (std::string(key) == "IconName")
            {
                gsize n;
                info->mutex.lock();
                info->icon = find_battery_icon_path(g_variant_get_string(value, &n));
                info->icon_updated = true;
                info->mutex.unlock();
            } else if (std::string(key) == "State")
            {
                uint32_t state = g_variant_get_uint32(value);

                info->mutex.lock();
                info->charging = (state == 1) || (state == 5);
                info->percentage_updated = true;
                info->mutex.unlock();
            }
        }

        g_variant_iter_free(iter);
    }
}

struct upower_backend
{
    GDBusConnection *dbus_connection;
    GDBusProxy *upower_proxy;
    GDBusProxy *battery_proxy;

    battery_info *info;

#define UPOWER_NAME "org.freedesktop.UPower"

    bool load(battery_info* info)
    {
        GError *error = NULL;
        dbus_connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

        if (!dbus_connection)
        {
            std::cerr << "Failed to connect to system bus: "
                      << error->message << std::endl;
            return false;
        }

        upower_proxy = g_dbus_proxy_new_sync(dbus_connection,
                                             G_DBUS_PROXY_FLAGS_NONE, NULL,
                                             UPOWER_NAME,
                                             "/org/freedesktop/UPower",
                                             "org.freedesktop.UPower",
                                             NULL, &error);

        if (!upower_proxy)
        {
            std::cerr << "Failed to connect to upower: " << error << std::endl;
            return false;
        }

        GVariant* gv = g_dbus_proxy_call_sync(upower_proxy,
                                              "EnumerateDevices",
                                              NULL, G_DBUS_CALL_FLAGS_NONE,
                                              -1, NULL, &error);

        if (!gv)
        {
            std::cerr << "Failed to enumerate devices: " << error->message << std::endl;
            return false;
        }

        gv = g_variant_get_child_value(gv, 0);

        GVariantIter iter;
        g_variant_iter_init(&iter, gv);

        gchar *buf;
        battery_proxy = nullptr;

        while(g_variant_iter_loop(&iter, "o", &buf))
        {
            battery_proxy =
                g_dbus_proxy_new_sync(dbus_connection,
                                      G_DBUS_PROXY_FLAGS_NONE, NULL,
                                      UPOWER_NAME,
                                      buf,
                                      "org.freedesktop.UPower.Device",
                                      NULL, &error);

            if (!battery_proxy)
            {
                std::cerr << "warning: failed to open device " << buf
                          << ": " << error->message << std::endl;
                continue;
            }

            GVariant *gv1 = g_dbus_proxy_get_cached_property(battery_proxy, "Type");
            uint32_t type = g_variant_get_uint32(gv1);

            if (type == 2)
            {
                g_variant_unref(gv1);
                break;
            }

            g_variant_unref(gv1);
            g_object_unref(battery_proxy);
            battery_proxy = nullptr;
        }

        if (!battery_proxy)
        {
            g_variant_unref(gv);
            return false;
        }

        uint32_t percentage, state;
        std::string icon;

        gv = g_dbus_proxy_get_cached_property(battery_proxy, "Percentage");
        percentage = g_variant_get_double(gv);
        g_variant_unref(gv);

        gv = g_dbus_proxy_get_cached_property(battery_proxy, "State");
        state = g_variant_get_uint32(gv);
        g_variant_unref(gv);

        gv = g_dbus_proxy_get_cached_property(battery_proxy, "IconName");
        gsize n;
        icon = find_battery_icon_path(g_variant_get_string(gv, &n));
        g_variant_unref(gv);

        this->info = info;

        info->mutex.lock();
        info->charging = (state == 1) || (state == 5);
        info->icon = icon;
        info->percentage = percentage;

        info->percentage_updated = info->icon_updated = true;
        info->mutex.unlock();

        return true;
    }

    void start_loop()
    {
        g_signal_connect(battery_proxy,
                "g-properties-changed", G_CALLBACK(on_battery_changed), info);

        auto loop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(loop);

        g_object_unref(upower_proxy);
        g_main_loop_unref(loop);
    }
};

void battery_widget::create()
{
    backend = new upower_backend();
    info = new battery_info();

    if (!backend || !info || !backend->load(info))
    {
        delete backend;
        delete info;

        active = false;
        return;
    }

    backend_thread = std::thread([=] () { backend->start_loop(); });

    load_default_font();

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); /* blank to white */
    cairo_set_font_size(cr, font_size * battery_options::text_scale);
    cairo_set_font_face(cr, cairo_font_face);

    /* calculate luminance of the background color */
    float y = background_color.r * 0.2126 + background_color.g * 0.7152 + background_color.b * 0.0722;
    y *= background_color.a;

    width = 1. * font_size + font_size * battery_options::text_scale * 4;
    active = true;
}

bool battery_widget::update()
{
    if (!active)
        return false;

    bool result;
    std::string battery_string;

    info->mutex.lock();
    result = info->icon_updated || info->percentage_updated;
    battery_string = std::to_string(info->percentage) + "%";
    info->mutex.unlock();

    cairo_text_extents_t te;
    cairo_text_extents(cr, battery_string.c_str(), &te);
    width = font_size + 0.2 * font_size + te.width;


    return result;
}

cairo_surface_t* prepare_icon(std::string path)
{
    auto img = cairo_image_surface_create_from_png(path.c_str());
    int w = cairo_image_surface_get_width(img);
    int h = cairo_image_surface_get_height(img);

    if (battery_options::invert_icons)
    {
        auto dest = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        auto cr = cairo_create(dest);

        cairo_set_source_rgba(cr, 1, 1, 1, 1);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_mask_surface(cr, img, 0, 0);

        cairo_new_path(cr);

        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        cairo_set_source_surface(cr, img, 0, 0);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_fill(cr);

        cairo_surface_flush(dest);

        cairo_surface_destroy(img);
        cairo_destroy(cr);

        return dest;
    } else
    {
        return img;
    }
}

void battery_widget::repaint()
{
    if (!active)
        return;

    /* this is main thread, even if the dbus one has to wait, we
     * don't care, as it isn't crucial for performance, so lock for more time in order
     * to avoid constant lock()/unlock() calls */
    info->mutex.lock();

    if (info->icon_updated)
    {
        if (icon_surface)
            cairo_surface_destroy(icon_surface);
        icon_surface = nullptr;

        if (info->icon != "none")
            icon_surface = prepare_icon(info->icon);
    }

    std::string battery_string = std::to_string(info->percentage) + "%";

    info->icon_updated = info->percentage_updated = false;
    info->mutex.unlock();

    cairo_identity_matrix(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);
    int icon_size = font_size;
    cairo_new_path(cr);

    cairo_text_extents_t te;
    cairo_text_extents(cr, battery_string.c_str(), &te);

    double sx = icon_size * 1.1, sy = (panel_h + te.height) / 2.0;
    cairo_set_source_rgb(cr, 0.91, 0.918, 0.965);

    sx = x + font_size + 0.2 * font_size;

    cairo_move_to(cr, sx, sy);
    cairo_show_text(cr, battery_string.c_str());

    if (icon_surface)
    {
        sy = (panel_h - icon_size) / 2;
        sx = x;

        double img_w = cairo_image_surface_get_width (icon_surface);
        double img_h = cairo_image_surface_get_height(icon_surface);

        cairo_identity_matrix(cr);
        cairo_new_path(cr);

        float scale_w = 1.0 * icon_size / img_w;
        float scale_h = 1.0 * icon_size / img_h;
        cairo_scale(cr, scale_w, scale_h);
        cairo_rectangle(cr, sx / scale_w, sy / scale_h, icon_size / scale_w, icon_size / scale_h);
        cairo_set_source_surface(cr, icon_surface, sx / scale_w, sy / scale_h);
        cairo_fill(cr);
    }

}

/* --------------- Launchers widget ---------------------- */
struct launcher
{
    cairo_surface_t *img;
    std::string command;

    float scale;

    int x, y, size;
    bool active = false;
};

void execute(std::string cmd)
{
    pid_t pid = fork();

    /* The following is a "hack" for disowning the child processes,
     * otherwise they will simply stay as zombie processes */
    if (!pid)
    {
        if (!fork())
        {
            exit(execl("/bin/sh", "/bin/sh", "-c", cmd.c_str(), NULL));
        } else
        {
            exit(0);
        }
    } else
    {
        int status;
        waitpid(pid, &status, 0);
    }
}

/* format in config file:
 * launcher1_icon
 * launcher1_cmd
 */

constexpr float default_launcher_scale = 0.9;
constexpr float hover_launcher_scale = 1.1;

void launchers_widget::init_launchers(wayfire_config *config)
{
    auto section = config->get_section("shell_panel");

    for (int i = 0; i < 10; i++)
    {
        std::string icon = section->get_string("launcher" + std::to_string(i) + "_icon", "");
        std::string cmd =  section->get_string("launcher" + std::to_string(i) + "_cmd", "");

        if (icon == "" || cmd == "")
            continue;

        launcher *l = new launcher;
        l->scale = default_launcher_scale;
        l->img = cairo_image_surface_create_from_png(icon.c_str());
        l->command = cmd;

        launchers.push_back(l);
    }
}

#define pointer_in_launcher(l,x,y) (l->x <= x && l->y <= y && \
                l->x + l->size > x && l->y + l->size > y)

int icon_offset, base_icon_size;

void launchers_widget::create()
{
    icon_offset = font_size * 0.5;
    base_icon_size = font_size * 1.1;

    width = launchers.size() * (base_icon_size + icon_offset) - icon_offset;

    pointer_motion = [=] (int x, int y)
    {
        for (auto l : launchers)
        {
            bool was_active = l->active;
            if (pointer_in_launcher(l, x, y))
            {
                l->scale = hover_launcher_scale;
                l->active = true;
            } else {
                l->scale = default_launcher_scale;
                l->active = false;
            }

            if (was_active != l->active)
                need_repaint = true;
        }
    };

    pointer_button = [=] (uint32_t button, uint32_t state, int x, int y)
    {
        if (button != BTN_LEFT || state != WL_POINTER_BUTTON_STATE_PRESSED)
            return;

        for (auto l : launchers)
        {
            if (pointer_in_launcher(l, x, y))
                execute(l->command);
        }
    };

}

void launchers_widget::repaint()
{
    cairo_identity_matrix(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_ATOP);

    for (size_t i = 0; i < launchers.size(); i++)
    {
        int icon_size = base_icon_size * launchers[i]->scale;

        int sy = (panel_h - icon_size) / 2;
        int sx = x + i * (base_icon_size + icon_offset) - (icon_size - base_icon_size) / 2;

        launchers[i]->x = sx;
        launchers[i]->y = sy;
        launchers[i]->size = icon_size;

        double img_w = cairo_image_surface_get_width(launchers[i]->img);
        double img_h = cairo_image_surface_get_height(launchers[i]->img);

        cairo_identity_matrix(cr);
        cairo_new_path(cr);

        float scale_w = 1.0 * icon_size / img_w;
        float scale_h = 1.0 * icon_size / img_h;
        cairo_scale(cr, scale_w, scale_h);

        cairo_rectangle(cr, sx / scale_w, sy / scale_h, icon_size / scale_w, icon_size / scale_h);
        cairo_set_source_rgba(cr, 1, 1, 1, 1);
        cairo_set_source_surface(cr, launchers[i]->img, sx / scale_w, sy / scale_h);
        cairo_fill(cr);
    }

    need_repaint = false;
}

bool launchers_widget::update()
{
    return need_repaint;
}
