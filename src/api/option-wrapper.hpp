#pragma once

#include <nonstd/noncopyable.hpp>
#include <wayfire/config/option.hpp>
#include <core.hpp>

namespace wf
{
template<class T>
    using option_sptr_t = std::shared_ptr<wf::config::option_t<T>>;

/**
 * Create an option which has a static value.
 */
template<class T>
    option_sptr_t<T> create_option(T value)
{
    return std::make_shared<wf::config::option_t<T>> ("Static", value);
}

/**
 * Create an option which has a static value,
 * from the given string description.
 */
template<class T>
    option_sptr_t<T> create_option_string(const std::string& value)
{
    return std::make_shared<wf::config::option_t<T>> ("Static",
        wf::option_type::from_string<T> (value).value());
}

/**
 * A simple wrapper around a config option.
 */
template<class Type>
class option_wrapper_t : public noncopyable_t
{
  public:
    /**
     * Initialize the option wrapper.
     */
    option_wrapper_t()
    {
        option_update_listener = [=] () {
            if (this->on_update)
                this->on_update();
        };
    }

    /**
     * Initialize the option wrapper and directly load the given option.
     */
    option_wrapper_t(const std::string& option_name)
        : option_wrapper_t()
    {
        load_option(option_name);
    }

    /**
     * Load the option with the given name from the config.
     * @throws logic_error if the option wrapper already has an option loaded.
     * @throws runtime_error if the given option does not exist or does not
     *   match the type of the option wrapper.
     */
    void load_option(const std::string& name)
    {
        if (raw_option)
        {
            throw std::logic_error(
                "Loading an option into option wrapper twice!");
        }

        auto untyped_option = wf::get_core().config.get_option(name);
        if (untyped_option == nullptr)
            throw std::runtime_error("No such option: " + std::string(name));

        raw_option = std::dynamic_pointer_cast<wf::config::option_t<Type>>(
            untyped_option);
        if (raw_option == nullptr)
            throw std::runtime_error("Bad option type: " + std::string(name));


        raw_option->add_updated_handler(&option_update_listener);
    }

    ~option_wrapper_t()
    {
        if (raw_option)
            raw_option->rem_updated_handler(&option_update_listener);
    }

    /** Implicitly convertible to the value of the option */
    operator Type() const
    {
        return raw_option->get_value();
    }

    operator option_sptr_t<Type>() const
    {
        return raw_option;
    }

    /** Set a callback to execute when the option value changes. */
    void set_callback(std::function<void()> callback)
    {
        this->on_update = callback;
    }

    /** The actual option wrapped by the option wrapper */
    option_sptr_t<Type> raw_option;

  private:
    std::function<void()> on_update;
    wf::config::option_base_t::updated_callback_t option_update_listener;
};
}
