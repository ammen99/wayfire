#ifndef SNAP_SIGNAL
#define SNAP_SIGNAL

/* A private signal, currently shared by move & grid
 *
 * It is used to provide autosnap functionality for the move plugin,
 * by reusing grid's abilities */

#include <wayfire/view.hpp>

/* The "slot" where the view should be snapped */
enum slot_type
{
    SLOT_BL     = 1,
    SLOT_BOTTOM = 2,
    SLOT_BR     = 3,
    SLOT_LEFT   = 4,
    SLOT_CENTER = 5,
    SLOT_RIGHT  = 6,
    SLOT_TL     = 7,
    SLOT_TOP    = 8,
    SLOT_TR     = 9,
};

/* Query the dimensions of the given slot */
struct snap_query_signal : public wf::signal_data_t
{
    slot_type slot;
    wf::geometry_t out_geometry;
};

/* Do snap the view to the given slot */
struct snap_signal : public wf::signal_data_t
{
    wayfire_view view;
    uint32_t slot; // 0 for unsnap
};

#endif /* end of include guard: SNAP_SIGNAL */
