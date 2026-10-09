#include "app/camera_controls.hpp"
#include "app/key_bindings.hpp"
#include "app/showcase.hpp"
#include "app/test_range.hpp"
#include "simulation/explosion_scenarios.hpp"
#include "simulation/impact_patterns.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <utility>
#include <vector>

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
    const auto is = [&](SDL_Keycode key, Kind kind) {
        const auto sent = command(key);
        return sent && sent->kind == kind;
    };
    check(is(SDLK_SPACE, Kind::trigger) && is(SDLK_R, Kind::clear) && is(SDLK_P, Kind::toggle_pause) && is(SDLK_N, Kind::step) &&
        is(SDLK_L, Kind::toggle_debug_lines), "Space, R, P, N and L keep their actions");
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

    check(command(SDLK_G) && command(SDLK_G)->kind == Kind::toggle_walk, "G switches walk and fly");
    check(command(SDLK_F8) && command(SDLK_F8)->kind == Kind::toggle_debug_view && command(SDLK_F8)->value == 2 && command(SDLK_F9) &&
        command(SDLK_F9)->value == 3, "F8 and F9 toggle the spawn regions and the wind arrow");
    check(command(SDLK_3) && command(SDLK_3)->kind == Kind::select_preset && command(SDLK_3)->value == 2 && command(SDLK_KP_7) &&
        command(SDLK_KP_7)->value == 6 && !command(SDLK_8), "1-7 and the keypad select the presets");
    check(is(SDLK_C, Kind::set_count) && is(SDLK_F, Kind::fire) && is(SDLK_B, Kind::barrage) && is(SDLK_V, Kind::toggle_bombardment),
        "C, F, B and V act at the crosshair");
    bool reachable = true;
    for (const auto kind : {Kind::trigger, Kind::clear, Kind::toggle_pause, Kind::step, Kind::toggle_debug_lines, Kind::select_preset,
             Kind::toggle_slow_motion, Kind::toggle_walk, Kind::recall_bookmark, Kind::store_bookmark, Kind::set_count, Kind::fire,
             Kind::barrage, Kind::toggle_bombardment, Kind::toggle_debug_view})
    {
        bool found = false;
        for (const auto& binding : key_bindings()) found |= binding.command.kind == kind;
        reachable &= found;
    }
    check(reachable, "every command but the sliders' has a key");
    bool bookmark_keys = true;
    const SDL_Keycode function_keys[] = {SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F11};
    for (std::uint32_t index = 0; index < 7; ++index)
    {
        const auto recall = command(function_keys[index]);
        const auto store = command(function_keys[index], SDL_KMOD_LCTRL);
        bookmark_keys &= recall && recall->kind == Kind::recall_bookmark && recall->value == index && store &&
            store->kind == Kind::store_bookmark && store->value == index;
        const auto shifted = command(function_keys[index], SDL_KMOD_LSHIFT);
        bookmark_keys &= shifted && shifted->kind == Kind::recall_bookmark;
    }
    check(bookmark_keys, "F2-F7 and F11 recall bookmarks 1-7, Ctrl stores them, Shift does not store");
    check(!command(SDLK_F10) && !command(SDLK_F12), "F10 and F12 are free");
    check(command(SDLK_R, SDL_KMOD_LCTRL) && command(SDLK_R, SDL_KMOD_LCTRL)->kind == Kind::clear,
        "Ctrl does not change a key without a Ctrl binding");

    TestRange test_range;
    const auto& world = test_range.environment();
    std::array<Camera, CameraRig::bookmark_count> views;
    std::array<std::string_view, CameraRig::bookmark_count> names{"overview", "ground", "hill", "aerial", "range", "yard", "far"};
    for (std::size_t index = 0; index < views.size(); ++index) views[index] = test_range.view(static_cast<RangeView>(index));
    const auto same_pose = [](const Camera& a, const Camera& b) {
        return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z && a.yaw == b.yaw &&
            a.pitch == b.pitch && a.vertical_fov == b.vertical_fov;
    };
    CameraRig rig(test_range.view(RangeView::overview), views, names);
    bool defaults = true;
    for (std::size_t index = 0; index < views.size(); ++index)
    {
        defaults &= rig.recall(index) && same_pose(rig.camera(), views[index]) && rig.mode() == CameraMode::fly;
    }
    check(defaults, "the seven bookmarks recall the named views exactly, flying");
    check(!rig.recall(7) && !rig.store(7), "bookmarks beyond the seventh are rejected");
    check(rig.recall(6) && rig.camera().vertical_fov == TestRange::far_vertical_fov && rig.recall(0) &&
        rig.camera().vertical_fov == Camera{}.vertical_fov, "the far bookmark narrows the field of view; the others restore 60 degrees");

    {
        const auto& ground = test_range.terrain();
        const auto eye = test_range.view(RangeView::far).position;
        float lowest = 1e9f;
        for (int x = -30; x <= 30; x += 3)
        {
            for (int z = -30; z <= 30; z += 3)
            {
                if (x * x + z * z > 900) continue;
                const float3 target{static_cast<float>(x), ground.height(static_cast<float>(x), static_cast<float>(z)) + 0.5f, static_cast<float>(z)};
                const auto offset = target - eye;
                const float distance = length(offset);
                for (float along = 0; along < distance - 1; along += 0.5f)
                {
                    const auto point = eye + offset * (along / distance);
                    lowest = std::min(lowest, point.y - ground.height(point.x, point.z));
                }
            }
        }
        std::printf("far view: lowest sight-line clearance over the 30 m disc %.3f m\n", static_cast<double>(lowest));
        check(lowest >= 0.1f, "the far view sees every point of the 30 m disc");
    }
    rig.recall(0);
    const auto still = rig.camera();
    check(!rig.update(CameraInput{}, 1.0f / 60, world, true) && same_pose(rig.camera(), still), "flying without input never moves");
    CameraInput forward{};
    forward.move = {0, 0, 1};
    check(rig.update(forward, 1.0f, world, true) && std::abs(rig.camera().position.z - (still.position.z + rig.camera().forward().z * 15)) < 1e-3f,
        "flying moves at the fly speed along the view");
    rig.store(2);
    const auto stored_pose = rig.camera();
    bool others = true;
    for (std::size_t index = 0; index < views.size(); ++index)
    {
        if (index == 2) continue;
        others &= rig.recall(index) && same_pose(rig.camera(), views[index]) && !rig.bookmarks()[index].stored && rig.mode() == CameraMode::fly;
    }
    check(rig.recall(2) && same_pose(rig.camera(), stored_pose) && rig.camera().position.z == still.position.z + rig.camera().forward().z * 15 &&
        rig.bookmarks()[2].stored && others, "a stored bookmark recalls its pose; the others keep their views");
    rig.scale_speed(1);
    check(rig.speed() == 15 * 1.25f, "the wheel scales the active mode's speed");
    rig.set_speed(1000);
    check(rig.speed() == CameraRig::fly_speed_max, "the fly speed is clamped to 400 m/s");
    rig.set_speed(15);

    const auto walk_at = [&](float x, float z, float yaw) {
        Camera start;
        start.position = {x, 20, z};
        start.yaw = yaw;
        start.pitch = -0.3f;
        CameraRig walker(start, views, names);
        walker.toggle_walk(world, true);
        return walker;
    };
    const auto walk = [&](CameraRig& walker, float3 move, int frames, bool props = true) {
        CameraInput input{};
        input.move = move;
        bool clear = true;
        for (int frame = 0; frame < frames; ++frame)
        {
            walker.update(input, 0.1f, world, props);
            const auto eye = walker.camera().position;
            clear &= !props || (!world.inside(eye) && !world.inside(eye - float3{0, walk_eye_height - walk_knee_height, 0}));
        }
        return clear;
    };
    auto walker = walk_at(0, 58, 0);
    check(walker.mode() == CameraMode::walk && std::abs(walker.camera().position.y - (world.terrain().height(0, 58) + walk_eye_height)) < 1e-4f,
        "walking stands at eye height on the terrain");
    check(walk(walker, {0, 1, 0}, 5) && walker.camera().position.y == world.terrain().height(0, 58) + walk_eye_height,
        "walking ignores up and down");
    walker = walk_at(-14, -45, 0);
    const bool wall_clear = walk(walker, {0, 0, 1}, 40);
    check(wall_clear && walker.camera().position.z >= -53.8f + walk_radius && walker.camera().position.z <= -53.8f + walk_radius + walk_substep,
        "walking north stops a body radius before wall A, never inside it");
    walker = walk_at(-17, -50, 0);
    walk(walker, {1, 0, 1}, 16);
    check(walker.camera().position.x > -12 && walker.camera().position.x < -9 && walker.camera().position.z >= -53.8f + walk_radius,
        "a diagonal push slides along the wall");
    const auto through = walk_move(world, true, {3, 0, -54}, {10, 0, 0});
    check(through.x <= 5.0f - walk_radius, "one long move does not pass through the hut's 0.3 m wall");
    walker = walk_at(8, -45, 0);
    const bool door_clear = walk(walker, {0, 0, 1}, 40);
    check(door_clear && walker.camera().position.z < -54 && walker.camera().position.z >= -56.2f + walk_radius &&
        std::abs(walker.camera().position.y - walk_eye_height) < 1e-4f, "the hut's door admits the walker, under the roof on the ground");
    walker = walk_at(-31.4f, -45, 0);
    walk(walker, {0, 0, 1}, 40);
    check(walker.camera().position.z >= -50 + 0.215f + walk_radius, "a jersey barrier blocks the walker");
    walker = walk_at(-2, -50, 0);
    check(std::abs(walker.camera().position.y - (2.4f + walk_eye_height)) < 1e-3f, "landing above the crate stack stands on its top");
    walk(walker, {1, 0, 0}, 10);
    check(std::abs(walker.camera().position.y - (world.terrain().height(walker.camera().position.x, walker.camera().position.z) +
        walk_eye_height)) < 1e-4f, "walking off a top drops to the ground");
    Camera inside;
    inside.position = {-14, walk_eye_height, -54};
    CameraRig trapped(inside, views, names);
    trapped.toggle_walk(world, true);
    walk(trapped, {0, 0, -1}, 10);
    check(trapped.camera().position.z > -53.8f, "a camera inside a wall walks out of it");
    walker = walk_at(-14, -45, 0);
    walk(walker, {0, 0, 1}, 40, false);
    check(walker.camera().position.z < -60, "with props hidden the walker passes through them");
    check(walk_move(world, true, {250, 0, 0}, {20, 0, 0}).x == Terrain::half_extent - walk_edge_margin, "walking stops at the terrain's edge");
    check(!walk_landing(world, true, {300, 10, 0}), "there is nothing to walk on beyond the terrain");
    walker = walk_at(0, 58, 0);
    const auto before = walker.camera().position;
    CameraInput run{};
    run.move = {0, 0, 1};
    walker.update(run, 1.0f, world, true);
    check(std::abs(walker.camera().position.z - before.z) <= 5 * 0.1f + 1e-4f, "a stall walks at most 0.1 s worth");
    check(walker.toggle_walk(world, true) && walker.mode() == CameraMode::fly, "G returns to flight");

    const auto open = drop_event(world, 3.5f, 7.25f, 9, ExplosionPreset::heavy, true);
    check(open && same_event(*open, ground_event(world.terrain(), 3.5f, 7.25f, 9, ExplosionPreset::heavy)),
        "on open ground a dropped event is the ground's");
    const auto crate_drop = drop_event(world, -2, -50, 9, ExplosionPreset::small_he, true);
    check(crate_drop && std::abs(crate_drop->position.y - 2.9f) < 1e-3f && crate_drop->surface == SurfaceType::dirt &&
        !world.inside(crate_drop->position), "a drop onto the crate stack lands on its top with wood's response");
    const auto roof_drop = drop_event(world, 9.5f, -55, 9, ExplosionPreset::small_he, true);
    check(roof_drop && std::abs(roof_drop->position.y - 3.35f) < 1e-3f && roof_drop->surface == SurfaceType::concrete,
        "a drop onto the hut lands on its roof");
    const auto bare_drop = drop_event(world, -2, -50, 9, ExplosionPreset::small_he, false);
    check(bare_drop && same_event(*bare_drop, ground_event(world.terrain(), -2, -50, 9, ExplosionPreset::small_he)),
        "with props hidden a drop reaches the ground");
    check(!drop_event(world, 257, 0, 9, ExplosionPreset::small_he, true) && !drop_event(world, 0, -300, 9, ExplosionPreset::small_he, true),
        "nothing is dropped beyond the terrain");

    bool within = true;
    float closest = std::numeric_limits<float>::max();
    for (const std::uint32_t count : {10u, 50u, 100u})
    {
        for (std::uint32_t a = 0; a < count; ++a)
        {
            const auto p = cluster_offset(77, a);
            within &= std::sqrt(p.x * p.x + p.y * p.y) <= cluster_radius(count) + 1e-4f;
            for (std::uint32_t b = a + 1; b < count; ++b)
            {
                const auto q = cluster_offset(77, b);
                closest = std::min(closest, std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y)));
            }
        }
    }
    std::printf("cluster radius 10/50/100: %.1f/%.1f/%.1f m, closest pair %.2f m\n", static_cast<double>(cluster_radius(10)),
        static_cast<double>(cluster_radius(50)), static_cast<double>(cluster_radius(100)), static_cast<double>(closest));
    check(cluster_offset(77, 0).x == 0 && cluster_offset(77, 0).y == 0 && within && closest >= 3, "clusters keep their radius and spacing");
    check(cluster_offset(77, 5).x != cluster_offset(79, 5).x, "another key moves a cluster");

    const Ray down{{0, 50, 0}, {0, -1, 0}};
    {
        Showcase showcase;
        showcase.preset = ExplosionPreset::artillery_he;
        ExplosionSystem effects(128);
        const auto fired = showcase.fire(world, true, down, 10, "crosshair", effects);
        effects.start(0);
        const auto aimed = aimed_event(world, down, Showcase::first_seed, ExplosionPreset::artillery_he, true);
        const auto slots = effects.slots();
        check(fired && fired->queued == 10 && fired->off_range == 0 && effects.active().size() == 10 && aimed &&
            slots[0].seed == Showcase::first_seed && slots[0].origin.x == aimed->event.position.x &&
            slots[0].origin.y == aimed->event.position.y && slots[9].seed == Showcase::first_seed + 9 &&
            showcase.next_seed == Showcase::first_seed + 10, "fire x10: the aimed event, then nine around it, seeds in order");
        const auto edge = showcase.fire(world, true, {{254, 50, 0}, {0, -1, 0}}, 50, "crosshair", effects);
        check(edge && edge->off_range > 0 && edge->queued + edge->off_range == 50 && showcase.next_seed == Showcase::first_seed + 10 + edge->queued,
            "near the edge, points off the range are skipped and take no seed");
        check(!showcase.fire(world, true, {{0, 50, 0}, {0, 1, 0}}, 10, "crosshair", effects), "firing at the sky does nothing");
        ExplosionSystem full(128);
        showcase.fire(world, true, down, 100, "crosshair", full);
        showcase.fire(world, true, down, 100, "crosshair", full);
        full.start(0);
        check(full.active().size() == 128 && full.dropped() == 72, "two salvos of 100 start 128 and drop 72");
        showcase.fire(world, true, down, 100, "crosshair", full);
        full.start(1);
        check(full.active().size() == 128 && full.dropped() == 172, "with every slot busy a salvo drops entirely");
    }
    {
        Showcase showcase;
        check(showcase.next_count() == 10, "C counts 1 -> 10");
        showcase.count = 100;
        check(showcase.next_count() == 1, "C counts 100 -> 1");
    }

    const auto newly_started = [](const ExplosionSystem& effects, std::uint32_t tick) {
        std::vector<EffectSlot> started;
        for (const auto slot : effects.active())
        {
            if (effects.slots()[slot].start_tick == tick) started.push_back(effects.slots()[slot]);
        }
        return started;
    };
    {
        ImpactSequence sequence;
        sequence.preset = ExplosionPreset::artillery_he;
        sequence.count = barrage_impacts;
        std::uint32_t seed = 1000;
        ExplosionSystem effects(128);
        std::vector<std::pair<std::uint32_t, EffectSlot>> released;
        for (std::uint32_t tick = 0; tick < 1300; ++tick)
        {
            release_due(sequence, tick, world, false, seed, effects);
            effects.start(tick);
            for (const auto& slot : newly_started(effects, tick)) released.push_back({tick, slot});
        }
        bool same = barrage && released.size() == barrage->events.size() && finished(sequence);
        for (std::size_t index = 0; same && index < released.size(); ++index)
        {
            const auto& [tick, slot] = released[index];
            const auto& scripted = barrage->events[index];
            same = tick == scripted.tick && slot.origin.x == scripted.event.position.x && slot.origin.y == scripted.event.position.y &&
                slot.origin.z == scripted.event.position.z && slot.normal.y == scripted.event.normal.y && slot.seed == scripted.event.seed &&
                slot.effect_class == effect_class_index(scripted.event.preset, scripted.event.surface);
        }
        check(same, "a barrage at the origin along +x with key 0 releases the barrage scenario's events exactly");
        ImpactSequence turned = sequence;
        turned.along = {0, 1};
        turned.centre = {-5, 20};
        const auto a = impact_position(sequence, 7);
        const auto b = impact_position(turned, 7);
        check(std::abs(b.x - (-5 - a.y)) < 1e-4f && std::abs(b.y - (20 + a.x)) < 1e-4f, "a barrage turns and moves with its aim");
    }
    const auto release_by = [&](ImpactSequence sequence, std::uint32_t step, bool repeat) {
        std::uint32_t seed = 500;
        ExplosionSystem effects(1024);
        std::vector<std::pair<float, std::uint32_t>> order;
        for (std::uint32_t tick = 0; tick <= 720; tick += step)
        {
            release_due(sequence, tick, world, true, seed, effects);
            if (repeat) release_due(sequence, tick, world, true, seed, effects);
            effects.start(tick);
            auto started = newly_started(effects, tick);
            std::sort(started.begin(), started.end(), [](const EffectSlot& a, const EffectSlot& b) { return a.seed < b.seed; });
            for (const auto& slot : started) order.push_back({slot.origin.x, slot.seed});
        }
        return order;
    };
    ImpactSequence storm;
    storm.kind = ImpactSequence::Kind::bombardment;
    storm.centre = {-30, 0};
    storm.radius = 30;
    storm.interval = 3;
    storm.key = 1234;
    ImpactSequence walk_line;
    walk_line.centre = {0, -30};
    walk_line.count = barrage_impacts;
    walk_line.key = 4321;
    bool grouping = true;
    for (const auto& sequence : {storm, walk_line})
    {
        const auto reference = release_by(sequence, 1, false);
        grouping &= !reference.empty() && release_by(sequence, 2, false) == reference && release_by(sequence, 3, false) == reference &&
            release_by(sequence, 4, false) == reference && release_by(sequence, 1, true) == reference;
    }
    check(grouping, "barrage and bombardment release the same events whatever the ticks per frame");
    const auto release_both = [&](std::uint32_t step, bool repeat) {
        Showcase showcase;
        showcase.barrage = walk_line;
        showcase.bombardment = storm;
        showcase.bombardment->interval = 1;
        ExplosionSystem effects(1024);
        std::vector<std::pair<float, std::uint32_t>> order;
        for (std::uint32_t tick = 0; tick <= 720; tick += step)
        {
            showcase.release_due(tick, world, true, effects);
            if (repeat) showcase.release_due(tick, world, true, effects);
            effects.start(tick);
            auto started = newly_started(effects, tick);
            std::sort(started.begin(), started.end(), [](const EffectSlot& a, const EffectSlot& b) { return a.seed < b.seed; });
            for (const auto& slot : started) order.push_back({slot.origin.x, slot.seed});
        }
        return order;
    };
    const auto both = release_both(1, false);
    check(both.size() > 721 && release_both(2, false) == both && release_both(3, false) == both && release_both(4, false) == both &&
        release_both(1, true) == both, "a barrage and a bombardment together keep every impact's seed whatever the ticks per frame");
    {
        bool inside_disc = true;
        for (std::uint32_t k = 0; k < 400; ++k)
        {
            const auto p = impact_position(storm, k);
            inside_disc &= std::sqrt((p.x + 30) * (p.x + 30) + p.y * p.y) <= 30 + 1e-3f;
        }
        ImpactSequence other = storm;
        other.key = 99;
        check(inside_disc && !finished(storm) && impact_position(other, 3).x != impact_position(storm, 3).x,
            "a bombardment stays in its disc, never finishes, and its key changes its scatter");
    }
    bool endless = true;
    const auto saturate = [&](std::uint32_t interval) {
        ImpactSequence sequence = storm;
        sequence.interval = interval;
        std::uint32_t seed = 1;
        ExplosionSystem effects(128);
        std::size_t peak = 0;
        for (std::uint32_t tick = 0; tick < 3000; ++tick)
        {
            release_due(sequence, tick, world, true, seed, effects);
            effects.start(tick);
            peak = std::max(peak, effects.active().size());
        }
        endless &= !finished(sequence) && sequence.next == 3000 / interval;
        return std::pair{peak, effects.dropped()};
    };
    const auto steady = saturate(15);
    const auto flood = saturate(3);
    std::printf("bombardment small-he: every 15 ticks peak %zu dropped %llu; every 3 ticks peak %zu dropped %llu\n", steady.first,
        static_cast<unsigned long long>(steady.second), flood.first, static_cast<unsigned long long>(flood.second));
    check(steady.first == effect_ticks(ExplosionPreset::small_he, SurfaceType::dirt) / 15 && steady.second == 0,
        "a bombardment every 15 ticks keeps lifetime / 15 effects and drops nothing");
    check(flood.first == 128 && flood.second > 0, "a bombardment every 3 ticks fills the 128 slots and drops the rest");
    {
        Showcase showcase;
        showcase.bombardment = storm;
        ExplosionSystem effects(128);
        for (std::uint32_t tick = 0; tick < 3000; ++tick)
        {
            showcase.release_due(tick, world, true, effects);
            effects.start(tick);
        }
        endless &= showcase.bombardment && showcase.bombardment->next == 3000 / storm.interval;
    }
    check(endless, "a bombardment releases every impact due and runs until stopped");

    {
        SlotPressure pressure;
        check(!pressure.record(10, 0.0, 20, 0) && pressure.peak_since_clear() == 20, "no drops, no report");
        const auto first = pressure.record(11, 0.1, 128, 5);
        check(first && first->events == 5 && first->first_tick == 11 && pressure.dropped_since_clear() == 5 &&
            pressure.dropped_recent(11) == 5, "the first drops are reported");
        check(!pressure.record(11, 0.5, 128, 8) && !pressure.record(11, 0.9, 128, 9), "within a second the report waits");
        check(pressure.dropped_recent(11) == 9, "drops at one paused tick all count as recent");
        const auto later = pressure.record(40, 1.2, 128, 12);
        check(later && later->events == 7 && later->first_tick == 11 && later->last_tick == 40 && pressure.dropped_recent(40) == 12 &&
            pressure.dropped_recent(80) == 3 && pressure.dropped_recent(200) == 0,
            "a second later the accumulated drops are reported with their ticks; recent drops age out after 60 ticks");
        check(!pressure.record(41, 1.3, 128, 14), "new drops wait for the next second");
        const auto flushed = pressure.clear(14);
        check(flushed && flushed->events == 2 && flushed->first_tick == 41 && pressure.dropped_since_clear() == 0 &&
            pressure.peak_since_clear() == 0 && !pressure.record(42, 5.0, 3, 14),
            "Clear hands back the unreported drops and rebases the counts and the peak");
    }

    {
        Showcase showcase;
        const auto overview = test_range.view(RangeView::overview);
        const auto aim = world.raycast(crosshair_ray(overview), std::numeric_limits<float>::infinity(), true);
        std::vector<ShowcaseMark> marks;
        showcase_marks(DebugViews{}, showcase, aim, overview, world, true, marks);
        check(aim && marks.empty(), "with the views off there are no marks");
        DebugViews regions{};
        regions.spawn_regions = true;
        showcase_marks(regions, showcase, aim, overview, world, true, marks);
        check(marks.size() == 1 && marks[0].kind == ShowcaseMark::ring && mark_vertices(marks[0]) == 2 * 32 + 2,
            "spawn regions at count 1: a ring at the aim");
        marks.clear();
        showcase.count = 100;
        showcase_marks(regions, showcase, aim, overview, world, true, marks);
        check(marks.size() == 2 && marks[1].kind == ShowcaseMark::draped_circle && marks[1].radius == cluster_radius(100) &&
            marks[1].segments <= 128 && marks[1].centre.x == aim->point.x, "at count 100 the cluster's footprint is draped around the aim");
        ExplosionSystem effects(128);
        showcase.start_barrage(world, true, overview, 0);
        showcase.toggle_bombardment(world, true, overview, 0);
        marks.clear();
        showcase_marks(regions, showcase, aim, overview, world, true, marks);
        check(marks.size() == 6 && marks[2].kind == ShowcaseMark::draped_rectangle && marks[2].radius == 64 && marks[2].half_width == 12 &&
            marks[2].segments <= 128 && marks[2].side_segments > marks[2].segments / 4 && marks[3].kind == ShowcaseMark::ring &&
            marks[4].kind == ShowcaseMark::draped_circle && marks[4].radius == showcase.bombardment_radius && marks[5].kind == ShowcaseMark::ring,
            "a running barrage shows its corridor and next impact, a bombardment its disc and next impact");
        DebugViews wind{};
        wind.wind_arrow = true;
        marks.clear();
        showcase_marks(wind, showcase, aim, overview, world, true, marks);
        check(marks.size() == 1 && marks[0].kind == ShowcaseMark::wind_arrow && marks[0].lift == 15 && mark_vertices(marks[0]) == 8 &&
            marks[0].centre.x == aim->point.x, "the wind arrow stands at the aim");
        marks.clear();
        showcase_marks(wind, showcase, std::nullopt, overview, world, true, marks);
        check(marks.size() == 1 && std::abs(marks[0].centre.y - world.terrain().height(marks[0].centre.x, marks[0].centre.z)) < 1e-4f,
            "without an aim the wind arrow stands on the terrain ahead");
    }

    {
        bool entries = true;
        bool clear = true;
        for (const char* name : {"heli-hover", "heli-pass", "heli-landing"})
        {
            const auto scenario = make_scenario(name, environment, ExplosionPreset::artillery_he);
            if (!scenario || !scenario->flight)
            {
                entries = false;
                continue;
            }
            const auto& plan = *scenario->flight;
            const auto hub = to_float3(flight_state(plan, plan.list_tick).position);
            for (const auto view : {RangeView::overview, RangeView::far, RangeView::ground, RangeView::aerial})
            {
                const auto camera = test_range.view(view);
                const auto matrix = camera.projection(16.0f / 9.0f) * camera.view();
                for (int corner = 0; corner < 8; ++corner)
                {
                    const float3 point = hub + float3{(corner & 1) ? 10.0f : -10.0f, (corner & 2) ? 10.0f : -10.0f, (corner & 4) ? 10.0f : -10.0f};
                    const auto clip = matrix * float4{point.x, point.y, point.z, 1};
                    const bool on_screen = clip.w > 0 && std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w;
                    entries = entries && !on_screen;
                }
            }
            const std::uint32_t last = std::min(plan.unlist_tick, plan.touchdown_tick == never ? plan.knots.back().tick : plan.touchdown_tick - 900);
            for (std::uint32_t tick = plan.list_tick; tick < last; tick += 6)
            {
                const auto position = flight_state(plan, tick).position;
                const double skids = position.y - rotor::hub_above_skids;
                double highest = terrain.height(static_cast<float>(position.x), static_cast<float>(position.z));
                for (const auto& box : environment.boxes())
                {
                    const double reach = std::hypot(box.half_extent.x, box.half_extent.z) + rotor::radius;
                    if (std::hypot(box.centre.x - position.x, box.centre.z - position.z) <= reach) highest = std::max(highest, static_cast<double>(box.centre.y + box.half_extent.y));
                }
                clear = clear && skids >= highest + 2 - 1e-6;
            }
        }
        check(entries, "the helicopter's scripts enter outside the overview, far, ground and aerial views");
        check(clear, "the helicopter's skids clear the terrain and props by 2 m until its landing");
    }

    if (failures == 0) std::puts("showcase tests passed");
    return failures == 0 ? 0 : 1;
}
