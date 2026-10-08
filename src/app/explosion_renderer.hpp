#pragma once

#include "camera.hpp"
#include "showcase_marks.hpp"
#include "test_range.hpp"
#include "../simulation/explosion_system.hpp"
#include "../simulation/particle_system.hpp"
#include "../simulation/wind.hpp"

namespace pyroclast
{
class ExplosionRenderer final
{
public:
    static constexpr TargetFormat smoke_format = TargetFormat::rgba16_float;
    bool initialize(GraphicsDevice& device, bool half_resolution_smoke, TargetFormat scene_format);
    struct FrameOptions
    {
        bool capture = false;
        ImDrawData* overlay = nullptr;
        DebugViews views;
        std::uint32_t layer_mask = particle_layout::full_mask;
        WindSettings wind;
        std::span<const ShowcaseMark> marks;
    };
    RenderResult render(GraphicsDevice& device, ParticleSystem& particles, ExplosionSystem& effects, const TestRange& range,
        const Camera& camera, int width, int height, double elapsed_seconds, const FrameOptions& options);
    std::uint32_t lights() const { return lights_; }

private:
    std::unique_ptr<RenderTarget> scene_;
    std::unique_ptr<RenderTarget> depth_;
    std::unique_ptr<GraphicsPipeline> present_;
    std::unique_ptr<GraphicsPipeline> lines_;
    std::unique_ptr<GraphicsPipeline> showcase_lines_;
    std::unique_ptr<RenderTarget> smoke_target_;
    std::unique_ptr<GraphicsPipeline> composite_;
    std::uint32_t lights_ = 0;
};
}
