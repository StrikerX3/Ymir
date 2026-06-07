#pragma once

/**
@file
@brief Includes all basic (wrapped) types used by the Direct3D 12 GPU backend.
*/

#include "wrappers/d3d12_commands.hpp"
#include "wrappers/d3d12_descriptor_heap.hpp"
#include "wrappers/d3d12_device.hpp"
#include "wrappers/d3d12_fence.hpp"
#include "wrappers/d3d12_pipeline_state.hpp"
#include "wrappers/d3d12_resource.hpp"
#include "wrappers/d3d12_root_signature.hpp"
#include "wrappers/d3d12_swap_chain.hpp"

// TODO: implement mid-level abstractions (similar to GraphicsContext)
//   shaders and bindings (CBVs, SRVs, UAVs, samplers)
