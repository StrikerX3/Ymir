#pragma once

#include "peripheral/peripheral_port.hpp"
#include "rtc.hpp"
#include "smpc_internal_callbacks.hpp"

#include <ymir/core/scheduler.hpp>
#include <ymir/sys/bus.hpp>

#include <ymir/state/state_smpc.hpp>

#include <ymir/hw/smpc/smpc_internal_callbacks.hpp>
#include <ymir/hw/vdp/vdp_internal_callbacks.hpp>
#include <ymir/sys/system_internal_callbacks.hpp>

#include <array>
#include <filesystem>
#include <vector>

namespace ymir::smpc {

class SMPC {
public:
    SMPC(core::Scheduler &scheduler, ISMPCOperations &smpcOps, core::Configuration::RTC &rtcConfig);
    ~SMPC();

    void Reset(bool hard);
    void FactoryReset();

    void MapCallbacks(CBInterruptCallback smCallback, CBInterruptCallback padCallback) {
        m_cbSystemManagerInterruptCallback = smCallback;
        m_cbPadInterruptCallback = padCallback;
    }

    void MapMemory(sys::Bus &bus);

    void UpdateClockRatios(const sys::ClockRatios &clockRatios);

    void LoadPersistentDataFrom(std::filesystem::path path, std::error_code &error);
    void SavePersistentDataTo(std::filesystem::path path, std::error_code &error);
    [[nodiscard]] std::filesystem::path GetPersistentDataPath() const {
        return m_persistentDataPath;
    }

    void SetResetButtonState(bool pressed) {
        bool prevState = m_resetState;
        m_resetState = pressed;
        if (prevState != m_resetState) {
            UpdateResetNMI();
        }
    }

    [[nodiscard]] uint8 GetAreaCode() const;
    void SetAreaCode(uint8 areaCode);

    peripheral::PeripheralPort &GetPeripheralPort1() {
        return m_port1;
    }
    const peripheral::PeripheralPort &GetPeripheralPort1() const {
        return m_port1;
    }

    peripheral::PeripheralPort &GetPeripheralPort2() {
        return m_port2;
    }
    const peripheral::PeripheralPort &GetPeripheralPort2() const {
        return m_port2;
    }

    rtc::RTC &GetRTC() {
        return m_rtc;
    }
    const rtc::RTC &GetRTC() const {
        return m_rtc;
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SMPCState &state) const;
    [[nodiscard]] bool ValidateState(const state::SMPCState &state) const;
    void LoadState(const state::SMPCState &state);

private:
    std::array<uint8, 7> IREG;
    std::array<uint8, 32> OREG;

    std::array<uint8, 4> SMEM;

    bool m_STE; // false = forces system configuration on boot up

    bool m_resetDisable; // RESD flag, masks the Reset state
    bool m_resetState;   // State of the console's Reset button

    void UpdateResetNMI();

    // Area code:
    //   0x1: (J) Japan
    //   0x2: (T) Asia NTSC
    //   0x4: (U) North America
    //   0x5: (B) Central/South America NTSC
    //   0x6: (K) Korea
    //   0xA: (A) Asia PAL
    //   0xC: (E) Europe PAL
    //   0xD: (L) Central/South America PAL
    // 0x0 and 0xF are prohibited; all others are reserved
    uint8 m_areaCode;

    CBInterruptCallback m_cbSystemManagerInterruptCallback;
    CBInterruptCallback m_cbPadInterruptCallback;

    ISMPCOperations &m_smpcOps;
    core::Scheduler &m_scheduler;
    core::EventID m_commandEvent;

    std::filesystem::path m_persistentDataPath;

    static void OnCommandEvent(core::EventContext &eventContext, void *userContext);
    static void OnINTBACKBreakEvent(core::EventContext &eventContext, void *userContext);

    // -------------------------------------------------------------------------
    // Memory accessors

    template <bool peek>
    uint8 Read(uint32 address);

    template <bool poke>
    void Write(uint32 address, uint8 value);

    // -------------------------------------------------------------------------
    // Persistent data

    static constexpr uint8 kPersistentDataVersion = 0x01;

    void ReadPersistentData();
    void WritePersistentData();

    // -------------------------------------------------------------------------
    // Registers

    enum class Command : uint8 {
        // Resetable system management commands
        MSHON = 0x00,    // Master SH-2 ON
        SSHON = 0x02,    // Slave SH-2 ON
        SSHOFF = 0x03,   // Slave SH-2 OFF
        SNDON = 0x06,    // Sound CPU ON (MC68EC000)
        SNDOFF = 0x07,   // Sound CPU OFF (MC68EC000)
        CDON = 0x08,     // CD ON
        CDOFF = 0x09,    // CD OFF
        SYSRES = 0x0D,   // Entire System Reset
        CKCHG352 = 0x0E, // Clock Change 352 Mode
        CKCHG320 = 0x0F, // Clock Change 320 Mode
        NMIREQ = 0x18,   // NMI Request
        RESENAB = 0x19,  // Reset Enable
        RESDISA = 0x1A,  // Reset Disable

