#include <wayland-client.h>
#include <cstring>
#include "config.hpp"
#include "vkeyboard.hpp"

#define ABC_TOGGLE 0x12345678
#define NUM_TOGGLE 0x87654321
#define SEND_SHIFT 0x10000000
#define MOVE       0x02000000
#define EXIT       0x03000000
#define USE_SHIFT  0x00100000

void match_output_size(void *data, wayfire_virtual_keyboard *vkbd,
                       uint32_t width, uint32_t height)
{
    auto vk = (vkeyboard*) data;
    vk->resize(width, height);
}

void show_virtual_keyboard(void *data, wayfire_virtual_keyboard *vkbd)
{
    auto vk = (vkeyboard*) data;
    if (vk) vk->show();
}

const static wayfire_virtual_keyboard_listener vkeyboard_listener =
{
    .match_output_size = match_output_size,
    .show_virtual_keyboard = show_virtual_keyboard
};

vkeyboard::vkeyboard(wayfire_config *config)
{
    if (!display.vkbd)
        return;

    this->config = config;

    init_layouts();
    wayfire_virtual_keyboard_add_listener(display.vkbd, &vkeyboard_listener, this);
}

void vkeyboard::init_layouts()
{
    default_layout = {
        {
            {KEY_Q, "q", 1},
            {KEY_W, "w", 1},
            {KEY_E, "e", 1},
            {KEY_R, "r", 1},
            {KEY_T, "t", 1},
            {KEY_Y, "y", 1},
            {KEY_U, "u", 1},
            {KEY_I, "i", 1},
            {KEY_O, "o", 1},
            {KEY_P, "p", 1},
            {KEY_BACKSPACE, "<--", 2}
        },
        {
            {0, " ", 0.5},
            {KEY_A, "a", 1},
            {KEY_S, "s", 1},
            {KEY_D, "d", 1},
            {KEY_F, "f", 1},
            {KEY_G, "g", 1},
            {KEY_H, "h", 1},
            {KEY_J, "j", 1},
            {KEY_K, "k", 1},
            {KEY_L, "l", 1},
            {KEY_ENTER, "enter", 2}
        },
        {
            {ABC_TOGGLE, "ABC", 1},
            {KEY_Z, "z", 1},
            {KEY_X, "x", 1},
            {KEY_C, "c", 1},
            {KEY_V, "v", 1},
            {KEY_B, "b", 1},
            {KEY_N, "n", 1},
            {KEY_M, "m", 1},
            {KEY_COMMA, ",", 1},
            {KEY_DOT, ".", 1}
        },
        {
            {NUM_TOGGLE, "123?", 1.5},
            {KEY_SPACE, "_", 9.5},
            {KEY_LEFT, "<", 0.5},
            {KEY_RIGHT, ">", 0.5},
            {KEY_UP, "/\\", 0.5},
            {KEY_DOWN, "\\/", 0.5}
        }
    };

    shift_layout = default_layout;
    for (auto& row : shift_layout)
    {
        for (auto& key : row)
        {
            for (auto& c : key.text)
            {
                if (std::isalpha(c))
                    c = std::toupper(c);
            }

            if (key.code < USE_SHIFT)
                key.code |= USE_SHIFT;

            if (key.text == "ABC")
                key.text = "abc";
        }
    }

    numeric_layout = {
        {
            {KEY_1, "1", 1},
            {KEY_2, "2", 1},
            {KEY_3, "3", 1},
            {KEY_4, "4", 1},
            {KEY_5, "5", 1},
            {KEY_6, "6", 1},
            {KEY_7, "7", 1},
            {KEY_8, "8", 1},
            {KEY_9, "9", 1},
            {KEY_0, "0", 1},
            {KEY_MINUS, "-", 1},
            {KEY_EQUAL, "=", 1},
            {KEY_BACKSPACE, "<--", 2}
        },
        {
            {KEY_1 | USE_SHIFT, "!", 1},
            {KEY_2 | USE_SHIFT, "@", 1},
            {KEY_3 | USE_SHIFT, "#", 1},
            {KEY_4 | USE_SHIFT, "$", 1},
            {KEY_5 | USE_SHIFT, "%", 1},
            {KEY_6 | USE_SHIFT, "^", 1},
            {KEY_7 | USE_SHIFT, "&", 1},
            {KEY_8 | USE_SHIFT, "*", 1},
            {KEY_9 | USE_SHIFT, "(", 1},
            {KEY_0 | USE_SHIFT, ")", 1},
            {KEY_SEMICOLON, ";", 1},
            {KEY_SEMICOLON | USE_SHIFT, ":", 1},
            {KEY_ENTER, "ent", 1}
        },
        {
            {KEY_LEFTBRACE, "[", 1},
            {KEY_RIGHTBRACE, "]", 1},
            {KEY_LEFTBRACE | USE_SHIFT, "{", 1},
            {KEY_RIGHTBRACE | USE_SHIFT, "}", 1},
            {KEY_COMMA | USE_SHIFT, "<", 1},
            {KEY_DOT | USE_SHIFT, ">", 1},
            {KEY_EQUAL | USE_SHIFT, "+", 1},
            {KEY_SLASH, "/", 1},
            {KEY_SLASH | USE_SHIFT, "?", 1},
            {KEY_APOSTROPHE, "\'", 1},
            {KEY_APOSTROPHE | USE_SHIFT, "\"", 1},
            {KEY_GRAVE, "`", 1},
            {KEY_GRAVE | USE_SHIFT, "~", 1}
        },
        {
            {ABC_TOGGLE, "abc", 1},
            {KEY_SPACE, "_", 10},
            {KEY_BACKSLASH, "\\", 1},
            {KEY_BACKSLASH | USE_SHIFT, "|", 1}
        }
    };


}

