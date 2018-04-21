#include <string>
#include <linux/input-event-codes.h>

#include "tree-definition.hpp"
#include "signal-definitions.hpp"
#include <output.hpp>
#include <core.hpp>
#include <opengl.hpp>
#include <view.hpp>
#include <render-manager.hpp>

#include "../../shared/config.hpp"
#include "../single_plugins/view-change-viewport-signal.hpp"

inline wf_tree_node* tile_node_from_view(const wayfire_view& view)
{
    auto it = view->custom_data.find(tile_data);
    if (it == view->custom_data.end())
        return nullptr;

    auto data = static_cast<wf_tile_view_data*> (it->second);
    assert(data);

    return data->node;
}

namespace wf_tiling
{
    wf_tree_node *root;

    struct
    {
        wf_split_type default_split_type;
    } options;

    void unmaximize()
    {

        if (root->view)
        {
            auto node = tile_node_from_view(root->view);
            node->recalculate_children_boxes();

            if (root->view->fullscreen)
            {
                root->view->set_fullscreen(false);

                view_fullscreen_signal data;
                data.view = root->view;
                data.state = false;
                root->view->output->emit_signal("view-fullscreen-request", &data);
            }
        }

        if (root->view && root->children.size())
            root->view = nullptr;
    }

    void maximize_view(wayfire_view view, bool make_fs = false)
    {
        if (root->view == view && !make_fs)
            return;

        unmaximize();

        root->view = view;
        auto box = root->box;

        if (make_fs)
        {
            box = view->output->get_full_geometry();
            GetTuple(vx, vy, view->output->workspace->get_current_workspace());
            GetTuple(sw, sh, view->output->get_screen_size());

            box.x += sw * vx;
            box.y += sh * vy;
        }

        view_fit_to_box(view, box);
        view->set_fullscreen(make_fs);
    }

    void add_view(wayfire_view view, wf_tree_node* container, wf_split_type type)
    {
        unmaximize();

        auto parent_node = container ? container : root;
        assert(parent_node);

        /* special case: there are no views */
        if (parent_node == root && root->children.empty() && !root->view)
        {
            debug << "our case" << std::endl;
            root->set_content(view);
        } else
        {
            if (parent_node->children.empty())
                parent_node->split(type);
            parent_node->append_child(view);
        }
    }

    void rem_view(wayfire_view view)
    {
        if (root->view == view && root->children.size())
            root->view = nullptr;

        auto node = tile_node_from_view(view);
        assert(node);

        if (node->parent)
        {
            auto parent = node->parent;
            parent->remove_child(view);
            parent->try_flatten();
        } else
        {
            assert(node == root && node->view == view);
            node->unset_content();
        }
    }

    void rem_node(wf_tree_node *node)
    {
        if (root->view == node->view && root->children.size())
            root->view = nullptr;

        assert(node->view);
        if (node->parent)
        {
            auto parent = node->parent;
            parent->remove_child(node->view);
            parent->try_flatten();
        } else
        {
            assert(node == root);
            node->unset_content();
        }
    }

    wf_tree_node *get_root_node(wf_tree_node *node)
    {
        assert(node);
        while(node->parent) node = node->parent;
        return node;
    };

    bool is_floating_view(wayfire_view view)
    {
        return view->is_special || view->parent != nullptr;
    }

    namespace selector
    {
        wf_tree_node *node;

        inline wf_geometry get_selected_box()
        {
            return node->box;
        }

        size_t get_child_idx(wf_tree_node *x)
        {
            auto p = x->parent;
            if (!p || p->children.size() == 0)
                return 0;

            for (size_t i = 0; i < p->children.size(); i++)
                if (p->children[i] == x)
                    return i;

            return -1;
        }

        enum move_direction
        {
            MOVE_TO_LAST    = 1 << 0,
            MOVE_TO_FIRST   = 1 << 1,
            MOVE_HORIZONTAL = 1 << 2,
            MOVE_VERTICAL   = 1 << 3,
            MOVE_LEFT  = MOVE_TO_FIRST | MOVE_HORIZONTAL,
            MOVE_RIGHT = MOVE_TO_LAST  | MOVE_HORIZONTAL,
            MOVE_UP    = MOVE_TO_FIRST | MOVE_VERTICAL,
            MOVE_DOWN  = MOVE_TO_LAST  | MOVE_VERTICAL
        };

