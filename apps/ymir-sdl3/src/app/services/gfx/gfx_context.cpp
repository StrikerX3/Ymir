#include "gfx_context.hpp"

#include <cassert>

namespace app::gfx {

TextureID IGraphicsContext::GetNextTextureID() {
    if (!m_freeTextureIDs.empty()) {
        const TextureID id = m_freeTextureIDs.back();
        m_freeTextureIDs.pop_back();
        return id;
    }

    const TextureID id = m_nextTextureID++;
    // This really should not happen unless we somehow managed to create over 4 billion textures
    assert(m_nextTextureID != 0);
    return id;
}

void IGraphicsContext::FreeTextureID(TextureID id) {
    assert(std::find(m_freeTextureIDs.begin(), m_freeTextureIDs.end(), id) == m_freeTextureIDs.end());
    m_freeTextureIDs.push_back(id);
}

} // namespace app::gfx