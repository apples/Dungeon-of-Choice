#include "util.hpp"

#include <ginseng/ginseng.hpp>
#include <sushi/sushi.hpp>
#include <raspberry/raspberry.hpp>
#include <soloud.h>
#include <soloud_wavstream.h>
#include <soloud_wav.h>
#include <soloud_speech.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <boost/variant.hpp>

#include <windows.h>

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

enum class Item {
    TORCH,
    BOOTS,
    HEAL,
    NUM_ITEMS,
    MIMIC
};

struct Treasure {
    Item item;
};

enum class BaddyType {
    BAD_DUDE,
    MIMIC
};
struct Baddy {
    BaddyType type = BaddyType::BAD_DUDE;
};

struct Hallway {
    int len;
    boost::variant<Nothing,Treasure,Baddy> inhabitant;
    std::shared_ptr<Hallway> left;
    std::shared_ptr<Hallway> right;

    enum Dir {
        NONE,
        LEFT,
        RIGHT
    };

    Dir from = NONE;
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

struct Config {
    int AA = 2;
    bool anisotropic = true;
};

static Config config = {};

static const auto LKEY = sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_LEFT};
static const auto RKEY = sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_RIGHT};

struct Game {
    static constexpr auto player_speed = 2.f;
    static constexpr auto battle_speed = 12.5f;

    using State = void(Game::*)(double);
    State cur_state = nullptr;
    State ui_state = nullptr;
    using HudState = void(Game::*)();
    HudState hud_state = nullptr;

    int player_health = 3;
    int difficulty = 1;
    std::vector<Item> player_items = {};

    glm::vec3 player_pos = {0.f, 0.f, 0.f};
    glm::quat player_rot = glm::quat();

    sushi::texture_2d halltex = sushi::load_texture_2d("assets/textures/hallway.png", false, false, config.anisotropic);
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

    std::array<sushi::texture_2d,int(Item::NUM_ITEMS)> itemtexs = {{
        sushi::load_texture_2d("assets/textures/lamp.png", false, false, false),
        sushi::load_texture_2d("assets/textures/boots.png", false, false, false),
        sushi::load_texture_2d("assets/textures/heal.png", false, false, false),
    }};

    sushi::static_mesh treasureobj = sushi::load_static_mesh_file("assets/models/treasure.obj");
    sushi::texture_2d treasuretex = sushi::load_texture_2d("assets/textures/treasure.png", false, false, config.anisotropic);

    sushi::texture_2d baddytex = sushi::load_texture_2d("assets/textures/baddy.png", false, false, config.anisotropic);
    sushi::texture_2d mimictex = sushi::load_texture_2d("assets/textures/mimic.png", false, false, config.anisotropic);
    sushi::texture_2d hearttex = sushi::load_texture_2d("assets/textures/heart.png", false, false, false);
    sushi::texture_2d battletex = sushi::load_texture_2d("assets/textures/battle.png", false, false, false);
    sushi::texture_2d playertex = sushi::load_texture_2d("assets/textures/player.png", false, false, false);
    sushi::texture_2d daggertex = sushi::load_texture_2d("assets/textures/dagger.png", false, false, false);

    sushi::texture_2d titletex = sushi::load_texture_2d("assets/textures/title.png", false, false, false);
    sushi::texture_2d gameovertex = sushi::load_texture_2d("assets/textures/gameover.png", false, false, false);

    std::shared_ptr<Hallway> cur_hall = make_random_hall();

    glm::mat4 proj_mat;
    glm::mat4 view_mat;

    sushi::window* window;

    LightSource lamp = LightSource(2.5, 5);

    struct BaddyState {
        float countdown = 0.5f;
        struct Bullet {
            glm::vec2 pos;
            bool alive = true;
        };
        std::vector<Bullet> bullets = {};
        glm::vec2 player_pos = {0.f,-7.f};
    };

    std::shared_ptr<BaddyState> baddy;

    struct TreasureState {
        Treasure treasure;
        float timer = 1.f;
    };
    std::shared_ptr<TreasureState> treasure_state;

    int winwidth;
    int winheight;

