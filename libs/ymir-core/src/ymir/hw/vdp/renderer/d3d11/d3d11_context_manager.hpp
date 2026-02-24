#pragma once

#include <ymir/core/types.hpp>

#include "d3d11_device_manager.hpp"

#include <d3d11.h>

#include <initializer_list>
#include <type_traits>
#include <vector>

namespace ymir::vdp::d3d11 {

/// @brief Manages a D3D11 deferred context's resources, avoiding unnecessary state changes when possible.
class ContextManager {
public:
    ContextManager(DeviceManager &devMgr);

    void Reset();

    ID3D11DeviceContext *GetDeferredContext() const {
        return m_deferredCtx;
    }

    void Dispatch(UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ) {
        m_deferredCtx->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
    }

    /// @brief Attempts to lock a resource for modification. If successful, invokes `fnProcess` with a reference to the
    /// `D3D11_MAPPED_SUBRESOURCE` created by mapping the resource and unmaps it afterwards.
    ///
    /// @tparam Fn the type of callable to invoke when the resource is mapped
    /// @param[in] resource the resource to modify
    /// @param[in] fnProcess the modifier function
    /// @return the result of the `Map` operation
    template <typename Fn>
        requires std::is_invocable_v<Fn, const D3D11_MAPPED_SUBRESOURCE &>
    HRESULT ModifyResource(ID3D11Resource *resource, UINT subresource, Fn &&fnProcess) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        const HRESULT hr = m_deferredCtx->Map(resource, subresource, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr)) {
            fnProcess(mappedResource);
            m_deferredCtx->Unmap(resource, subresource);
        }
        return hr;
    }

    void VSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs, uint32 offset = 0);
    void VSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs, uint32 offset = 0);
    void VSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs, uint32 offset = 0);
    void VSSetShader(ID3D11VertexShader *shader);

    void PSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs, uint32 offset = 0);
    void PSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs, uint32 offset = 0);
    void PSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs, uint32 offset = 0);
    void PSSetShader(ID3D11PixelShader *shader);

    void CSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs, uint32 offset = 0);
    void CSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs, uint32 offset = 0);
    void CSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs, uint32 offset = 0);
    void CSSetShader(ID3D11ComputeShader *shader);

    HRESULT FinishCommandList(ID3D11CommandList *&cmdList);

private:
    DeviceManager &m_devMgr;

    ID3D11DeviceContext *m_deferredCtx = nullptr;

    struct ResourceSet {
        void Reset() {
            cbufs.clear();
            srvs.clear();
            uavs.clear();
        }

        std::vector<ID3D11Buffer *> cbufs;
        std::vector<ID3D11ShaderResourceView *> srvs;
        std::vector<ID3D11UnorderedAccessView *> uavs;
    };

    ResourceSet m_resVS;
    ID3D11VertexShader *m_curVS = nullptr;
    ResourceSet m_resPS;
    ID3D11PixelShader *m_curPS = nullptr;
    ResourceSet m_resCS;
    ID3D11ComputeShader *m_curCS = nullptr;

    template <typename T>
    bool UpdateResources(uint32 offset, std::initializer_list<T *> src, std::vector<T *> &dst);

    void SetConstantBuffers(uint32 offset, std::initializer_list<ID3D11Buffer *> src, std::vector<ID3D11Buffer *> &dst);

    void SetUnorderedAccessViews(uint32 offset, std::initializer_list<ID3D11UnorderedAccessView *> src,
                                 std::vector<ID3D11UnorderedAccessView *> &dst);

    void SetShaderResources(uint32 offset, std::initializer_list<ID3D11ShaderResourceView *> src,
                            std::vector<ID3D11ShaderResourceView *> &dst);
};

} // namespace ymir::vdp::d3d11
