#include "graphics_service.hpp"

#include <ymir/util/scope_guard.hpp>

#include <cassert>
#include <limits>

using namespace app::gfx;

namespace app::services {

GraphicsService::~GraphicsService() {
    DestroyResources();
}

SDL_Renderer *GraphicsService::CreateRenderer(Backend backend, SDL_Window *window, int vsync) {
    SDL_PropertiesID rendererProps = SDL_CreateProperties();
    if (rendererProps == 0) {
        return nullptr;
    }
    util::ScopeGuard sgDestroyRendererProps{[&] { SDL_DestroyProperties(rendererProps); }};

    // Assume the following calls succeed
    SDL_SetPointerProperty(rendererProps, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
    SDL_SetStringProperty(rendererProps, SDL_PROP_RENDERER_CREATE_NAME_STRING, GraphicsBackendRendererID(backend));
    SDL_SetNumberProperty(rendererProps, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, vsync);

    if (m_renderer != nullptr) {
        DestroyResources();
    }
    m_renderer = SDL_CreateRendererWithProperties(rendererProps);
    if (m_renderer != nullptr) {
        RecreateResources();
    }
    return m_renderer;
}

TextureHandle GraphicsService::CreateTexture(SDL_PixelFormat format, SDL_TextureAccess access, int w, int h,
                                             FnTextureSetup fnSetup) {
    TextureParams params{
        .format = format,
        .access = access,
        .width = w,
        .height = h,
        .fnSetup = std::move(fnSetup),
    };
    SDL_Texture *texture = InternalCreateTexture(params, false);
    if (texture == nullptr) {
        return kInvalidTextureHandle;
    }

    const TextureHandle handle = GetNextTextureHandle();
    if (handle == kInvalidTextureHandle) {
        return kInvalidTextureHandle;
    }

    m_textures[handle] = params;
    return handle;
}

bool GraphicsService::IsTextureHandleValid(gfx::TextureHandle handle) const {
    return m_textures.contains(handle);
}

bool GraphicsService::ResizeTexture(gfx::TextureHandle handle, int w, int h) {
    if (!m_textures.contains(handle)) {
        return false;
    }

    auto &tex = m_textures.at(handle);

    // Try creating the new texture first
    SDL_Texture *newTexture = SDL_CreateTexture(m_renderer, tex.format, tex.access, w, h);
    if (newTexture == nullptr) {
        return false;
    }

    // Delete old texture and update parameters
    SDL_DestroyTexture(tex.texture);
    tex.texture = newTexture;
    tex.width = w;
    tex.height = h;
    return true;
}

SDL_Texture *GraphicsService::GetSDLTexture(TextureHandle handle) const {
    if (m_textures.contains(handle)) {
        return m_textures.at(handle).texture;
    }
    return nullptr;
}

bool GraphicsService::DestroyTexture(gfx::TextureHandle handle) {
    if (auto it = m_textures.find(handle); it != m_textures.end()) {
        SDL_DestroyTexture(it->second.texture);
        m_textures.erase(it);
        return true;
    }
    return false;
}

TextureHandle GraphicsService::GetNextTextureHandle() {
    if (m_textures.size() == std::numeric_limits<uint32>::max() - 1) {
        // Exhausted handles; should never happen
        return kInvalidTextureHandle;
    }

    TextureHandle handle = m_nextTextureHandle++;
    while (handle == kInvalidTextureHandle || m_textures.contains(handle)) {
        // Avoid handle collision or invalid handle
        handle = m_nextTextureHandle++;
    }
    return handle;
}

SDL_Texture *GraphicsService::InternalCreateTexture(TextureParams &params, bool recreated) {
    SDL_Texture *texture = SDL_CreateTexture(m_renderer, params.format, params.access, params.width, params.height);
    if (texture != nullptr) {
        params.fnSetup(texture, recreated);
    }
    params.texture = texture;
    return texture;
}

void GraphicsService::RecreateResources() {
    for (auto &[_, params] : m_textures) {
        InternalCreateTexture(params, true);
    }
}

void GraphicsService::DestroyResources() {
    for (auto &[_, params] : m_textures) {
        SDL_DestroyTexture(params.texture);
        params.texture = nullptr;
    }
    SDL_DestroyRenderer(m_renderer);
}

} // namespace app::services
