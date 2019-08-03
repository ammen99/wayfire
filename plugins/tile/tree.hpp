#ifndef WF_TILE_PLUGIN_TREE
#define WF_TILE_PLUGIN_TREE

#include <view.hpp>

namespace wf {
namespace tile {

/**
 * A tree node represents a logical container of views in the tiled part of
 * a workspace.
 *
 * There are two types of nodes:
 * 1. View tree nodes, i.e leaves, they contain a single view
 * 2. Split tree nodes, they contain at least 1 child view.
 */
struct split_node_t;
struct view_node_t;

struct tree_node_t
{
    /** The node parent, or nullptr if this is the root node */
    nonstd::observer_ptr<split_node_t> parent;

    /** The children of the node */
    std::vector<std::unique_ptr<tree_node_t>> children;

    /** The geometry occupied by the node */
    wf_geometry geometry;

    /** Set the geometry available for the node and its subnodes. */
    virtual void set_geometry(wf_geometry geometry);
    virtual ~tree_node_t() {};

    /** Simply dynamic cast this to a split_node_t */
    nonstd::observer_ptr<split_node_t> as_split_node();
    /** Simply dynamic cast this to a view_node_t */
    nonstd::observer_ptr<view_node_t> as_view_node();
};

/**
 * A node which contains a split can be split either horizontally or vertically
 */
enum split_direction_t
{
    SPLIT_HORIZONTAL = 0,
    SPLIT_VERTICAL   = 1,
};

/*
 * Represents a node in the tree which contains at 1 one child node
 */
struct split_node_t : public tree_node_t
{
    /**
     * Add the given child to the list of children.
     *
     * The new child will get resized so that its area is at most 1/(N+1) of the
     * total node area, where N is the number of children before adding the new
     * child.
     */
    void add_child(std::unique_ptr<tree_node_t> child);

    /**
     * Remove a child from the node, and return its unique_ptr
     */
    std::unique_ptr<tree_node_t> remove_child(
        nonstd::observer_ptr<tree_node_t> child);

    /**
     * Set the total geometry available to the node. This will recursively
     * resize the children nodes, so that they fit inside the new geometry and
     * have a size proportional to their old size.
     */
    void set_geometry(wf_geometry geometry);

    split_node_t(split_direction_t direction);
    split_direction_t get_split_direction() const;

  private:
    split_direction_t split_direction;

    /**
     * Resize the children so that they fit inside the given
     * available_geometry.
     */
    void recalculate_children(wf_geometry available_geometry);

    /**
     * Calculate the geometry of a child if it has child_size as one
     * dimension. Whether this is width/height depends on the node split type.
     *
     * @param child_pos The position from which the child starts, relative to
     *                  the node itself
     *
     * @return The geometry of the child, in global coordinates
     */
    wf_geometry get_child_geometry(int32_t child_pos, int32_t child_size);

    /** Return the size of the node in the dimension in which the split happens */
    int32_t calculate_splittable() const;
    /** Return the size of the geometry in the dimension in which the split
     * happens */
    int32_t calculate_splittable(wf_geometry geometry) const;
};

/**
 * Represents a leaf in the tree, contains a single view
 */
struct view_node_t : public tree_node_t
{
    view_node_t(wayfire_view view);
    ~view_node_t();

    wayfire_view view;
    /**
     * Set the geomety of the node and the contained view.
     */
    void set_geometry(wf_geometry geometry) override;

    /* Return the tree node corresponding to the view, or nullptr if none */
    static nonstd::observer_ptr<view_node_t> get_node(wayfire_view view);

  private:
};

/**
 * Flatten the tree as much as possible, i.e remove nodes with only one
 * split-node child.
 *
 * The only exception is "the root", which will always be a split node.
 *
 * Note: this will potentially invalidate pointers to the tree and modify
 * the given parameter.
 */
void flatten_tree(std::unique_ptr<tree_node_t>& root);

/**
 * Get the root of the tree which node is part of
 */
nonstd::observer_ptr<split_node_t> get_root( nonstd::observer_ptr<tree_node_t> node);

}
}
#endif /* end of include guard: WF_TILE_PLUGIN_TREE */