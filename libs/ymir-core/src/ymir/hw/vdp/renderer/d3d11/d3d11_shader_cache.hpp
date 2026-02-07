#pragma once

#include <ymir/util/hashing.hpp>

#include <ymir/core/types.hpp>

#include <d3d11.h>

#include <string_view>
#include <unordered_map>

namespace d3dutil {

enum class ShaderType { VertexShader, PixelShader, ComputeShader };

/// @brief Compiles and caches Direct3D shaders.
class D3DShaderCache {
public:
    static D3DShaderCache &Instance(bool debug) {
        return debug ? s_debugInstance : s_normalInstance;
    }

    // TODO: persist shader blobs to disk

    ID3D11VertexShader *GetVertexShader(ID3D11Device *device, std::string_view code, std::string_view entrypoint,
                                        D3D_SHADER_MACRO *macros);
    ID3D11PixelShader *GetPixelShader(ID3D11Device *device, std::string_view code, std::string_view entrypoint,
                                      D3D_SHADER_MACRO *macros);
    ID3D11ComputeShader *GetComputeShader(ID3D11Device *device, std::string_view code, std::string_view entrypoint,
                                          D3D_SHADER_MACRO *macros);

private:
    explicit D3DShaderCache(bool debug) noexcept
        : m_debug(debug) {}
    D3DShaderCache(const D3DShaderCache &) = delete;
    D3DShaderCache(D3DShaderCache &&) = delete;
    ~D3DShaderCache();

    static D3DShaderCache s_debugInstance;
    static D3DShaderCache s_normalInstance;

    const bool m_debug;

    struct CacheKey {
        ShaderType type;
        uint64 entrypointHash;
        uint64 macrosHash;
        uint64 codeHash;
        std::size_t codeLength;

        struct Hash {
            std::size_t operator()(const CacheKey &key) const noexcept {
                std::size_t hash = 0;
                util::hash_combine(hash, key.type, key.entrypointHash, key.macrosHash, key.codeHash, key.codeLength);
                return hash;
            }
        };

        static CacheKey From(ShaderType type, std::string_view code, std::string_view entrypoint,
                             D3D_SHADER_MACRO *macros);

        bool operator==(const CacheKey &key) const = default;
    };

    struct CacheEntry {
        ShaderType type;
        ID3DBlob *blob;
    };

    std::unordered_map<CacheKey, ID3DBlob *, CacheKey::Hash> m_cache;

    ID3DBlob *GetOrCompileShader(ShaderType type, std::string_view code, std::string_view entrypoint,
                                 D3D_SHADER_MACRO *macros);
};

} // namespace d3dutil
