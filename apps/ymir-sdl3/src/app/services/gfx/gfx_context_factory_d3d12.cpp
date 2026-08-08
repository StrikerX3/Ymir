#include "gfx_context_impl_d3d12.hpp"

#include "gfx_result.hpp"

namespace app::gfxv2 {

GfxObjectResult<Direct3D12GraphicsContext> Create(const Direct3D12GraphicsContextSpec &spec) {
    return GfxOperationError{"Unimplemented"};
}

} // namespace app::gfxv2
