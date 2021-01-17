#include "wayfire/util.hpp"
#include <wayfire/debug.hpp>
#include <wayfire/core.hpp>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cmath>
#include <wayfire/nonstd/wlroots-full.hpp>

/* Geometry helpers */
std::ostream& operator <<(std::ostream& stream, const wf::geometry_t& geometry)
{
    stream << '(' << geometry.x << ',' << geometry.y <<
        ' ' << geometry.width << 'x' << geometry.height << ')';

    return stream;
}

std::ostream& operator <<(std::ostream& stream, const wf::point_t& point)
{
    stream << '(' << point.x << ',' << point.y << ')';

    return stream;
}

std::ostream& operator <<(std::ostream& stream, const wf::pointf_t& pointf)
{
    stream << std::fixed << std::setprecision(4) <<
        '(' << pointf.x << ',' << pointf.y << ')';

    return stream;
}

wf::point_t wf::origin(const geometry_t& geometry)
{
    return {geometry.x, geometry.y};
}

wf::dimensions_t wf::dimensions(const geometry_t& geometry)
{
    return {geometry.width, geometry.height};
}

bool operator ==(const wf::dimensions_t& a, const wf::dimensions_t& b)
{
    return a.width == b.width && a.height == b.height;
}

bool operator !=(const wf::dimensions_t& a, const wf::dimensions_t& b)
{
    return !(a == b);
}

bool operator ==(const wf::point_t& a, const wf::point_t& b)
{
    return a.x == b.x && a.y == b.y;
}

bool operator !=(const wf::point_t& a, const wf::point_t& b)
{
    return !(a == b);
}