    // The texture we're going to render to
    sushi::texture_2d renderedTexture = {sushi::make_unique_texture(),0,0};
    GLuint framebuffer = 0;

    SoLoud::Soloud* soloud;

    SoLoud::Wav hurtsfx;
    SoLoud::Wav misssfx;
    SoLoud::Speech itemsfx;

    bool player_lost = false;

    void reset() {
        ui_state = state_title;
        player_lost = false;
        baddy = {};
        treasure_state = {};
        losetimer = {};
        difficulty = 1;
        player_health = 3;
        player_pos = {0.f, 0.f, 0.f};
        player_rot = glm::quat();
        player_items = {};
        cur_hall = make_random_hall();
        cur_hall->inhabitant = {};
        cur_hall->left = make_random_hall();
        cur_hall->right = make_random_hall();
        cur_state = nullptr;
        hud_state = nullptr;
    };

    Game(sushi::window* window, SoLoud::Soloud* soloud) : window(window), soloud(soloud) {
        hurtsfx.load("assets/sfx/hurt.wav");
        misssfx.load("assets/sfx/miss.wav");
        itemsfx.setText("");//.load("assets/sfx/item.wav");

        cur_hall->left = make_random_hall();
        cur_hall->right = make_random_hall();

        winwidth = window->width();
        winheight = window->height();

        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

        // "Bind" the newly created texture : all future texture functions will modify this texture
        glBindTexture(GL_TEXTURE_2D, renderedTexture.handle.get());

        // Give an empty image to OpenGL ( the last "0" )
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, winwidth * config.AA, winheight * config.AA, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

        // Poor filtering. Needed !
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        // The depth buffer
        GLuint depthrenderbuffer;
        glGenRenderbuffers(1, &depthrenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, depthrenderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, winwidth * config.AA, winheight * config.AA);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthrenderbuffer);