void vkeyboard::set_layout(layout& l)
{
    if (current_layout && current_layout->front().front().code == MOVE)
        current_layout->erase(current_layout->begin());

    current_layout = &l;

    double row_height = 0.9 * height / l.size();

    double x = 0, y = height - row_height * l.size();
    for (auto& row : *current_layout)
    {
        double total_key_width = 0;
        for (const auto& key : row)
            total_key_width += key.target_w;

        for (auto& key : row)
        {
            key.x = x;
            key.y = y;
            key.w = key.target_w / total_key_width * width;
            key.h = row_height;

            x += key.w;
        }

        y += row_height;
        x  = 0;
    }

    key move = {MOVE, "<->", 19}, exit = {EXIT, "X", 1};
    move.x = 0;
    move.y = 0;
    move.w = width * 0.95;
    move.h = 0.1 * height;

    exit.x = move.w;
    exit.y = 0;
    exit.w = width * 0.05;
    exit.h = 0.1 * height;

    current_layout->insert(current_layout->begin(), {move, exit});
}

void vkeyboard::input_motion(int x, int y)
{
    uint32_t code1 = 0, code2 = 0;
    for (const auto& row : *current_layout)
    {
        for (const auto& key : row)
        {
            if (key.x <= cx && key.y <= cy && key.x + key.w >= cx && key.y + key.h >= cy && key.code != 0)
                code1 = key.code;
            if (key.x <= x && key.y <= y && key.x + key.w >= x && key.y + key.h >= y && key.code != 0)
                code2 = key.code;
        }
    }

    if (code1 != code2)
    {
        handle_action_end(code1, false);
        handle_action_start(code2);
    }

    cx = x;
    cy = y;

    add_callback();
}

void vkeyboard::handle_action_start(uint32_t code)
{
    if (code == ABC_TOGGLE)
    {
        if (current_layout == &default_layout)
        {
            set_layout(shift_layout);
        } else
        {
            set_layout(default_layout);
        }

        return;
    }

    if (code == NUM_TOGGLE)
    {
        set_layout(numeric_layout);
        return;
    }

    if (code == EXIT)
    {
        return;
    }

    if (code == MOVE)
    {
        wayfire_virtual_keyboard_start_interactive_move(display.vkbd, window->surface);
        return;
    }


    if (code & USE_SHIFT)
        wayfire_virtual_keyboard_send_key_pressed(display.vkbd, KEY_LEFTSHIFT);

    wayfire_virtual_keyboard_send_key_pressed(display.vkbd, code & (~USE_SHIFT));
}

