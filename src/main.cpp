#include <ginseng/ginseng.hpp>
#include <sushi/sushi.hpp>
#include <raspberry/raspberry.hpp>
#include <soloud.h>

#include <cstdlib>
#include <iostream>
#include <chrono>

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

    SoLoud::Soloud soloud;
    soloud.init();
    SCOPE_EXIT {soloud.deinit();};

    auto halltex = sushi::load_texture_2d("assets/textures/hallway.png", false, false);
    auto hallobj = sushi::load_static_mesh_file("assets/models/hallway.obj");

    auto pos = 0.f;

    using clock = std::chrono::high_resolution_clock;
    auto last_tick = clock::now();

    window.main_loop([&]{
        auto this_tick = clock::now();
        auto delta = std::chrono::duration<double>(this_tick-last_tick).count();
        last_tick = this_tick;

        sushi::set_program(shader);

        auto proj_mat = glm::perspective(90.f, 4.f / 3.f, 0.01f, 100.f);
        auto view_mat = glm::translate(glm::mat4(1.f), glm::vec3{0.f, 0.f, 0.f});
        auto model_mat = glm::mat4(1.f);

        sushi::set_uniform(shader, "Texture", 0);
        sushi::set_texture(0, halltex);

        if (window.is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_UP})) {
            pos += delta;
        }
        if (window.is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_DOWN})) {
            pos -= delta;
        }

        model_mat = glm::translate(model_mat, {0.f, 0.f, pos});

        for (int i=0; i<10; ++i) {
            auto mvp = proj_mat * view_mat * model_mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::draw_mesh(hallobj);
            model_mat = glm::translate(model_mat, {0.f, 0.f, -2.f});
        }
    });

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
}
