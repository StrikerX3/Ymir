//
// Created by Lennart Kotzur on 18.11.25.
//

#include "save_state_service.hpp"

#include <app/events/emu_event_factory.hpp>
#include <app/services/window_manager_service.hpp>
#include <ymir/sys/saturn.hpp>
#include <ymir/ymir.hpp>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <serdes/cereal_savestate.hpp>

#include <SDL3/SDL.h>
#include <cassert>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/std.h>
#include <fstream>
#include <iterator>

#include <app/profile.hpp>
#include <ymir/util/bitmask_enum.hpp>
#include <ymir/util/dev_log.hpp>

using namespace app::savestates;
using clk = std::chrono::steady_clock;

namespace app::services {

SaveStateService::SaveStateService(SharedContext &context, Settings &settings)
    : m_context(context)
    , m_settings(settings) {}

const Slot *SaveStateService::Peek(std::size_t slotIndex) noexcept {
    if (!IsValidIndex(slotIndex)) {
        return nullptr;
    }
    return &m_slots[slotIndex];
}

savestates::Entry *SaveStateService::Push(std::size_t slotIndex) noexcept {
    if (!IsValidIndex(slotIndex)) {
        return nullptr;
    }

    auto &slot = m_slots[slotIndex];
    std::swap(slot.backup, slot.primary);
    if (!slot.primary.state) {
        slot.primary.state = std::make_unique<ymir::savestate::SaveState>();
    }

    return &slot.primary;
}

bool SaveStateService::Pop(std::size_t slotIndex) {
    if (GetBackupStatesCount(slotIndex) == 0) {
        return false;
    }

    // Swap current state with undo state
    std::swap(m_slots[slotIndex].primary, m_slots[slotIndex].backup);

    // Clear undo after use
    m_slots[slotIndex].backup = {};
    return true;
}

bool SaveStateService::Set(std::size_t slotIndex, savestates::Slot &&slot) {
    if (!IsValidIndex(slotIndex)) {
        return false;
    }
    m_slots[slotIndex] = std::move(slot);
    return true;
}

bool SaveStateService::Erase(std::size_t slotIndex) {
    if (!IsValidIndex(slotIndex)) {
        return false;
    }
    m_slots[slotIndex] = {};
    return true;
}

std::size_t SaveStateService::GetBackupStatesCount(std::size_t slotIndex) const noexcept {
    if (!IsValidIndex(slotIndex)) {
        return false;
    }
    return m_slots[slotIndex].backup.state != nullptr ? 1 : 0;
}

std::size_t SaveStateService::GetCurrentSlotBackupStatesCount() const noexcept {
    return GetBackupStatesCount(m_currentSlot);
}

std::array<savestates::SlotMeta, SaveStateService::kSlots> SaveStateService::List() const {
    std::array<savestates::SlotMeta, kSlots> out;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        const bool present = static_cast<bool>(m_slots[i].primary.state);
        out[i] = {
            .index = i,
            .present = present,
            .backupCount = GetBackupStatesCount(i),
            .ts = m_slots[i].primary.timestamp,
        };
    }
    return out;
}

void SaveStateService::SetCurrentSlot(std::size_t slotIndex) noexcept {
    m_currentSlot = std::min(slotIndex, GetSlotCount());
}

std::mutex &SaveStateService::SlotMutex(std::size_t slotIndex) noexcept {
    if (IsValidIndex(slotIndex)) {
        return m_saveStateLocks[slotIndex];
    }
    return m_invalidSlotLock;
}

void SaveStateService::PushUndoLoadState(std::unique_ptr<ymir::savestate::SaveState> &&state) {
    m_undoLoadState.swap(state);
}

std::unique_ptr<ymir::savestate::SaveState> SaveStateService::PopUndoLoadState() {
    std::unique_ptr<ymir::savestate::SaveState> out{};
    out.swap(m_undoLoadState);
    return out;
}

