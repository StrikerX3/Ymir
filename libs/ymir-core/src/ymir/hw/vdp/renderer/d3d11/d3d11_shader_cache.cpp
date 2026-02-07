#include "d3d11_shader_cache.hpp"

#include "d3d11_utils.hpp"

#include <ymir/util/dev_assert.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/unreachable.hpp>

#include <xxh3.h>

#include <d3dcompiler.h>

#include <cassert>
#include <cstring>

namespace d3dutil {

D3DShaderCache D3DShaderCache::s_debugInstance{true};
D3DShaderCache D3DShaderCache::s_normalInstance{false};

static constexpr const char *GetShaderTargetForType(ShaderType type) {
    switch (type) {
    case ShaderType::VertexShader: return "vs_5_0";
    case ShaderType::PixelShader: return "ps_5_0";
    case ShaderType::ComputeShader: return "cs_5_0";
    default:
        YMIR_DEV_CHECK(); // should never happen
        util::unreachable();
    }
}

ID3D11VertexShader *D3DShaderCache::GetVertexShader(ID3D11Device *device, std::string_view code,
                                                    std::string_view entrypoint, D3D_SHADER_MACRO *macros) {
    ID3DBlob *blob = GetOrCompileShader(ShaderType::VertexShader, code, entrypoint, macros);
    if (blob == nullptr) {
        return nullptr;
    }

    ID3D11VertexShader *shader = nullptr;
    if (HRESULT hr = device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
        FAILED(hr)) {
        // TODO: report error
        return nullptr;
    }

    return shader;
}

ID3D11PixelShader *D3DShaderCache::GetPixelShader(ID3D11Device *device, std::string_view code,
                                                  std::string_view entrypoint, D3D_SHADER_MACRO *macros) {
    ID3DBlob *blob = GetOrCompileShader(ShaderType::PixelShader, code, entrypoint, macros);
    if (blob == nullptr) {
        return nullptr;
    }

    ID3D11PixelShader *shader = nullptr;
    if (HRESULT hr = device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
        FAILED(hr)) {
        // TODO: report error
        return nullptr;
    }

    return shader;
}

ID3D11ComputeShader *D3DShaderCache::GetComputeShader(ID3D11Device *device, std::string_view code,
                                                      std::string_view entrypoint, D3D_SHADER_MACRO *macros) {
    ID3DBlob *blob = GetOrCompileShader(ShaderType::ComputeShader, code, entrypoint, macros);
    if (blob == nullptr) {
        return nullptr;
    }

    ID3D11ComputeShader *shader = nullptr;
    if (HRESULT hr = device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &shader);
        FAILED(hr)) {
        // TODO: report error
        return nullptr;
    }

    return shader;
}

D3DShaderCache::~D3DShaderCache() {
    for (auto &[_, value] : m_cache) {
        SafeRelease(value);
    }
}

ID3DBlob *D3DShaderCache::GetOrCompileShader(ShaderType type, std::string_view code, std::string_view entrypoint,
                                             D3D_SHADER_MACRO *macros) {
    const CacheKey key = CacheKey::From(type, code, entrypoint, macros);
    if (auto it = m_cache.find(key); it != m_cache.end()) {
        return it->second;
    }

    ID3DBlob *blob = nullptr;
    ID3DBlob *errors = nullptr;
    util::ScopeGuard sgReleaseErrors{[&] { SafeRelease(errors); }};

    static constexpr UINT kNormalFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    static constexpr UINT kDebugFlags =
        D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG | D3DCOMPILE_DEBUG_NAME_FOR_SOURCE;

    // TODO: debug flag
    const bool debug = true;
    UINT flags = debug ? kDebugFlags : kNormalFlags;

    if (HRESULT hr = D3DCompile(code.data(), code.size(), NULL, macros, NULL, entrypoint.data(),
                                GetShaderTargetForType(type), flags, 0, &blob, &errors);
        FAILED(hr)) {
        if (errors != nullptr) {
            // TODO: report errors
            // (const char *)errors->GetBufferPointer()
        }
        return nullptr;
    }

    assert(blob != nullptr);

    m_cache.insert({key, blob});

    return blob;
}

D3DShaderCache::CacheKey D3DShaderCache::CacheKey::From(ShaderType type, std::string_view code,
                                                        std::string_view entrypoint, D3D_SHADER_MACRO *macros) {
    CacheKey key{};
    key.type = type;
    key.entrypointHash = XXH64(entrypoint.data(), entrypoint.size(), 0);
    key.codeHash = XXH64(code.data(), code.size(), 0);
    key.codeLength = code.size();
    if (macros != nullptr) {
        XXH64_state_t *state = XXH64_createState();
        for (D3D_SHADER_MACRO *macro = macros; macro->Name != nullptr; ++macro) {
            XXH64_update(state, macro->Name, strlen(macro->Name));
            XXH64_update(state, macro->Definition, strlen(macro->Definition));
        }
        key.macrosHash = XXH64_digest(state);
        XXH64_freeState(state);
    } else {
        key.macrosHash = 0;
    }

    return key;
}

} // namespace d3dutil
