#include "app/counter_log.hpp"
#include "app/frame_statistics.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

int main()
{
    using pyroclast::summarize;
    int failures = 0;
    const auto check = [&failures](const char* name, pyroclast::FrameStatistics actual, double average, double median, double p99) {
        const auto equal = [](double left, double right) { return std::abs(left - right) < 1e-9; };
        if (!equal(actual.average, average) || !equal(actual.median, median) || !equal(actual.p99, p99))
        {
            std::fprintf(stderr, "FAIL: %s (average %.9f, median %.9f, p99 %.9f)\n", name, actual.average, actual.median, actual.p99);
            ++failures;
        }
    };
    const auto sequence = [](int count) {
        std::vector<double> values;
        for (int value = count; value >= 1; --value) values.push_back(value);
        return values;
    };

    auto benchmark = sequence(600);
    check("600-frame benchmark sample", summarize(benchmark), 300.5, 300.5, 594);
    auto hundred = sequence(100);
    check("100 samples", summarize(hundred), 50.5, 50.5, 99);
    auto hundred_and_one = sequence(101);
    check("101 samples", summarize(hundred_and_one), 51, 51, 100);
    auto odd = sequence(5);
    check("odd count uses the middle value", summarize(odd), 3, 3, 5);
    auto pair = sequence(2);
    check("two samples", summarize(pair), 1.5, 1.5, 2);
    std::vector<double> single{4.25};
    check("single sample", summarize(single), 4.25, 4.25, 4.25);
    std::vector<double> spike(600, 2.0);
    for (int index = 0; index < 6; ++index) spike[static_cast<std::size_t>(index) * 100] = 9.0;
    check("six slow frames in 600 leave p99 at the common value", summarize(spike), 2.07, 2.0, 2.0);
    spike[1] = 9.0;
    check("a seventh slow frame moves p99", summarize(spike), 2.0 + 7 * 7.0 / 600, 2.0, 9.0);

    auto twenty = sequence(20);
    const auto tail = summarize(twenty);
    auto thousand = sequence(1000);
    const auto large = summarize(thousand);
    if (tail.p95 != 19 || tail.max != 20 || large.p95 != 950 || large.p99 != 990 || large.max != 1000)
    {
        std::fprintf(stderr, "FAIL: p95 and max (20: %.1f %.1f; 1000: %.1f %.1f %.1f)\n", tail.p95, tail.max, large.p95, large.p99, large.max);
        ++failures;
    }
    auto worst_600 = sequence(600);
    auto worst_100 = sequence(100);
    auto worst_101 = sequence(101);
    auto worst_1000 = sequence(1000);
    std::vector<double> worst_single{4.25};
    std::vector<double> six_spikes(600, 2.0);
    for (int index = 0; index < 6; ++index) six_spikes[static_cast<std::size_t>(index) * 100] = 9.0;
    if (summarize(worst_600).worst_1_percent != 597.5 || summarize(worst_100).worst_1_percent != 100 ||
        summarize(worst_101).worst_1_percent != 100.5 || summarize(worst_1000).worst_1_percent != 995.5 ||
        summarize(worst_single).worst_1_percent != 4.25 || summarize(six_spikes).worst_1_percent != 9.0)
    {
        std::fputs("FAIL: the 1% worst is the mean of the slowest ceil(n/100) samples\n", stderr);
        ++failures;
    }

    pyroclast::ParticleCounters first_record;
    first_record.tick = 1;
    first_record.alive = 2;
    first_record.smoke = 3;
    first_record.light = 4;
    std::uint64_t expected_checksum = 0xcbf29ce484222325ull;
    for (const std::uint32_t word : {1u, 2u, 3u, 4u})
    {
        for (int byte = 0; byte < 4; ++byte) expected_checksum = (expected_checksum ^ ((word >> (8 * byte)) & 0xff)) * 0x100000001b3ull;
    }
    first_record.debris = 99;
    if (pyroclast::counter_checksum(pyroclast::checksum_basis, first_record) != expected_checksum ||
        pyroclast::counter_checksum(pyroclast::checksum_basis, {}) == expected_checksum)
    {
        std::fputs("FAIL: counter_checksum folds tick, alive, smoke and light, not debris\n", stderr);
        ++failures;
    }
    std::printf("counter checksum of {1, 2, 3, 4}: %016llx\n", static_cast<unsigned long long>(expected_checksum));

    pyroclast::TickRate rate;
    if (rate.rate() != 0)
    {
        std::fputs("FAIL: no rate before two samples\n", stderr);
        ++failures;
    }
    for (int frame = 0; frame <= 240; ++frame) rate.add(frame / 60.0, static_cast<std::uint32_t>(frame / 4));
    if (std::abs(rate.rate() - 15.0) > 0.3)
    {
        std::fprintf(stderr, "FAIL: 0.25x at 60 frames a second is 15 ticks a second (got %f)\n", rate.rate());
        ++failures;
    }
    for (int frame = 241; frame <= 480; ++frame) rate.add(frame / 60.0, static_cast<std::uint32_t>(60 + (frame - 240)));
    if (std::abs(rate.rate() - 60.0) > 0.5)
    {
        std::fprintf(stderr, "FAIL: real time is 60 ticks a second over the last second (got %f)\n", rate.rate());
        ++failures;
    }

    pyroclast::CounterLog log;
    log.add_frame({1, 1, 0, 0, 0, false});
    log.set_cpu_ms(0.5);
    log.add_frame({2, 1, 3, 3, 1, true});
    log.add_timing(0.25, 0.01);
    log.add_frame({3, 1, 3, 0, 0, true});
    log.add_timing(0.5, 0.02);
    pyroclast::ParticleCounters record;
    record.tick = 2;
    record.alive = 70;
    record.smoke = 40;
    record.light = 20;
    record.debris = 10;
    log.add_counters(record);
    const std::string expected =
        "frame,tick,steps,active,started,dropped,counter_tick,alive,smoke,light,debris,draw_ms,compute_ms,cpu_ms\n"
        "0,1,1,0,0,0,,,,,,0.250000,0.010000,0.5000\n"
        "1,2,1,3,3,1,2,70,40,20,10,0.500000,0.020000,0.0000\n"
        "2,3,1,3,0,0,,,,,,,,0.0000\n";
    if (log.csv() != expected || log.mismatches() != 0 || log.size() != 3)
    {
        std::fprintf(stderr, "FAIL: counter log rows\n%s", log.csv().c_str());
        ++failures;
    }
    pyroclast::CounterLog covered(true);
    covered.add_frame({4, 1, 1, 1, 0, true});
    record.tick = 4;
    record.offscreen = 3;
    record.smoke_coverage = {1, 2, 3, 4};
    record.light_coverage = 5;
    covered.add_counters(record);
    const std::string covered_expected =
        "frame,tick,steps,active,started,dropped,counter_tick,alive,smoke,light,debris,draw_ms,compute_ms,cpu_ms,"
        "offscreen,smoke_bin0,smoke_bin1,smoke_bin2,smoke_bin3,coverage_smoke,coverage_light\n"
        "0,4,1,1,1,0,4,70,40,20,10,,,0.0000,3,1,2,3,4,10,5\n";
    if (covered.csv() != covered_expected)
    {
        std::fprintf(stderr, "FAIL: counter log coverage columns\n%s", covered.csv().c_str());
        ++failures;
    }
    pyroclast::CounterLog thinned(true, true, true, true);
    thinned.add_frame({4, 1, 1, 1, 0, true, true, 2});
    record.thinned = 6;
    record.faint = 7;
    thinned.add_counters(record);
    const std::string thinned_expected =
        "frame,tick,steps,active,started,dropped,counter_tick,alive,smoke,light,debris,draw_ms,compute_ms,cpu_ms,"
        "offscreen,smoke_bin0,smoke_bin1,smoke_bin2,smoke_bin3,coverage_smoke,coverage_light,thinned,faint,half,evicted\n"
        "0,4,1,1,1,0,4,70,40,20,10,,,0.0000,3,1,2,3,4,10,5,6,7,1,2\n";
    if (thinned.csv() != thinned_expected)
    {
        std::fprintf(stderr, "FAIL: counter log thinned, faint and evicted columns\n%s", thinned.csv().c_str());
        ++failures;
    }
    record.tick = 7;
    log.add_counters(record);
    log.add_timing(1, 1);
    log.add_timing(1, 1);
    if (log.mismatches() != 2)
    {
        std::fprintf(stderr, "FAIL: counter log mismatches (got %zu)\n", log.mismatches());
        ++failures;
    }

    pyroclast::CoverageStatistics coverage;
    if (!coverage.json().empty())
    {
        std::fputs("FAIL: no coverage record without samples\n", stderr);
        ++failures;
    }
    const std::uint32_t frame_a[] = {100, 0, 0, 0};
    const std::uint32_t frame_b[] = {0, 100, 0, 100};
    coverage.add(frame_a, 50, 6, 1600);
    coverage.add(frame_b, 0, 2, 1600);
    const std::string coverage_expected = "\"coverage\":{\"samples\":2,\"smoke\":{\"average\":1.5000,\"p95\":2.0000,\"max\":2.0000},"
        "\"bins\":[0.5000,0.5000,0.0000,0.5000],\"light\":0.2500,\"offscreen\":4.0}";
    if (coverage.samples() != 2 || coverage.json() != coverage_expected)
    {
        std::fprintf(stderr, "FAIL: coverage statistics %s\n", coverage.json().c_str());
        ++failures;
    }

    return failures == 0 ? 0 : 1;
}