void SaveStateService::LoadSaveStates() {
    WriteSaveStateMeta();

    auto basePath = m_context.profile.GetPath(app::ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.instance->GetDiscHash());

    auto &saves = *this;
    for (const auto &slotMeta : saves.List()) {
        auto load = [&](savestates::Entry &entry, std::string name) {
            auto statePath = gameStatesPath / name;
            std::ifstream in{statePath, std::ios::binary};

            if (in) {
                cereal::PortableBinaryInputArchive archive{in};
                try {
                    auto state = std::make_unique<ymir::savestate::SaveState>();
                    archive(*state);
                    entry.state.swap(state);

                    SDL_PathInfo pathInfo{};
                    if (SDL_GetPathInfo(fmt::format("{}", statePath).c_str(), &pathInfo)) {
                        const time_t time = SDL_NS_TO_SECONDS(pathInfo.modify_time);
                        const auto sysClockTime = std::chrono::system_clock::from_time_t(time);
                        entry.timestamp = sysClockTime;
                    } else {
                        entry.timestamp = std::chrono::system_clock::now();
                    }
                } catch (const cereal::Exception &e) {
                    devlog::error<grp::base>("Could not load save state from {}: {}", statePath, e.what());
                } catch (const std::exception &e) {
                    devlog::error<grp::base>("Could not load save state from {}: {}", statePath, e.what());
                } catch (...) {
                    devlog::error<grp::base>("Could not load save state from {}: unspecified error", statePath);
                }
            }
        };

        const auto slotIndex = static_cast<std::size_t>(slotMeta.index);
        auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};

        savestates::Slot state{};
        load(state.primary, fmt::format("{}.savestate", slotIndex));
        if (state.IsValid()) {
            load(state.backup, fmt::format("{}-1.savestate", slotIndex));
            saves.Set(slotIndex, std::move(state));
        } else {
            saves.Erase(slotIndex);
        }
    }
}

void SaveStateService::ClearSaveStates() {
    auto basePath = m_context.profile.GetPath(app::ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.instance->GetDiscHash());

    auto &saves = *this;

    for (const auto &slotMeta : saves.List()) {
        const auto slotIndex = slotMeta.index;
        {
            auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};
            saves.Erase(slotIndex);
        }

        std::filesystem::remove(gameStatesPath / fmt::format("{}.savestate", slotIndex));
        std::filesystem::remove(gameStatesPath / fmt::format("{}-1.savestate", slotIndex));
    }
    m_context.DisplayMessage("All save states cleared");
}

void SaveStateService::LoadSaveStateSlot(std::size_t slotIndex) {
    SetCurrentSlot(slotIndex);
    m_context.EnqueueEvent(events::emu::LoadState(CurrentSlot()));
}

void SaveStateService::SaveSaveStateSlot(std::size_t slotIndex) {
    SetCurrentSlot(slotIndex);
    m_context.EnqueueEvent(events::emu::SaveState(CurrentSlot()));
}

void SaveStateService::SelectSaveStateSlot(std::size_t slotIndex) {
    SetCurrentSlot(slotIndex);
    m_context.DisplayMessage(fmt::format("Save state slot {} selected", CurrentSlot() + 1));
}

void SaveStateService::PersistSaveState(std::size_t slotIndex) {
    auto &saves = *this;

    if (!saves.IsValidIndex(slotIndex)) {
        return;
    }

    auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};

    // ensure to not dereference empty slots
    auto *slot = saves.Peek(slotIndex);
    if (slot) {
        auto save = [&](const std::unique_ptr<ymir::savestate::SaveState> &state, std::string name) {
            if (state) {
                // Create directory for this game's save states
                auto basePath = m_context.profile.GetPath(app::ProfilePath::SaveStates);
                auto gameStatesPath = basePath / ymir::ToString(state->discHash);
                std::filesystem::create_directories(gameStatesPath);

                // Write save state
                auto statePath = gameStatesPath / name;
                std::ofstream out{statePath, std::ios::binary};
                cereal::PortableBinaryOutputArchive archive{out};
                archive(*state);
            }
            return state.get() != nullptr;
        };

        if (slot->IsValid()) {
            save(slot->primary.state, fmt::format("{}.savestate", slotIndex));
            save(slot->backup.state, fmt::format("{}-1.savestate", slotIndex));
            WriteSaveStateMeta();
            m_context.DisplayMessage(fmt::format("State {} saved", slotIndex + 1));
        }
    }
}

