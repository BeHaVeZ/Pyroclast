#include "graphics/draw_validation.hpp"

#include <cstdio>
#include <string_view>
#include <vector>

int main()
{
    using namespace pyroclast;
    constexpr std::uint32_t records = 1000;
    const VertexBuffer buffer(records, particle_layout::particle_stride);
    const VertexBuffer other_buffer(records, particle_layout::particle_stride);
    const VertexBuffer wrong_stride(records, 24);
    const GraphicsPipeline smoke(BlendMode::premultiplied_alpha);
    const GraphicsPipeline light(BlendMode::additive);
    const ComputePipeline compute(buffer);
    const ComputePipeline other_compute(other_buffer);
    const std::array<float, 4> clear{};

    int failures = 0;
    const auto check = [&failures](const char* name, const char* error, const char* expected_fragment) {
        const bool passed = expected_fragment
            ? error && std::string_view(error).find(expected_fragment) != std::string_view::npos
            : error == nullptr;
        if (!passed)
        {
            std::fprintf(stderr, "FAIL: %s (error: %s)\n", name, error ? error : "none");
            ++failures;
        }
    };
    const auto validate = [&](const VertexBuffer& vertices, const std::vector<DrawRange>& ranges,
        const ComputeDispatch* dispatch, bool half) {
        return validate_draw({vertices, ranges, clear, dispatch}, half);
    };

    const std::vector<DrawRange> layered{{smoke, 600, 400}, {light, 0, 600}};
    const ComputeDispatch dispatch{compute, {records, 1, 0, 1}, 16};
    check("layered draw with compute", validate(buffer, layered, &dispatch, false), nullptr);
    check("layered draw at half resolution", validate(buffer, layered, &dispatch, true), nullptr);
    check("draw without compute", validate(buffer, layered, nullptr, false), nullptr);
    check("range ending at the last record", validate(buffer, {{light, records - 1, 1}}, nullptr, false), nullptr);

    check("no ranges", validate(buffer, {}, nullptr, false), "at least one range");
    check("buffer with another stride", validate(wrong_stride, layered, nullptr, false), "particle buffer");
    check("empty range", validate(buffer, {{light, 0, 0}}, nullptr, false), "outside the vertex buffer");
    check("range starting past the end", validate(buffer, {{light, records + 1, 1}}, nullptr, false), "outside the vertex buffer");
    check("range running past the end", validate(buffer, {{light, 600, 401}}, nullptr, false), "outside the vertex buffer");
    check("range count that would overflow", validate(buffer, {{light, 2, 0xffffffffu}}, nullptr, false), "outside the vertex buffer");
    check("smoke layer", validate(buffer, {{smoke, 600, 400, particle_layout::particle_layer_smoke}}, nullptr, false), nullptr);
    check("unknown layer", validate(buffer, {{light, 0, 600, 2}}, nullptr, false), "unknown particle layer");

    check("half resolution without smoke", validate(buffer, {{light, 0, 600}}, nullptr, true), "one leading particle smoke range");
    check("half resolution with smoke last", validate(buffer, {{light, 0, 600}, {smoke, 600, 400}}, nullptr, true),
        "one leading particle smoke range");
    check("half resolution with two smoke ranges", validate(buffer, {{smoke, 600, 200}, {smoke, 800, 200}}, nullptr, true),
        "one leading particle smoke range");
    check("full resolution accepts any layer order", validate(buffer, {{light, 0, 600}, {smoke, 600, 400}}, nullptr, false), nullptr);

    const ComputeDispatch foreign{other_compute, {records, 1, 0, 1}, 16};
    const ComputeDispatch no_groups{compute, {records, 1, 0, 1}, 0};
    const ComputeDispatch too_many_groups{compute, {records, 1, 0, 1}, 65536};
    const ComputeDispatch most_groups{compute, {records, 1, 0, 1}, 65535};
    const ComputeDispatch wrong_count{compute, {records - 1, 1, 0, 1}, 16};
    check("compute bound to another buffer", validate(buffer, layered, &foreign, false), "Compute dispatch");
    check("compute without groups", validate(buffer, layered, &no_groups, false), "Compute dispatch");
    check("compute with too many groups", validate(buffer, layered, &too_many_groups, false), "Compute dispatch");
    check("compute at the group limit", validate(buffer, layered, &most_groups, false), nullptr);
    check("compute record count mismatch", validate(buffer, layered, &wrong_count, false), "Compute dispatch");

    return failures == 0 ? 0 : 1;
}