        // Set "renderedTexture" as our colour attachement #0
        glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, renderedTexture.handle.get(), 0);

        // Set the list of draw buffers.
        GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, DrawBuffers); // "1" is the size of DrawBuffers
        // Always check that our framebuffer is ok

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Failed to create framebuffer!");
        }

        reset();
    }

    float get_run_speed() {
        return (player_speed + std::count(begin(player_items),end(player_items),Item::BOOTS));
    }

    glm::mat4 draw_hallway(const Hallway& hall, glm::mat4 model_mat) {
        auto vp = proj_mat * view_mat;
        sushi::set_texture(0, halltex);
        switch (hall.from) {
            case Hallway::LEFT: {
                auto mmat = glm::translate(model_mat, {0.f, 0.f, 1.5773503f});
                auto mmat2 = glm::rotate(mmat, glm::radians(60.f), {0.f, 1.f, 0.f});
                auto mvp = vp * mmat2;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", mmat2);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                sushi::draw_mesh(juncobj);
                mmat2 = glm::translate(mmat2, {0.f, 0.f, 1.5773503f});
                mvp = vp * mmat2;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", mmat2);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                sushi::draw_mesh(hallobj);
                mmat2 = glm::rotate(mmat, glm::radians(-60.f), {0.f, 1.f, 0.f});
                mmat2 = glm::translate(mmat2, {0.f, 0.f, 1.5773503f});
                mvp = vp * mmat2;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", mmat2);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                sushi::draw_mesh(hallobj);
            } break;
            case Hallway::RIGHT: {
                auto mmat = glm::translate(model_mat, {0.f, 0.f, 1.5773503f});
                auto mmat2 = glm::rotate(mmat, glm::radians(-60.f), {0.f, 1.f, 0.f});
                auto mvp = vp * mmat2;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", mmat2);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                sushi::draw_mesh(juncobj);
                mmat2 = glm::translate(mmat2, {0.f, 0.f, 1.5773503f});
                mvp = vp * mmat2;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", mmat2);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                sushi::draw_mesh(hallobj);
                mmat2 = glm::rotate(mmat, glm::radians(60.f), {0.f, 1.f, 0.f});
                mmat2 = glm::translate(mmat2, {0.f, 0.f, 1.5773503f});
                mvp = vp * mmat2;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", mmat2);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                sushi::draw_mesh(hallobj);
            } break;
            default: break;
        }
        for (int i=0; i<hall.len; ++i) {
            auto mvp = vp * model_mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::set_uniform(shader, "ModelMat", model_mat);
            sushi::set_uniform(shader, "ViewMat", view_mat);
            sushi::draw_mesh(hallobj);
            model_mat = glm::translate(model_mat, {0.f, 0.f, -2.f});
        }
        auto mvp = vp * model_mat;
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
                auto treasure_mat = glm::translate(model_mat, {0.f, 0.f, 0.5773503f});
                auto mvp = proj_mat * view_mat * treasure_mat;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", treasure_mat);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                sushi::set_texture(0, treasuretex);
                sushi::draw_mesh(treasureobj);
            },
            [&](const Baddy& bd){
                auto treasure_mat = glm::translate(model_mat, {0.f, 0.f, 0.5773503f});
                auto mvp = proj_mat * view_mat * treasure_mat;
                sushi::set_uniform(shader, "MVP", mvp);
                sushi::set_uniform(shader, "ModelMat", treasure_mat);
                sushi::set_uniform(shader, "ViewMat", view_mat);
                switch (bd.type) {
                    case BaddyType::BAD_DUDE:
                        sushi::set_texture(0, baddytex);
                        break;
                    case BaddyType::MIMIC:
                        sushi::set_texture(0, mimictex);
                        break;
                }
                sushi::draw_mesh(spriteobj);
            }
        ), hall.inhabitant);
    }

    void main_loop(double delta) {
        if (window->was_pressed(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_ESCAPE})) {
            if (ui_state == &state_title) {
                window->stop_loop();
            } else {
                reset();
            }
        }

        // Render to our framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0,0,winwidth * config.AA,winheight * config.AA);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        sushi::set_program(shader);

        sushi::set_uniform(shader, "Texture", 0);
        sushi::set_uniform(shader, "EnableFisheye", 0);
        sushi::set_uniform(shader, "FisheyeTheta", glm::radians(120.f));

        if (window->is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_F5})) {
            sushi::set_uniform(shader, "FullBright", 1);
        } else {
            sushi::set_uniform(shader, "FullBright", 0);
        }

        auto player_lamps = std::count(begin(player_items),end(player_items),Item::TORCH);

        lamp.bright_radius = player_lamps * 2;
        lamp.dim_radius = player_lamps * 3 + 2;

        lamp.flicker(delta);
        sushi::set_uniform(shader, "BrightRadius", lamp.bright_radius + lamp.bright_flicker);
        sushi::set_uniform(shader, "DimRadius", lamp.dim_radius + lamp.dim_flicker);

        if (!player_lost && player_health <= 0) {
            player_lost = true;
            cur_state = state_lose;
            ui_state = nullptr;
        }

        proj_mat = glm::perspectiveFov(glm::radians(120.f), float(winwidth), float(winheight), 0.01f, 50.f);
        if (cur_state) {
            (this->*cur_state)(delta);
        }

        // Render to the screen
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0,0,winwidth,winheight);
        glClear(GL_DEPTH_BUFFER_BIT);

        proj_mat = glm::ortho(-1.f,1.f,1.f,-1.f,-1.f,1.f);
        view_mat = glm::mat4();
        auto model_mat = glm::mat4();

        if (window->is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_F6})) {
            sushi::set_uniform(shader, "EnableFisheye", 0);
        } else {
            sushi::set_uniform(shader, "EnableFisheye", 1);
        }

        auto mvp = proj_mat * view_mat * model_mat;
        sushi::set_uniform(shader, "MVP", mvp);
        sushi::set_uniform(shader, "ModelMat", model_mat);
        sushi::set_uniform(shader, "ViewMat", view_mat);
        sushi::set_uniform(shader, "FullBright", 1);
        sushi::set_texture(0, renderedTexture);
        sushi::draw_mesh(spriteobj);

        sushi::set_uniform(shader, "EnableFisheye", 0);
        if (hud_state) {
            (this->*hud_state)();
        }
        if (ui_state) {
            (this->*ui_state)(delta);
        }
    }

    std::shared_ptr<Hallway> make_random_hall() {
        auto rv = std::make_shared<Hallway>();

        std::uniform_int_distribution<int> len_dist (1+difficulty/10,1+difficulty/10+2);
        rv->len = len_dist(rng);

        std::discrete_distribution<int> inhab_dist ({2,1,2});
        switch (inhab_dist(rng)) {
            case 0:
                rv->inhabitant = Nothing{};
                break;
            case 1:
                rv->inhabitant = make_random_treasure();
                break;
            case 2: {
                std::discrete_distribution<int> mimic_dist ({5,1});
                if (mimic_dist(rng) == 1) {
                    rv->inhabitant = Treasure{Item::MIMIC};
                } else {
                    rv->inhabitant = Baddy{};
                }
            } break;
        }

        return rv;
    }

    Treasure make_random_treasure() {
        auto rv = Treasure();

        static_assert(int(Item::NUM_ITEMS)==3, "Item count mismatch!");
        // TORCH, BOOTS, HEAL
        std::discrete_distribution<int> inhab_dist {2,3,5};
        rv.item = Item(inhab_dist(rng));

        return rv;
    }

    struct LoseTimer {
        float timer = 1.f;
    };
    std::shared_ptr<LoseTimer> losetimer;

    void state_lose(double delta) {
        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);

        if (!losetimer) {
            losetimer = std::make_shared<LoseTimer>();
        }
        losetimer->timer -= delta;
        if (losetimer->timer <= 0) {
            cur_state = nullptr;
            ui_state = state_gameover;
            losetimer = {};
        }
    }

    void state_moving(double delta) {
        auto until_stop = cur_hall->len * 2.f - 2.f - player_pos.z;
        auto step_size = delta * get_run_speed();

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
        auto step_size = delta * get_run_speed();

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
            player_pos.z = -1.5773503f;
            player_rot = glm::quat();
            cur_state = state_moving;
            cur_hall->from = Hallway::LEFT;
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
            player_pos.z = -1.5773503f;
            player_rot = glm::quat();
            cur_state = state_moving;
            cur_hall->from = Hallway::RIGHT;
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

        if (window->was_pressed(LKEY)) {
            cur_state = state_turnleft;
        }
        if (window->was_pressed(RKEY)) {
            cur_state = state_turnright;
        }

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);
    }

    void state_treasure(double delta) {
        if (!treasure_state) {
            treasure_state = std::make_shared<TreasureState>();
            treasure_state->treasure = boost::get<Treasure>(cur_hall->inhabitant);
        }

        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);

        treasure_state->timer -= delta;

        if (treasure_state->timer <= 0) {
            cur_hall->inhabitant = Nothing{};
            cur_state = state_treasure_get;
            switch (treasure_state->treasure.item) {
                case Item::TORCH:
                    itemsfx.setText("Light Source");
                    break;
                case Item::BOOTS:
                    itemsfx.setText("Speed Boots");
                    break;
                case Item::HEAL:
                    itemsfx.setText("Hart");
                    break;
                case Item::MIMIC:
                    itemsfx.setText("Memic");
                    cur_hall->inhabitant = Baddy{BaddyType::MIMIC};
                    break;
            }
            itemsfx.setVolume(1.f);
            soloud->play(itemsfx);
            treasure_state->timer = 1.f;
        };
    }

    void state_treasure_get(double delta) {
        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);

        auto w = float(winwidth);
        auto h = float(winheight);
        proj_mat = glm::ortho(-w/2.f,w/2.f,-h/2.f,h/2.f,-1.f,1.f);
        glClear(GL_DEPTH_BUFFER_BIT);
        view_mat = glm::mat4();
        model_mat = glm::scale(glm::mat4(1.f), {64.f,64.f,1.f});
        sushi::set_uniform(shader, "FullBright", 1);

        if (treasure_state->treasure.item != Item::MIMIC) {
            auto mvp = proj_mat * view_mat * model_mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::set_uniform(shader, "ModelMat", model_mat);
            sushi::set_uniform(shader, "ViewMat", view_mat);
            sushi::set_texture(0, itemtexs[int(treasure_state->treasure.item)]);
            sushi::draw_mesh(spriteobj);
            model_mat = glm::translate(model_mat, {2.f,0.f,0.f});
        }

        treasure_state->timer -= delta;

        if (treasure_state->timer <= 0) {
            switch (treasure_state->treasure.item) {
                case Item::TORCH:
                    player_items.push_back(Item::TORCH);
                    cur_state = state_tojunc;
                    break;
                case Item::BOOTS:
                    player_items.push_back(Item::BOOTS);
                    cur_state = state_tojunc;
                    break;
                case Item::HEAL:
                    ++player_health;
                    cur_state = state_tojunc;
                    break;
                case Item::MIMIC:
                    cur_state = state_baddy;
                    break;
            }
            treasure_state = {};
        };
    }

    void state_baddy(double delta) {
        std::uniform_real_distribution<float> spawn_dist (-7.5f,7.5f);
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

        baddy->countdown -= delta;
        if (baddy->countdown > 0) {
            return;
        }

        ui_state = baddy_ui_state;

        auto player_battle_speed = battle_speed * (std::count(begin(player_items),end(player_items),Item::BOOTS) + 1);

        if (window->is_down(LKEY)) {
            baddy->player_pos.x -= delta * player_battle_speed;
            if (baddy->player_pos.x < -7.f) {
                baddy->player_pos.x = -7.f;
            }
        }
        if (window->is_down(RKEY)) {
            baddy->player_pos.x += delta * player_battle_speed;
            if (baddy->player_pos.x > 7.f) {
                baddy->player_pos.x = 7.f;
            }
        }

        for (auto& b : baddy->bullets) {
            b.pos.y -= delta * (battle_speed * difficulty / 7.5f + 2.f);

            if (glm::distance(b.pos, baddy->player_pos) < 0.9) {
                --player_health;
                b.alive = false;
                soloud->play(hurtsfx);
            }

            if (b.pos.y <= -7.5f) {
                b.alive = false;
                soloud->play(misssfx);
            }
        }

        baddy->bullets.erase(std::remove_if(baddy->bullets.begin(),baddy->bullets.end(),[](auto b){return !b.alive;}),baddy->bullets.end());

        if (baddy->bullets.empty()) {
            cur_state = state_battlewin;
            ui_state = nullptr;
            baddy->countdown = 1.f;
        };
    }

    void state_battlewin(double delta) {
        view_mat = mat4_cast(player_rot) * glm::mat4(1.f);
        view_mat = glm::translate(view_mat, player_pos);

        auto model_mat = glm::mat4(1.f);
        draw_hallway_full(*cur_hall, model_mat);

        baddy->countdown -= delta;
        if (baddy->countdown > 0) {
            return;
        }

        baddy = {};
        auto bt = boost::get<Baddy>(cur_hall->inhabitant).type;
        cur_hall->inhabitant = Nothing{};

        std::discrete_distribution<int> drops {3,1};

        if (bt == BaddyType::MIMIC || drops(rng) == 1) {
            treasure_state = std::make_shared<TreasureState>();
            treasure_state->treasure = make_random_treasure();
            cur_state = state_treasure;
        } else {
            cur_state = state_tojunc;
        }
    }

    void baddy_ui_state(double delta) {
        auto w = float(winwidth);
        auto h = float(winheight);
        proj_mat = glm::ortho(-w/2.f,w/2.f,-h/2.f,h/2.f,-1.f,1.f);
        glClear(GL_DEPTH_BUFFER_BIT);
        view_mat = glm::mat4();
        auto model_mat = glm::scale(glm::mat4(1.f), {256.f,256.f,1.f});
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
            auto mat = glm::translate(model_mat, {b.pos.x,b.pos.y,0.5});
            auto mvp = proj_mat * view_mat * mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::set_uniform(shader, "ModelMat", mat);
            sushi::set_uniform(shader, "ViewMat", view_mat);
            sushi::set_texture(0, daggertex);
            sushi::draw_mesh(spriteobj);
        }
    }

    void state_title(double delta) {
        auto w = float(winwidth);
        auto h = float(winheight);
        proj_mat = glm::ortho(-w/2.f,w/2.f,-h/2.f,h/2.f,-1.f,1.f);
        glClear(GL_DEPTH_BUFFER_BIT);
        view_mat = glm::mat4();
        auto model_mat = glm::scale(glm::mat4(1.f), {h*4.f/3.f/2.f,h/2.f,1.f});
        sushi::set_uniform(shader, "FullBright", 1);
        auto mvp = proj_mat * view_mat * model_mat;
        sushi::set_uniform(shader, "MVP", mvp);
        sushi::set_uniform(shader, "ModelMat", model_mat);
        sushi::set_uniform(shader, "ViewMat", view_mat);
        sushi::set_texture(0, titletex);
        sushi::draw_mesh(spriteobj);

        if (window->was_pressed(LKEY) || window->was_pressed(RKEY)) {
            cur_state = state_moving;
            ui_state = nullptr;
            hud_state = render_hud;
        }
    }

    void state_gameover(double delta) {
        auto w = float(winwidth);
        auto h = float(winheight);
        proj_mat = glm::ortho(-w/2.f,w/2.f,-h/2.f,h/2.f,-1.f,1.f);
        glClear(GL_DEPTH_BUFFER_BIT);
        view_mat = glm::mat4();
        auto model_mat = glm::scale(glm::mat4(1.f), {h*4.f/3.f/2.f,h/2.f,1.f});
        sushi::set_uniform(shader, "FullBright", 1);
        auto mvp = proj_mat * view_mat * model_mat;
        sushi::set_uniform(shader, "MVP", mvp);
        sushi::set_uniform(shader, "ModelMat", model_mat);
        sushi::set_uniform(shader, "ViewMat", view_mat);
        sushi::set_texture(0, gameovertex);
        sushi::draw_mesh(spriteobj);
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
        proj_mat = glm::ortho(0.f,float(winwidth),0.f,float(winheight),-1.f,1.f);
        glClear(GL_DEPTH_BUFFER_BIT);
        view_mat = glm::mat4();
        model_mat = glm::scale(glm::mat4(1.f), {32.f,32.f,1.f});
        model_mat = glm::translate(model_mat, {1.f,1.f,0.f});
        sushi::set_uniform(shader, "FullBright", 1);
        for (auto item : player_items) {
            auto mvp = proj_mat * view_mat * model_mat;
            sushi::set_uniform(shader, "MVP", mvp);
            sushi::set_uniform(shader, "ModelMat", model_mat);
            sushi::set_uniform(shader, "ViewMat", view_mat);
            sushi::set_texture(0, itemtexs[int(item)]);
            sushi::draw_mesh(spriteobj);
            model_mat = glm::translate(model_mat, {2.f,0.f,0.f});
        }
    }
};

int main() try {
    auto fullscreen = MessageBox(nullptr, "Do you want to run the game fullscreen?", "Dungeon of Choice", MB_YESNO | MB_ICONQUESTION);

    std::clog << "Opening window..." << std::endl;
    auto window = sushi::window(0, 0, "Dungeon of Choice", (fullscreen == IDYES));

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
    auto game = Game(&window, &soloud);

    using clock = std::chrono::high_resolution_clock;
    auto last_tick = clock::now();

    std::clog << "Starting main loop..." << std::endl;
    window.main_loop([&]{
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        auto this_tick = clock::now();
        auto delta = std::chrono::duration<double>(this_tick-last_tick).count();
        last_tick = this_tick;

        if (window.is_down(sushi::input_button{sushi::input_type::KEYBOARD, GLFW_KEY_F7})) {
            delta *= 5.f;
        }
        game.main_loop(delta);
    });

    std::clog << "Ending without problem..." << std::endl;

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    std::cerr << "ERROR: " << e.what() << std::endl;
    return EXIT_FAILURE;
}
