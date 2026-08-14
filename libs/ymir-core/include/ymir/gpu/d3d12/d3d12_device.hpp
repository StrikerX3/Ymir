#pragma once

/**
@file
@brief Defines `D3D12Device`, a wrapper for `ID3D12Device` objects.
*/

#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12Device`.
class D3D12Device final : public D3D12ObjectWrapper<ID3D12Device> {
public:
    D3D12Device() = default;
    D3D12Device(wil::com_ptr_nothrow<ID3D12Device> &&ptr)
        : D3D12ObjectWrapper(std::move(ptr)) {}
    D3D12Device(ID3D12Device *ptr)
        : D3D12ObjectWrapper(ptr) {}

    /// @brief Creates an `ID3D12Device` object using the given adapter and minimum feature level.
    /// @param[in,opt] adapter the adapter to reference
    /// @param[in] minFeatureLevel the minimum feature level
    /// @return the result of the attempt to create the device
    HRESULT Create(IUnknown *adapter, D3D_FEATURE_LEVEL minFeatureLevel) {
        const HRESULT hr = D3D12CreateDevice(adapter, minFeatureLevel, IID_PPV_ARGS(m_object.put()));
        if (SUCCEEDED(hr)) {
            QueryUpgrades();
        }
        return hr;
    }

    // clang-format off
    ID3D12Device1  *As1 () const { return m_device1 .get(); }
    ID3D12Device2  *As2 () const { return m_device2 .get(); }
    ID3D12Device3  *As3 () const { return m_device3 .get(); }
    ID3D12Device4  *As4 () const { return m_device4 .get(); }
    ID3D12Device5  *As5 () const { return m_device5 .get(); }
    ID3D12Device6  *As6 () const { return m_device6 .get(); }
    ID3D12Device7  *As7 () const { return m_device7 .get(); }
    ID3D12Device8  *As8 () const { return m_device8 .get(); }
    ID3D12Device9  *As9 () const { return m_device9 .get(); }
    ID3D12Device10 *As10() const { return m_device10.get(); }
    ID3D12Device11 *As11() const { return m_device11.get(); }
    ID3D12Device12 *As12() const { return m_device12.get(); }
    ID3D12Device13 *As13() const { return m_device13.get(); }
    ID3D12Device14 *As14() const { return m_device14.get(); }
    // clang-format on

private:
    void QueryUpgrades() {
        // clang-format off
        m_device1  = m_object.try_query<ID3D12Device1 >(); if (!m_device1 ) { return; }
        m_device2  = m_object.try_query<ID3D12Device2 >(); if (!m_device2 ) { return; }
        m_device3  = m_object.try_query<ID3D12Device3 >(); if (!m_device3 ) { return; }
        m_device4  = m_object.try_query<ID3D12Device4 >(); if (!m_device4 ) { return; }
        m_device5  = m_object.try_query<ID3D12Device5 >(); if (!m_device5 ) { return; }
        m_device6  = m_object.try_query<ID3D12Device6 >(); if (!m_device6 ) { return; }
        m_device7  = m_object.try_query<ID3D12Device7 >(); if (!m_device7 ) { return; }
        m_device8  = m_object.try_query<ID3D12Device8 >(); if (!m_device8 ) { return; }
        m_device9  = m_object.try_query<ID3D12Device9 >(); if (!m_device9 ) { return; }
        m_device10 = m_object.try_query<ID3D12Device10>(); if (!m_device10) { return; }
        m_device11 = m_object.try_query<ID3D12Device11>(); if (!m_device11) { return; }
        m_device12 = m_object.try_query<ID3D12Device12>(); if (!m_device12) { return; }
        m_device13 = m_object.try_query<ID3D12Device13>(); if (!m_device13) { return; }
        m_device14 = m_object.try_query<ID3D12Device14>();
        // clang-format on
    }

    void DestroyExt() override {
        m_device14.reset();
        m_device13.reset();
        m_device12.reset();
        m_device11.reset();
        m_device10.reset();
        m_device9.reset();
        m_device8.reset();
        m_device7.reset();
        m_device6.reset();
        m_device5.reset();
        m_device4.reset();
        m_device3.reset();
        m_device2.reset();
        m_device1.reset();
    }

    wil::com_ptr_nothrow<ID3D12Device1> m_device1;
    wil::com_ptr_nothrow<ID3D12Device2> m_device2;
    wil::com_ptr_nothrow<ID3D12Device3> m_device3;
    wil::com_ptr_nothrow<ID3D12Device4> m_device4;
    wil::com_ptr_nothrow<ID3D12Device5> m_device5;
    wil::com_ptr_nothrow<ID3D12Device6> m_device6;
    wil::com_ptr_nothrow<ID3D12Device7> m_device7;
    wil::com_ptr_nothrow<ID3D12Device8> m_device8;
    wil::com_ptr_nothrow<ID3D12Device9> m_device9;
    wil::com_ptr_nothrow<ID3D12Device10> m_device10;
    wil::com_ptr_nothrow<ID3D12Device11> m_device11;
    wil::com_ptr_nothrow<ID3D12Device12> m_device12;
    wil::com_ptr_nothrow<ID3D12Device13> m_device13;
    wil::com_ptr_nothrow<ID3D12Device14> m_device14;
};

} // namespace ymir::gpu::d3d12
