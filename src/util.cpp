#include "util.hpp"
#include <debug.hpp>
#include <core.hpp>
#include <ctime>

extern "C"
{
#include <wlr/util/region.h>
}

/* Geometry helpers */
bool operator == (const wf_point& a, const wf_point& b)
{
    return a.x == b.x && a.y == b.y;
}

bool operator != (const wf_point& a, const wf_point& b)
{
    return !(a == b);
}

bool operator == (const wf_geometry& a, const wf_geometry& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool operator != (const wf_geometry& a, const wf_geometry& b)
{
    return !(a == b);
}

wf_point operator + (const wf_point& a, const wf_point& b)
{
    return {a.x + b.x, a.y + b.y};
}

wf_point operator + (const wf_point& a, const wf_geometry& b)
{
    return {a.x + b.x, a.y + b.y};
}

wf_geometry operator + (const wf_geometry &a, const wf_point& b)
{
    return {
        a.x + b.x,
        a.y + b.y,
        a.width,
        a.height
    };
}

wf_point operator - (const wf_point& a)
{
    return {-a.x, -a.y};
}

bool operator & (const wf_geometry& rect, const wf_point& point)
{
    return wlr_box_contains_point(&rect, point.x, point.y);
}

bool operator & (const wf_geometry& r1, const wf_geometry& r2)
{
    wlr_box result;
    return wlr_box_intersection(&result, &r1, &r2);
}

wf_geometry wf_geometry_intersection(const wf_geometry& r1,
    const wf_geometry& r2)
{
    wlr_box result;
    if (wlr_box_intersection(&result, &r1, &r2))
        return result;

    return {0, 0, 0, 0};
}

/* Pixman helpers */
wlr_box wlr_box_from_pixman_box(const pixman_box32_t& box)
{
    return {
        box.x1, box.y1,
        box.x2 - box.x1,
        box.y2 - box.y1
    };
}

pixman_box32_t pixman_box_from_wlr_box(const wlr_box& box)
{
    return {
        box.x, box.y,
        box.x + box.width,
        box.y + box.height
    };
}

wf_region::wf_region()
{
    pixman_region32_init(&_region);
}

wf_region::wf_region(pixman_region32_t *region) : wf_region()
{
    pixman_region32_copy(this->to_pixman(), region);
}

wf_region::wf_region(const wlr_box& box)
{
    pixman_region32_init_rect(&_region, box.x, box.y, box.width, box.height);
}

wf_region:: ~wf_region()
{
    pixman_region32_fini(&_region);
}

wf_region::wf_region(const wf_region& other) : wf_region()
{
    pixman_region32_copy(this->to_pixman(), other.unconst());
}

wf_region::wf_region(wf_region&& other) : wf_region()
{
    std::swap(this->_region, other._region);
}

wf_region& wf_region::operator = (const wf_region& other)
{
    if (&other == this)
        return *this;

    pixman_region32_copy(&_region, other.unconst());
    return *this;
}

wf_region& wf_region::operator = (wf_region&& other)
{
    if (&other == this)
        return *this;

    std::swap(_region, other._region);
    return *this;
}

bool wf_region::empty() const
{
    return !pixman_region32_not_empty(this->unconst());
}

void wf_region::clear()
{
    pixman_region32_clear(&_region);
}

void wf_region::expand_edges(int amount)
{
    /* FIXME: make sure we don't throw pixman errors when amount is bigger
     * than a rectangle size */
    wlr_region_expand(this->to_pixman(), this->to_pixman(), amount);
}

pixman_box32_t wf_region::get_extents() const
{
    return *pixman_region32_extents(this->unconst());
}

/* Translate the region */
wf_region wf_region::operator + (const wf_point& vector) const
{
    wf_region result{*this};
    pixman_region32_translate(&result._region, vector.x, vector.y);
    return result;
}

wf_region& wf_region::operator += (const wf_point& vector)
{
    pixman_region32_translate(&_region, vector.x, vector.y);
    return *this;
}

wf_region wf_region::operator * (float scale) const
{
    wf_region result;
    wlr_region_scale(result.to_pixman(), this->unconst(), scale);
    return result;
}

wf_region& wf_region::operator *= (float scale)
{
    wlr_region_scale(this->to_pixman(), this->to_pixman(), scale);
    return *this;
}

/* Region intersection */
wf_region wf_region::operator & (const wlr_box& box) const
{
    wf_region result;
    pixman_region32_intersect_rect(result.to_pixman(), this->unconst(),
        box.x, box.y, box.width, box.height);

    return result;
}

wf_region wf_region::operator & (const wf_region& other) const
{
    wf_region result;
    pixman_region32_intersect(result.to_pixman(),
        this->unconst(), other.unconst());

    return result;
}

wf_region& wf_region::operator &= (const wlr_box& box)
{
    pixman_region32_intersect_rect(this->to_pixman(), this->to_pixman(),
        box.x, box.y, box.width, box.height);
    return *this;
}

wf_region& wf_region::operator &= (const wf_region& other)
{
    pixman_region32_intersect(this->to_pixman(),
        this->to_pixman(), other.unconst());
    return *this;
}

/* Region union */
wf_region wf_region::operator | (const wlr_box& other) const
{
    wf_region result;
    pixman_region32_union_rect(result.to_pixman(), this->unconst(),
        other.x, other.y, other.width, other.height);
    return result;
}

wf_region wf_region::operator | (const wf_region& other) const
{
    wf_region result;
    pixman_region32_union(result.to_pixman(), this->unconst(), other.unconst());
    return result;
}

wf_region& wf_region::operator |= (const wlr_box& other)
{
    pixman_region32_union_rect(this->to_pixman(), this->to_pixman(),
        other.x, other.y, other.width, other.height);
    return *this;
}

wf_region& wf_region::operator |= (const wf_region& other)
{
    pixman_region32_union(this->to_pixman(), this->to_pixman(), other.unconst());
    return *this;
}

/* Subtract the box/region from the current region */
wf_region wf_region::operator ^ (const wlr_box& box) const
{
    wf_region result;
    wf_region sub{box};
    pixman_region32_subtract(result.to_pixman(), this->unconst(), sub.to_pixman());
    return result;
}

wf_region wf_region::operator ^ (const wf_region& other) const
{
    wf_region result;
    pixman_region32_subtract(result.to_pixman(),
        this->unconst(), other.unconst());
    return result;
}

wf_region& wf_region::operator ^= (const wlr_box& box)
{
    wf_region sub{box};
    pixman_region32_subtract(this->to_pixman(),
        this->to_pixman(), sub.to_pixman());
    return *this;
}

wf_region& wf_region::operator ^= (const wf_region& other)
{
    pixman_region32_subtract(this->to_pixman(),
        this->to_pixman(), other.unconst());
    return *this;
}

pixman_region32_t *wf_region::to_pixman()
{
    return &_region;
}

pixman_region32_t* wf_region::unconst() const
{
    return const_cast<pixman_region32_t*> (&_region);
}

const pixman_box32_t* wf_region::begin() const
{
    int n;
    return pixman_region32_rectangles(unconst(), &n);
}

const pixman_box32_t* wf_region::end() const
{
    int n;
    auto data = pixman_region32_rectangles(unconst(), &n);
    return data + n;
}

/* Misc helper functions */
int64_t timespec_to_msec(const timespec& ts)
{
    return ts.tv_sec * 1000ll + ts.tv_nsec / 1000000ll;
}

uint32_t get_current_time()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return timespec_to_msec(ts);
}

