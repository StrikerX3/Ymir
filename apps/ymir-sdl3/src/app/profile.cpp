#include "profile.hpp"

#include <SDL3/SDL_filesystem.h>

#include <string>

namespace app {

// Must match the order listed in the ProfilePath enum.
const std::filesystem::path kPathSuffixes[] = {
    "",                                           // Root
    std::filesystem::path("roms") / "ipl",        // IPLROMImages
    std::filesystem::path("roms") / "cart",       // ROMCartImages
    "backup",                                     // BackupMemory
    std::filesystem::path("backup") / "exported", // ExportedBackups
    "state",                                      // PersistentState
    "savestates",                                 // SaveStates
    "dumps",                                      // Dumps
};

Profile::Profile() {
    UsePortableProfilePath();
}

void Profile::UseUserProfilePath() {
    char *path = SDL_GetPrefPath(Ymir_ORGANIZATION_NAME, Ymir_APP_NAME);
    m_profilePath = path;
    SDL_free(path);
}

void Profile::UsePortableProfilePath() {
    char *cpath = SDL_GetCurrentDirectory();
    std::string path = cpath;
    std::u8string u8Path{path.begin(), path.end()};
    m_profilePath = u8Path;
    SDL_free(cpath);
}

void Profile::UseProfilePath(std::filesystem::path path) {
    m_profilePath = path;
}

bool Profile::CheckFolders() const {
    for (size_t i = 0; i < static_cast<size_t>(ProfilePath::_Count); ++i) {
        const auto path = m_pathOverrides[i].empty() ? m_profilePath / kPathSuffixes[i] : m_pathOverrides[i];
        if (!std::filesystem::is_directory(path)) {
            return false;
        }
    }
    return true;
}

bool Profile::CreateFolders(std::error_code &error) {
    error.clear();

    for (size_t i = 0; i < static_cast<size_t>(ProfilePath::_Count); ++i) {
        const auto path = m_pathOverrides[i].empty() ? m_profilePath / kPathSuffixes[i] : m_pathOverrides[i];
        if (!std::filesystem::is_directory(path)) {
            std::filesystem::create_directories(path, error);
        }

        if (error) {
            return false;
        }
    }
    return true;
}

std::filesystem::path Profile::GetPath(ProfilePath profPath) const {
    const auto index = static_cast<size_t>(profPath);
    if (index < std::size(kPathSuffixes)) {
        if (!m_pathOverrides[index].empty()) {
            return m_pathOverrides[index];
        }
        return m_profilePath / kPathSuffixes[index] / "";
    } else {
        return m_profilePath / "";
    }
}

std::filesystem::path Profile::GetPathOverride(ProfilePath profPath) const {
    const auto index = static_cast<size_t>(profPath);
    return m_pathOverrides[index];
}

void Profile::SetPathOverride(ProfilePath profPath, std::filesystem::path path) {
    const auto index = static_cast<size_t>(profPath);
    m_pathOverrides[index] = path;
}

void Profile::ClearOverridePath(ProfilePath profPath) {
    const auto index = static_cast<size_t>(profPath);
    m_pathOverrides[index].clear();
}

} // namespace app
