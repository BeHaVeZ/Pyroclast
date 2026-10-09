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
                    const float latest_birth = source.delay_range + 1 / source.fade_in_rate;
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
        check(largest_smoke * layout::maximum_effects + realism::residual_records <= layout::light_list_first &&
                realism::pool_records <= layout::debris_list_first, "the smoke list holds every slab's smoke and the residual pool");
        check(realism::residual_first == layout::maximum_effects * layout::explosion_particles &&
                realism::pool_records == realism::residual_first + realism::residual_groups * layout::simulation_group_size &&
                realism::maximum_unit_groups * 4 + layout::scan_threads * 12 <= 32768,
            "the residual pool follows the slabs, and the scan's shared memory holds every unit group");
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

    return failures == 0 ? 0 : 1;
}
