#pragma once

#include <ymir/core/types.hpp>

#include <ymir/hw/vdp/renderer/vdp_renderer_hw_callbacks.hpp>

#include "d3d11_device_manager.hpp"

#include <d3d11.h>

#include <initializer_list>
#include <vector>

namespace ymir::vdp::d3d11 {

/// @brief Manages a D3D11 deferred context's resources, avoiding unnecessary state changes when possible.
class ContextManager {
public:
    ContextManager(DeviceManager &devMgr);
    ~ContextManager();

    void Reset();

    ID3D11DeviceContext *GetDeferredContext() const {
        return m_deferredCtx;
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
    bool ExecutePendingCommandLists(ID3D11DeviceContext *immediateCtx, bool restoreState,
                                    HardwareRendererCallbacks &hwCallbacks);

private:
    DeviceManager &m_devMgr;

    ID3D11DeviceContext *m_deferredCtx = nullptr;

    std::mutex m_mtxCmdList{};
    std::vector<ID3D11CommandList *> m_cmdListQueue; //< Pending command list queue

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
