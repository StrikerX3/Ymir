#pragma once

#include <ymir/hw/smpc/smpc_defs.hpp>

#include <filesystem>
#include <functional>

namespace app::services {

class PersistenceService {
public:
    /// @brief Attempts to loada persistent SMPC data from the given file. Updates the given `std::error_code` with the
    /// result of the filesystem operations and invokes `fnErrorMessage` for other kinds of errors (e.g. validation).
    /// The `data` object is only updated if the file is successfully loaded and validated.
    ///
    /// @param[in] data the persistent SMPC data object to be loaded
    /// @param[in] path the path of the file containing serialized SMPC data
    /// @param[in] error receives the error code of the file system operations
    /// @param[in] fnErrorMessage invoked when a validation or any non-filesystem error occurs
    /// @return `true` if the SMPC data was loaded successfully, `false` otherwise
    bool LoadPersistentSMPCData(
        ymir::smpc::PersistentSMPCData &data, std::filesystem::path path, std::error_code &error,
        std::function<void(std::string_view)> fnErrorMessage = [](auto) {}) const;

    /// @brief Requests to save persistent SMPC data to the specified path.
    /// Data is persisted after a short debounce period in `DoPendingPersistences()`.
    ///
    /// @param[in] data the persistent SMPC data object to be saved
    /// @param[in] path the path of the file to serialize SMPC data to
    /// @param[in] fnErrorMessage invoked when an error occurs
    void SavePersistentSMPCData(
        const ymir::smpc::PersistentSMPCData &data, std::filesystem::path path,
        std::function<void(std::string_view)> fnErrorMessage = [](auto) {});

    /// @brief Applies any pending persistences after their debounce periods.
    void DoPendingPersistences();

private:
    bool m_smpcDataPending = false;
    std::chrono::steady_clock::time_point m_smpcDataTimestamp{};
    ymir::smpc::PersistentSMPCData m_smpcData{};
    std::filesystem::path m_smpcDataPath{};
    std::function<void(std::string_view)> m_smpcDataFnErrorMessage = [](auto) {};

    void DoSavePersistentSMPCData();
};

} // namespace app::services
