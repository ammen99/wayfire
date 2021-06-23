#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "../src/core/transaction/transaction-priv.hpp"
#include "mock-instruction.hpp"
#include "../mock-core.hpp"

using namespace wf::txn;

TEST_CASE("Transaction Impl Basics")
{
    // Register a fake view a
    mock_core().fake_views["a"] =
        wayfire_view((wf::view_interface_t*)0x1234);

    auto tx_pub = transaction_t::create();
    auto tx_ab  = dynamic_cast<transaction_impl_t*>(tx_pub.get());
    REQUIRE(tx_ab != nullptr);

    auto i1 = new mock_instruction_t("a");
    auto i2 = new mock_instruction_t("b");
    auto i3 = new mock_instruction_t("b");

    tx_ab->add_instruction(instruction_uptr_t(i1));
    tx_ab->add_instruction(instruction_uptr_t(i2));
    tx_ab->add_instruction(instruction_uptr_t(i3));

    REQUIRE(tx_ab->get_objects() == std::set<std::string>{"a", "b"});
    REQUIRE(tx_ab->get_views() ==
        std::set<wayfire_view>{mock_core().fake_views["a"]});

    for (auto i : {i1, i2, i3})
    {
        REQUIRE(i->pending == 0);
        REQUIRE(i->committed == 0);
        REQUIRE(i->applied == 0);
    }

    auto tx_c = transaction_t::create();
    tx_c->add_instruction(mock_instruction_t::get("c"));

    auto tx_a = transaction_t::create();
    tx_a->add_instruction(mock_instruction_t::get("a"));

    REQUIRE(tx_ab->does_intersect(dynamic_cast<transaction_impl_t&>(*tx_a)));
    REQUIRE_FALSE(tx_ab->does_intersect(dynamic_cast<transaction_impl_t&>(*tx_c)));
}
