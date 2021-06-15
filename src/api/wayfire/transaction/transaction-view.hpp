#pragma once
#include <cstdint>
#include <vector>
#include <wayfire/transaction/instruction.hpp>

namespace wf
{
/**
 * view_state_t describes a state of a view.
 *
 * All state changes are done via transactions.
 */
struct view_state_t
{
    /** Indicates the view's tiled edges. */
    uint32_t tiled_edges = 0;

    /** Indicates whether the view is fullscreen. */
    bool fullscreen = false;
};

namespace txn
{
class transaction_t;

/**
 * A view_transaction_t is an abstract factory class used to create instructions
 * for concrete implentations of views.
 *
 * Each view implementation typically has its own mechanisms of applying and
 * waiting on instructions. Because of that, instructions cannot be implemented
 * without knowledge of these implementation details. Instead, each view
 * implementation is required to provide a factory which implements the
 * view_transaction_t interface. It can be used to generate and batch together
 * instructions for the particular view implementation.
 */
class view_transaction_t
{
  public:
    /**
     * Add an instruction for setting the fullscreen state of a view.
     */
    virtual view_transaction_t& set_fullscreen(bool state) = 0;

    /**
     * Add an instruction for setting the tiled egdes of a view.
     */
    virtual view_transaction_t& set_tiled(uint32_t edges) = 0;

    /**
     * Schedule all batched instructions in the given transaction.
     */
    virtual void schedule_in(
        nonstd::observer_ptr<txn::transaction_t> transaction) = 0;

    /**
     * Convenience function for creating a new transaction, scheduling all
     * instructions there and submitting the transaction to core.
     *
     * @return The new transaction ID.
     */
    virtual uint64_t submit();

    virtual ~view_transaction_t() = default;
};
}
}