        void move(move_direction dir)
        {
            if (!node->parent)
                return;

            auto iter = node;

            while(iter->parent)
            {
                size_t forbid_idx = (dir & MOVE_TO_FIRST) ?
                    0 : iter->parent->children.size() - 1;
                wf_split_type wrong_split =
                    (dir & MOVE_HORIZONTAL) ? SPLIT_VERTICAL : SPLIT_HORIZONTAL;

                if (iter->parent->split_type == wrong_split ||
                        get_child_idx(iter) == forbid_idx)
                {
                    iter = iter->parent;
                } else
                {
                    break;
                }
            }

            if (iter->parent)
            {
                size_t idx = get_child_idx(iter) + ((dir & MOVE_TO_FIRST) ? -1 : 1);
                iter = iter->parent->children[idx];
            }

            node = iter;
        }

        void choose_child()
        {
            if (node->children.size())
                node = node->children[0];
        }

        void choose_parent()
        {
            if (node->parent)
                node = node->parent;
        }

        void choose_view(wayfire_view view)
        {
            node = tile_node_from_view(view);
        }
    }

    void set_root(wf_tree_node *r)
    {
        root = r;
        selector::node = r;
    }
};

class wayfire_tile : public wayfire_plugin_t
{
    signal_callback_t view_added, view_attached, view_removed, view_ws_moved,
                      view_focused, output_gain_focus, viewport_changed,
                      view_set_parent, view_fs_request, workarea_changed;
    key_callback select_view, maximize_view;
    button_callback resize_container;
    effect_hook_t draw_selected;

    std::vector<std::vector<wf_tree_node>> root;

    int last_x, last_y;
    bool in_click = false;
    bool stop_select_when_resize_done = false;

    struct tiling_implementation : wf_workspace_implementation
    {
        bool view_movable(wayfire_view v)   { return wf_tiling::is_floating_view(v); }
        bool view_resizable(wayfire_view v) { return wf_tiling::is_floating_view(v); }
    } default_impl;

    wayfire_config *config;

    enum
    {
        SELECTOR_ACTION_GO_LEFT           = 0,
        SELECTOR_ACTION_GO_RIGHT          = 1,
        SELECTOR_ACTION_GO_UP             = 2,
        SELECTOR_ACTION_GO_DOWN           = 3,
        SELECTOR_ACTION_SEL_CHILD         = 4,
        SELECTOR_ACTION_SEL_PARENT        = 5,
        SELECTOR_ACTION_ROTATE_CHILDREN   = 6,
        SELECTOR_ACTION_EXIT              = 7,
        SELECTOR_ACTION_SPLIT_HORIZONTAL  = 8,
        SELECTOR_ACTION_SPLIT_VERTICAL    = 9,
        SELECTOR_ACTION_CHANGE_SPLIT_TYPE = 10,
        SELECTOR_ALL_ACTIONS
    };

    uint32_t action_map[SELECTOR_ALL_ACTIONS];

    public:
    void init(wayfire_config *config)
    {
        this->config = config;
        grab_interface->name = "tile";
        grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;

        read_config();
        init_roots();
        setup_event_handlers();

        wf_tiling::set_root(&root[0][0]);
    }

    void read_config()
    {
        auto section = config->get_section("tile");

        action_map[SELECTOR_ACTION_GO_LEFT          ] = section->get_key("action_left",        {0,  KEY_H}).keyval;
        action_map[SELECTOR_ACTION_GO_RIGHT         ] = section->get_key("action_right",       {0,  KEY_L}).keyval;
        action_map[SELECTOR_ACTION_GO_UP            ] = section->get_key("action_up",          {0,  KEY_K}).keyval;
        action_map[SELECTOR_ACTION_GO_DOWN          ] = section->get_key("action_down",        {0,  KEY_J}).keyval;
        action_map[SELECTOR_ACTION_SEL_CHILD        ] = section->get_key("action_child",       {0,  KEY_C}).keyval;
        action_map[SELECTOR_ACTION_SEL_PARENT       ] = section->get_key("action_parent",      {0,  KEY_P}).keyval;
        action_map[SELECTOR_ACTION_ROTATE_CHILDREN  ] = section->get_key("action_rotate",      {0,  KEY_R}).keyval;
        action_map[SELECTOR_ACTION_EXIT             ] = section->get_key("action_exit",        {0,  KEY_ENTER}).keyval;
        action_map[SELECTOR_ACTION_SPLIT_HORIZONTAL ] = section->get_key("action_split_horiz", {0,  KEY_O}).keyval;
        action_map[SELECTOR_ACTION_SPLIT_VERTICAL   ] = section->get_key("action_split_vert",  {0,  KEY_E}).keyval;
        action_map[SELECTOR_ACTION_CHANGE_SPLIT_TYPE] = section->get_key("action_split_type",  {0,  KEY_T}).keyval;
    }

