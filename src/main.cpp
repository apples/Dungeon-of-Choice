#include <ginseng/ginseng.hpp>
#include <sushi/sushi.hpp>
#include <raspberry/raspberry.hpp>
#include <soloud.h>

#include <cstdlib>
#include <iostream>

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

int main() try {
    auto window = sushi::window(800, 600, "Ludum Dare 32", false);
    auto shader = sushi::link_program({
        sushi::compile_shader_file(sushi::shader_type::VERTEX, "assets/shaders/vertex.glsl"),
        sushi::compile_shader_file(sushi::shader_type::FRAGMENT, "assets/shaders/fragment.glsl")
    });

    sushi::set_program(shader);

    SoLoud::Soloud soloud;
    soloud.init();
    SCOPE_EXIT {soloud.deinit();};

    window.main_loop([&]{

    });

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
}
