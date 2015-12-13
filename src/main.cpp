#include "util.hpp"

#include <ginseng/ginseng.hpp>
#include <sushi/sushi.hpp>
#include <raspberry/raspberry.hpp>
#include <soloud.h>
#include <soloud_wavstream.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <boost/variant.hpp>

#include <cstdlib>
#include <cmath>
#include <iostream>
#include <chrono>
#include <random>
#include <array>

using PRNG = std::mt19937_64;
static auto rng = []{
    auto data = std::array<PRNG::result_type,PRNG::state_size>();
    std::random_device seeder;
    std::generate(begin(data),end(data),std::ref(seeder));
    auto sseq = std::seed_seq(begin(data),end(data));
    return PRNG(sseq);
}();

struct Nothing {};

struct Treasure {
};

struct Hallway {
    int len;
    boost::variant<Nothing,Treasure> inhabitant;
    std::shared_ptr<Hallway> left;
    std::shared_ptr<Hallway> right;
};

static const auto default_hall = Hallway{3, {}, nullptr, nullptr};

static auto flicker_dist = std::uniform_real_distribution<float>(-.05, .05);

struct LightSource {
    float bright_radius;
    float dim_radius;
    float bright_flicker = 0.f;
    float dim_flicker = 0.f;
    float flicker_timer = 0.f;

    LightSource(float bright_radius, float dim_radius) : bright_radius(bright_radius), dim_radius(dim_radius) {}

    void flicker(double delta) {
        flicker_timer += delta;
        if (flicker_timer >= 0.1) {
            bright_flicker = flicker_dist(rng);
            dim_flicker = flicker_dist(rng);
            flicker_timer = 0;
        }
    }
};

static auto get_rot_mat(float deg) {
    auto rv = glm::mat4(1.f);
    rv = glm::translate(rv, {0.f, 0.f, 1.f});
    rv = glm::rotate(rv, glm::radians(deg), {0.f,1.f,0.f});
    rv = glm::translate(rv, {0.f, 0.f, -1.f});
    rv = glm::rotate(rv, glm::radians(deg), {0.f,1.f,0.f});
    rv = glm::translate(rv, {0.f, 0.f, -1.f});
    return rv;
}

static const auto rot_left_mat = get_rot_mat(30.f);
static const auto rot_right_mat = get_rot_mat(-30.f);

template <typename R, typename T, typename... Ts>
struct Overloaded : T, Overloaded<R, Ts...> {
    using T::operator();
    using Overloaded<R, Ts...>::operator();
    Overloaded(T&& t, Ts&&... ts) : T(std::forward<T>(t)), Overloaded<R, Ts...>(std::forward<Ts>(ts)...) {}
};

template <typename R, typename T>
struct Overloaded<R,T> : T, boost::static_visitor<R> {
    using T::operator();
    Overloaded(T&& t) : T(std::forward<T>(t)), boost::static_visitor<R>() {}
};

template <typename R, typename... Ts>
Overloaded<R,Ts...> overload(Ts&&... ts) {
    return Overloaded<R,Ts...>(std::forward<Ts>(ts)...);
}

struct Game {
    static constexpr auto player_speed = 3.f;

    void (Game::*cur_state)(double);

    glm::vec3 player_pos = {0.f, 0.f, 0.f};
    glm::quat player_rot = glm::quat();

    sushi::texture_2d halltex = sushi::load_texture_2d("assets/textures/hallway.png", false, false);
    sushi::static_mesh hallobj = sushi::load_static_mesh_file("assets/models/hallway.obj");
    sushi::static_mesh juncobj = sushi::load_static_mesh_file("assets/models/junction.obj");
    sushi::unique_program shader = sushi::link_program({
        sushi::compile_shader_file(sushi::shader_type::VERTEX, "assets/shaders/vertex.glsl"),
        sushi::compile_shader_file(sushi::shader_type::FRAGMENT, "assets/shaders/fragment.glsl")
    });

    std::shared_ptr<Hallway> cur_hall = std::make_shared<Hallway>(default_hall);

