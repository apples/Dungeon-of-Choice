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
#include <thread>

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

struct Baddy {};

struct Hallway {
    int len;
    boost::variant<Nothing,Treasure,Baddy> inhabitant;
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
    static constexpr auto player_speed = 2.f;
    static constexpr auto battle_speed = 7.5f;

    using State = void(Game::*)(double);
    State cur_state;

    int player_health = 3;
    int difficulty = 1;

    glm::vec3 player_pos = {0.f, 0.f, 0.f};
    glm::quat player_rot = glm::quat();

    sushi::texture_2d halltex = sushi::load_texture_2d("assets/textures/hallway.png", false, false);
    sushi::static_mesh hallobj = sushi::load_static_mesh_file("assets/models/hallway.obj");
    sushi::static_mesh juncobj = sushi::load_static_mesh_file("assets/models/junction.obj");
    sushi::unique_program shader = sushi::link_program({
        sushi::compile_shader_file(sushi::shader_type::VERTEX, "assets/shaders/vertex.glsl"),
        sushi::compile_shader_file(sushi::shader_type::FRAGMENT, "assets/shaders/fragment.glsl")
    });

    sushi::static_mesh spriteobj = sushi::load_static_mesh_data(
        {{-1,1,0},{1,1,0},{-1,-1,0},{1,-1,0}},
        {{0,0,1}},
        {{0,0},{1,0},{0,1},{1,1}},
        {{{{0,0,0},{1,0,1},{2,0,2}}},{{{2,0,2},{1,0,1},{3,0,3}}}}
    );

    sushi::static_mesh treasureobj = sushi::load_static_mesh_file("assets/models/treasure.obj");
    sushi::texture_2d treasuretex = sushi::load_texture_2d("assets/textures/treasure.png", false, false);

    sushi::texture_2d baddytex = sushi::load_texture_2d("assets/textures/baddy.png", false, false);
    sushi::texture_2d hearttex = sushi::load_texture_2d("assets/textures/heart.png", false, false);
    sushi::texture_2d battletex = sushi::load_texture_2d("assets/textures/battle.png", false, false);
    sushi::texture_2d playertex = sushi::load_texture_2d("assets/textures/player.png", false, false);
    sushi::texture_2d daggertex = sushi::load_texture_2d("assets/textures/dagger.png", false, false);

    std::shared_ptr<Hallway> cur_hall = make_random_hall();

    glm::mat4 proj_mat;
    glm::mat4 view_mat;

    sushi::window* window;

    LightSource lamp = LightSource(2.5, 5);

    struct BaddyState {
        struct Bullet {
            glm::vec2 pos;
            bool alive = true;
        };
        std::vector<Bullet> bullets = {};
        glm::vec2 player_pos = {0.f,-7.f};
    };

    std::shared_ptr<BaddyState> baddy;

    int winwidth;
    int winheight;

    Game(sushi::window* window) : cur_state(state_moving), window(window) {
        cur_hall->left = make_random_hall();
        cur_hall->right = make_random_hall();

        winwidth = window->width();
        winheight = window->height();
    }

