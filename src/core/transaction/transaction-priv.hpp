#pragma once

#include <map>
#include <wayfire/transaction/transaction.hpp>

namespace wf
{
namespace txn
{
class transaction_impl_t;
using transaction_iuptr_t = std::unique_ptr<transaction_impl_t>;

class transaction_manager_t::impl
{
  public:

  private:
    /**
     * The single pending transaction, if it exists.
     * Otherwise, nullptr.
     */
    transaction_iuptr_t pending;

    /**
     * A list of committed transactions.
     */
    std::vector<transaction_iuptr_t> committed;
};

/**
 * Emits done/cancel if an instruction does so.
 */
class transaction_impl_t : public transaction_t, public signal_provider_t
{
  public:
    /** Commit all instructions in the transaction. */
    void commit()
    {}

    /**
     * Move instructions from the other transaction to this,
     * thereby destroying the other transaction.
     */
    void merge(transaction_iuptr_t other)
    {}

    /**
     * Test whether instructions collide with each other (i.e have instructions
     * for the same objects).
     */
    bool does_collide(const transaction_iuptr_t& other) const
    {}

    void add_instruction(const std::string& object,
        instruction_uptr_t instr) override
    {}

    std::vector<std::string> get_objects() override
    {}

    std::vector<wayfire_view> get_views() override
    {}

  private:
    std::map<std::string, std::vector<instruction_uptr_t>> instructions;
};
}
}
