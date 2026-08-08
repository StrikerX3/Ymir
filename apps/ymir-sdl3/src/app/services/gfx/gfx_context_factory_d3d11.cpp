#include "gfx_context_impl_d3d11.hpp"

#include "gfx_result.hpp"

namespace app::gfxv2 {

GfxObjectResult<Direct3D11GraphicsContext> Create(const Direct3D11GraphicsContextSpec &spec) {
    return GfxOperationError{"Unimplemented"};
}

} // namespace app::gfxv2
