#include <wayland-client.h>
#include <cstring>
#include <config.h>
#include <cstdio>
#include <cassert>
#include <fstream>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "window.hpp"

#include <asoundlib.h>

struct audio_data
{
    snd_mixer_selem_id_t *sid;
    snd_mixer_t *handle;
    snd_mixer_elem_t *elem;
    long min, max;
} audata;

void setup_audio()
{
    const char *card = "default";
    const char *selem_name = "Master";

    snd_mixer_open(&audata.handle, 0);
    snd_mixer_attach(audata.handle, card);
    snd_mixer_selem_register(audata.handle, NULL, NULL);
    snd_mixer_load(audata.handle);

    snd_mixer_selem_id_alloca(&audata.sid);
    snd_mixer_selem_id_set_index(audata.sid, 0);
    snd_mixer_selem_id_set_name(audata.sid, selem_name);

    audata.elem = snd_mixer_find_selem(audata.handle, audata.sid);
    snd_mixer_selem_get_playback_volume_range(audata.elem, &audata.min, &audata.max);
}

void cleanup_audio()
{
    snd_mixer_close(audata.handle);
}

int tar_level = -1;

int get_audio_level()
{
    setup_audio();

    float dta = -1;

    if (tar_level == -1)
    {

        long volume;
        snd_mixer_selem_get_playback_volume(audata.elem, (snd_mixer_selem_channel_id_t)0, &volume);
        volume -= audata.min;

        dta =  1. * volume / (audata.max - audata.min) * 100.f;
    } else
    {
        dta = tar_level;
        long volume = audata.min + (audata.max - audata.min) * tar_level / 100;
        snd_mixer_selem_set_playback_volume_all(audata.elem, volume);

        tar_level = -1;
    }


    cleanup_audio();
    return dta + 0.5;
}

timeval get_current_time()
{
    timeval time;
    gettimeofday(&time, 0);
    return time;
}

const char *lock_file = "/tmp/.wayfire-sound-lock";
bool check_has_lock_file()
{
    struct stat buffer;
    if (stat(lock_file, &buffer))
        return false;
    return true;
}

void create_lock_file()
{
    std::ofstream out(lock_file);
    out << 1 << std::endl;
}

void cleanup()
{
    finish_wayland_connection();
    remove(lock_file);
}


wayfire_window *window = nullptr;
cairo_t *cr = nullptr;

int input_count = 0;
const int input_pointer_button = (1 << 31);
const int input_input_focus = (1 << 30);
timeval start_time;

struct
{
    int x = 100, y = 100, w = 450, h = 70;
} geometry, bar_geometry;

int alpha = 0;
int step = 1;
int cur_level = -1;

long inactive_time_mms = 1e6;

bool fade_in = true;

void render_frame();
void redraw_handler(void *data, wl_callback*, uint32_t)
{ render_frame(); }

static const struct wl_callback_listener frame_listener = { redraw_handler };

static wl_callback *repaint_callback = nullptr;
void add_callback()
{
    if (repaint_callback)
        wl_callback_destroy(repaint_callback);

    repaint_callback = wl_surface_frame(window->surface);
    wl_callback_add_listener(repaint_callback, &frame_listener, window);
}

