#pragma once

#include <string_view>

namespace ymir::vdp {

/// @brief VDP renderer type enumeration.
enum class VDPRendererType { Null, Software };

/// @brief Retrieves the name of a given VDP renderer type.
/// @param[in] type the VDP renderer type
/// @return a string with the human-readable name of the VDP renderer
inline std::string_view GetRendererName(VDPRendererType type) {
    switch (type) {
    case VDPRendererType::Null: return "Null";
    case VDPRendererType::Software: return "Software";
    default: return "Invalid";
    }
}

/// @brief All supported VDP renderer types.
inline constexpr VDPRendererType kTypes[] = {VDPRendererType::Null, VDPRendererType::Software};

// Forward declarations of concrete VDP renderer implementations.
// See the vdp_renderer_* headers under their dedicated subfolders.

class NullVDPRenderer;
class SoftwareVDPRenderer;

namespace detail {

    /// @brief Metadata about VDP renderer types.
    /// @tparam type the VDP renderer type
    template <VDPRendererType type>
    struct VDPRendererTypeMeta {};

    /// @brief Metadata about the null VDP renderer.
    template <>
    struct VDPRendererTypeMeta<VDPRendererType::Null> {
        using type = NullVDPRenderer;
    };

    /// @brief Metadata about the software VDP renderer.
    template <>
    struct VDPRendererTypeMeta<VDPRendererType::Software> {
        using type = SoftwareVDPRenderer;
    };

    /// @brief Retrieves the class type of the given `VDPRendererType`.
    /// @tparam type the VDP renderer type
    template <VDPRendererType type>
    using VDPRendererType_t = typename VDPRendererTypeMeta<type>::type;

} // namespace detail

} // namespace ymir::vdp
