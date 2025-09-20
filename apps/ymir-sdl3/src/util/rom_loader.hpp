#pragma once

#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------------------------------------------------
// Forward declarations

namespace ymir {
struct Saturn;
}

// ---------------------------------------------------------------------------------------------------------------------

namespace util {

struct ROMLoadResult {
    bool succeeded;
    std::string errorMessage;

    static ROMLoadResult Success() {
        return {true};
    }

    static ROMLoadResult Fail(std::string message) {
        return {false, message};
    }
};

ROMLoadResult LoadIPLROM(std::filesystem::path path, ymir::Saturn &saturn);
ROMLoadResult LoadCDBlockROM(std::filesystem::path path, ymir::Saturn &saturn);

} // namespace util