    glm::mat4 draw_hallway(const Hallway& hall, glm::mat4 model_mat) {
        sushi::set_texture(0, halltex);
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
            [&](const Treasure&){
                auto treasure_mat = glm::translate(model_mat, {0.f, 0.f, 0.42265});
                auto mvp = proj_mat * view_mat * treasure_mat;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", treasure_mat);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                sushi::set_texture(0, treasuretex);
                sushi::draw_mesh(treasureobj);
            },
            [&](const Baddy&){
                auto treasure_mat = glm::translate(model_mat, {0.f, 0.f, 0.42265});
                auto mvp = proj_mat * view_mat * treasure_mat;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", treasure_mat);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                sushi::set_texture(0, baddytex);
                sushi::draw_mesh(spriteobj);
            }
        ), hall.inhabitant);
    }

    void main_loop(double delta) {
        sushi::set_program(shader);

        sushi::set_uniform(shader, "Texture", 0);

        if (window->is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_F1})) {
            sushi::set_uniform(shader, "FullBright", 1);
        } else {
            sushi::set_uniform(shader, "FullBright", 0);
        }

        lamp.flicker(delta);
        sushi::set_uniform(shader, "BrightRadius", lamp.bright_radius + lamp.bright_flicker);
        sushi::set_uniform(shader, "DimRadius", lamp.dim_radius + lamp.dim_flicker);

        if (player_health <= 0) {
            cur_state = state_lose;
        }

        proj_mat = glm::perspectiveFov(glm::radians(120.f), float(winwidth), float(winheight), 0.01f, 50.f);
        (this->*cur_state)(delta);

        render_hud();
    }

    std::shared_ptr<Hallway> make_random_hall() {
        auto rv = std::make_shared<Hallway>();

        std::uniform_int_distribution<int> len_dist (1+difficulty/5,1+difficulty/5+2);
        rv->len = len_dist(rng);

        std::uniform_int_distribution<int> inhab_dist (0,2);
        switch (inhab_dist(rng)) {
            case 0:
                rv->inhabitant = Nothing{};
                break;
            case 1:
                rv->inhabitant = Treasure{};
                break;
            case 2:
                rv->inhabitant = Baddy{};
                break;
        }

        return rv;
    }

    void state_lose(double delta) {
        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }

    void state_moving(double delta) {
        auto until_stop = cur_hall->len * 2.f - 2.f - player_pos.z;
        auto step_size = delta * player_speed;

        if (until_stop < step_size) {
            player_pos.z += until_stop;
            cur_state = boost::apply_visitor(overload<State>(
                [&](const Nothing&){ return &state_tojunc; },
                [&](const Treasure&){ return &state_treasure; },
                [&](const Baddy&){ return &state_baddy; }
            ), cur_hall->inhabitant);
        } else {
            player_pos.z += step_size;
        }

        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }

    void state_tojunc(double delta) {
        auto until_stop = cur_hall->len * 2.f - 0.4226497f - player_pos.z;
        auto step_size = delta * player_speed;

        if (until_stop < step_size) {
            player_pos.z += until_stop;
            cur_state = state_whichway;
        } else {
            player_pos.z += step_size;
        }

        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }

    void state_turnleft(double delta) {
        auto until_stop = glm::radians(-60.f) - glm::yaw(player_rot);
        auto step_size = -float(delta * player_speed);

        if (until_stop > step_size) {
            player_rot *= glm::angleAxis(until_stop, glm::vec3{0.f, 1.f, 0.f});
            cur_hall = cur_hall->left;
            cur_hall->left = make_random_hall();
            cur_hall->right = make_random_hall();
            player_pos.z = -1.57735f;
            player_rot = glm::quat();
            cur_state = state_moving;
            ++difficulty;
        } else {
            player_rot *= glm::angleAxis(step_size, glm::vec3{0.f, 1.f, 0.f});
        }

        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);
        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }

    void state_turnright(double delta) {
        auto until_stop = glm::radians(60.f) - glm::yaw(player_rot);
        auto step_size = float(delta * player_speed);

        if (until_stop < step_size) {
            player_rot *= glm::angleAxis(until_stop, glm::vec3{0.f, 1.f, 0.f});
            cur_hall = cur_hall->right;
            cur_hall->left = make_random_hall();
            cur_hall->right = make_random_hall();
            player_pos.z = -1.57735f;
            player_rot = glm::quat();
            cur_state = state_moving;
            ++difficulty;
        } else {
            player_rot *= glm::angleAxis(step_size, glm::vec3{0.f, 1.f, 0.f});
        }
        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);
        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }

    void state_whichway(double delta) {
        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        if (window->was_pressed(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_LEFT})) {
            cur_state = state_turnleft;
        }
        if (window->was_pressed(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_RIGHT})) {
            cur_state = state_turnright;
        }

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }

    void state_treasure(double delta) {
        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        cur_hall->inhabitant = Nothing{};
        cur_state = state_tojunc;

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }

    void state_baddy(double delta) {
        std::uniform_real_distribution<float> spawn_dist (-7.f,7.f);
        if (!baddy) {
            baddy = std::make_shared<BaddyState>();
            for (int i=0; i<difficulty*3+1; ++i) {
                baddy->bullets.push_back({{spawn_dist(rng),7.f+2*i}});
            }
        }

        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);

        if (window->is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_LEFT})) {
            baddy->player_pos.x -= delta * battle_speed * difficulty / 5.f;
            if (baddy->player_pos.x < -7.f) {
                baddy->player_pos.x = -7.f;
            }
        }
        if (window->is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_RIGHT})) {
            baddy->player_pos.x += delta * battle_speed * difficulty / 5.f;
            if (baddy->player_pos.x > 7.f) {
                baddy->player_pos.x = 7.f;
            }
        }

        auto w = float(winwidth);
        auto h = float(winheight);
        proj_mat = glm::ortho(-w/2.f,w/2.f,-h/2.f,h/2.f,-1.f,1.f);
        glClear(GL_DEPTH_BUFFER_BIT);
        view_mat = glm::mat4();
        model_mat = glm::scale(glm::mat4(1.f), {256.f,256.f,1.f});
        sushi::set_uniform(shader, "FullBright", 1);
        auto mvp = proj_mat * view_mat * model_mat;
        sushi::set_uniform(shader, "MVP", mvp);
        sushi::set_uniform(shader, "ModelMat", model_mat);
        sushi::set_uniform(shader, "ViewMat", view_mat);
        sushi::set_texture(0, battletex);
        sushi::draw_mesh(spriteobj);

        model_mat = glm::scale(glm::mat4(1.f), {32.f,32.f,1.f});

        {
            auto mat = glm::translate(model_mat, {baddy->player_pos.x,baddy->player_pos.y,0.5});
            auto mvp = proj_mat * view_mat * mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::set_uniform(shader, "ModelMat", mat);
            sushi::set_uniform(shader, "ViewMat", view_mat);
            sushi::set_texture(0, playertex);
            sushi::draw_mesh(spriteobj);
        }

        for (auto& b : baddy->bullets) {
            b.pos.y -= delta * battle_speed * 2.f/3.f * difficulty / 5.f;
            auto mat = glm::translate(model_mat, {b.pos.x,b.pos.y,0.5});
            auto mvp = proj_mat * view_mat * mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::set_uniform(shader, "ModelMat", mat);
            sushi::set_uniform(shader, "ViewMat", view_mat);
            sushi::set_texture(0, daggertex);
            sushi::draw_mesh(spriteobj);

            if (glm::distance(b.pos, baddy->player_pos) < 0.9) {
                --player_health;
                b.alive = false;
            }

            if (b.pos.y <= -7.5f) {
                b.alive = false;
            }
        }

        baddy->bullets.erase(std::remove_if(baddy->bullets.begin(),baddy->bullets.end(),[](auto b){return !b.alive;}),baddy->bullets.end());

        if (baddy->bullets.empty()) {
            cur_state = state_tojunc;
            baddy = {};
            cur_hall->inhabitant = Nothing{};
        };
    }

    void render_hud() {
        proj_mat = glm::ortho(0.f,float(winwidth),float(winheight),0.f,-1.f,1.f);
        glClear(GL_DEPTH_BUFFER_BIT);
        view_mat = glm::mat4();
        auto model_mat = glm::scale(glm::mat4(1.f), {32.f,-32.f,1.f});
        model_mat = glm::translate(model_mat, {1.f,-1.f,0.f});
        sushi::set_uniform(shader, "FullBright", 1);
        for (int i=0; i<player_health; ++i) {
            auto mvp = proj_mat * view_mat * model_mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::set_uniform(shader, "ModelMat", model_mat);
            sushi::set_uniform(shader, "ViewMat", view_mat);
            sushi::set_texture(0, hearttex);
            sushi::draw_mesh(spriteobj);
            model_mat = glm::translate(model_mat, {2.f,0.f,0.f});
        }
    }
};

int main() try {
    std::clog << "Opening window..." << std::endl;
    auto window = sushi::window(0, 0, "Ludum Dare 32", true);

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
