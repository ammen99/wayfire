#include "bindings-repository.hpp"
#include <algorithm>

bool wf::bindings_repository_t::handle_key(const wf::keybinding_t& pressed)
{
    std::vector<std::function<bool()>> callbacks;
    for (auto& binding : this->keys)
    {
        if (binding->activated_by->get_value() == pressed)
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([pressed, callback] ()
            {
                return (*callback)(pressed.get_key());
            });
        }
    }

    for (auto& binding : this->activators)
    {
        if (binding->activated_by->get_value().has_match(pressed))
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([pressed, callback] ()
            {
                return (*callback)(ACTIVATOR_SOURCE_KEYBINDING, pressed.get_key());
            });
        }
    }

    bool handled = false;
    for (auto& cb : callbacks)
    {
        handled |= cb();
    }

    return handled;
}

bool wf::bindings_repository_t::handle_axis(uint32_t modifiers,
    wlr_event_pointer_axis *ev)
{
    std::vector<wf::axis_callback*> callbacks;

    for (auto& binding : this->axes)
    {
        if (binding->activated_by->get_value() == wf::keybinding_t{modifiers, 0})
        {
            callbacks.push_back(binding->callback);
        }
    }

    for (auto call : callbacks)
    {
        (*call)(ev);
    }

    return !callbacks.empty();
}

bool wf::bindings_repository_t::handle_button(const wf::buttonbinding_t& pressed,
    const wf::pointf_t& cursor)
{
    std::vector<std::function<bool()>> callbacks;
    for (auto& binding : this->buttons)
    {
        if (binding->activated_by->get_value() == pressed)
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([=] ()
            {
                return (*callback)(pressed.get_button(), cursor.x, cursor.y);
            });
        }
    }

    for (auto& binding : this->activators)
    {
        if (binding->activated_by->get_value().has_match(pressed))
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([=] ()
            {
                return (*callback)(wf::ACTIVATOR_SOURCE_BUTTONBINDING,
                    pressed.get_button());
            });
        }
    }

    bool binding_handled = false;
    for (auto call : callbacks)
    {
        binding_handled |= call();
    }

    return binding_handled;
}

void wf::bindings_repository_t::handle_gesture(const wf::touchgesture_t& gesture)
{
    std::vector<std::function<void()>> callbacks;
    for (auto& binding : this->activators)
    {
        if (binding->activated_by->get_value().has_match(gesture))
        {
            /* We must be careful because the callback might be erased,
             * so force copy the callback into the lambda */
            auto callback = binding->callback;
            callbacks.emplace_back([=] ()
            {
                (*callback)(ACTIVATOR_SOURCE_GESTURE, 0);
            });
        }
    }

    for (auto& cb : callbacks)
    {
        cb();
    }
}

void wf::bindings_repository_t::rem_binding(void *callback)
{
    const auto& erase = [callback] (auto& container)
    {
        auto it = std::remove_if(container.begin(), container.end(),
            [callback] (const auto& ptr)
        {
            return ptr->callback == callback;
        });
        container.erase(it, container.end());
    };

    erase(keys);
    erase(buttons);
    erase(axes);
    erase(activators);
}

void wf::bindings_repository_t::rem_binding(binding_t *binding)
{
    const auto& erase = [binding] (auto& container)
    {
        auto it = std::remove_if(container.begin(), container.end(),
            [binding] (const auto& ptr)
        {
            return ptr.get() == binding;
        });
        container.erase(it, container.end());
    };

    erase(keys);
    erase(buttons);
    erase(axes);
    erase(activators);
}
