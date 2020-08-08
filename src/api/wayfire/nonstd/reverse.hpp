#ifndef WF_REVERSE_HPP
#define WF_REVERSE_HPP

#include <iterator>

/* from https://stackoverflow.com/questions/8542591/c11-reverse-range-based-for-loop
 * */
namespace wf
{
template<typename T>
struct reversion_wrapper
{
    T& iterable;
};

template<typename T>
auto begin(reversion_wrapper<T> w)
{
    return std::rbegin(w.iterable);
}

template<typename T>
auto end(reversion_wrapper<T> w)
{
    return std::rend(w.iterable);
}

template<typename T>
reversion_wrapper<T> reverse(T&& iterable)
{
    return {iterable};
}
}

#endif /* end of include guard: WF_REVERSE_HPP */