void vkeyboard::handle_action_end(uint32_t code, bool finger_up)
{
    if (code == EXIT && finger_up)
    {
        wl_display_disconnect(display.wl_disp);
        std::exit(0);
    }

    if (code > 0x01000000)
    {
        return;
    }

    if (code & USE_SHIFT)
        wayfire_virtual_keyboard_send_key_released(display.vkbd, KEY_LEFTSHIFT);

    wayfire_virtual_keyboard_send_key_released(display.vkbd, code & (~USE_SHIFT));
}

void vkeyboard::input_released()
{
    for (const auto& row : *current_layout)
    {
        for (const auto& key : row)
        {
            if (key.x <= cx && key.y <= cy && key.x + key.w >= cx && key.y + key.h >= cy && key.code != 0)
                handle_action_end(key.code);
        }
    }

    cx = -1, cy = -1;
    add_callback();
}

void redraw_callback(void *data, wl_callback*, uint32_t)
{
    auto vk = (vkeyboard*) data;
    vk->render_frame();
}

const static struct wl_callback_listener callback_listener =
{
    redraw_callback
};

void vkeyboard::add_callback()
{
    if (repaint_callback)
        return;

    repaint_callback = wl_surface_frame(window->surface);
    wl_callback_add_listener(repaint_callback, &callback_listener, this);
    wl_surface_commit(window->surface);
}

void vkeyboard::render_frame()
{
    double font_size = height * 0.15;
    cairo_set_font_size(cr, font_size);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.05, 0.05, 0.05, 0.8);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    for (const auto& row : *current_layout)
    {
        for (const auto& key : row)
        {
            if (key.x <= cx && key.y <= cy && key.x + key.w >= cx && key.y + key.h >= cy && key.code != 0)
            {
                cairo_set_source_rgba(cr, 0.4, 0.4, 0.4, 1);
                cairo_rectangle(cr, key.x, key.y, key.w, key.h);
                cairo_fill(cr);
            }

            cairo_set_source_rgba(cr, 0.3, 0.3, 0.3, 1);
            cairo_set_line_width(cr, 5);
            cairo_rectangle(cr, key.x, key.y, key.w, key.h);
            cairo_stroke(cr);

            cairo_text_extents_t te;
            cairo_text_extents(cr, key.text.c_str(), &te);

            double sx = key.x + key.w / 2 - te.width / 2;
            double sy = key.y + key.h / 2 + te.height / 2;

            cairo_move_to(cr, sx, sy);
            cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 1);
            cairo_show_text(cr, key.text.c_str());
        }
    }

    damage_commit_window(window);

    if (repaint_callback)
    {
        wl_callback_destroy(repaint_callback);
        repaint_callback = nullptr;
    }
}

void vkeyboard::show()
{
}

void vkeyboard::resize(uint32_t w, uint32_t h)
{
    auto section = config->get_section("vkeyboard");

    auto wcoeff = section->get_double("widthp", 0.8);
    auto hcoeff = section->get_double("heightp", 0.33);

    width = w * wcoeff; height = h * hcoeff;

    if (window)
        delete_window(window);

    window = create_window(width, height);
    cr = cairo_create(window->cairo_surface);

    wayfire_virtual_keyboard_set_virtual_keyboard(display.vkbd, window->surface);
    wayfire_virtual_keyboard_configure_keyboard(display.vkbd, window->surface, (w - width) / 2.0, h - height);

    window->touch_down   = [=] (uint32_t, int32_t id, int x, int y) { if (id == 0) input_motion(x, y); };
    window->touch_motion = [=] (int32_t id, int x, int y) { if (id == 0) input_motion(x, y); };
    window->touch_up     = [=] (int32_t id) { if (id == 0) input_released(); };

    set_layout(default_layout);
    render_frame();
}

int main()
{
    std::string home_dir = secure_getenv("HOME");
    auto config = new wayfire_config(home_dir + "/.config/wayfire.ini");
    if (!setup_wayland_connection())
        return -1;

    auto vk = new vkeyboard(config);
    while(true) {
        if (wl_display_dispatch(display.wl_disp) < 0)
            break;
    }

    delete vk;
    finish_wayland_connection();
}