wf_geometry clamp(wf_geometry window, wf_geometry output)
{
    window.width = clamp(window.width, 0, output.width);
    window.height = clamp(window.height, 0, output.height);

    window.x = clamp(window.x,
        output.x, output.x + output.width - window.width);
    window.y = clamp(window.y,
        output.y, output.y + output.height - window.height);

    return window;
}

static void handle_wrapped_listener(wl_listener *listener, void *data)
{
    wf::wl_listener_wrapper::wrapper *wrap = wl_container_of(listener, wrap, listener);
    wrap->self->emit(data);
}

static void handle_idle_listener(void *data)
{
    auto call = (wf::wl_idle_call*)(data);
    call->execute();
}

static int handle_timeout(void *data)
{
    auto timer = (wf::wl_timer*) (data);
    timer->execute();
    return 0;
}

namespace wf
{
    wl_listener_wrapper::wl_listener_wrapper()
    {
        _wrap.self = this;
        _wrap.listener.notify = handle_wrapped_listener;
        wl_list_init(&_wrap.listener.link);
    }

    wl_listener_wrapper::~wl_listener_wrapper()
    {
        disconnect();
    }

    void wl_listener_wrapper::set_callback(callback_t _call)
    {
        this->call = _call;
    }

    bool wl_listener_wrapper::connect(wl_signal *signal)
    {
        if (is_connected())
            return false;

        wl_signal_add(signal, &_wrap.listener);
        return true;
    }

    void wl_listener_wrapper::disconnect()
    {
        wl_list_remove(&_wrap.listener.link);
        wl_list_init(&_wrap.listener.link);
    }

    bool wl_listener_wrapper::is_connected() const
    {
        return !wl_list_empty(&_wrap.listener.link);
    }

    void wl_listener_wrapper::emit(void *data)
    {
        if (this->call)
            this->call(data);
    }

    wl_idle_call::wl_idle_call() = default;
    wl_idle_call::~wl_idle_call()
    {
        disconnect();
    }

    void wl_idle_call::set_event_loop(wl_event_loop *loop)
    {
        disconnect();
        this->loop = loop;
    }

    void wl_idle_call::set_callback(callback_t call)
    {
        disconnect();
        this->call = call;
    }

    void wl_idle_call::run_once()
    {
        if (!call || source)
            return;

        auto use_loop = loop ?: get_core().ev_loop;
        source = wl_event_loop_add_idle(use_loop, handle_idle_listener, this);
    }

    void wl_idle_call::run_once(callback_t cb)
    {
        set_callback(cb);
        run_once();
    }

    void wl_idle_call::disconnect()
    {
        if (!source)
            return;

        wl_event_source_remove(source);
        source = nullptr;
    }

    bool wl_idle_call::is_connected()
    {
        return source;
    }

    void wl_idle_call::execute()
    {
        source = nullptr;
        if (call)
            call();
    }

    wl_timer::~wl_timer()
    {
        if (source)
            wl_event_source_remove(source);
    }

    void wl_timer::set_timeout(uint32_t timeout_ms, callback_t call)
    {
        this->call = call;
        if (!source)
            source = wl_event_loop_add_timer(get_core().ev_loop, handle_timeout, this);

        wl_event_source_timer_update(source, timeout_ms);
    }

    void wl_timer::disconnect()
    {
        wl_event_source_remove(source);
        source = NULL;
    }

    void wl_timer::execute()
    {
        if (call)
            call();
    }
}
