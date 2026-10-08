#include "graphics/draw_validation.hpp"

#include <cstdio>
#include <string_view>
#include <vector>

int main()
{
    using namespace pyroclast;
    const Buffer particles(1024);
    const Buffer other(1024);
    Buffer writable(1024);
    const GraphicsPipeline smoke(BlendMode::premultiplied_alpha);
    const GraphicsPipeline light(BlendMode::additive);
    const ComputePipeline compute;
    RenderTarget half({2, {}});
    RenderTarget quarter({4, {}});

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
    const auto validate = [](const std::vector<Pass>& passes, const std::vector<Dispatch>& dispatches = {},
        std::size_t constant_bytes = 16) {
        const std::vector<std::byte> constants(constant_bytes);
        return validate_frame({dispatches, passes, {}, constants});
    };

    Bindings reads{};
    reads.read[0] = &particles;
    const Draw smoke_draw{smoke, reads, {}, 6, 400};
    const Draw light_draw{light, reads, {}, 6, 600};
    const Draw layered[] = {smoke_draw, light_draw};
    const std::vector<Pass> one_pass{{nullptr, {}, layered}};
    const std::vector<Pass> two_passes{{&half, {}, {&smoke_draw, 1}}, {nullptr, {&half, nullptr}, {&light_draw, 1}}};
    Bindings simulation{};
    simulation.write[0] = &writable;
    const Dispatch dispatch{compute, simulation, {}, {16, 1, 1}};
    check("one backbuffer pass with compute", validate(one_pass, {dispatch}), nullptr);
    check("one backbuffer pass without compute", validate(one_pass), nullptr);
    check("target sampled by the backbuffer pass", validate(two_passes, {dispatch}), nullptr);
    check("backbuffer pass that only clears", validate({{}}), nullptr);
    check("two targets feeding later passes", validate(
        {{&quarter, {}, {&smoke_draw, 1}}, {&half, {&quarter, nullptr}, {}}, {nullptr, {nullptr, &half}, {&light_draw, 1}}}), nullptr);
    check("two inputs in one pass", validate(
        {{&quarter, {}, {}}, {&half, {}, {}}, {nullptr, {&half, &quarter}, {}}}), nullptr);
    check("frame constants at the limit", validate(one_pass, {}, binding_layout::frame_constant_bytes), nullptr);
    check("no frame constants", validate(one_pass, {}, 0), nullptr);

    check("no passes", validate({}), "at least one pass");
    check("frame constants too large", validate(one_pass, {}, binding_layout::frame_constant_bytes + 4), "Frame constants");
    check("frame ending on a target", validate({{&half, {}, layered}}), "last pass");
    check("backbuffer pass before the last", validate({{nullptr, {}, layered}, {nullptr, {}, layered}}), "last pass");
    check("target drawn twice", validate(
        {{&half, {}, {&smoke_draw, 1}}, {&half, {}, {&smoke_draw, 1}}, {nullptr, {&half, nullptr}, {}}}), "more than one pass");
    check("input never drawn", validate({{nullptr, {&half, nullptr}, layered}}), "drawn earlier");
    check("input drawn later", validate(
        {{&quarter, {&half, nullptr}, {}}, {&half, {}, {&smoke_draw, 1}}, {nullptr, {&quarter, nullptr}, {}}}), "drawn earlier");
    check("target used as its own input", validate({{&half, {&half, nullptr}, {}}, {nullptr, {&half, nullptr}, {}}}), "drawn earlier");
    check("input in the second slot never drawn", validate({{nullptr, {nullptr, &quarter}, {}}}), "drawn earlier");

    const auto with_draw = [](const Draw& draw) { return std::vector<Pass>{{nullptr, {}, {&draw, 1}}}; };
    Bindings writes{};
    writes.write[2] = &writable;
    const Draw no_vertices{light, reads, {}, 0, 1};
    const Draw no_instances{light, reads, {}, 6, 0};
    const Draw writing{light, writes, {}, 6, 1};
    const Draw unbound{light, {}, {}, 3, 1};
    check("draw without vertices", validate(with_draw(no_vertices)), "at least one vertex");
    check("draw without instances", validate(with_draw(no_instances)), "at least one vertex");
    check("draw binding a writable buffer", validate(with_draw(writing)), "writable");
    check("draw without bindings", validate(with_draw(unbound)), nullptr);

    Bindings aliased{};
    aliased.read[3] = &writable;
    aliased.write[1] = &writable;
    Bindings separate{};
    separate.read[0] = &other;
    separate.write[0] = &writable;
    check("dispatch without groups", validate(one_pass, {{compute, simulation, {}, {0, 1, 1}}}), "group counts");
    check("dispatch with too many groups", validate(one_pass, {{compute, simulation, {}, {1, 65536, 1}}}), "group counts");
    check("dispatch at the group limit", validate(one_pass, {{compute, simulation, {}, {65535, 65535, 65535}}}), nullptr);
    check("dispatch reading and writing one buffer", validate(one_pass, {{compute, aliased, {}, {1, 1, 1}}}), "readable and writable");
    check("dispatch reading one buffer and writing another", validate(one_pass, {{compute, separate, {}, {1, 1, 1}}}), nullptr);
    check("dispatches in sequence", validate(one_pass, {dispatch, {compute, separate, {}, {4, 1, 1}}}), nullptr);

    RenderTarget scene({1, {}});
    RenderTarget second_scene({1, {}});
    RenderTarget depth({1, {}, TargetFormat::depth});
    const GraphicsPipeline writes_depth(BlendMode::opaque, DepthMode::test_write);
    const GraphicsPipeline tests_depth(BlendMode::premultiplied_alpha, DepthMode::test);
    const GraphicsPipeline ignores_depth(BlendMode::opaque, DepthMode::ignore);
    const Draw terrain{writes_depth, reads, {}, 6, 1};
    const Draw blended_particles{tests_depth, reads, {}, 6, 100};
    const Draw copy_ignoring_depth{ignores_depth, {}, {}, 3, 1};
    const Draw copy{light, {}, {}, 3, 1};
    const Pass opaque{&scene, {}, {&terrain, 1}, nullptr, false, &depth, true};
    const Pass blended{&scene, {&depth, nullptr}, {&blended_particles, 1}, nullptr, true, &depth, false};
    const Pass present{nullptr, {&scene, nullptr}, {&copy, 1}};
    check("opaque, blended and present passes", validate({opaque, blended, present}), nullptr);
    const Draw over_scene[] = {copy_ignoring_depth, blended_particles};
    check("backbuffer pass with read-only depth", validate(
        {opaque, {nullptr, {&scene, &depth}, over_scene, nullptr, false, &depth, false}}), nullptr);

    check("load without an earlier pass", validate({{&scene, {}, {}, nullptr, true}, present}), "load only");
    check("backbuffer load", validate({{nullptr, {}, {}, nullptr, true}}), "load only");
    check("colour pass on a depth target", validate({{&depth, {}, {}}, {nullptr, {}, {}}}), "a colour format");
    check("depth pass on a colour target", validate({{&scene, {}, {}, nullptr, false, &half, true}, present}), "depth format");
    check("depth target of another size", validate({{&half, {}, {}, nullptr, false, &depth, true}, {nullptr, {&half, nullptr}, {}}}), "size");
    check("depth cleared twice", validate({opaque, {&second_scene, {}, {}, nullptr, false, &depth, true}, present}), "more than one pass");
    check("read-only depth never written", validate({{nullptr, {}, {}, nullptr, false, &depth, false}}), "written by an earlier pass");
    check("writable depth without a target", validate({{nullptr, {}, {}, nullptr, false, nullptr, true}}), "needs a depth target");
    check("sampling the loaded target", validate({opaque, {&scene, {&scene, nullptr}, {}, nullptr, true}, present}), "cannot sample");
    check("depth sampled before it is written", validate({{&scene, {&depth, nullptr}, {&terrain, 1}, nullptr, false, &depth, true}, present}), "drawn earlier");
    check("depthless pipeline in a depth pass", validate({opaque, {&scene, {}, {&copy, 1}, nullptr, true, &depth, false}, present}), "depth mode");
    check("depth pipeline without a depth target", validate({opaque, {nullptr, {&scene, nullptr}, {&blended_particles, 1}}}), "depth mode");
    RenderTarget hdr({1, {}, TargetFormat::rgba16_float});
    const GraphicsPipeline hdr_terrain(BlendMode::opaque, DepthMode::test_write, TargetFormat::rgba16_float);
    const Draw hdr_draw{hdr_terrain, reads, {}, 6, 1};
    const Pass hdr_scene{&hdr, {}, {&hdr_draw, 1}, nullptr, false, &depth, true};
    const Pass tonemap{nullptr, {&hdr, nullptr}, {&copy, 1}};
    check("an HDR scene tonemapped to the backbuffer", validate({hdr_scene, tonemap}), nullptr);
    check("a backbuffer pipeline in an HDR pass", validate({{&hdr, {}, {&terrain, 1}, nullptr, false, &depth, true}, tonemap}), "colour format");
    check("an HDR pipeline in the backbuffer pass", validate({{nullptr, {}, {&hdr_draw, 1}, nullptr, false, &depth, true}}), "colour format");
    Buffer arguments(64);
    Draw indirect{light, reads, {}, 0, 0};
    indirect.indirect = &arguments;
    indirect.indirect_offset = 48;
    check("an indirect draw ignores its own counts", validate(with_draw(indirect)), nullptr);
    indirect.indirect_offset = 52;
    check("indirect arguments past the buffer", validate(with_draw(indirect)), "Indirect draw arguments");
    indirect.indirect_offset = 2;
    check("unaligned indirect arguments", validate(with_draw(indirect)), "Indirect draw arguments");
    indirect.indirect_offset = 0xFFFFFFF0u;
    check("indirect arguments whose end wraps around", validate(with_draw(indirect)), "Indirect draw arguments");
    const auto with_readback = [&](Frame::Readback readback) {
        const std::vector<std::byte> constants(16);
        Frame frame{{}, one_pass, {}, constants};
        frame.readback = readback;
        return validate_frame(frame);
    };
    check("a readback of four counters", with_readback({&arguments, 48, 16}), nullptr);
    check("a readback past its source", with_readback({&arguments, 56, 16}), "readback");
    check("a readback without a source", with_readback({nullptr, 0, 16}), "readback");
    check("a readback larger than allowed", with_readback({&writable, 0, max_readback_bytes + 4}), "readback");
    check("a readback whose end wraps around", with_readback({&arguments, 0xFFFFFFF0u, 16}), "readback");
    check("an empty readback from a source", with_readback({&arguments, 0, 0}), "readback");
    check("a readback of a partial word", with_readback({&arguments, 0, 6}), "readback");
    check("an unaligned readback", with_readback({&arguments, 2, 16}), "readback");
    check("depth writes against read-only depth", validate({opaque, {&scene, {}, {&terrain, 1}, nullptr, true, &depth, false}, present}), "depth-writing");

    const std::vector<std::byte> no_constants;
    const auto counted = count_work({std::span(&dispatch, 1), two_passes, {}, no_constants});
    const auto drawn = count_work({{}, one_pass, {}, no_constants});
    const std::vector<Pass> clears{{}, {}};
    const auto cleared = count_work({{}, clears, {}, no_constants});
    if (counted.dispatches != 1 || counted.passes != 2 || counted.draws != 2 || drawn.dispatches != 0 || drawn.passes != 1 ||
        drawn.draws != 2 || cleared.passes != 2 || cleared.draws != 0)
    {
        std::fputs("FAIL: count_work counts dispatches, passes and draws\n", stderr);
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
