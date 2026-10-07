#include "graphics/draw_validation.hpp"

#include <cstdio>
#include <string_view>
#include <vector>

int main()
{
    using namespace pyroclast;
    constexpr std::uint32_t records = 1000;
    const Buffer buffer(records * particle_layout::particle_stride);
    const Buffer other_buffer(records * particle_layout::particle_stride);
    const Buffer wrong_stride(records * particle_layout::particle_stride + 8);
    const GraphicsPipeline smoke(BlendMode::premultiplied_alpha);
    const GraphicsPipeline light(BlendMode::additive);
    const ComputePipeline compute(buffer);
    const ComputePipeline other_compute(other_buffer);
    RenderTarget half({2, {}});
    RenderTarget quarter({4, {}});
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
    const auto validate = [&](const Buffer& vertices, const std::vector<Pass>& passes, const ComputeDispatch* dispatch) {
        return validate_frame({vertices, passes, clear, dispatch});
    };

    const DrawRange smoke_range{smoke, 600, 400, particle_layout::particle_layer_smoke};
    const DrawRange light_range{light, 0, 600, particle_layout::particle_layer_emissive};
    const DrawRange layered[] = {smoke_range, light_range};
    const std::vector<Pass> one_pass{{nullptr, nullptr, layered}};
    const std::vector<Pass> two_passes{{&half, nullptr, {&smoke_range, 1}}, {nullptr, &half, {&light_range, 1}}};
    const ComputeDispatch dispatch{compute, {records, 1, 0, 1}, 16};
    check("one backbuffer pass with compute", validate(buffer, one_pass, &dispatch), nullptr);
    check("one backbuffer pass without compute", validate(buffer, one_pass, nullptr), nullptr);
    check("target pass used as the backbuffer background", validate(buffer, two_passes, &dispatch), nullptr);
    check("backbuffer pass that only clears", validate(buffer, {{}}, nullptr), nullptr);
    check("two targets feeding later passes", validate(buffer,
        {{&quarter, nullptr, {&smoke_range, 1}}, {&half, &quarter, {}}, {nullptr, &half, {&light_range, 1}}}, nullptr), nullptr);

    check("no passes", validate(buffer, {}, nullptr), "at least one pass");
    check("buffer that is not whole records", validate(wrong_stride, one_pass, nullptr), "particle buffer");
    check("frame ending on a target", validate(buffer, {{&half, nullptr, layered}}, nullptr), "last pass");
    check("backbuffer pass before the last", validate(buffer, {{nullptr, nullptr, layered}, {nullptr, nullptr, layered}}, nullptr), "last pass");
    check("target drawn twice", validate(buffer,
        {{&half, nullptr, {&smoke_range, 1}}, {&half, nullptr, {&smoke_range, 1}}, {nullptr, &half, {}}}, nullptr), "more than one pass");
    check("background never drawn", validate(buffer, {{nullptr, &half, layered}}, nullptr), "drawn earlier");
    check("background drawn later", validate(buffer,
        {{&quarter, &half, {}}, {&half, nullptr, {&smoke_range, 1}}, {nullptr, &quarter, {}}}, nullptr), "drawn earlier");
    check("target used as its own background", validate(buffer, {{&half, &half, {}}, {nullptr, &half, {}}}, nullptr), "drawn earlier");

    const auto with_range = [&](const DrawRange& range) { return std::vector<Pass>{{nullptr, nullptr, {&range, 1}}}; };
    const DrawRange last_record{light, records - 1, 1, 0};
    const DrawRange empty{light, 0, 0, 0};
    const DrawRange past_end{light, records + 1, 1, 0};
    const DrawRange too_long{light, 600, 401, 0};
    const DrawRange overflowing{light, 2, 0xffffffffu, 0};
    const DrawRange unknown_layer{light, 0, 600, 2};
    check("range ending at the last record", validate(buffer, with_range(last_record), nullptr), nullptr);
    check("empty range", validate(buffer, with_range(empty), nullptr), "outside the vertex buffer");
    check("range starting past the end", validate(buffer, with_range(past_end), nullptr), "outside the vertex buffer");
    check("range running past the end", validate(buffer, with_range(too_long), nullptr), "outside the vertex buffer");
    check("range count that would overflow", validate(buffer, with_range(overflowing), nullptr), "outside the vertex buffer");
    check("unknown layer", validate(buffer, with_range(unknown_layer), nullptr), "unknown particle layer");
    check("bad range in a target pass", validate(buffer, {{&half, nullptr, {&too_long, 1}}, {nullptr, &half, {}}}, nullptr),
        "outside the vertex buffer");

    const ComputeDispatch foreign{other_compute, {records, 1, 0, 1}, 16};
    const ComputeDispatch no_groups{compute, {records, 1, 0, 1}, 0};
    const ComputeDispatch too_many_groups{compute, {records, 1, 0, 1}, 65536};
    const ComputeDispatch most_groups{compute, {records, 1, 0, 1}, 65535};
    const ComputeDispatch wrong_count{compute, {records - 1, 1, 0, 1}, 16};
    check("compute bound to another buffer", validate(buffer, one_pass, &foreign), "Compute dispatch");
    check("compute without groups", validate(buffer, one_pass, &no_groups), "Compute dispatch");
    check("compute with too many groups", validate(buffer, one_pass, &too_many_groups), "Compute dispatch");
    check("compute at the group limit", validate(buffer, one_pass, &most_groups), nullptr);
    check("compute record count mismatch", validate(buffer, one_pass, &wrong_count), "Compute dispatch");

    return failures == 0 ? 0 : 1;
}
