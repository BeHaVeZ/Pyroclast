#include "app/key_bindings.hpp"
#include "simulation/explosion_scenarios.hpp"
#include "simulation/impact_patterns.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace
{
using namespace pyroclast;

int failures = 0;

void check(bool value, const char* name)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", name);
        ++failures;
    }
}

bool same_event(const ExplosionEvent& a, const ExplosionEvent& b)
{
    return std::memcmp(&a, &b, sizeof(ExplosionEvent)) == 0;
}
}

int main()
{
    const Environment environment;
    const auto& terrain = environment.terrain();

    const auto group = group_events(terrain, 10, ExplosionPreset::heavy);
    bool grounded = true;
    for (std::uint32_t index = 0; index < group.size(); ++index)
    {
        const auto& event = group[index];
        grounded &= same_event(event, ground_event(terrain, event.position.x, event.position.z, index, ExplosionPreset::heavy));
    }
    check(grounded, "the startup group places its events with ground_event");
    const auto barrage = make_scenario("barrage", environment, ExplosionPreset::artillery_he);
    bool walked = barrage && barrage->events.size() == barrage_impacts;
    for (std::uint32_t k = 0; walked && k < barrage_impacts; ++k)
    {
        const auto offset = barrage_offset(0, k);
        walked = barrage->events[k].tick == k * barrage_interval &&
            same_event(barrage->events[k].event, ground_event(terrain, offset.x, offset.y, 1000 + k, ExplosionPreset::artillery_he));
    }
    check(walked, "the barrage scenario walks barrage_offset(0, k), one impact every barrage_interval ticks");
    check(pattern_jitter(7) >= -1 && pattern_jitter(7) < 1 && pattern_jitter(7) == pattern_jitter(7) && pattern_jitter(7) != pattern_jitter(8),
        "pattern_jitter is a deterministic value in [-1, 1)");

    const auto range = make_scenario("range", environment);
    const Ray wall_ray{{-11, 1.5f, -40}, {0, 0, -1}};
    const auto wall = aimed_event(environment, wall_ray, 3001, ExplosionPreset::small_impact, true);
    check(range && wall && same_event(wall->event, range->events[1].event) && wall->hit.prop,
        "aimed_event places the range scenario's event on wall A");
    const auto bare = aimed_event(environment, wall_ray, 5, ExplosionPreset::small_he, false);
    check(bare && !bare->hit.prop && std::abs(bare->event.position.x - (bare->hit.point.x + bare->hit.normal.x * 0.5f)) < 1e-6f &&
        std::abs(bare->event.position.y - (bare->hit.point.y + bare->hit.normal.y * 0.5f)) < 1e-6f,
        "without props the ray reaches the terrain and the origin is 0.5 m off it");
    check(!aimed_event(environment, {{0, 10, 0}, {0, 1, 0}}, 5, ExplosionPreset::small_he, true), "a ray into the sky aims at nothing");

    const auto command = [](SDL_Keycode key, SDL_Keymod modifiers = SDL_KMOD_NONE) { return key_command(key, modifiers); };
    using Kind = UiCommand::Kind;
    check(command(SDLK_SPACE) && command(SDLK_SPACE)->kind == Kind::trigger && command(SDLK_R)->kind == Kind::clear &&
        command(SDLK_P)->kind == Kind::toggle_pause && command(SDLK_N)->kind == Kind::step &&
        command(SDLK_L)->kind == Kind::toggle_debug_lines, "Space, R, P, N and L keep their actions");
    check(command(SDLK_T) && command(SDLK_T)->kind == Kind::toggle_slow_motion, "T toggles slow motion");
    check(command(SDLK_R, SDL_KMOD_LSHIFT) && command(SDLK_R, SDL_KMOD_LSHIFT)->kind == Kind::clear,
        "Shift does not change a key's action");
    check(!command(SDLK_R, SDL_KMOD_LALT) && !command(SDLK_F4, SDL_KMOD_LALT), "Alt-modified keys send nothing");
    std::set<std::pair<SDL_Keycode, bool>> bound;
    bool unique = true;
    for (const auto& binding : key_bindings()) unique &= bound.insert({binding.key, binding.ctrl}).second;
    check(unique, "no key is bound twice");
    bool movement_free = true;
    for (const auto key : {SDLK_W, SDLK_A, SDLK_S, SDLK_D, SDLK_Q, SDLK_E, SDLK_LSHIFT, SDLK_RSHIFT, SDLK_F1})
    {
        movement_free &= !command(key);
    }
    check(movement_free, "movement keys and F1 are not bound");

    if (failures == 0) std::puts("showcase tests passed");
    return failures == 0 ? 0 : 1;
}
