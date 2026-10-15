#include "simulation/blast_front.hpp"
#include "simulation/effect_lights.hpp"
#include "simulation/explosion_presets.hpp"
#include "simulation/terrain.hpp"
#include "simulation/wind.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

int main()
{
    using namespace pyroclast;
    namespace layout = particle_layout;
    int failures = 0;
    const auto check = [&failures](bool value, const char* name) {
        if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
    };
    const auto layer = [](const EffectClass& effect, EffectLayer which) -> const LayerParameters& {
        return effect.layers[static_cast<std::uint32_t>(which)];
    };

    bool names = true;
    for (std::uint32_t index = 0; index < preset_count; ++index)
    {
        const auto preset = static_cast<ExplosionPreset>(index);
        names = names && preset_from_name(preset_name(preset)) == preset && preset_names.find(preset_name(preset)) != std::string_view::npos;
    }
    check(names && !preset_from_name("nuke") && !preset_from_name(""), "every preset has a name --preset accepts");

    const auto classes = effect_classes();
    bool ordered = classes.size() == effect_class_count && effect_class_index(ExplosionPreset::stress_test, SurfaceType::concrete) == effect_class_count - 1;
    for (std::uint32_t preset = 0; preset < preset_count && ordered; ++preset)
    {
        for (std::uint32_t surface = 0; surface < surface_type_count && ordered; ++surface)
        {
            const auto expected = make_effect_class(static_cast<ExplosionPreset>(preset), static_cast<SurfaceType>(surface));
            const auto index = effect_class_index(static_cast<ExplosionPreset>(preset), static_cast<SurfaceType>(surface));
            ordered = std::memcmp(&classes[index], &expected, sizeof(EffectClass)) == 0;
        }
    }
    check(ordered, "one class per preset and surface, in index order");
    bool layouts = true;
    for (std::uint32_t index = 0; index < classes.size() && layouts; ++index)
    {
        const auto& effect = classes[index];
        std::uint32_t next = 0;
        for (std::uint32_t which = 0; which < layout::layer_count && layouts; ++which)
        {
            const auto& entry = effect.layers[which];
            layouts = entry.first_group == next && entry.groups * layout::simulation_group_size >= entry.count &&
                (entry.groups == 0 || (entry.groups - 1) * layout::simulation_group_size < entry.count);
            for (std::uint32_t group = 0; group < entry.groups && layouts; ++group)
            {
                layouts = effect.group_layers[entry.first_group + group] == which;
            }
            next += entry.groups;
        }
        layouts = layouts && next <= layout::slab_groups;
        for (std::uint32_t group = next; group < layout::slab_groups && layouts; ++group)
        {
            layouts = effect.group_layers[group] == layout::no_layer;
        }
        layouts = layouts && layer(effect, EffectLayer::ground_dust).spawn == layout::spawn_ring &&
            layer(effect, EffectLayer::blast_smoke).spawn == layout::spawn_burst;
    }
    check(layouts, "every class lays its layers out in whole groups, in draw order, within one slab");
    bool composed = true;
    for (const auto& effect : classes)
    {
        composed = composed && layer(effect, EffectLayer::blast_smoke).count > 0 && layer(effect, EffectLayer::fireball).count > 0 &&
            layer(effect, EffectLayer::flash).count > 0;
    }
    check(composed, "every preset has a flash, a fireball and blast smoke");
    const auto artillery = make_effect_class(ExplosionPreset::artillery_he, SurfaceType::dirt);
    const auto fuel = make_effect_class(ExplosionPreset::fuel_fire, SurfaceType::dirt);
    const auto dust = make_effect_class(ExplosionPreset::dust_blast, SurfaceType::dirt);
    check(layer(artillery, EffectLayer::plume).count > 0 && layer(artillery, EffectLayer::plume).delay_range > 0 &&
        layer(fuel, EffectLayer::debris).count == 0 &&
        layer(fuel, EffectLayer::fireball).count > layer(artillery, EffectLayer::fireball).count &&
        layer(dust, EffectLayer::ground_dust).count > layer(artillery, EffectLayer::ground_dust).count,
        "presets differ by their layers: a fed plume, fuel without debris and with a larger fireball, a dust-heavy blast");

    bool surfaces = true;
    for (std::uint32_t index = 0; index < preset_count; ++index)
    {
        const auto preset = static_cast<ExplosionPreset>(index);
        const auto dirt = make_effect_class(preset, SurfaceType::dirt);
        const auto concrete = make_effect_class(preset, SurfaceType::concrete);
        const auto& dirt_debris = layer(dirt, EffectLayer::debris);
        const auto& concrete_debris = layer(concrete, EffectLayer::debris);
        surfaces = surfaces && layer(concrete, EffectLayer::ground_dust).count < layer(dirt, EffectLayer::ground_dust).count &&
            layer(concrete, EffectLayer::sparks).count > layer(dirt, EffectLayer::sparks).count &&
            layer(concrete, EffectLayer::ground_dust).first_color.x > layer(dirt, EffectLayer::ground_dust).first_color.x &&
            (dirt_debris.count == 0 || (concrete_debris.count > dirt_debris.count && concrete_debris.size_min < dirt_debris.size_min &&
                concrete_debris.first_color.x > dirt_debris.first_color.x));
    }
    check(surfaces, "concrete: less and lighter dust, smaller lighter fragments, more sparks");

    check(effect_ticks(ExplosionPreset::small_he, SurfaceType::dirt) == 480, "small-he lives 8 s, its smoke's longest life");
    const auto& feed = layer(fuel, EffectLayer::plume);
    check(effect_ticks(ExplosionPreset::fuel_fire, SurfaceType::dirt) ==
        static_cast<std::uint32_t>(std::ceil((feed.delay_range + feed.lifetime_min + feed.lifetime_range) * 60.0f)),
        "a fed plume lives until its last-born record has died");

    const auto curve = light_curve(ExplosionPreset::artillery_he);
    check(light_intensity(curve, 0) == curve.flash_intensity + curve.glow_intensity &&
        light_intensity(curve, curve.flash_seconds) < curve.glow_intensity && light_intensity(curve, curve.flash_seconds) > 0 &&
        light_intensity(curve, curve.glow_seconds) == 0 && light_intensity(curve, -1) == 0,
        "a light starts at flash plus glow, keeps only the fading glow, then goes out");
    bool fading = true;
    for (int tick = 1; tick < 120 && fading; ++tick) fading = light_intensity(curve, tick / 60.0f) <= light_intensity(curve, (tick - 1) / 60.0f);
    check(fading, "lights never brighten");

    ExplosionSystem system(particle_layout::maximum_effects);
    for (std::uint32_t index = 0; index < 20; ++index)
    {
        const auto preset = index % 2 == 0 ? ExplosionPreset::small_he : ExplosionPreset::heavy;
        system.submit({{static_cast<float>(index), 0, 0}, {0, 1, 0}, index, preset, SurfaceType::dirt});
    }
    system.start(100);
    std::array<PointLight, particle_layout::maximum_lights> lights;
    const auto count = select_lights(system.slots(), system.active(), 103, lights);
    bool brightest = count == particle_layout::maximum_lights;
    for (std::uint32_t index = 0; index < 10 && brightest; ++index)
    {
        brightest = lights[index].position.x == static_cast<float>(2 * index + 1) &&
            lights[index].position.y == light_curve(ExplosionPreset::heavy).height;
    }
    for (std::uint32_t index = 10; index < count && brightest; ++index)
    {
        brightest = lights[index].position.x == static_cast<float>(2 * (index - 10)) && lights[index].radius == light_curve(ExplosionPreset::small_he).radius;
    }
    check(brightest, "the brightest lights win, equal ones in slot order, positioned above their events");
    check(select_lights(system.slots(), system.active(), 100 + 600, lights) == 0, "faded lights are not selected");
    std::array<PointLight, 4> few;
    check(select_lights(system.slots(), system.active(), 100, few) == 4, "selection respects the space given");

    const auto& heavy = system.slots()[1];
    check(heavy.preset == ExplosionPreset::heavy && heavy.effect_class == effect_class_index(ExplosionPreset::heavy, SurfaceType::dirt) &&
        heavy.lifetime_ticks == effect_ticks(ExplosionPreset::heavy, SurfaceType::dirt), "a slot keeps its preset, class and lifetime");

    check(Terrain::surface(30, 0) == SurfaceType::concrete && Terrain::surface(18, -18) == SurfaceType::concrete &&
        Terrain::surface(54, 18) == SurfaceType::concrete && Terrain::surface(17.9f, 0) == SurfaceType::dirt &&
        Terrain::surface(0, 0) == SurfaceType::dirt && Terrain::surface(30, -18.1f) == SurfaceType::dirt,
        "the pad is concrete, edges included; the rest of the range is dirt");

    const auto flow = [](ExplosionPreset preset) { return make_effect_class(preset, SurfaceType::dirt).flow; };
    check(flow(ExplosionPreset::small_impact).strength == 0 && flow(ExplosionPreset::small_impact).cloud_radius == 0 &&
        flow(ExplosionPreset::small_he).strength < flow(ExplosionPreset::artillery_he).strength &&
        flow(ExplosionPreset::artillery_he).strength < flow(ExplosionPreset::heavy).strength &&
        flow(ExplosionPreset::artillery_he).rise_height < flow(ExplosionPreset::heavy).rise_height &&
        flow(ExplosionPreset::heavy).core_radius > 0 && flow(ExplosionPreset::heavy).rise_time > 0,
        "flow fields grow with the preset; the small impact has none");
    bool smoke_parameters = true;
    for (const auto& effect : classes)
    {
        const auto& smoke = layer(effect, EffectLayer::blast_smoke);
        smoke_parameters = smoke_parameters && smoke.heat > 0 && smoke.cooling > 0 && smoke.turbulence > 0 && smoke.shadow_density > 0;
    }
    check(smoke_parameters, "every class's blast smoke is hot, cools, is turbulent and has a shadow density (it shades only with a flow field)");

    {
        namespace realism = realism_layout;
        const std::uint32_t expected_records[] = {32, 96, 176, 208, 136, 184, 192};
        const std::uint32_t expected_groups[] = {1, 2, 3, 4, 3, 3, 3};
        bool reservations = true;
        bool births = true;
        bool notice = true;
        bool counts = true;
        bool ticks = true;
        std::uint32_t largest_smoke = 0;
        std::uint32_t largest_groups = 0;
        std::uint32_t longest_graduation = 0;
        std::uint32_t shortest_life = 0xffffffffu;
        for (std::uint32_t preset = 0; preset < preset_count; ++preset)
        {
            for (std::uint32_t surface = 0; surface < surface_type_count; ++surface)
            {
                const auto effect = make_effect_class(static_cast<ExplosionPreset>(preset), static_cast<SurfaceType>(surface));
                const auto& lingering = effect.realism;
                if (surface == 0)
                {
                    reservations = reservations && lingering.residual_records == expected_records[preset] &&
                        lingering.residual_groups == expected_groups[preset];
                }
                std::uint32_t records = 0;
                std::uint32_t smoke = 0;
                std::uint32_t totals[3]{};
                for (std::uint32_t index = 0; index < 3; ++index)
                {
                    const auto& source = effect.layers[index];
                    const auto& linger = lingering.layers[index];
                    smoke += source.count;
                    if (source.count == 0)
                    {
                        reservations = reservations && linger.survivor_stride == 0;
                        continue;
                    }
                    reservations = reservations && linger.survivor_stride == 8 && linger.residual_offset == records &&
                        linger.size_cap > 0 && std::abs(linger.optical_depth + std::log(1 - source.strength)) < 1e-6f;
                    records += (source.count + 7) / 8;
                    const auto trail = trail_of(layer(effect, EffectLayer::blast_smoke));
                    const float latest_birth = std::max(source.delay_range, index == 1 ? static_cast<float>(trail.trail_ticks) / 60 : 0.0f) + 1 / source.fade_in_rate;
                    births = births && lingering.graduation_ticks > static_cast<std::uint32_t>(std::ceil(latest_birth * 60));
                    for (std::uint32_t stride = 0; stride < 3; ++stride) totals[stride] += (source.count + (4u << stride) - 1) / (4u << stride);
                }
                counts = counts && totals[0] <= 511 && totals[1] <= 511 && totals[2] <= 511;
                reservations = reservations && lingering.residual_records == records &&
                    lingering.residual_groups == (records + layout::simulation_group_size - 1) / layout::simulation_group_size;
                notice = notice && lingering.graduation_ticks >= realism::fade_unit_ticks + 4;
                const auto slab_ticks = realism_effect_ticks(static_cast<ExplosionPreset>(preset), static_cast<SurfaceType>(surface));
                ticks = ticks && slab_ticks >= lingering.graduation_ticks + realism::crossfade_ticks &&
                    slab_ticks <= effect_ticks(static_cast<ExplosionPreset>(preset), static_cast<SurfaceType>(surface)) + 600;
                largest_smoke = std::max(largest_smoke, smoke);
                largest_groups = std::max(largest_groups, lingering.residual_groups);
                longest_graduation = std::max(longest_graduation, lingering.graduation_ticks);
                const auto early = realism_early_ticks(static_cast<ExplosionPreset>(preset), static_cast<SurfaceType>(surface));
                ticks = ticks && early <= slab_ticks;
                shortest_life = std::min(shortest_life, early);
            }
        }
        check(reservations, "lingering layers keep every eighth record; residual ranges of 32/96/176/208/136/184/192 records on dirt");
        check(births, "every survivor is born and faded in before its effect graduates");
        bool star = true;
        for (std::uint32_t preset = 0; preset < preset_count; ++preset)
        {
            const auto kind = static_cast<ExplosionPreset>(preset);
            const auto effect = make_effect_class(kind, SurfaceType::dirt);
            const auto trail = trail_of(layer(effect, EffectLayer::blast_smoke));
            const auto clods = clods_of(layer(effect, EffectLayer::debris));
            const auto expected = trail_parameters(kind);
            star = star && std::memcmp(&trail, &expected, sizeof trail) == 0 && clods.size_exponent == 3 && clods.chunk_share > 0 && clods.lead_size > 1;
            const bool jets = kind != ExplosionPreset::small_impact && kind != ExplosionPreset::fuel_fire;
            star = star && (effect.look.jet_speed > 0) == jets && (trail.jets_min > 0) == jets;
            if (!jets) continue;
            star = star && trail.trail_ticks > 0 && trail.trail_ticks + 10 < effect.realism.graduation_ticks && trail.head_speed == 1.2f * effect.look.jet_speed &&
                trail.jets_min + trail.jets_range - 1 <= 9 && layer(effect, EffectLayer::debris).gravity == 9.81f && layer(effect, EffectLayer::debris).drag == 0 &&
                trail.hang_seconds > 0 && trail.knot_soot > 0 && trail.knot_share > 0;
        }
        check(star, "the star: every HE class but the small impact throws 3-9 jets whose knots are born within its trail ticks, led by clods at 9.81");
        check(notice, "graduation gives at least 124 ticks of notice, room for a 120-tick forced fade");
        check(counts, "a cluster's record count fits 9 bits for strides 4, 8 and 16");
        check(ticks, "a lingering slab frees after graduation and the crossfade");
        check(largest_groups * layout::maximum_effects <= realism::residual_groups / 2 && largest_groups <= 4,
            "128 of the largest reservations fit half the ring");
        const std::uint32_t window = longest_graduation + realism::crossfade_ticks;
        const std::uint32_t starts_per_slot = (window - 1) / shortest_life + 1;
        const std::uint32_t worst = (layout::maximum_effects - 1) * starts_per_slot * largest_groups + (largest_groups - 1);
        std::printf("residual ring: at most %u groups handed out while an effect waits %u ticks to graduate (of %u; slots restart after %u)\n",
            worst, window, realism::residual_groups, shortest_life);
        check(worst + largest_groups <= realism::residual_groups, "no reservation is handed out again before it graduates");
        check(largest_smoke * (layout::maximum_effects - 2) + layout::explosion_particles + realism::residual_records <= layout::light_list_first &&
                largest_smoke * layout::maximum_effects + realism::residual_records <= layout::light_list_first &&
                realism::pool_records <= layout::debris_list_first, "the smoke list holds every slab's smoke and the residual pool");
        const auto rotor = make_rotor_class();
        bool groups = true;
        for (std::uint32_t group = 0; group < layout::slab_groups; ++group) groups = groups && rotor.group_layers[group] == layout::layer_ground_dust;
        const auto& rotor_dust = rotor.realism.layers[layout::layer_ground_dust];
        check(groups && rotor.layers[layout::layer_ground_dust].count == layout::explosion_particles && rotor_dust.survivor_stride == 0 &&
                rotor_dust.residual_drag == 2.2f && rotor.layers[layout::layer_ground_dust].drag == 2.2f && rotor.realism.residual_groups == 0 &&
                rotor.blast.wave_ticks == 0 && rotor_class_index == effect_class_count && gpu_class_count <= layout::maximum_effect_classes &&
                gpu_effect_classes().size() == gpu_class_count,
            "the rotor class: 40 dust groups, no survivors, drag 2.2 /s, class 14 of 15");
        check(realism::residual_first == layout::maximum_effects * layout::explosion_particles &&
                realism::pool_records == realism::residual_first + realism::residual_groups * layout::simulation_group_size &&
                realism::maximum_unit_groups * 4 + layout::scan_threads * 12 <= 32768,
            "the residual pool follows the slabs, and the scan's shared memory holds every unit group");
    }

    {
        const float expected[] = {15.56f, 27.67f, 45.0f, 73.57f, 100.0f, 20.0f, 54.45f};
        bool heights = true;
        bool palettes = true;
        for (std::uint32_t preset = 0; preset < preset_count; ++preset)
        {
            const auto kind = static_cast<ExplosionPreset>(preset);
            const auto effect = make_effect_class(kind, SurfaceType::dirt);
            heights = heights && std::abs(stabilization_height(kind) - expected[preset]) < 0.01f && effect.realism.rise.x == stabilization_height(kind) &&
                effect.realism.rise.y > 0 && effect.realism.rise.z > 0 && effect.realism.spread.x > 0 && effect.realism.spread.y > 0;
            for (std::uint32_t index = 0; index < 3; ++index)
            {
                if (effect.layers[index].count == 0) continue;
                const auto& albedo = effect.realism.layers[index].albedo;
                palettes = palettes && albedo.x > 0 && albedo.x < 1 && albedo.w > 0 && (index != 0 || effect.realism.layers[index].settling > 0);
            }
        }
        check(heights, "stabilization heights: 15.6, 27.7, 45, 73.6, 100, 20 and 54.4 m by preset");
        check(palettes, "every lingering layer has a lingering colour, and dust settles");
    }

    {
        const auto at = [](float height) { return wind_at_height(height); };
        check(std::abs(at(0).strength - 0.35f) < 1e-6f && std::abs(at(15).strength - 1) < 1e-6f && std::abs(at(15.001f).strength - 1) < 1e-4f &&
            at(15).veer == 0, "the profile is continuous at 15 m, unturned below it");
        check(std::abs(at(200).strength - std::pow(200.0f / 15, 0.2f)) < 1e-6f && at(1000).strength == at(200).strength &&
            at(200).strength < 1.69f && std::abs(at(215).veer - 0.34906585f) < 1e-6f && at(1000).veer == at(215).veer,
            "above 15 m the wind strengthens to at most 1.68 times by 200 m and turns by at most 20 degrees");
        bool rising = true;
        for (float height = 0; height < 300; height += 0.5f) rising = rising && at(height + 0.5f).strength >= at(height).strength;
        check(rising, "the wind never weakens with height");
        check(maximum_wind_speed * 1.69f < 114, "the sheared wind stays below lingering smoke's 114 m/s limit");
    }

    const WindSettings settings{4, 90, 0.5f};
    const auto steady = wind_velocity(settings, 1234, false);
    check(std::abs(steady.x) < 1e-5f && std::abs(steady.z - 4) < 1e-5f && steady.y == 0, "a steady wind blows at its speed towards its heading");
    bool gusty = true;
    float lowest = 1e9f;
    float highest = 0;
    for (std::uint32_t tick = 0; tick < 60 * 120 && gusty; ++tick)
    {
        const auto wind = wind_velocity(settings, tick, true);
        const float speed = std::sqrt(wind.x * wind.x + wind.z * wind.z);
        lowest = std::min(lowest, speed);
        highest = std::max(highest, speed);
        const auto again = wind_velocity(settings, tick, true);
        gusty = speed >= 4 * 0.5f - 1e-4f && speed <= 4 * 1.5f + 1e-4f && again.x == wind.x && again.z == wind.z &&
            std::abs(std::atan2(wind.z, wind.x) - 1.5707963f) <= 0.5f * 12 * 3.1415927f / 180 + 1e-4f;
    }
    check(gusty && highest - lowest > 2.0f, "gusts are deterministic, bounded by the gustiness, and vary the speed");
    check(wind_velocity({4, 0, 0}, 77, true).x == 4, "no gustiness, no gusts");

    check(std::abs(overpressure_ratio(3) - 0.8127) < 5e-4 && std::abs(overpressure_ratio(10) - 0.0985) < 5e-4 &&
        std::abs(overpressure_ratio(20) - 0.0434) < 5e-4, "Kinney-Graham: dp/p0 0.813 at Z = 3, 0.0985 at 10, 0.0434 at 20");
    check(std::abs(particle_velocity(0.0985) - 22.97) < 0.01 && front_speed(0) == 340 && std::abs(front_speed(1) - 340 * std::sqrt(1 + 6.0 / 7)) < 1e-9,
        "u_p is 23.0 m/s at dp/p0 = 0.0985; the front moves at the speed of sound as dp/p0 goes to 0");
    {
        bool knots = true;
        bool arrivals = true;
        bool radii = true;
        bool ratios = true;
        bool lives = true;
        for (std::uint32_t preset = 0; preset < preset_count; ++preset)
        {
            const auto kind = static_cast<ExplosionPreset>(preset);
            const auto blast = make_effect_class(kind, SurfaceType::dirt).blast;
            const double kilograms = charge_kilograms(kind);
            const double scale = std::cbrt(kilograms);
            knots = knots && blast.knots[0].radius_cm == static_cast<std::uint32_t>(std::llround(scale * 100)) && blast.knots[0].arrival_q16 == 0 &&
                blast.knots.back().radius_cm == 60000 && blast.charge_kilograms == static_cast<float>(kilograms);
            for (std::uint32_t index = 1; index < blast.knots.size(); ++index)
            {
                const auto& knot = blast.knots[index];
                knots = knots && knot.radius_cm > blast.knots[index - 1].radius_cm && knot.arrival_q16 > blast.knots[index - 1].arrival_q16 &&
                    knot.ticks_per_cm_q24 > 0 && knot.overpressure < blast.knots[index - 1].overpressure;
                radii = radii && std::abs(blast_radius(blast, knot.arrival_q16 / 65536.0f) - knot.radius_cm / 100.0f) < 1e-3f * knot.radius_cm / 100.0f;
                ratios = ratios && std::abs(blast_overpressure(blast, knot.radius_cm / 100.0f) / knot.overpressure - 1) < 1e-4f;
            }
            lives = lives && blast.wave_ticks >= 100 && blast.wave_ticks <= 110 && blast.wave_ticks == (blast.knots.back().arrival_q16 + 0xffffu) >> 16;
            for (std::uint32_t centimetres = blast.knots[0].radius_cm + 50; centimetres <= 60000; centimetres += 137)
            {
                const double metres = centimetres / 100.0;
                const double exact = integrated_arrival_ticks(kilograms, metres);
                const auto tick = blast_arrival_tick(blast, centimetres);
                arrivals = arrivals && tick + 0.01 >= exact && tick <= exact + 1.01;
                ratios = ratios && std::abs(blast_overpressure(blast, static_cast<float>(metres)) / overpressure_ratio(metres / scale) - 1) < 0.03;
            }
        }
        check(knots, "blast knots: Z = 1 to 600 m, in order, overpressure falling");
        check(radii, "the front's radius is the knot's at each knot's arrival");
        check(arrivals, "the integer arrival tick is within a tick of the integrated arrival (rounded up) from Z = 1 to 600 m");
        check(ratios, "the overpressure ratio between knots is within 3% of Kinney-Graham, exact at the knots");
        check(lives, "a front lives 100-110 ticks (to 600 m)");
    }
    {
        const auto heavy_blast = make_effect_class(ExplosionPreset::heavy, SurfaceType::dirt).blast;
        const auto artillery_blast = make_effect_class(ExplosionPreset::artillery_he, SurfaceType::dirt).blast;
        check(wilson_opacity(heavy_blast, 8.0f, 0.8f) > 0.29f && wilson_opacity(heavy_blast, 8.0f, 0.95f) == wilson_opacity(heavy_blast, 8.0f, 1.0f) &&
            wilson_opacity(heavy_blast, 8.0f, 0.7f) == 0 && wilson_opacity(heavy_blast, 0, 0.95f) == 0 &&
            wilson_opacity(artillery_blast, 8.0f, 1.0f) == 0, "a Wilson shell only around heavy charges, only above 70% humidity");
        bool grows = true;
        bool clears = false;
        float last_outer = 0;
        for (float ticks = 1; ticks < 120; ticks += 1)
        {
            const auto shell = wilson_shell(heavy_blast, ticks, 0.95f);
            grows = grows && shell.outer >= last_outer && shell.outer <= heavy_blast.wilson.w && shell.inner <= shell.outer;
            last_outer = shell.outer;
            if (ticks > 2 && shell.opacity == 0) clears = true;
            if (clears) grows = grows && shell.opacity == 0;
        }
        check(grows && clears && wilson_shell(heavy_blast, 3, 0.95f).opacity > 0.5f, "the dome grows with the front, then clears for good");
    }
    {
        ExplosionSystem effects(64);
        std::vector<BlastParameters> blasts;
        for (const auto& effect : effect_classes()) blasts.push_back(effect.blast);
        for (std::uint32_t index = 0; index < 20; ++index)
        {
            effects.submit({{static_cast<float>(index), 0, 0}, {0, 1, 0}, index, ExplosionPreset::artillery_he});
            effects.start(index * 5);
        }
        const auto list = select_blasts(effects, blasts, 99, 100, 0.8f);
        bool newest = list.count == 16;
        for (std::uint32_t entry = 0; entry + 1 < list.count; ++entry)
        {
            newest = newest && effects.slots()[list.slots[entry]].start_tick > effects.slots()[list.slots[entry + 1]].start_tick;
        }
        const auto& first = effects.slots()[list.slots[0]];
        const auto& blast = blasts[first.effect_class];
        newest = newest && first.start_tick == 95 && list.rings[0].x == blast_radius(blast, 5) && list.fronts[0].x == blast_radius(blast, 4) &&
            list.rings[0].y == blast.wave.x * std::pow(blast_overpressure(blast, list.rings[0].x), blast.wave.w) && list.rings[0].w == 0 && list.fronts[0].z == push_speed(blast, list.rings[0].x);
        check(newest, "the blast list holds the 16 newest fronts, newest first, with their radius before and after the frame");
        const auto late = select_blasts(effects, blasts, 149, 150, 0.8f);
        bool young = true;
        for (std::uint32_t entry = 0; entry < late.count; ++entry)
        {
            const auto age = 150 - effects.slots()[late.slots[entry]].start_tick;
            young = young && age < blast.wave_ticks + realism_layout::push_return_ticks &&
                (age < blast.wave_ticks || (late.rings[entry].y == 0 && late.fronts[entry].y == 0 && late.fronts[entry].z == 0));
        }
        check(young && late.count < 16 && late.count > 0 && select_blasts(effects, blasts, 299, 300, 0.8f).count == 0,
            "a front leaves the list once its return has reached 600 m, and draws nothing after the front has");
        const auto& oldest = effects.slots()[list.slots[15]];
        const float lagged = static_cast<float>(100 - oldest.start_tick - realism_layout::push_return_ticks);
        const float first_knot = blast_radius(blast, 0);
        check(list.returns[0].x == first_knot && list.returns[0].w == first_knot && list.returns[15].w == blast_radius(blast, lagged) &&
            list.returns[15].x == blast_radius(blast, lagged - 1) && list.returns[15].z == push_speed(blast, list.returns[15].w),
            "the push's return is the same front push_return_ticks later");
        const auto crossing = select_blasts(effects, blasts, 95 + realism_layout::push_return_ticks - 1, 95 + realism_layout::push_return_ticks, 0.8f);
        check(crossing.returns[0].x == first_knot && crossing.returns[0].w == first_knot && list.fronts[0].x >= first_knot,
            "the frame that crosses the return's lag returns nothing inside the first knot");
        check(push_speed(blast, 1) == blast.push_cap && push_speed(blast, 100) < 1, "the push is capped near the origin and fades with distance");
    }
    {
        ExplosionSystem effects(4);
        std::vector<BlastParameters> blasts;
        for (const auto& effect : effect_classes()) blasts.push_back(effect.blast);
        effects.submit({{0, 0, 0}, {0, 1, 0}, 1, ExplosionPreset::heavy});
        effects.start(0);
        const float3 camera{30, 2, 0};
        const auto& blast = blasts[effects.slots()[effects.active()[0]].effect_class];
        const float arrival = blast_arrival_ticks(blast, std::sqrt(30.0f * 30 + 2 * 2));
        bool quiet_before = true;
        bool bounded = true;
        bool moving = false;
        bool quiet_after = true;
        for (std::uint32_t tick = 0; tick < 200; ++tick)
        {
            const float angle = shake_angle(effects, blasts, camera, tick);
            bounded = bounded && std::abs(angle) <= 0.02f;
            if (tick + 0.0f < arrival) quiet_before = quiet_before && angle == 0;
            else if (tick < arrival + 30) moving = moving || std::abs(angle) > 1e-3f;
            else if (tick > arrival + 91) quiet_after = quiet_after && angle == 0;
        }
        check(arrival > 2 && arrival < 8 && quiet_before && moving && bounded && quiet_after,
            "the camera shakes when the front reaches it, at most 0.02 rad, and stops 1.5 s later");
    }

    return failures == 0 ? 0 : 1;
}
