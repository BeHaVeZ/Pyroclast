#pragma once

#include "camera.hpp"
#include "test_range.hpp"
#include "../simulation/particle_system.hpp"

namespace pyroclast
{
class ExplosionRenderer final
{
public:
    static constexpr TargetFormat smoke_format = TargetFormat::rgba16_float;
    bool initialize(GraphicsDevice& device, bool half_resolution_smoke, TargetFormat scene_format);
    RenderResult render(GraphicsDevice& device, ParticleSystem& particles, const TestRange& range, const Camera& camera,
        int width, int height, double elapsed_seconds, bool capture = false, ImDrawData* overlay = nullptr);

private:
    std::unique_ptr<RenderTarget> scene_;
    std::unique_ptr<RenderTarget> depth_;
    std::unique_ptr<GraphicsPipeline> present_;
    std::unique_ptr<RenderTarget> smoke_target_;
    std::unique_ptr<GraphicsPipeline> composite_;
};
}
