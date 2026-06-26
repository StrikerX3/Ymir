#include "persistence_service.hpp"

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/scope_guard.hpp>

namespace app::services {

static constexpr uint8 kPersistentSMPCDataVersion = 0x01;

bool PersistenceService::LoadPersistentSMPCData(ymir::smpc::PersistentSMPCData &data, std::filesystem::path path,
                                                std::error_code &error,
                                                std::function<void(std::string_view)> fnErrorMessage) const {
    errno = 0;
    error.clear();
    util::ScopeGuard sgSetErrorCode{[&] { error.assign(errno, std::generic_category()); }};

    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return false;
    }

    int version = in.get();
    if (version < 0) {
        return false;
    }
    if (version != kPersistentSMPCDataVersion) {
        fnErrorMessage(fmt::format("Invalid version - expected {}, got {}", path, kPersistentSMPCDataVersion, version));
        return false;
    }
    in.seekg(3, std::ios::cur); // skip 3 reserved bytes

    std::array<uint8, 4> smem{};
    bool ste{};
    uint64 rtcOffset{};
    uint64 rtcTimestamp{};

    in.read((char *)smem.data(), sizeof(smem));
    in.read((char *)&ste, sizeof(ste));
    in.read((char *)&rtcOffset, sizeof(rtcOffset));
    in.read((char *)&rtcTimestamp, sizeof(rtcTimestamp));
    if (!in) {
        return false;
    }

    data.SMEM = smem;
    data.STE = ste;
    data.rtc.offset = bit::little_endian_swap(rtcOffset);
    data.rtc.timestamp = bit::little_endian_swap(rtcTimestamp);

    return true;
}

void PersistenceService::SavePersistentSMPCData(const ymir::smpc::PersistentSMPCData &data, std::filesystem::path path,
                                                std::function<void(std::string_view)> fnErrorMessage) {
    m_smpcDataPending = true;
    m_smpcDataTimestamp = std::chrono::steady_clock::now();
    m_smpcDataPath = path;
    m_smpcData = data;
    m_smpcDataFnErrorMessage = fnErrorMessage;
}

void PersistenceService::DoPendingPersistences() {
    using namespace std::chrono_literals;

    if (m_smpcDataPending && std::chrono::steady_clock::now() > m_smpcDataTimestamp + 250ms) {
        m_smpcDataPending = false;
        DoSavePersistentSMPCData();
    }
}

void PersistenceService::DoSavePersistentSMPCData() {
    errno = 0;
    util::ScopeGuard sgSetErrorCode{[&] {
        if (errno != 0) {
            std::error_code error{};
            error.assign(errno, std::generic_category());
            m_smpcDataFnErrorMessage(error.message());
        }
    }};

    std::ofstream out{m_smpcDataPath, std::ios::binary};
    if (!out) {
        return;
    }

    out.put(kPersistentSMPCDataVersion);
    out.put(0x00); // reserved for future expansion
    out.put(0x00); // reserved for future expansion
    out.put(0x00); // reserved for future expansion

    const uint64 rtcOffset = bit::little_endian_swap<uint64>(m_smpcData.rtc.offset);
    const uint64 rtcTimestamp = bit::little_endian_swap<uint64>(m_smpcData.rtc.timestamp);

    out.write((const char *)&m_smpcData.SMEM[0], sizeof(m_smpcData.SMEM));
    out.write((const char *)&m_smpcData.STE, sizeof(m_smpcData.STE));
    out.write((const char *)&rtcOffset, sizeof(rtcOffset));
    out.write((const char *)&rtcTimestamp, sizeof(rtcTimestamp));
}

} // namespace app::services
