#ifndef WF_GEOMETRY_HPP
#define WF_GEOMETRY_HPP

extern "C"
{
#include <wlr/types/wlr_box.h>
}

/* Format for log_* to use when printing wf_point/wf_geometry/wlr_box */
#define Prwp "(%d,%d)"
#define Ewp(v) v.x, v.y

/* Format for log_* to use when printing wf_geometry/wlr_box */
#define Prwg "(%d,%d %dx%d)"
#define Ewg(v) v.x, v.y, v.width, v.height

struct wf_point
{
    int x, y;
};

struct wf_surface_size_t
{
    int32_t width;
    int32_t height;
};

using wf_geometry = wlr_box;

bool operator == (const wf_point& a, const wf_point& b);
bool operator != (const wf_point& a, const wf_point& b);

bool operator == (const wf_geometry& a, const wf_geometry& b);
bool operator != (const wf_geometry& a, const wf_geometry& b);

wf_point    operator + (const wf_point& a, const wf_point& b);
wf_point    operator + (const wf_point& a, const wf_geometry& b);
wf_geometry operator + (const wf_geometry &a, const wf_point& b);
wf_point    operator - (const wf_point& a);

/* Returns true if point is inside rect */
bool operator & (const wf_geometry& rect, const wf_point& point);
/* Returns true if the two geometries have a common point */
bool operator & (const wf_geometry& r1, const wf_geometry& r2);

/* Returns the intersection of the two boxes, if the boxes don't intersect,
 * the resulting geometry has undefined (x,y) and width == height == 0 */
wf_geometry wf_geometry_intersection(const wf_geometry& r1,
    const wf_geometry& r2);

#endif /* end of include guard: WF_GEOMETRY_HPP */
