#include <memory>
#include <getopt.h>

#include "config.hpp"
#include "window.hpp"

#ifdef BUILD_WITH_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkcairo.h>
#endif


bool g_type_init_ran = false;

static cairo_surface_t *create_dummy_surface(int w, int h)
{
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(surf);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_fill(cr);
    cairo_destroy(cr);

    return surf;
}

static cairo_surface_t *create_cairo_surface_from_file(std::string name, int w, int h)
{

#ifdef BUILD_WITH_PIXBUF

#if !GLIB_CHECK_VERSION(2,35,0)
    if (!g_type_init_ran)
    {
        g_type_init();
        g_type_init_ran = true;
    }
#endif

    auto pbuf = gdk_pixbuf_new_from_file(name.c_str(), NULL);
    if (!pbuf)
    {
        std::cerr << "Failed to create a pbuf. Possibly wrong background path?" << std::endl;
        return create_dummy_surface(w, h);
    }

    int w_ = gdk_pixbuf_get_width(pbuf);
    int h_ = gdk_pixbuf_get_height(pbuf);

    auto surface = cairo_image_surface_create(
        gdk_pixbuf_get_has_alpha(pbuf) ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24,
        w_, h_);

    auto cr = cairo_create(surface);
    gdk_cairo_set_source_pixbuf(cr, pbuf, 0, 0);
    cairo_paint(cr);

    cairo_destroy(cr);
    g_object_unref(pbuf);

    return surface;
#else
    auto surface = cairo_try_load_png(name.c_str());
    return surface ?: create_dummy_surface(w, h);
#endif
}

class wayfire_background
{
    uint32_t output;
    std::string image;

    cairo_surface_t *img_surface = NULL;
    cairo_t *cr;

    wayfire_window *window;

    public:
    wayfire_background(std::string image, uint32_t output, uint32_t w, uint32_t h)
    {
        this->image = image;

        w *= display.scale;
        h *= display.scale;
        this->output = output;

        window = create_window(w, h, [=] () { create_background(w, h); });
    }

    void create_background(uint32_t w, uint32_t h)
    {
        window->set_scale(display.scale);
        wayfire_shell_add_background(display.wfshell, output, window->surface, 0, 0);

        using namespace std::placeholders;
        window->pointer_enter = std::bind(std::mem_fn(&wayfire_background::on_enter),
                                          this, _1, _2, _3, _4);

        cr = cairo_create(window->cairo_surface);

        if (!img_surface)
            img_surface = create_cairo_surface_from_file(image, w, h);

        set_active_window(window);

        double img_w = cairo_image_surface_get_width(img_surface);
        double img_h = cairo_image_surface_get_height(img_surface);

        cairo_rectangle(cr, 0, 0, w, h);
        cairo_scale(cr, w / img_w, h / img_h);
        cairo_set_source_surface(cr, img_surface, 0, 0);
        cairo_fill(cr);

        damage_commit_window(window);
    }

    void resize(uint32_t w, uint32_t h)
    {
        cairo_destroy(cr);
        delete_window(window);

        w *= display.scale;
        h *= display.scale;

        window = create_window(w, h, [=] () { create_background(w, h); });
    }

    void on_enter(wl_pointer *ptr, uint32_t serial, int x, int y)
    {
        show_default_cursor(serial);
    }

    ~wayfire_background()
    {
        cairo_destroy(cr);
        cairo_surface_destroy(img_surface);
        delete_window(window);
    }
};

wayfire_config *config;
std::map<uint32_t, std::unique_ptr<wayfire_background>> outputs;

std::string bg_path;

void output_created_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{

    outputs[output] = std::unique_ptr<wayfire_background> (new wayfire_background(bg_path, output, width, height));
    wayfire_shell_output_fade_in_start(wayfire_shell, output);
}

void output_resized_cb(void *data, wayfire_shell *wayfire_shell,
        uint32_t output, uint32_t width, uint32_t height)
{
    auto it = outputs.find(output);
    if (it == outputs.end() || !it->second)
        return;

    it->second->resize(width, height);
}

void output_destroyed_cb(void *data, wayfire_shell *wayfire_shell, uint32_t output)
{
    auto it = outputs.find(output);

    if (it == outputs.end())
        return;

    if (it->second)
        delete it->second.release();
    outputs.erase(it);
}

void output_autohide_panels_cb(void *data, wayfire_shell *wayfire_shell, uint32_t output, uint32_t autohide) { }
void output_gamma_size_cb(void *data, wayfire_shell *shell, uint32_t output, uint32_t size) { }

static const struct wayfire_shell_listener bg_shell_listener = {
    .output_created = output_created_cb,
    .output_resized = output_resized_cb,
    .output_destroyed = output_destroyed_cb,
    .output_autohide_panels = output_autohide_panels_cb,
    .gamma_size = output_gamma_size_cb,
};

/* TODO: share option parsing between panel and background */
int main(int argc, char *argv[])
{
    std::string home_dir = secure_getenv("HOME");
    std::string config_file = home_dir + "/.config/wayfire.ini";

    struct option opts[] = {
        { "config",   required_argument, NULL, 'c' },
        { 0,          0,                 NULL,  0  }
    };

    int c, i;
    while((c = getopt_long(argc, argv, "c:l:", opts, &i)) != -1)
    {
        switch(c)
        {
            case 'c':
                config_file = optarg;
                break;
            default:
                std::cerr << "failed to parse option " << optarg << std::endl;
        }
    }

    config = new wayfire_config(config_file);
    auto section = config->get_section("shell");

    bg_path = section->get_string("background", "none");

    if (!setup_wayland_connection())
        return -1;

    wayfire_shell_add_listener(display.wfshell, &bg_shell_listener, 0);

    while(true)
    {
        if (wl_display_dispatch(display.wl_disp) < 0)
            break;
    }

    for (auto& x : outputs)
    {
        if (x.second)
            delete x.second.release();
    }

    finish_wayland_connection();
}
