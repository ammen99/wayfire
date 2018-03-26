#ifndef TREE_DEFINITION_HPP
#define TREE_DEFINITION_HPP

#include <vector>
#include <algorithm>
#include <view.hpp>
#include <output.hpp>
#include <workspace-manager.hpp>
#include <debug.hpp>

#define tile_data "__tile_data"

#define debug_call(msg) debug << __func__ << ": " << msg << " at address " << this << std::endl
#define debug_scall debug_call("start")

enum wf_split_type
{
    SPLIT_HORIZONTAL = 1 << 0,
    SPLIT_VERTICAL   = 1 << 1
};

struct wf_tree_node;
struct wf_tile_view_data : public wf_custom_view_data
{
    wf_tree_node *node;
};

void view_fit_to_box(wayfire_view view, wf_geometry box)
{
    GetTuple(vx, vy, view->output->workspace->get_current_workspace());
    GetTuple(sw, sh, view->output->get_screen_size());

    box.x -= sw * vx;
    box.y -= sh * vy;
    view->set_geometry(box);
}

struct wf_tree_node
{
    /* view in this tree node. non-null only on leaf nodes */
    wayfire_view view = nullptr;

    wf_geometry box;
    wf_split_type split_type;

    wf_tree_node *parent = nullptr;
    std::vector<wf_tree_node*> children;

    void set_geometry(wf_geometry tbox)
    {
        box = tbox;
        recalculate_children_boxes();
    }

#define RECALC_ALL (SPLIT_HORIZONTAL | SPLIT_VERTICAL)

    void recalculate_children_boxes(uint32_t recalculate = RECALC_ALL)
    {
        debug_scall;
        debug << box.x << " " << box.y << " " << box.width << " " << box.height << " " << 
            (view ? view->desktop_surface : 0) << std::endl;
        size_t size = children.size();
        for (size_t i = 0; i < size; i++)
        {
            if (recalculate == RECALC_ALL)
                children[i]->box = box;

            if (split_type & SPLIT_VERTICAL & recalculate)
            {
                children[i]->box.height = box.height / size;
                children[i]->box.y = box.y + i * box.height / size;
            } else if (split_type & SPLIT_VERTICAL)
            {
                children[i]->box.x = box.x;
                children[i]->box.width = box.width;
            }

            if (split_type & SPLIT_HORIZONTAL & recalculate)
            {
                children[i]->box.width = box.width / size;
                children[i]->box.x = box.x + i * box.width / size;
            } else if (split_type & SPLIT_HORIZONTAL)
            {
                children[i]->box.y = box.y;
                children[i]->box.height = box.height;
            }

            children[i]->recalculate_children_boxes(recalculate);
        }

        if (view)
            view_fit_to_box(view, box);
    }
    /* make this node correspond to a split.
     * will actually create a new node for the current view.
     * after this we MUST append another child, otherwise the logic
     * for the other functions below will fail */
    void split(wf_split_type type)
    {
        debug_scall;
        assert(view && children.empty());
        children.resize(1);

        children[0] = new wf_tree_node;
        children[0]->box = box;
        children[0]->set_parent(this);

        unset_content(false);
        if (view)
            children[0]->set_content(view);

        split_type = type;
        view = nullptr;
    }

    void append_child(wayfire_view view)
    {
        debug_scall;
        auto child = new wf_tree_node;

        child->set_parent(this);
        children.push_back(child);
        child->set_content(view);

        recalculate_children_boxes();
    }

    /* simply remove the child from the list of children
     * and resize boxes */
    void remove_child(wayfire_view child)
    {
        debug_scall;

        size_t i = 0;
        for (; i < children.size() && children[i]->view != child; i++);
        assert(i != children.size());

        children[i]->unset_content();
        children.erase(children.begin() + i);

        recalculate_children_boxes();
    }

    /* if there is only one child, move it one level up. */
    /* we preserve the invariant: this is called every time a node is removed.
     * As a result, each node must either be a leaf or have >= 2 children */
    void try_flatten()
    {
        debug_scall;
        if (children.size() != 1)
            return;

        auto child = children[0];

        box = child->box;
        view = child->view;
        children = child->children;
        split_type = child->split_type;

        if (view)
            ((wf_tile_view_data*) view->custom_data[tile_data])->node = this;

        delete child;

        for (auto child : children)
            child->set_parent(this);
    }

    void resplit(wf_split_type type)
    {
        debug_scall;
        split_type = type;
        recalculate_children_boxes();
    }

    void rotate_children()
    {
        if (children.empty())
            return;

        for (size_t i = 0; i < children.size() - 1; i++)
            std::swap(children[i], children[i + 1]);

        recalculate_children_boxes();
    }

    void set_parent(wf_tree_node *p)
    {
        parent = p;
    }

    void set_content(wayfire_view view)
    {
        debug_scall;
        assert(children.empty());

        this->view = view;

        auto it = view->custom_data.find(tile_data);

        if (it == view->custom_data.end())
        {
            auto cdata = new wf_tile_view_data;
            cdata->node = this;
            view->custom_data[tile_data] = cdata;
        } else
        {
            auto data = static_cast<wf_tile_view_data*> (it->second);
            data->node = this;
        }

        recalculate_children_boxes();
    }

    void unset_content(bool reset_view = true)
    {
        debug_scall;
        assert(view);

        auto cit = view->custom_data.find(tile_data);

        assert(cit != view->custom_data.end());
        auto data = static_cast<wf_tile_view_data*> (cit->second);

        /* if we have moved the view to some other node, we shouldn't free the data */
        if (data->node == this)
        {
            delete cit->second;
            view->custom_data.erase(cit);
        }

        if (reset_view)
            view = nullptr;
    }
};
#endif /* end of include guard: TREE_DEFINITION_HPP */
