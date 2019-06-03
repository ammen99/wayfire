#include "object.hpp"
#include "nonstd/safe-list.hpp"
#include <unordered_map>

class wf::signal_provider_t::sprovider_impl
{
  public:
    std::unordered_map<std::string,
        wf::safe_list_t<signal_callback_t*>> signals;
};

wf::signal_provider_t::signal_provider_t()
{
    this->sprovider_priv = std::make_unique<sprovider_impl> ();
}

wf::signal_provider_t::~signal_provider_t()
{
}

void wf::signal_provider_t::connect_signal(std::string name,
    signal_callback_t* callback)
{
    sprovider_priv->signals[name].push_back(callback);
}

/* Unregister a registered callback */
void wf::signal_provider_t::disconnect_signal(std::string name,
    signal_callback_t* callback)
{
    sprovider_priv->signals[name].remove_all(callback);
}

/* Emit the given signal. No type checking for data is required */
void wf::signal_provider_t::emit_signal(std::string name, wf::signal_data_t *data)
{
    sprovider_priv->signals[name].for_each([data] (auto call) {
        (*call) (data);
    });
}

class wf::object_base_t::obase_impl
{
  public:
    std::unordered_map<std::string, std::unique_ptr<custom_data_t>> data;
    uint32_t object_id;
};

wf::object_base_t::object_base_t()
{
    this->obase_priv = std::make_unique<obase_impl>();

    static uint32_t global_id = 0;
    obase_priv->object_id = global_id++;
}

wf::object_base_t::~object_base_t()
{
}

std::string wf::object_base_t::to_string() const
{
    return std::to_string(get_id());
}

uint32_t wf::object_base_t::get_id() const
{
    return obase_priv->object_id;
}

bool wf::object_base_t::has_data(std::string name)
{
    return obase_priv->data.count(name);
}

void wf::object_base_t::erase_data(std::string name)
{
    obase_priv->data.erase(name);
}

wf::custom_data_t *wf::object_base_t::_fetch_data(std::string name)
{
    return obase_priv->data[name].get();
}

wf::custom_data_t *wf::object_base_t::_fetch_erase(std::string name)
{
    auto data = obase_priv->data[name].release();
    erase_data(name);

    return data;
}

void wf::object_base_t::_store_data(std::unique_ptr<wf::custom_data_t> data,
    std::string name)
{
    obase_priv->data[name] = std::move(data);
}
