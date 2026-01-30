#pragma once

#include "vdp_renderer_defs.hpp"

#include <ymir/util/inline.hpp>

namespace ymir::vdp {

/// @brief Interface for VDP1 and VDP2 renderers.
class IVDPRenderer {
public:
    IVDPRenderer(VDPRendererType type)
        : m_type(type) {}

    // If this renderer object has the specified VDPRendererType, casts it to the corresponding concrete type.
    // Returns nullptr otherwise.
    template <VDPRendererType type>
    FORCE_INLINE typename detail::VDPRendererType_t<type> *As() {
        if (m_type == type) {
            return static_cast<detail::VDPRendererType_t<type> *>(this);
        } else {
            return nullptr;
        }
    }

    // If this renderer object has the specified VDPRendererType, casts it to the corresponding concrete type.
    // Returns nullptr otherwise.
    template <VDPRendererType type>
    FORCE_INLINE const typename detail::VDPRendererType_t<type> *As() const {
        if (m_type == type) {
            return static_cast<detail::VDPRendererType_t<type> *>(this);
        } else {
            return nullptr;
        }
    }

    std::string_view GetName() const {
        return GetRendererName(m_type);
    }

    VDPRendererType GetType() const {
        return m_type;
    }

private:
    const VDPRendererType m_type;
};

} // namespace ymir::vdp
