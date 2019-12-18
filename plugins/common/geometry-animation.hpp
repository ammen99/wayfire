#include <option-wrapper.hpp>
#include <wayfire/util/duration.hpp>

namespace wf
{
using namespace wf::animation;
class geometry_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;

    timed_transition_t x{*this};
    timed_transition_t y{*this};
    timed_transition_t width{*this};
    timed_transition_t height{*this};

    void set_start(wf_geometry geometry)
    {
        copy_fields(geometry, &timed_transition_t::start);
    }

    void set_end(wf_geometry geometry)
    {
        copy_fields(geometry, &timed_transition_t::end);
    }

    operator wf_geometry() const
    {
        return { (int)x, (int)y, (int)width, (int)height };
    }

  protected:
    void copy_fields(wf_geometry geometry, double timed_transition_t::* member)
    {
        this->x.*member = geometry.x;
        this->y.*member = geometry.y;
        this->width.*member = geometry.width;
        this->height.*member = geometry.height;
    }
};
}
