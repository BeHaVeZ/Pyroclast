#include "app/smoke_resolution_policy.hpp"

#include <cmath>
#include <cstdio>

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

ParticleCounters covering(double screens, double pixels)
{
    ParticleCounters counters;
    counters.smoke_coverage[3] = static_cast<std::uint32_t>(std::lround(screens * pixels / particle_layout::coverage_unit_pixels));
    counters.smoke = 1000;
    return counters;
}

bool run(SmokeResolutionPolicy& policy, const std::optional<ParticleCounters>& record, const SmokeBudget& budget, double pixels,
    int frames)
{
    bool half = policy.half();
    for (int frame = 0; frame < frames; ++frame)
    {
        half = policy.decide(record, budget, pixels);
        policy.commit(half);
    }
    return half;
}
}

int main()
{
    constexpr double pixels = 1280.0 * 720.0;
    const SmokeBudget coverage{false, 100};
    check(SmokeResolutionPolicy::limit({}) == SmokeResolutionPolicy::default_coverage_screens &&
        SmokeResolutionPolicy::limit({true, 0}) == SmokeResolutionPolicy::default_smoke_particles && SmokeResolutionPolicy::limit(coverage) == 100,
        "an unset budget takes the default limit of its kind");
    ParticleCounters sample;
    sample.smoke_coverage = {1, 2, 3, 4};
    sample.smoke = 777;
    check(std::abs(SmokeResolutionPolicy::load(sample, coverage, 160) - 1.0) < 1e-12 && SmokeResolutionPolicy::load(sample, {true, 5}, 160) == 777,
        "the load is screens of smoke coverage (16-pixel units over the backbuffer), or smoke particles");

    SmokeResolutionPolicy policy;
    check(!policy.half() && !policy.decide(std::nullopt, coverage, pixels), "it starts at full resolution and keeps it without a record");
    check(!policy.decide(covering(99, pixels), coverage, pixels) && policy.decide(covering(100, pixels), coverage, pixels),
        "half resolution from the limit");
    const auto heavy = covering(150, pixels);
    check(policy.decide(heavy, coverage, pixels) && !policy.half() && policy.switches() == 0, "deciding does not change the state");
    policy.commit(true);
    check(policy.half() && policy.switches() == 1 && policy.half_frames() == 1, "a submitted half frame switches and counts");
    const auto light = covering(10, pixels);
    check(run(policy, light, coverage, pixels, SmokeResolutionPolicy::dwell_frames) && policy.switches() == 1,
        "within the dwell the choice holds");
    check(!run(policy, light, coverage, pixels, 1) && policy.switches() == 2 && policy.half_frames() == SmokeResolutionPolicy::dwell_frames + 1,
        "after the dwell a light load returns to full resolution");
    const auto band = covering(90, pixels);
    check(!run(policy, band, coverage, pixels, 100) && policy.switches() == 2, "inside the band full resolution holds");
    run(policy, heavy, coverage, pixels, 1);
    check(run(policy, band, coverage, pixels, 100) && policy.switches() == 3, "inside the band half resolution holds");
    check(!run(policy, covering(80, pixels), coverage, pixels, 1) && policy.switches() == 4, "at down_share of the limit it returns to full");
    SmokeResolutionPolicy alternating;
    std::uint64_t frames = 0;
    for (int frame = 0; frame < 300; ++frame, ++frames) alternating.commit(alternating.decide(frame % 2 == 0 ? heavy : light, coverage, pixels));
    check(alternating.switches() <= frames / SmokeResolutionPolicy::dwell_frames + 1, "an alternating load cannot switch faster than the dwell");
    SmokeResolutionPolicy first;
    SmokeResolutionPolicy second;
    bool same = true;
    for (int frame = 0; frame < 500; ++frame)
    {
        const auto record = covering(60 + (frame * 37) % 90, pixels);
        const bool a = first.decide(record, coverage, pixels);
        const bool b = second.decide(record, coverage, pixels);
        same &= a == b;
        first.commit(a);
        second.commit(b);
    }
    check(same && first.switches() == second.switches() && first.half_frames() == second.half_frames(), "the choices depend only on the trace");
    SmokeResolutionPolicy counted;
    ParticleCounters many;
    many.smoke = 70000;
    check(counted.decide(many, {true, 60000}, pixels) && !counted.decide(sample, {true, 60000}, pixels), "the count arm compares smoke particles");

    if (failures == 0) std::puts("scalability tests passed");
    return failures == 0 ? 0 : 1;
}