void SaveStateService::WriteSaveStateMeta() {
    auto basePath = m_context.profile.GetPath(app::ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.instance->GetDiscHash());
    auto gameMetaPath = gameStatesPath / "meta.txt";

    // No need to write the meta file if it exists and is recent enough
    if (std::filesystem::is_regular_file(gameMetaPath)) {
        using namespace std::chrono_literals;
        auto lastWriteTime = std::filesystem::last_write_time(gameMetaPath);
        if (std::chrono::file_clock::now() < lastWriteTime + 24h) {
            return;
        }
    }

    std::filesystem::create_directories(gameStatesPath);
    std::ofstream out{gameMetaPath};
    if (out) {
        std::unique_lock lock{m_context.locks.disc};
        const auto &disc = m_context.saturn.GetDisc();

        auto iter = std::ostream_iterator<char>(out);
        fmt::format_to(iter, "IPL ROM hash: {}\n", ymir::ToString(m_context.saturn.instance->GetIPLHash()));
        fmt::format_to(iter, "Title: {}\n", disc.header.gameTitle);
        fmt::format_to(iter, "Product Number: {}\n", disc.header.productNumber);
        fmt::format_to(iter, "Version: {}\n", disc.header.version);
        fmt::format_to(iter, "Release date: {}\n", disc.header.releaseDate);
        fmt::format_to(iter, "Disc: {}\n", disc.header.deviceInfo);
        fmt::format_to(iter, "Compatible area codes: ");
        auto bmAreaCodes = BitmaskEnum(disc.header.compatAreaCode);
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::Japan)) {
            fmt::format_to(iter, "J");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::AsiaNTSC)) {
            fmt::format_to(iter, "T");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::NorthAmerica)) {
            fmt::format_to(iter, "U");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::CentralSouthAmericaNTSC)) {
            fmt::format_to(iter, "B");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::AsiaPAL)) {
            fmt::format_to(iter, "A");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::EuropePAL)) {
            fmt::format_to(iter, "E");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::Korea)) {
            fmt::format_to(iter, "K");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::CentralSouthAmericaPAL)) {
            fmt::format_to(iter, "L");
        }
        fmt::format_to(iter, "\n");
    }
}

void SaveStateService::LoadDebuggerState() {
    const auto discHash = [&] {
        std::unique_lock lock{m_context.locks.disc};
        return ymir::ToString(m_context.saturn.GetDiscHash());
    }();
    const auto basePath = m_context.profile.GetPath(app::ProfilePath::PersistentState) / "debugger";
    if (std::filesystem::is_directory(basePath)) {
        auto &windowManager = m_context.serviceLocator.GetRequired<WindowManagerService>();
        {
            std::unique_lock lock{m_context.locks.breakpoints};
            const auto msh2Path = basePath / fmt::format("msh2-breakpoints-{}.txt", discHash);
            const auto ssh2Path = basePath / fmt::format("ssh2-breakpoints-{}.txt", discHash);
            windowManager.MasterSH2WindowSet().debuggerModel.breakpoints.LoadState(msh2Path);
            windowManager.SlaveSH2WindowSet().debuggerModel.breakpoints.LoadState(ssh2Path);
        }
        {
            std::unique_lock lock{m_context.locks.watchpoints};
            const auto msh2Path = basePath / fmt::format("msh2-watchpoints-{}.txt", discHash);
            const auto ssh2Path = basePath / fmt::format("ssh2-watchpoints-{}.txt", discHash);
            windowManager.MasterSH2WindowSet().debuggerModel.watchpoints.LoadState(msh2Path);
            windowManager.SlaveSH2WindowSet().debuggerModel.watchpoints.LoadState(ssh2Path);
        }
        m_context.debuggers.dirty = false;
        m_context.debuggers.dirtyTimestamp = clk::now();
    }
}

void SaveStateService::SaveDebuggerState() {
    const auto discHash = [&] {
        std::unique_lock lock{m_context.locks.disc};
        return ymir::ToString(m_context.saturn.GetDiscHash());
    }();
    const auto basePath = m_context.profile.GetPath(app::ProfilePath::PersistentState) / "debugger";
    std::filesystem::create_directories(basePath);
    auto &windowManager = m_context.serviceLocator.GetRequired<WindowManagerService>();
    {
        const auto msh2Path = basePath / fmt::format("msh2-breakpoints-{}.txt", discHash);
        const auto ssh2Path = basePath / fmt::format("ssh2-breakpoints-{}.txt", discHash);
        windowManager.MasterSH2WindowSet().debuggerModel.breakpoints.SaveState(msh2Path);
        windowManager.SlaveSH2WindowSet().debuggerModel.breakpoints.SaveState(ssh2Path);
    }
    {
        const auto msh2Path = basePath / fmt::format("msh2-watchpoints-{}.txt", discHash);
        const auto ssh2Path = basePath / fmt::format("ssh2-watchpoints-{}.txt", discHash);
        windowManager.MasterSH2WindowSet().debuggerModel.watchpoints.SaveState(msh2Path);
        windowManager.SlaveSH2WindowSet().debuggerModel.watchpoints.SaveState(ssh2Path);
    }
    m_context.debuggers.dirty = false;
}

void SaveStateService::CheckDebuggerStateDirty() {
    using namespace std::chrono_literals;

    if (m_context.debuggers.dirty && (clk::now() - m_context.debuggers.dirtyTimestamp) > 250ms) {
        SaveDebuggerState();
        m_context.debuggers.dirty = false;
    }
}

} // namespace app::services
