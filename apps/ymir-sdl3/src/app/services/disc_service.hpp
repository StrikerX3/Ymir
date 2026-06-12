#pragma once

#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace app::services {

/// @brief Handles loading disc images and the recent games list.
class DiscService {
public:
    using ShowModalCallback = std::function<void(std::string title, std::function<void()> contents)>;

    DiscService(SharedContext &context, Settings &settings, ShowModalCallback showModal);
    ~DiscService() = default;

    DiscService(const DiscService &) = delete;
    DiscService &operator=(const DiscService &) = delete;

    /// @brief Opens the dialog to select a Saturn disc image.
    void OpenLoadDiscDialog();

    /// @brief Callback for the disc image file dialog selection.
    /// @param[in] filelist List of selected files.
    /// @param[in] filter Selected file dialog filter index.
    void ProcessOpenDiscImageFileDialogSelection(const char *const *filelist, int filter);

    /// @brief Loads a disc image file and updates the recent list.
    /// @param[in] path Path to the disc image.
    /// @param[in] showErrorModal Whether to show an error dialog if loading fails.
    /// @return True if successful.
    bool LoadDiscImage(std::filesystem::path path, bool showErrorModal);

    /// @brief Loads the list of recent discs from disk.
    void LoadRecentDiscs();

    /// @brief Saves the list of recent discs to disk.
    void SaveRecentDiscs();

private:
    SharedContext &m_context;
    Settings &m_settings;
    ShowModalCallback m_showModal;
};

} // namespace app::services