void render_frame()
{
    if (fade_in)
    {
        alpha += 1;
        if (alpha >= 8)
        {
            alpha = 8;
            fade_in = 0;
        }
    }

    auto ctime = get_current_time();
    auto level = get_audio_level();

    if (cur_level != level || input_count)
    {
        start_time = get_current_time();

        fade_in = true;
    }

    long long elapsed = (ctime.tv_sec - start_time.tv_sec) * 1e6 + (ctime.tv_usec - start_time.tv_usec);
    if (elapsed > inactive_time_mms)
    {
        fade_in = false;
        alpha -= 1;
        if (alpha == 0)
        {
            cleanup();
            std::exit(0);
        }
    } else if (!fade_in)
    {
        usleep(1e6/15);
    }

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    render_rounded_rectangle(cr, 0, 0, geometry.w, geometry.h,
                             0.02 * geometry.w, 0.022, 0.028, 0.032, alpha / 10.f);

    std::string label = " " + std::to_string(level) + "%  ";
    if (level < 10) label = " " + label;
    if (level < 100) label = " " + label;

    cairo_set_source_rgba(cr, 1, 1, 1, 1.25 * alpha / 10.f);
    float font_size = geometry.h * 0.4;
    cairo_set_font_size(cr, font_size);

    cairo_text_extents_t te;
    cairo_text_extents(cr, " 100% ", &te);

    float y = (geometry.h + te.height) / 2.0;

    cairo_move_to(cr, 0, y);
    cairo_show_text(cr, label.c_str());

    cairo_set_source_rgba(cr, 0.3, 0.7, 1.0, 1.25 * alpha / 10.f);
    float x = te.x_advance;

    cairo_new_path(cr);
    cairo_move_to(cr, 0, 0);

    float bar_height = font_size * 0.8;
    y = (geometry.h - bar_height) / 2.0;

    if (std::abs(cur_level - level) < 3)
        cur_level = level;
    else
    {
        cur_level = (2 * cur_level + 3 * level) / 5;
    }

    float bar_width = geometry.w * 0.96 - te.x_advance;
    float vol_width = bar_width * cur_level / 100.f;

    bar_geometry.x = x;
    bar_geometry.y = y;
    bar_geometry.w = bar_width;
    bar_geometry.h = bar_height;

    cairo_rectangle(cr, x, y, vol_width, bar_height);
    cairo_fill(cr);
    cairo_new_path(cr);
    cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 1.25 * alpha / 10.f);
    cairo_rectangle(cr, x + vol_width, y, bar_width - vol_width, bar_height);
    cairo_fill(cr);
    cairo_new_path(cr);

    add_callback();
    damage_commit_window(window);
}

bool should_handle_input(int x, int y)
{
    return bar_geometry.x <= x && bar_geometry.y <= y &&
        bar_geometry.x + bar_geometry.w >= x && bar_geometry.y + bar_geometry.h >= y;
}

void handle_input(int x, int y)
{
    double d = x - bar_geometry.x;
    d /= bar_geometry.w;

    tar_level = 100 * d;

    tar_level = std::min(tar_level, 100);
    tar_level = std::max(tar_level, 0);
}

void setup_window()
{
    window->touch_down = [] (uint32_t, int id, int x, int y)
    {
        input_count |= input_input_focus;
        if (should_handle_input(x, y))
        {
            input_count |= (1 << id);
            handle_input(x, y);
        }
    };

    window->touch_up = [] (int id)
    {
        input_count &= ~(1 << id);
        input_count &= ~input_input_focus;
    };

    window->touch_motion = [] (int id, int x, int y)
    {
        if (should_handle_input(x, y))
            handle_input(x, y);
    };

    window->pointer_enter = [] (wl_pointer*, uint32_t time, int, int)
    {
        show_default_cursor(time);
        input_count |= input_input_focus;
    };

    window->pointer_leave = [] ()
    {
        input_count &= ~input_input_focus;
    };

    window->pointer_button = [] (uint32_t button, uint32_t type, int x, int y)
    {
        if (type == WL_POINTER_BUTTON_STATE_PRESSED)
        {
            if (should_handle_input(x, y))
            {
                input_count |= input_pointer_button;
                handle_input(x, y);
            }
        } else
        {
            input_count &= ~input_pointer_button;
        }
    };

    window->pointer_move = [] (int x, int y)
    {
        if (input_count & ~input_input_focus)
            handle_input(x, y);
    };
    wayfire_shell_configure_panel(display.wfshell, -1, window->surface, geometry.x, geometry.y);

    cr = cairo_create(window->cairo_surface);
}


int main(int argc, char **argv)
{
    if (check_has_lock_file())
        return 0;

    create_lock_file();

    if (!setup_wayland_connection())
        return -1;

    if (argc >= 5)
    {
        geometry.x = std::atoi(argv[1]);
        geometry.y = std::atoi(argv[2]);
        geometry.w = std::atoi(argv[3]);
        geometry.h = std::atoi(argv[4]);
    }

    if (argc >= 6)
        inactive_time_mms = std::atoi(argv[5]) * 1000;

    setup_audio();
    window = create_window(geometry.w, geometry.h,
                           [] () {
                               setup_window();
                               render_frame();
                           });
    wayfire_shell_add_panel(display.wfshell, -1, window->surface);

    while(true) {
        if (wl_display_dispatch(display.wl_disp) < 0)
            break;
    }

    cleanup();
}
