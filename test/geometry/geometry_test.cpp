#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <wayfire/geometry.hpp>

TEST_CASE("Point addition")
{
    wf::point_t a = {1, 2};
    wf::point_t b = {3, 4};

    using namespace wf;
    REQUIRE_EQ(a + b, wf::point_t{4, 6});
}
