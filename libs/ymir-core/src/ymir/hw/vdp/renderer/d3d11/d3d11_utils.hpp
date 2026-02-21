#pragma once

#include <ymir/util/dev_assert.hpp>
#include <ymir/util/inline.hpp>

#include <d3d11.h>

#include <array>
#include <bit>
#include <concepts>
#include <string_view>
#include <utility>

namespace d3dutil {

/// @brief Returns the size of a single pixel (in bytes) of the given format.
/// @param[in] format the format to check
/// @return the number of bytes per pixel of the format. Not all types are handled.
FORCE_INLINE UINT GetFormatSize(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_UNKNOWN: return 1;
    case DXGI_FORMAT_R8G8B8A8_UINT: return 4;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
    case DXGI_FORMAT_R32_TYPELESS: return 4;
    case DXGI_FORMAT_R32G32_UINT: return 8;
    default: YMIR_DEV_CHECK(); return 0;
    }
}

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

/// @brief Safely releases a vector of objects, handling `nullptr`s gracefully.
///
/// @tparam T the type of objects in the vector
/// @param[in] objects the vector of objects to release. The vector is cleared afterwards.
template <typename T>
    requires std::derived_from<T, IUnknown>
FORCE_INLINE void SafeRelease(std::vector<T *> &objects) {
    for (auto &object : objects) {
        if (object != nullptr) {
            object->Release();
        }
    }
    objects.clear();
}

/// @brief Tracks dirty bits and allows processing ranges of dirty bits.
/// @tparam numBits the number of bits in the bitmap
template <size_t numBits>
struct DirtyBitmap {
    using TEntry = uint64;
    static constexpr size_t kBitsPerEntry = sizeof(TEntry) * 8;
    static constexpr size_t kEntryMask = kBitsPerEntry - 1;
    static constexpr size_t kEntryShift = std::countr_zero(kBitsPerEntry);
    static constexpr size_t kNumEntries = (numBits + kBitsPerEntry - 1) >> kEntryShift;
    static constexpr TEntry kAllBits = ~static_cast<TEntry>(0);

    /// @brief Sets the specified bit as dirty.
    /// @param[in] index the bit to set
    void Set(TEntry index) {
        if (index < numBits) {
            m_bitmap[index >> kEntryShift] |= 1ull << (index & kEntryMask);
        }
    }

    /// @brief Sets all bits as dirty.
    void SetAll() {
        m_bitmap.fill(kAllBits);
        if constexpr ((numBits & kEntryMask) != 0) {
            m_bitmap.back() = kAllBits >> (-numBits & kEntryMask);
        }
    }

    /// @brief Resets all dirty bits.
    void ClearAll() {
        m_bitmap.fill(0);
    }

    /// @brief Checks if any bit is set in the bitmap.
    /// @return `true` if any bit is set
    bool AnySet() const {
        for (TEntry entry : m_bitmap) {
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
    /// `TEntry` offset from zero of the current dirty bit range, `TEntry` count of set bits in the position.
    template <typename Fn>
        requires std::is_invocable_v<Fn, TEntry /*offset from zero*/, TEntry /*contiguous dirty bits set count*/>
    void Process(Fn &&fn) {
        TEntry offset = 0;
        TEntry accumOnes = 0;
        std::size_t i = 0;
        TEntry entry = m_bitmap[0];
        TEntry remaining = std::min<TEntry>(kBitsPerEntry, numBits);
        while (i < kNumEntries) {
            // Zeros search phase
            while (entry == 0) {
                offset += remaining;
                ++i;
                if (i >= kNumEntries) {
                    break;
                }
                entry = m_bitmap[i];
                remaining = kBitsPerEntry;
                continue;
            }

            const TEntry zeros = std::min<TEntry>(std::countr_zero(entry), remaining);
            offset += zeros;
            remaining -= zeros;
            entry >>= zeros;

            // Ones search phase
            while (true) {
                const TEntry ones = std::countr_one(entry);
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
                remaining = kBitsPerEntry;
            }
        }
        if (accumOnes != 0) {
            fn(offset, accumOnes);
        }

        m_bitmap.fill(0);
    }

private:
    alignas(16) std::array<TEntry, kNumEntries> m_bitmap = {};
};

} // namespace d3dutil