bool operator ==(const wf::geometry_t& a, const wf::geometry_t& b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

bool operator !=(const wf::geometry_t& a, const wf::geometry_t& b)
{
    return !(a == b);
}

wf::point_t operator +(const wf::point_t& a, const wf::point_t& b)
{
    return {a.x + b.x, a.y + b.y};
}

wf::point_t operator -(const wf::point_t& a, const wf::point_t& b)
{
    return {a.x - b.x, a.y - b.y};
}

wf::point_t operator +(const wf::point_t& a, const wf::geometry_t& b)
{
    return {a.x + b.x, a.y + b.y};
}

wf::geometry_t operator +(const wf::geometry_t & a, const wf::point_t& b)
{
    return {
        a.x + b.x,
        a.y + b.y,
        a.width,
        a.height
    };
}

wf::point_t operator -(const wf::point_t& a)
{
    return {-a.x, -a.y};
}

wf::geometry_t operator *(const wf::geometry_t& box, double scale)
{
    wlr_box scaled;
    scaled.x = std::floor(box.x * scale);
    scaled.y = std::floor(box.y * scale);
    /* Scale it the same way that regions are scaled, otherwise
     * we get numerical issues. */
    scaled.width  = std::ceil((box.x + box.width) * scale) - scaled.x;
    scaled.height = std::ceil((box.y + box.height) * scale) - scaled.y;

    return scaled;
}

double abs(const wf::point_t& p)
{
    return std::sqrt(p.x * p.x + p.y * p.y);
}

bool operator &(const wf::geometry_t& rect, const wf::point_t& point)
{
    return wlr_box_contains_point(&rect, point.x, point.y);
}

bool operator &(const wf::geometry_t& rect, const wf::pointf_t& point)
{
    return wlr_box_contains_point(&rect, point.x, point.y);
}

bool operator &(const wf::geometry_t& r1, const wf::geometry_t& r2)
{
    if ((r1.x + r1.width <= r2.x) || (r2.x + r2.width <= r1.x) ||
        (r1.y + r1.height <= r2.y) || (r2.y + r2.height <= r1.y))
    {
        return false;
    }

    return true;
}

wf::geometry_t wf::geometry_intersection(const wf::geometry_t& r1,
    const wf::geometry_t& r2)
{
    wlr_box result;
    if (wlr_box_intersection(&result, &r1, &r2))
    {
        return result;
    }

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

wf::region_t::region_t()
{
    pixman_region32_init(&_region);
}

wf::region_t::region_t(pixman_region32_t *region) : wf::region_t()
{
    pixman_region32_copy(this->to_pixman(), region);
}

wf::region_t::region_t(const wlr_box& box)
{
    pixman_region32_init_rect(&_region, box.x, box.y, box.width, box.height);
}

wf::region_t::~region_t()
{
    pixman_region32_fini(&_region);
}

wf::region_t::region_t(const wf::region_t& other) : wf::region_t()
{
    pixman_region32_copy(this->to_pixman(), other.unconst());
}

wf::region_t::region_t(wf::region_t&& other) : wf::region_t()
{
    std::swap(this->_region, other._region);
}

wf::region_t& wf::region_t::operator =(const wf::region_t& other)
{
    if (&other == this)
    {
        return *this;
    }

    pixman_region32_copy(&_region, other.unconst());

    return *this;
}

wf::region_t& wf::region_t::operator =(wf::region_t&& other)
{
    if (&other == this)
    {
        return *this;
    }

    std::swap(_region, other._region);

    return *this;
}

bool wf::region_t::empty() const
{
    return !pixman_region32_not_empty(this->unconst());
}

void wf::region_t::clear()
{
    pixman_region32_clear(&_region);
}

void wf::region_t::expand_edges(int amount)
{
    /* FIXME: make sure we don't throw pixman errors when amount is bigger
     * than a rectangle size */
    wlr_region_expand(this->to_pixman(), this->to_pixman(), amount);
}

pixman_box32_t wf::region_t::get_extents() const
{
    return *pixman_region32_extents(this->unconst());
}

bool wf::region_t::contains_point(const wf::point_t& point) const
{
    return pixman_region32_contains_point(this->unconst(),
        point.x, point.y, NULL);
}

bool wf::region_t::contains_pointf(const wf::pointf_t& point) const
{
    for (auto& box : *this)
    {
        if ((box.x1 <= point.x) && (point.x < box.x2))
        {
            if ((box.y1 <= point.y) && (point.y < box.y2))
            {
                return true;
            }
        }
    }

    return false;
}

/* Translate the region */
wf::region_t wf::region_t::operator +(const wf::point_t& vector) const
{
    wf::region_t result{*this};
    pixman_region32_translate(&result._region, vector.x, vector.y);

    return result;
}

wf::region_t& wf::region_t::operator +=(const wf::point_t& vector)
{
    pixman_region32_translate(&_region, vector.x, vector.y);

    return *this;
}

wf::region_t wf::region_t::operator *(float scale) const
{
    wf::region_t result;
    wlr_region_scale(result.to_pixman(), this->unconst(), scale);

    return result;
}

wf::region_t& wf::region_t::operator *=(float scale)
{
    wlr_region_scale(this->to_pixman(), this->to_pixman(), scale);

    return *this;
}

/* Region intersection */
wf::region_t wf::region_t::operator &(const wlr_box& box) const
{
    wf::region_t result;
    pixman_region32_intersect_rect(result.to_pixman(), this->unconst(),
        box.x, box.y, box.width, box.height);

    return result;
}

wf::region_t wf::region_t::operator &(const wf::region_t& other) const
{
    wf::region_t result;
    pixman_region32_intersect(result.to_pixman(),
        this->unconst(), other.unconst());

    return result;
}

wf::region_t& wf::region_t::operator &=(const wlr_box& box)
{
    pixman_region32_intersect_rect(this->to_pixman(), this->to_pixman(),
        box.x, box.y, box.width, box.height);

    return *this;
}

wf::region_t& wf::region_t::operator &=(const wf::region_t& other)
{
    pixman_region32_intersect(this->to_pixman(),
        this->to_pixman(), other.unconst());

    return *this;
}

/* Region union */
wf::region_t wf::region_t::operator |(const wlr_box& other) const
{
    wf::region_t result;
    pixman_region32_union_rect(result.to_pixman(), this->unconst(),
        other.x, other.y, other.width, other.height);

    return result;
}

wf::region_t wf::region_t::operator |(const wf::region_t& other) const
{
    wf::region_t result;
    pixman_region32_union(result.to_pixman(), this->unconst(), other.unconst());

    return result;
}

wf::region_t& wf::region_t::operator |=(const wlr_box& other)
{
    pixman_region32_union_rect(this->to_pixman(), this->to_pixman(),
        other.x, other.y, other.width, other.height);

    return *this;
}

wf::region_t& wf::region_t::operator |=(const wf::region_t& other)
{
    pixman_region32_union(this->to_pixman(), this->to_pixman(), other.unconst());

    return *this;
}

/* Subtract the box/region from the current region */
wf::region_t wf::region_t::operator ^(const wlr_box& box) const
{
    wf::region_t result;
    wf::region_t sub{box};
    pixman_region32_subtract(result.to_pixman(), this->unconst(), sub.to_pixman());

    return result;
}

wf::region_t wf::region_t::operator ^(const wf::region_t& other) const
{
    wf::region_t result;
    pixman_region32_subtract(result.to_pixman(),
        this->unconst(), other.unconst());

    return result;
}

wf::region_t& wf::region_t::operator ^=(const wlr_box& box)
{
    wf::region_t sub{box};
    pixman_region32_subtract(this->to_pixman(),
        this->to_pixman(), sub.to_pixman());

    return *this;
}

wf::region_t& wf::region_t::operator ^=(const wf::region_t& other)
{
    pixman_region32_subtract(this->to_pixman(),
        this->to_pixman(), other.unconst());

    return *this;
}

pixman_region32_t*wf::region_t::to_pixman()
{
    return &_region;
}

pixman_region32_t*wf::region_t::unconst() const
{
    return const_cast<pixman_region32_t*>(&_region);
}

const pixman_box32_t*wf::region_t::begin() const
{
    int n;

    return pixman_region32_rectangles(unconst(), &n);
}

const pixman_box32_t*wf::region_t::end() const
{
    int n;
    auto data = pixman_region32_rectangles(unconst(), &n);

    return data + n;
}

/* Misc helper functions */
int64_t wf::timespec_to_msec(const timespec& ts)
{
    return ts.tv_sec * 1000ll + ts.tv_nsec / 1000000ll;
}

uint32_t wf::get_current_time()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return wf::timespec_to_msec(ts);
}

wf::geometry_t wf::clamp(wf::geometry_t window, wf::geometry_t output)
{
    window.width  = wf::clamp(window.width, 0, output.width);
    window.height = wf::clamp(window.height, 0, output.height);

    window.x = wf::clamp(window.x,
        output.x, output.x + output.width - window.width);
    window.y = wf::clamp(window.y,
        output.y, output.y + output.height - window.height);

    return window;
}

static void handle_wrapped_listener(wl_listener *listener, void *data)
{
    wf::wl_listener_wrapper::wrapper *wrap =
        wl_container_of(listener, wrap, listener);
    wrap->self->emit(data);
}

static void handle_idle_listener(void *data)
{
    auto call = (wf::wl_idle_call*)(data);
    call->execute();
}

static int handle_timeout(void *data)
{
    auto timer = (wf::wl_timer*)(data);
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
    {
        return false;
    }

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
    {
        this->call(data);
    }
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
    {
        return;
    }

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
    {
        return;
    }

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
    {
        call();
    }
}

wl_timer::~wl_timer()
{
    if (source)
    {
        wl_event_source_remove(source);
    }
}

void wl_timer::set_timeout(uint32_t timeout_ms, callback_t call)
{
    if (timeout_ms == 0)
    {
        disconnect();
        call();
        return;
    }

    this->call    = call;
    this->timeout = timeout_ms;
    if (!source)
    {
        source = wl_event_loop_add_timer(get_core().ev_loop, handle_timeout, this);
    }

    wl_event_source_timer_update(source, timeout_ms);
}

void wl_timer::disconnect()
{
    if (source)
    {
        wl_event_source_remove(source);
    }

    source = NULL;
}

bool wl_timer::is_connected()
{
    return source != NULL;
}

void wl_timer::execute()
{
    if (call)
    {
        bool repeat = call();
        if (repeat)
        {
            wl_event_source_timer_update(source, this->timeout);
        } else
        {
            disconnect();
        }
    }
}
} // namespace wf
