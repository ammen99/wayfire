#include <wayfire/transaction/instruction.hpp>
#include <doctest/doctest.h>
#include <optional>

class mock_instruction_t : public wf::txn::instruction_t
{
  public:
    static inline wf::txn::instruction_uptr_t get(std::string object)
    {
        return std::make_unique<mock_instruction_t>(object);
    }

    int *cnt_destroy = nullptr;
    std::optional<int> require_destroy_on_commit;

    std::string object;
    mock_instruction_t(std::string object = "")
    {
        this->object = object;
    }

    ~mock_instruction_t()
    {
        if (cnt_destroy)
        {
            (*cnt_destroy)++;
        }
    }

    int pending   = 0;
    int committed = 0;
    int applied   = 0;

    std::string get_object() override
    {
        return object;
    }

    void set_pending() override
    {
        ++pending;
    }

    void commit() override
    {
        if (require_destroy_on_commit.has_value() &&
            cnt_destroy)
        {
            REQUIRE(*cnt_destroy == require_destroy_on_commit);
        }

        ++committed;
    }

    void apply() override
    {
        ++applied;
    }

    void send_ready()
    {
        wf::txn::instruction_ready_signal data;
        data.instruction = {this};
        this->emit_signal("ready", &data);
    }

    void send_cancel()
    {
        wf::txn::instruction_cancel_signal data;
        data.instruction = {this};
        this->emit_signal("cancel", &data);
    }
};
