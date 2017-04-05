#ifndef SNAP_SIGNAL
#define SNAP_SIGNAL

#include <output.hpp>

enum slot_type {
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

struct snap_signal : public signal_data {
    wayfire_view view;
    slot_type tslot;
};

#endif /* end of include guard: SNAP_SIGNAL */
