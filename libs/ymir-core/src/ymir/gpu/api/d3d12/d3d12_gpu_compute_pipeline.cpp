#include <ymir/gpu/api/d3d12/d3d12_gpu_compute_pipeline.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

#include <fmt/format.h>

namespace ymir::gpu {

D3D12ComputePipeline::D3D12ComputePipeline(d3d12::D3D12PipelineState &&pipelineState, const ComputePipelineSpec &spec)
    : IGPUComputePipeline(kBackend, spec)
    , m_pipelineState(std::move(pipelineState)) {}

GPUObjectResult<IGPUComputePipeline> D3D12ComputePipeline::Create(d3d12::D3D12Device &device,
                                                                  const d3d12::D3D12RootSignature &rootSig,
                                                                  const ComputePipelineSpec &spec) {
    d3d12::D3D12PipelineState pipelineState{};
    auto builder = pipelineState.ComputeBuilder();
    // TODO: builder.NodeMask(nodeMask);
    // TODO: builder.Flags(...)?
    // TODO: builder.CachedPSO()?
    HRESULT hr = builder.Build(device, rootSig, spec.shader.bytecode.data(), spec.shader.bytecode.size());
    if (FAILED(hr)) {
        return GPUOperationError{fmt::format("Failed to build compute pipeline: error code {:X}", hr)};
    }

    return {std::make_unique<D3D12ComputePipeline>(std::move(pipelineState), spec)};
}

void D3D12ComputePipeline::SetName(std::string_view name) {
    m_pipelineState->SetName(util::StringToWString(name).c_str());
}

} // namespace ymir::gpu
