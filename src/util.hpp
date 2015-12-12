//
// Created by dbral on 12/12/2015.
//

#ifndef LD34_UTIL_HPP
#define LD34_UTIL_HPP

#include <utility>

struct _scope_exit {
    template <typename T>
    struct _scope_exit_impl {
        T func;
        bool active;
        _scope_exit_impl(T&& func) : func(std::forward<T>(func)), active(true) {}
        _scope_exit_impl(const _scope_exit_impl&) = delete;
        _scope_exit_impl(_scope_exit_impl&& other) noexcept : func(other.func), active(std::exchange(other.active,false)) {}
        _scope_exit_impl& operator=(const _scope_exit_impl&) = delete;
        _scope_exit_impl& operator=(_scope_exit_impl&&) = delete;
        ~_scope_exit_impl() { if (active) func(); }
    };
    template <typename T>
    _scope_exit_impl<T> operator+(T&& t) const { return _scope_exit_impl<T>(std::forward<T>(t)); }
};
#define CAT_IMPL(A,B) A##B
#define CAT(A,B) CAT_IMPL(A,B)
#define SCOPE_EXIT auto CAT(_scope_exit_,__LINE__) = _scope_exit{} + [&]()

#endif //LD34_UTIL_HPP
