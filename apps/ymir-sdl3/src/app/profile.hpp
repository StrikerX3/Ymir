#pragma once

#include <array>
#include <filesystem>

namespace app {

enum class ProfilePath {
    Root,             // Root of the profile    <profile>/
    IPLROMImages,     // IPL ROM images         <profile>/roms/ipl/
    CDBlockROMImages, // CD block ROM images    <profile>/roms/cdb/
    ROMCartImages,    // Cartridge ROMs         <profile>/roms/cart/
    BackupMemory,     // Backup memory images   <profile>/backup/
    ExportedBackups,  // Exported backup files  <profile>/backup/exported/
    PersistentState,  // Persistent app state   <profile>/state/
    SaveStates,       // Save states            <profile>/savestates/
    Dumps,            // Memory dumps           <profile>/dumps/
    Screenshots,      // Screenshots            <profile>/screenshots/

    _Count,
};

class Profile {
public:
    // Creates the folder manager pointing to the current working directory.
    Profile();

    // Retrieves the OS's standard user profile path.
    static std::filesystem::path GetUserProfilePath();

    // Retrieves the current working directory (aka "portable profile path").
    static std::filesystem::path GetPortableProfilePath();

    // Uses the OS's standard user profile path.
    void UseUserProfilePath();

    // Uses the current working directory as the profile path.
    void UsePortableProfilePath();

    // Uses the specified profile path.
    void UseProfilePath(std::filesystem::path path);

    // Checks if the standard folders are present in the current profile path.
    bool CheckFolders() const;

    // Creates all standard folders under the given profile path.
    // Returns true if all folders were created successfully.
    bool CreateFolders(std::error_code &error);

    // Gets the specified path relative to the profile path.
    std::filesystem::path GetPath(ProfilePath profPath) const;

    // Gets the specified path override.
    std::filesystem::path GetPathOverride(ProfilePath profPath) const;

    // Overrides the specified path.
    void SetPathOverride(ProfilePath profPath, std::filesystem::path path);

    // Clears the override for the specified path.
    void ClearOverridePath(ProfilePath profPath);

private:
    std::filesystem::path m_profilePath;

    std::array<std::filesystem::path, static_cast<size_t>(ProfilePath::_Count)> m_pathOverrides;
};

} // namespace app
