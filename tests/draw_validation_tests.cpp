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
    return failures == 0 ? 0 : 1;
}
