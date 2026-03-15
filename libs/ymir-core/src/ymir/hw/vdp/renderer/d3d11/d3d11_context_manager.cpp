#include "d3d11_context_manager.hpp"

#include "d3d11_utils.hpp"

#include <algorithm>

namespace ymir::vdp::d3d11 {

ContextManager::ContextManager(DeviceManager &devMgr)
    : m_devMgr(devMgr) {

    if (HRESULT hr = devMgr.CreateDeferredContext(m_deferredCtx); FAILED(hr)) {
        // TODO: report error
        return;
    }
}

void ContextManager::Reset() {
    m_resVS.Reset();
    m_resPS.Reset();
    m_resCS.Reset();
    m_curVS = nullptr;
    m_curPS = nullptr;
    m_curCS = nullptr;
}

void ContextManager::VSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs, uint32 offset) {
    SetConstantBuffers(offset, bufs, m_resVS.cbufs);
}

void ContextManager::VSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs, uint32 offset) {
    SetUnorderedAccessViews(offset, uavs, m_resVS.uavs);
}

void ContextManager::VSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs, uint32 offset) {
    SetShaderResources(offset, srvs, m_resVS.srvs);
}

void ContextManager::VSSetShader(ID3D11VertexShader *shader) {
    if (shader != m_curVS) {
        m_curVS = shader;
        m_deferredCtx->VSSetShader(shader, nullptr, 0);
    }
}

void ContextManager::PSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs, uint32 offset) {
    SetConstantBuffers(offset, bufs, m_resPS.cbufs);
}

void ContextManager::PSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs, uint32 offset) {
    SetUnorderedAccessViews(offset, uavs, m_resPS.uavs);
}

void ContextManager::PSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs, uint32 offset) {
    SetShaderResources(offset, srvs, m_resPS.srvs);
}

void ContextManager::PSSetShader(ID3D11PixelShader *shader) {
    if (shader != m_curPS) {
        m_curPS = shader;
        m_deferredCtx->PSSetShader(shader, nullptr, 0);
    }
}

void ContextManager::CSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs, uint32 offset) {
    SetConstantBuffers(offset, bufs, m_resCS.cbufs);
}

void ContextManager::CSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs, uint32 offset) {
    SetUnorderedAccessViews(offset, uavs, m_resCS.uavs);
}

void ContextManager::CSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs, uint32 offset) {
    SetShaderResources(offset, srvs, m_resCS.srvs);
}

void ContextManager::CSSetShader(ID3D11ComputeShader *shader) {
    if (shader != m_curCS) {
        m_curCS = shader;
        m_deferredCtx->CSSetShader(shader, nullptr, 0);
    }
}

HRESULT ContextManager::FinishCommandList(wil::com_ptr_nothrow<ID3D11CommandList> &cmdList) {
    const HRESULT hr = m_deferredCtx->FinishCommandList(FALSE, cmdList.put());
    Reset();
    return hr;
}

template <typename T>
bool ContextManager::UpdateResources(uint32 offset, std::initializer_list<T *> src, std::vector<T *> &dst) {
    if (dst.size() == src.size() + offset && std::equal(src.begin() + offset, src.end(), dst.begin())) {
        return false;
    }
    if (src.size() + offset > dst.size()) {
        dst.resize(src.size() + offset);
    }
    std::copy(src.begin(), src.end(), dst.begin() + offset);
    if (src.size() + offset < dst.size()) {
        std::fill(dst.begin() + offset + src.size(), dst.end(), nullptr);
    }
    return true;
}

void ContextManager::SetConstantBuffers(uint32 offset, std::initializer_list<ID3D11Buffer *> src,
                                        std::vector<ID3D11Buffer *> &dst) {
    if (!UpdateResources(offset, src, dst)) {
        return;
    }
    m_deferredCtx->CSSetConstantBuffers(offset, dst.size(), dst.data());
    dst.resize(src.size() + offset);
}

void ContextManager::SetUnorderedAccessViews(uint32 offset, std::initializer_list<ID3D11UnorderedAccessView *> src,
                                             std::vector<ID3D11UnorderedAccessView *> &dst) {
    if (!UpdateResources(offset, src, dst)) {
        return;
    }
    m_deferredCtx->CSSetUnorderedAccessViews(offset, dst.size(), dst.data(), nullptr);
    dst.resize(src.size() + offset);
}

void ContextManager::SetShaderResources(uint32 offset, std::initializer_list<ID3D11ShaderResourceView *> src,
                                        std::vector<ID3D11ShaderResourceView *> &dst) {
    if (!UpdateResources(offset, src, dst)) {
        return;
    }
    m_deferredCtx->CSSetShaderResources(offset, dst.size() - offset, &dst[offset]);
    dst.resize(src.size() + offset);
}

} // namespace ymir::vdp::d3d11
