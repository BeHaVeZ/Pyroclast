#pragma once

#include "camera.hpp"
#include "scalability.hpp"
#include "showcase_marks.hpp"
#include "smoke_resolution_policy.hpp"
#include "test_range.hpp"
#include "../simulation/explosion_system.hpp"
#include "../simulation/particle_system.hpp"
#include "../simulation/wind.hpp"
#include "../graphics/draw_validation.hpp"

namespace pyroclast
{
class ExplosionRenderer final
{
public:
    static constexpr TargetFormat smoke_format = TargetFormat::rgba16_float;
    bool initialize(GraphicsDevice& device, TargetFormat scene_format);
    struct FrameOptions
    {
        bool capture = false;
        ImDrawData* overlay = nullptr;
        DebugViews views;
        std::uint32_t layer_mask = particle_layout::full_mask;
        WindSettings wind;
        std::span<const ShowcaseMark> marks;
        ScalabilitySettings scalability;
        std::optional<ParticleCounters> counters;
    };
    RenderResult render(GraphicsDevice& device, ParticleSystem& particles, ExplosionSystem& effects, const TestRange& range,
        const Camera& camera, int width, int height, double elapsed_seconds, const FrameOptions& options);
    std::uint32_t lights() const { return lights_; }
    bool half_smoke() const { return half_smoke_; }
    const FrameWork& submitted_work() const { return submitted_work_; }
    const SmokeResolutionPolicy& smoke_policy() const { return smoke_policy_; }

private:
    std::unique_ptr<RenderTarget> scene_;
    std::unique_ptr<RenderTarget> depth_;
    std::unique_ptr<GraphicsPipeline> present_;
    std::unique_ptr<GraphicsPipeline> lines_;
    std::unique_ptr<GraphicsPipeline> showcase_lines_;
    std::unique_ptr<RenderTarget> smoke_target_;
    std::unique_ptr<GraphicsPipeline> composite_;
    std::uint32_t lights_ = 0;
    bool half_smoke_ = false;
    FrameWork submitted_work_;
    SmokeResolutionPolicy smoke_policy_;
};
}
