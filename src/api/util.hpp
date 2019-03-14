#ifndef WF_UTIL_HPP
#define WF_UTIL_HPP

#include <functional>
#include <pixman.h>
extern "C"
{
#include <wlr/types/wlr_box.h>
}

/* ------------------ point and geometry utility functions ------------------ */

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

using wf_geometry = wlr_box;

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

/* ---------------------- pixman utility functions -------------------------- */
struct wf_region
{
    wf_region();
    /* Makes a copy of the given region */
    wf_region(pixman_region32_t *damage);
    wf_region(const wlr_box& box);
    ~wf_region();

    wf_region(const wf_region& other);
    wf_region(wf_region&& other);

    wf_region& operator = (const wf_region& other);
    wf_region& operator = (wf_region&& other);

    bool empty() const;
    void clear();

    void expand_edges(int amount);
    pixman_box32_t get_extents() const;

    /* Translate the region */
    wf_region operator + (const wf_point& vector) const;
    wf_region& operator += (const wf_point& vector);

    wf_region operator * (float scale) const;
    wf_region& operator *= (float scale);

    /* Region intersection */
    wf_region operator & (const wlr_box& box) const;
    wf_region operator & (const wf_region& other) const;
    wf_region& operator &= (const wlr_box& box);
    wf_region& operator &= (const wf_region& other);

    /* Region union */
    wf_region operator | (const wlr_box& other) const;
    wf_region operator | (const wf_region& other) const;
    wf_region& operator |= (const wlr_box& other);
    wf_region& operator |= (const wf_region& other);

    /* Subtract the box/region from the current region */
    wf_region operator ^ (const wlr_box& box) const;
    wf_region operator ^ (const wf_region& other) const;
    wf_region& operator ^= (const wlr_box& box);
    wf_region& operator ^= (const wf_region& other);

    pixman_region32_t *to_pixman();

    const pixman_box32_t* begin() const;
    const pixman_box32_t* end() const;

    private:
    pixman_region32_t _region;
    /* Returns a const-casted pixman_region32_t*, useful in const operators
     * where we use this->_region as only source for calculations, but pixman
     * won't let us pass a const pixman_region32_t* */
    pixman_region32_t* unconst() const;
};

wlr_box wlr_box_from_pixman_box(const pixman_box32_t& box);
pixman_box32_t pixman_box_from_wlr_box(const wlr_box& box);

/* ------------------------- misc helper functions ------------------------- */
int64_t timespec_to_msec(const timespec& ts);

/* Returns current time in msec, using CLOCK_MONOTONIC as a base */
uint32_t get_current_time();

/* Ensure that value is in the interval [min, max] */
template<class T>
T clamp(T value, T min, T max)
{
    return std::min(std::max(value, min), max);
}

#endif /* end of include guard: WF_UTIL_HPP */
