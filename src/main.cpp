#include "util.hpp"

#include <ginseng/ginseng.hpp>
#include <sushi/sushi.hpp>
#include <raspberry/raspberry.hpp>
#include <soloud.h>
#include <soloud_wavstream.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <chrono>

int main() try {
    auto window = sushi::window(800, 600, "Ludum Dare 32", false);
    auto shader = sushi::link_program({
        sushi::compile_shader_file(sushi::shader_type::VERTEX, "assets/shaders/vertex.glsl"),
        sushi::compile_shader_file(sushi::shader_type::FRAGMENT, "assets/shaders/fragment.glsl")
    });

    SoLoud::Soloud soloud;
    soloud.init();
    SCOPE_EXIT {soloud.deinit();};

    SoLoud::WavStream ambiance;
    ambiance.load("assets/music/ambiance.ogg");
    ambiance.setLooping(true);

    soloud.play(ambiance);

    auto halltex = sushi::load_texture_2d("assets/textures/hallway.png", false, false);
    auto hallobj = sushi::load_static_mesh_file("assets/models/hallway.obj");

    auto speed = 3.f;
    auto pos = 0.f;
    auto target_rot = glm::quat();
    auto cur_rot = target_rot;
    auto rot_time = 1.f;
    auto get_rot = [&](double delta){
        if (rot_time >= 1.f) {
            return target_rot;
        }
        rot_time += delta;
        return mix(cur_rot, target_rot, rot_time);
    };
    auto rot = [&](float degs){
        cur_rot = get_rot(0);
        rot_time = 0;
        target_rot = rotate(target_rot, glm::radians(degs), {0.f,1.f,0.f});
    };

    using clock = std::chrono::high_resolution_clock;
    auto last_tick = clock::now();

    window.main_loop([&]{
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        auto this_tick = clock::now();
        auto delta = std::chrono::duration<double>(this_tick-last_tick).count();
        last_tick = this_tick;

        sushi::set_program(shader);

        auto proj_mat = glm::perspective(glm::radians(90.f), 4.f / 3.f, 0.01f, 50.f);
        auto view_mat = glm::translate(glm::mat4(1.f), glm::vec3{0.f, 0.f, 0.f});
        auto model_mat = glm::mat4(1.f);

        sushi::set_uniform(shader, "Texture", 0);
        sushi::set_texture(0, halltex);

        if (window.is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_UP})) {
            pos += delta * speed;
        }
        if (window.is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_DOWN})) {
            pos -= delta * speed;
        }

        if (window.was_pressed(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_LEFT})) {
            rot(-60);
        }
        if (window.was_pressed(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_RIGHT})) {
            rot(60);
        }

        view_mat = mat4_cast(get_rot(delta)) * view_mat;
        view_mat = glm::translate(view_mat, {0.f, 0.f, pos});

        for (int i=0; i<10; ++i) {
            auto mvp = proj_mat * view_mat * model_mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::draw_mesh(hallobj);
            model_mat = glm::translate(model_mat, {0.f, 0.f, -2.f});
        }
        auto wheyat = model_mat;
        model_mat = glm::translate(model_mat, {0.f, 0.f, 1.f});
        model_mat = glm::rotate(model_mat, glm::radians(30.f), {0.f,1.f,0.f});
        model_mat = glm::translate(model_mat, {0.f, 0.f, -1.f});
        model_mat = glm::rotate(model_mat, glm::radians(30.f), {0.f,1.f,0.f});
        model_mat = glm::translate(model_mat, {0.f, 0.f, -1.f});
        for (int i=0; i<10; ++i) {
            auto mvp = proj_mat * view_mat * model_mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::draw_mesh(hallobj);
            model_mat = glm::translate(model_mat, {0.f, 0.f, -2.f});
        }
        model_mat = wheyat;
        model_mat = glm::translate(model_mat, {0.f, 0.f, 1.f});
        model_mat = glm::rotate(model_mat, glm::radians(-30.f), {0.f,1.f,0.f});
        model_mat = glm::translate(model_mat, {0.f, 0.f, -1.f});
        model_mat = glm::rotate(model_mat, glm::radians(-30.f), {0.f,1.f,0.f});
        model_mat = glm::translate(model_mat, {0.f, 0.f, -1.f});
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