        // Non-resetable system management commands
        INTBACK = 0x10, // Interrupt Back (SMPC Status Acquisition)
        SETSMEM = 0x17, // SMPC Memory Setting

        // RTC commands
        SETTIME = 0x16, // Time Setting

        None = 0xFF,
    };

    Command COMREG;

    // bits   r/w  code     description
    //    7   R    -        ??
    //    6   R    PDL      Peripheral Data Location bit (0=2nd+, 1=1st)
    //    5   R    NPE      Remaining Peripheral Existence bit (0=no remaining data, 1=more remaining data)
    //    4   R    RESB     Reset button status (0=released, 1=pressed)
    //  3-2   R    P2MD0-1  Port 2 Mode
    //                        00: 15-byte mode
    //                        01: 255-byte mode
    //                        10: Unused
    //                        11: 0-byte mode
    //  1-0   R    P1MD0-1  Port 1 Mode
    //                        00: 15-byte mode
    //                        01: 255-byte mode
    //                        10: Unused
    //                        11: 0-byte mode
    union RegSR {
        uint8 u8;
        struct {
            uint8 P1MDn : 2;
            uint8 P2MDn : 2;
            uint8 RESB : 1;
            uint8 NPE : 1;
            uint8 PDL : 1;
            uint8 bit7 : 1;
        };
    } SR;

    bool SF;

    uint8 PDR1;
    uint8 PDR2;
    uint8 DDR1;
    uint8 DDR2;

    uint8 m_busValue;

    uint8 ReadIREG(uint8 offset) const; // debug only
    uint8 ReadCOMREG() const;           // debug only
    uint8 ReadOREG(uint8 offset) const;
    uint8 ReadSR() const;
    uint8 ReadSF() const;
    uint8 ReadPDR1() const;
    uint8 ReadPDR2() const;
    uint8 ReadDDR1() const; // debug only
    uint8 ReadDDR2() const; // debug only
    uint8 ReadIOSEL() const;
    uint8 ReadEXLE() const;

    void WriteIREG(uint8 offset, uint8 value);
    template <bool poke>
    void WriteCOMREG(uint8 value);
    void WriteOREG(uint8 offset, uint8 value); // debug only
    void WriteSR(uint8 value);                 // debug only
    template <bool poke>
    void WriteSF(uint8 value);
    template <bool poke>
    void WritePDR1(uint8 value);
    template <bool poke>
    void WritePDR2(uint8 value);
    void WriteDDR1(uint8 value);
    void WriteDDR2(uint8 value);
    void WriteIOSEL(uint8 value);
    void WriteEXLE(uint8 value);

    // -------------------------------------------------------------------------
    // RTC

    rtc::RTC m_rtc;

    // -------------------------------------------------------------------------
    // Input, parallel I/O and INTBACK

    peripheral::PeripheralPort m_port1;
    peripheral::PeripheralPort m_port2;

    // (IOSEL) Parallel I/O SMPC-controlled (false) or SH-2 direct mode (true)
    bool m_pioMode1;
    bool m_pioMode2;

    // (EXLE) External latch enable flags
    bool m_extLatchEnable1;
    bool m_extLatchEnable2;

    // INTBACK request parameters
    bool m_getPeripheralData;
    bool m_optimize;
    uint8 m_port1mode;
    uint8 m_port2mode;

    // INTBACK output control
    std::vector<uint8> m_intbackReport; // Full peripheral report for both ports
    size_t m_intbackReportOffset;       // Offset into full peripheral report to continue reading
    bool m_intbackInProgress;           // Whether an INTBACK peripheral report read is in progress

    void TriggerOptimizedINTBACKRead();
    void TriggerVBlankIN();

    void ReadPeripherals();

    void WriteINTBACKStatusReport();
    void WriteINTBACKPeripheralReport();

    // -------------------------------------------------------------------------
    // Commands

    void ProcessCommand();
    void INTBACKBreak();

    void MSHON();
    void SSHON();
    void SSHOFF();
    void SNDON();
    void SNDOFF();
    void SYSRES();
    void CKCHG352();
    void CKCHG320();
    void NMIREQ();
    void RESENAB();
    void RESDISA();
    void INTBACK();
    void SETSMEM();
    void SETTIME();

    void ClockChange(sys::ClockSpeed clockSpeed);

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const vdp::CBTriggerEvent CbTriggerOptimizedINTBACKRead =
        util::MakeClassMemberRequiredCallback<&SMPC::TriggerOptimizedINTBACKRead>(this);

    const vdp::CBTriggerEvent CbTriggerVBlankIN = util::MakeClassMemberRequiredCallback<&SMPC::TriggerVBlankIN>(this);

    const sys::CBClockSpeedChange CbClockSpeedChange =
        util::MakeClassMemberRequiredCallback<&SMPC::UpdateClockRatios>(this);

    // -------------------------------------------------------------------------
    // Debugger

    class Probe {
    public:
        Probe(SMPC &smpc);

        util::datetime::DateTime GetRTCDateTime() const;

    private:
        SMPC &m_smpc;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    Probe m_probe{*this};
};

} // namespace ymir::smpc
