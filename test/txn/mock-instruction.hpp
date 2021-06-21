#include <wayfire/transaction/instruction.hpp>

class mock_instruction_t : wf::txn::instruction_t
{
  public:
    int pending   = 0;
    int committed = 0;
    int applied   = 0;

    void set_pending() override
    {
        ++pending;
    }

    void commit() override
    {
        ++applied;
    }

    void apply() override
    {
        ++committed;
    }

    void send_done()
    {
        wf::txn::instruction_done_signal data;
        data.instruction = {this};
        this->emit_signal("done", &data);
    }
};