    void init_roots()
    {
        GetTuple(vw, vh, output->workspace->get_workspace_grid_size());
        GetTuple(sw, sh, output->get_screen_size());

        if (root.size() != (uint)vw)
            root.resize(vw);

        for (int i = 0; i < vw; i++)
        {
            if (root[i].size() != (uint) vh)
                root[i].resize(vh);

            for (int j = 0; j < vh; j++)
            {
                auto g = output->workspace->get_workarea();
                g.x += i * sw;
                g.y += j * sh;

                root[i][j].set_geometry(g);

                output->workspace->set_implementation(std::tuple<int, int> {i, j}, &default_impl, true);
            }
        }
    }

    void setup_event_handlers()
    {
        setup_signals();
        setup_bindings();
        setup_grab_handlers();

        draw_selected = [=] () { draw_selection(); };
    }

    void setup_signals()
    {
        view_added = [=] (signal_data *data)
        {
            auto view = static_cast<create_view_signal*>(data)->created_view;
            if (!wf_tiling::is_floating_view(view))
            {
                change_workspace();
                wf_tiling::add_view(view, nullptr, SPLIT_HORIZONTAL);
            }
        };
        output->connect_signal("create-view", &view_added);

        view_removed = [=] (signal_data *data)
        {
            auto view = static_cast<destroy_view_signal*>(data)->destroyed_view;
            /* we remove special views before they've been created. as such they haven't been
             * added to the tree, and we can(must) ignore them */
            if (!wf_tiling::is_floating_view(view))
            {
                auto node = tile_node_from_view(view);
                wf_tiling::set_root(wf_tiling::get_root_node(node));
                wf_tiling::rem_view(view);

                if (view == current_view)
                    stop_select_mode();
            }
        };
        output->connect_signal("detach-view", &view_removed);

        view_attached = [=] (signal_data *data)
        {
            auto conv = static_cast<create_view_signal*> (data);
            assert(conv);

            /* already mapped view is attached to the output =>
             * it has been moved to this output */
            if (conv->created_view->is_mapped && !wf_tiling::is_floating_view(conv->created_view))
            {
                change_workspace();
                wf_tiling::add_view(conv->created_view, nullptr, SPLIT_HORIZONTAL);
            }
        };
        output->connect_signal("attach-view", &view_attached);

        view_ws_moved = [=] (signal_data* data)
        {
            auto conv = static_cast<view_change_viewport_signal*> (data);
            assert(conv);

            if (!conv->view->is_special && !wf_tiling::is_floating_view(conv->view))
            {
                change_workspace(conv->from);
                wf_tiling::rem_view(conv->view);

                change_workspace(conv->to);
                wf_tiling::add_view(conv->view, nullptr, SPLIT_HORIZONTAL);
            }
        };
        output->connect_signal("view-change-viewport", &view_ws_moved);

        view_focused = [=] (signal_data *data)
        {
            auto conv = static_cast<focus_view_signal*> (data);
            assert(conv);

            if (wf_tiling::root->view && wf_tiling::root->children.size() && conv->focus
                    && !wf_tiling::is_floating_view(conv->focus))
                wf_tiling::maximize_view(conv->focus);
        };
        output->connect_signal("focus-view", &view_focused);

        view_fs_request = [=] (signal_data *data)
        {
            auto conv = static_cast<view_fullscreen_signal*> (data);
            assert(conv);

            if (conv->state && !wf_tiling::is_floating_view(conv->view))
            {
                wf_tiling::maximize_view(conv->view, true);
            } else
            {
                wf_tiling::unmaximize();
            }
        };
        output->connect_signal("view-fullscreen-request", &view_fs_request);

        viewport_changed = [=] (signal_data *data)
        {
            auto conv = static_cast<change_viewport_notify*> (data);
            assert(conv);

            change_workspace(std::tuple<int, int> {conv->new_vx, conv->new_vy});
        };
        output->connect_signal("viewport-changed", &viewport_changed);

        view_set_parent = [=] (signal_data *data)
        {
            auto conv = static_cast<view_set_parent_signal*> (data);
            assert(conv);

            if (conv->view->parent && tile_node_from_view(conv->view))
                wf_tiling::rem_view(conv->view);
        };
        output->connect_signal("view-set-parent", &view_set_parent);

        /* if the focused output changes, we must make sure that the wf_tiling
         * globals point to the right output's root */
        output_gain_focus = [=] (signal_data* data)
        {
            change_workspace();
        };
        output->connect_signal("output-gain-focus", &output_gain_focus);

        workarea_changed = [=] (signal_data *data)
        {
            init_roots();
        };
        output->connect_signal("reserved-workarea", &workarea_changed);
    }

