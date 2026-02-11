#pragma once

#include <ymir/util/inline.hpp>

#include <d3d11.h>

#include <concepts>

namespace d3dutil {

/// @brief Sets a debug name for a D3D11 resource.
/// The name is displayed on tools like RenderDoc.
///
/// @tparam N the size of the debug name string
/// @param[in] deviceResource the resource to name
/// @param[in] debugName the name
template <UINT N>
FORCE_INLINE void SetDebugName(_In_ ID3D11DeviceChild *deviceResource, _In_z_ const char (&debugName)[N]) {
    if (deviceResource != nullptr) {
        deviceResource->SetPrivateData(::WKPDID_D3DDebugObjectName, N - 1, debugName);
    }
}

/// @brief Sets a debug name for a D3D11 resource.
/// The name is displayed on tools like RenderDoc.
///
/// @param[in] deviceResource the resource to name
/// @param[in] debugName the name
FORCE_INLINE void SetDebugName(_In_ ID3D11DeviceChild *deviceResource, _In_z_ std::string_view debugName) {
    if (deviceResource != nullptr) {
        deviceResource->SetPrivateData(::WKPDID_D3DDebugObjectName, debugName.size(), debugName.data());
    }
}

/// @brief Safely releases an object, handling `nullptr` gracefully.
/// @param[in] object the object to release
FORCE_INLINE void SafeRelease(IUnknown *object) {
    if (object != nullptr) {
        object->Release();
    }
}

/// @brief Safely releases an array of objects, handling `nullptr`s gracefully.
///
/// @tparam T the type of objects in the array
/// @tparam N the size of the array
/// @param[in] objects the array of objects to release
template <typename T, size_t N>
    requires std::derived_from<T, IUnknown>
FORCE_INLINE void SafeRelease(std::array<T *, N> &objects) {
    for (auto &object : objects) {
        if (object != nullptr) {
            object->Release();
        }
    }
}

/// @brief Tracks dirty bits and allows processing ranges of dirty bits.
/// @tparam numBits the number of bits in the bitmap
template <size_t numBits>
struct DirtyBitmap {
    /// @brief Sets the specified bit as dirty.
    /// @param[in] index the bit to set
    void Set(uint64 index) {
        if (index < numBits) {
            m_bitmap[index / kBitsPerEntry] |= 1ull << (index & 63ull);
        }
    }

    /// @brief Sets all bits as dirty.
    void SetAll() {
        m_bitmap.fill(0xFFFFFFFF'FFFFFFFF);
        if constexpr ((numBits & 63) != 0) {
            m_bitmap.back() = 0xFFFFFFFF'FFFFFFFFull >> (-numBits & 63ull);
        }
    }

    /// @brief Resets all dirty bits.
    void ClearAll() {
        m_bitmap.fill(0);
    }

    /// @brief Checks if any bit is set in the bitmap.
    /// @return `true` if any bit is set
    bool AnySet() const {
        for (uint64 entry : m_bitmap) {
            if (entry != 0) {
                return true;
            }
        }
        return false;
    }

    /// @brief Returns `true` if any bit is set.
    operator bool() {
        return AnySet();
    }

    /// @brief Processes all dirty bits and clears the bitmap.
    /// @tparam Fn the type of function that processes the bit ranges.
    /// @param[in] fn a function that processes the dirty bit ranges. The function is invoked with two parameters:
    /// `uint64` offset from zero of the current dirty bit range, `uint64` count of set bits in the position.
    template <typename Fn>
        requires std::is_invocable_v<Fn, uint64 /*offset from zero*/, uint64 /*contiguous dirty bits set count*/>
    void Process(Fn &&fn) {
        uint64 offset = 0;
        uint64 accumOnes = 0;
        std::size_t i = 0;
        uint64 entry = m_bitmap[0];
        uint64 remaining = std::min<uint64>(64, numBits);
        while (i < kNumEntries) {
            // Zeros search phase
            while (entry == 0) {
                offset += remaining;
                ++i;
                if (i >= kNumEntries) {
                    break;
                }
                entry = m_bitmap[i];
                remaining = 64;
                continue;
            }

            const uint64 zeros = std::min<uint64>(std::countr_zero(entry), remaining);
            offset += zeros;
            remaining -= zeros;
            entry >>= zeros;

            // Ones search phase
            while (true) {
                const uint64 ones = std::countr_one(entry);
                accumOnes += ones;
                entry >>= ones;
                remaining -= ones;
                if (remaining > 0) {
                    fn(offset, accumOnes);
                    offset += accumOnes;
                    accumOnes = 0;
                    break;
                }
                ++i;
                if (i >= kNumEntries) {
                    break;
                }
                entry = m_bitmap[i];
                remaining = 64;
            }
        }
        if (accumOnes != 0) {
            fn(offset, accumOnes);
        }

        m_bitmap.fill(0);
    }

private:
    static constexpr size_t kBitsPerEntry = sizeof(uint64) * 8;
    static constexpr size_t kNumEntries = (numBits + kBitsPerEntry - 1) / kBitsPerEntry;
    alignas(16) std::array<uint64, kNumEntries> m_bitmap = {};
};

} // namespace d3dutil