    glm::mat4 proj_mat = glm::perspectiveFov(glm::radians(120.f), 4.f, 3.f, 0.01f, 50.f);
    glm::mat4 view_mat;

    sushi::window* window;

    LightSource lamp = LightSource(2.5, 3);

    Game(sushi::window* window) : cur_state(state_moving), window(window) {}

    glm::mat4 draw_hallway(const Hallway& hall, glm::mat4 model_mat) {
        for (int i=0; i<hall.len; ++i) {
            auto mvp = proj_mat * view_mat * model_mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::set_uniform(shader, "ModelMat", model_mat);
            sushi::set_uniform(shader, "ViewMat", view_mat);
            sushi::draw_mesh(hallobj);
            model_mat = glm::translate(model_mat, {0.f, 0.f, -2.f});
        }
        auto mvp = proj_mat * view_mat * model_mat;
        sushi::set_uniform(shader, "MVP", mvp);
        sushi::set_uniform(shader, "ModelMat", model_mat);
        sushi::set_uniform(shader, "ViewMat", view_mat);
        sushi::draw_mesh(juncobj);
        return model_mat;
    };

    void draw_hallway_full(const Hallway& hall, glm::mat4 model_mat) {
        model_mat = draw_hallway(hall, model_mat);
        if (hall.left) {
            auto tmp_model_mat = model_mat * rot_left_mat;
            draw_hallway_full(*hall.left, tmp_model_mat);
        } else {
            auto tmp_model_mat = model_mat * rot_left_mat;
            draw_hallway(default_hall, tmp_model_mat);
        }
        if (hall.right) {
            auto tmp_model_mat = model_mat * rot_right_mat;
            draw_hallway_full(*hall.right, tmp_model_mat);
        } else {
            auto tmp_model_mat = model_mat * rot_right_mat;
            draw_hallway(default_hall, tmp_model_mat);
        }
        boost::apply_visitor(overload<void>(
            [&](const Nothing&){},
            [&](const Treasure&){}
        ), hall.inhabitant);
    }

    void main_loop(double delta) {
        sushi::set_program(shader);

        sushi::set_uniform(shader, "Texture", 0);
        sushi::set_texture(0, halltex);

        if (window->is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_F1})) {
            sushi::set_uniform(shader, "FullBright", 1);
        } else {
            sushi::set_uniform(shader, "FullBright", 0);
        }

        lamp.flicker(delta);
        sushi::set_uniform(shader, "BrightRadius", lamp.bright_radius + lamp.bright_flicker);
        sushi::set_uniform(shader, "DimRadius", lamp.dim_radius + lamp.dim_flicker);

        (this->*cur_state)(delta);
    }

    void state_moving(double delta) {
        auto until_stop = cur_hall->len * 2.f - 2.f - player_pos.z;
        auto step_size = delta * player_speed;

        if (until_stop < step_size) {
            player_pos.z += until_stop;
            cur_state = state_event;
        } else {
            player_pos.z += step_size;
        }

        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }

    void state_event(double delta) {
        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }
};

int main() try {
    std::clog << "Opening window..." << std::endl;
    auto window = sushi::window(800, 600, "Ludum Dare 32", false);

    std::clog << "Initializing audio..." << std::endl;
    SoLoud::Soloud soloud;
    soloud.init();
    SCOPE_EXIT {soloud.deinit();};

    std::clog << "Loading ambiance..." << std::endl;
    SoLoud::WavStream ambiance;
    ambiance.load("assets/music/ambiance.ogg");
    ambiance.setLooping(true);

    soloud.play(ambiance);

    std::clog << "Creating Game..." << std::endl;
    auto game = Game(&window);

    using clock = std::chrono::high_resolution_clock;
    auto last_tick = clock::now();

    std::clog << "Starting main loop..." << std::endl;
    window.main_loop([&]{
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        auto this_tick = clock::now();
        auto delta = std::chrono::duration<double>(this_tick-last_tick).count();
        last_tick = this_tick;

        game.main_loop(delta);
    });

    std::clog << "Ending without problem..." << std::endl;

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
}