    void setup_bindings()
    {
        select_view = [=] (weston_keyboard *kbd, uint32_t key)
        {
            auto view = output->get_top_view();
            stop_select_when_resize_done = false;

            if (view && !wf_tiling::is_floating_view(view) &&
                    output->workspace->view_visible_on(view,
                        output->workspace->get_current_workspace()))
                start_place_view(view);
        };

        auto select_key = config->get_section("tile")->get_key("select-mode",
                {WLR_MODIFIER_ALT, KEY_S});
        if (select_key.keyval)
            output->add_key(select_key.mod, select_key.keyval, &select_view);

        maximize_view = [=] (weston_keyboard *kbd, uint32_t key)
        {
            if (wf_tiling::root->view && wf_tiling::root->children.size())
            {
                wf_tiling::unmaximize();
            } else
            {
                auto view = output->get_top_view();
                if (view && !wf_tiling::is_floating_view(view))
                    wf_tiling::maximize_view(view);
            }
        };

        auto maximize_key = config->get_section("tile")->get_key("maximize",
                {WLR_MODIFIER_SUPER, KEY_M});
        if (maximize_key.keyval)
            output->add_key(maximize_key.mod, maximize_key.keyval, &maximize_view);

        resize_container = [=] (weston_pointer *ptr, uint32_t)
        {
            last_x = wl_fixed_to_int(ptr->x);
            last_y = wl_fixed_to_int(ptr->y);

            stop_select_when_resize_done = true;
            auto view = output->get_view_at_point(last_x, last_y);
            if (view)
            {
                wf_tiling::selector::choose_view(view);
                start_select_mode();
                in_click = true;
            }
        };

        auto resize_key = config->get_section("tile")->get_button("resize",
                {WLR_MODIFIER_SUPER, BTN_LEFT});
        if (resize_key.button)
            output->add_button(resize_key.mod, resize_key.button, &resize_container);
    }

    void setup_grab_handlers()
    {
        grab_interface->callbacks.keyboard.key = [=] (weston_keyboard *,
                uint32_t key, uint32_t state)
        {
            if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
                handle_action(key);
        };

        grab_interface->callbacks.pointer.button = [=] (weston_pointer *ptr,
                uint32_t button, uint32_t state)
        {
            last_x = wl_fixed_to_int(ptr->x);
            last_y = wl_fixed_to_int(ptr->y);

            if (state == WL_POINTER_BUTTON_STATE_PRESSED)
            {
                in_click = true;

                if (button == BTN_LEFT)
                {
                    auto view = output->get_view_at_point(last_x, last_y);
                    if (view)
                        wf_tiling::selector::choose_view(view);
                }
            } else
            {
                in_click = false;

                if (stop_select_when_resize_done)
                {
                    stop_select_mode();

                    auto view = output->get_view_at_point(last_x, last_y);
                    if (view)
                        output->focus_view(view);
                }
            }
        };

        grab_interface->callbacks.pointer.motion = [=] (weston_pointer *ptr,
                weston_pointer_motion_event *ev)
        {
            handle_input_motion(wl_fixed_to_int(ptr->x), wl_fixed_to_int(ptr->y));
        };
    }

    void change_workspace(std::tuple<int, int> ws = std::tuple<int, int> {-1, -1})
    {
        int x, y;
        std::tie(x, y) = ws;
        if (x == -1) std::tie(x, y) = output->workspace->get_current_workspace();

        wf_tiling::set_root(&root[x][y]);
    }

    GLuint colored_texture = -1;
    void draw_selection()
    {
        if (colored_texture == (uint)-1)
        {
            GLubyte data[4];
            auto color = config->get_section("tile")->get_color("selection-color", {0.5, 0.5, 1, 0.5});
            data[0] = color.r * 255; data[1] = color.g * 255;
            data[2] = color.b * 255; data[3] = color.a * 255;

            GL_CALL(glGenTextures(1, &colored_texture));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, colored_texture));
            GL_CALL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
            GL_CALL(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
            GL_CALL(glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0));
            GL_CALL(glPixelStorei(GL_UNPACK_SKIP_ROWS, 0));

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

            GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                        1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, data));
        }

        auto box = wf_tiling::selector::get_selected_box();
        GetTuple(vx, vy, output->workspace->get_current_workspace());
        auto og = output->get_full_geometry();

        box.x -= og.x + og.width * vx;
        box.y -= og.y + og.height * vy;

        OpenGL::render_transformed_texture(colored_texture, box, {});
    }

    void start_select_mode()
    {
        if (!output->activate_plugin(grab_interface))
            return;

        wf_tiling::unmaximize();

        output->render->auto_redraw(1);
        output->render->set_renderer();
        output->render->add_output_effect(&draw_selected);

        grab_interface->grab();
    }

    void stop_select_mode()
    {
        output->render->auto_redraw(0);
        output->render->reset_renderer();
        output->render->rem_effect(&draw_selected);

        output->deactivate_plugin(grab_interface);
        grab_interface->ungrab();
    }

    wayfire_view current_view;
    void start_place_view(wayfire_view v)
    {
        current_view = v;
        start_select_mode();
    }

    void resize_split(int delta, wf_split_type type)
    {
        if (delta == 0)
            return;

        auto iter = wf_tiling::selector::node;
        while (iter->parent &&
                iter->parent->split_type != type)
            iter = iter->parent;

        if (iter->parent)
        {
            size_t idx = wf_tiling::selector::get_child_idx(iter);

            wf_tree_node *node1 = nullptr, *node2 = nullptr;

            if ((idx != 0 && delta < 0) ||
                (idx == iter->parent->children.size() - 1 && delta > 0))
            {
                node1 = iter->parent->children[idx - 1];
                node2 = iter;
            } else {
                node1 = iter;
                node2 = iter->parent->children[idx + 1];
            }

            if (node1 && node2)
            {
                auto& g1 = node1->box, &g2 = node2->box;

                if (type == SPLIT_HORIZONTAL)
                {
                    g1.width += delta;
                    g2.width -= delta;
                    g2.x += delta;
                } else
                {
                    g1.height += delta;
                    g2.height -= delta;
                    g2.y += delta;
                }

                node1->recalculate_children_boxes(type);
                node2->recalculate_children_boxes(type);
            }
        }

    }

    void handle_input_motion(int x, int y)
    {
        if (in_click)
        {
            int dx = x - last_x;
            int dy = y - last_y;

            resize_split(dx, SPLIT_HORIZONTAL);
            resize_split(dy, SPLIT_VERTICAL);
        }

        last_x = x;
        last_y = y;
    }

    void handle_action(const uint32_t& key)
    {
        if (key == action_map[SELECTOR_ACTION_SEL_PARENT])
            wf_tiling::selector::choose_parent();
        else if (key == action_map[SELECTOR_ACTION_SEL_CHILD])
            wf_tiling::selector::choose_child();
        else if (key == action_map[SELECTOR_ACTION_GO_LEFT])
            wf_tiling::selector::move(wf_tiling::selector::MOVE_LEFT);
        else if (key == action_map[SELECTOR_ACTION_GO_RIGHT])
            wf_tiling::selector::move(wf_tiling::selector::MOVE_RIGHT);
        else if (key == action_map[SELECTOR_ACTION_GO_UP])
            wf_tiling::selector::move(wf_tiling::selector::MOVE_UP);
        else if (key == action_map[SELECTOR_ACTION_GO_DOWN])
            wf_tiling::selector::move(wf_tiling::selector::MOVE_DOWN);
        else if (key == action_map[SELECTOR_ACTION_ROTATE_CHILDREN])
                wf_tiling::selector::node->rotate_children();
        else if (key == action_map[SELECTOR_ACTION_EXIT])
            stop_select_mode();
        else if (key == action_map[SELECTOR_ACTION_SPLIT_VERTICAL] ||
                 key == action_map[SELECTOR_ACTION_SPLIT_HORIZONTAL])
        {
            wf_split_type type = (key == action_map[SELECTOR_ACTION_SPLIT_VERTICAL] ? SPLIT_VERTICAL : SPLIT_HORIZONTAL);

            auto node = tile_node_from_view(current_view);

            if (node != wf_tiling::selector::node)
            {
                wf_tiling::add_view(current_view,
                        wf_tiling::selector::node, type);

                wf_tiling::rem_node(node);
                debug << "stopping select mode" << std::endl;
            }
            stop_select_mode();
        } else if (key == action_map[SELECTOR_ACTION_CHANGE_SPLIT_TYPE])
        {
            auto newtype = (SPLIT_VERTICAL + SPLIT_HORIZONTAL) - wf_tiling::selector::node->split_type;
            wf_tiling::selector::node->resplit((wf_split_type)newtype);
        }
    }
};

extern "C" {
    wayfire_plugin_t* newInstance()
    {
        return new wayfire_tile();
    }
}

