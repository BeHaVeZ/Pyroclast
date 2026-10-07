#include "d3d12_renderer.hpp"
#include "d3d12_resources.hpp"
#include "d3d12_shaders.hpp"

#include <SDL3/SDL.h>

namespace pyroclast
{
std::unique_ptr<ComputePipeline> D3D12Renderer::create_compute_pipeline(Shader program)
{
    const auto shader = d3d12_shader(program);
    auto pipeline = std::make_unique<D3D12ComputePipeline>();
    pipeline->owner = this;
    D3D12_COMPUTE_PIPELINE_STATE_DESC description{};
    description.pRootSignature = root_signature_.Get();
    description.CS = {shader.data(), shader.size()};
    if (!check(device_->CreateComputePipelineState(&description, IID_PPV_ARGS(&pipeline->state)),
        "Create compute pipeline") || !check_validation())
    {
        return nullptr;
    }
    return pipeline;
}
}
