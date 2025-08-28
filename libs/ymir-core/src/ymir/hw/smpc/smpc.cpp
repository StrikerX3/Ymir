#include <ymir/hw/smpc/smpc.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/bit_ops.hpp>
#include <ymir/util/date_time.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/inline.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>

namespace ymir::smpc {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   regs
    //   intback

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "SMPC";
    };

    struct regs : public base {
        static constexpr std::string_view name = "SMPC-Regs";
    };

    struct intback : public base {
        static constexpr devlog::Level level = devlog::level::trace;
        static constexpr std::string_view name = "SMPC-INTBACK";
    };

} // namespace grp

SMPC::SMPC(core::Scheduler &scheduler, ISMPCOperations &smpcOps, core::Configuration::RTC &rtcConfig)
    : m_smpcOps(smpcOps)
    , m_scheduler(scheduler)
    , m_rtc(rtcConfig) {

    SMEM.fill(0);
    m_STE = false;

    m_resetState = false;
    m_areaCode = 0x1;

    m_commandEvent = m_scheduler.RegisterEvent(core::events::SMPCCommand, this, OnCommandEvent);

    Reset(true);
}

SMPC::~SMPC() {
    WritePersistentData();
}

void SMPC::Reset(bool hard) {
    m_resetDisable = true;

    IREG.fill(0x00);
    OREG.fill(0x00);
    COMREG = Command::None;
    SR.u8 = 0x80;
    SF = false;

    PDR1 = 0;
    PDR2 = 0;
    DDR1 = 0;
    DDR2 = 0;

    m_busValue = 0x00;

    m_rtc.Reset(hard);

    m_pioMode1 = false;
    m_pioMode2 = false;

    m_extLatchEnable1 = false;
    m_extLatchEnable2 = false;

    m_getPeripheralData = false;
    m_optimize = false;
    m_port1mode = 0;
    m_port2mode = 0;

    m_intbackReport.clear();
    m_intbackReportOffset = 0;

    m_intbackInProgress = false;
    m_intbackEnqueued = false;

    m_scheduler.Cancel(m_commandEvent);
}

void SMPC::FactoryReset() {
    SMEM.fill(0x00);
    m_STE = false;

    if (m_rtc.IsVirtualMode()) {
        util::datetime::DateTime defaultDateTime{
            .year = 1994, .month = 1, .day = 1, .hour = 0, .minute = 0, .second = 0};
        m_rtc.SetDateTime(defaultDateTime);
    }
}

void SMPC::MapMemory(sys::Bus &bus) {
    static constexpr auto cast = [](void *ctx) -> SMPC & { return *static_cast<SMPC *>(ctx); };

    bus.MapNormal(
        0x010'0000, 0x017'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).Read<false>((address & 0x7F) | 1); },
        [](uint32 address, void *ctx) -> uint16 { return 0xFF00 | cast(ctx).Read<false>((address & 0x7F) | 1); },
        [](uint32 address, void *ctx) -> uint32 { return 0xFF00 | cast(ctx).Read<false>((address & 0x7F) | 1); },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).Write<false>((address & 0x7F) | 1, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).Write<false>((address & 0x7F) | 1, value); },
        [](uint32 address, uint32 value, void *ctx) { cast(ctx).Write<false>((address & 0x7F) | 1, value); });

    bus.MapSideEffectFree(
        0x010'0000, 0x017'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).Read<true>((address & 0x7F) | 1); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).Read<true>((address & 0x7F) | 1); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).Read<true>((address & 0x7F) | 1) << 16u;
            value |= cast(ctx).Read<true>(((address & 0x7F) | 1) + 2);
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).Write<true>((address & 0x7F) | 1, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).Write<true>((address & 0x7F) | 1, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).Write<true>(((address & 0x7F) | 1) + 0, value >> 16u);
            cast(ctx).Write<true>(((address & 0x7F) | 1) + 2, value >> 0u);
        });
}

FLATTEN void SMPC::UpdateClockRatios(const sys::ClockRatios &clockRatios) {
    m_rtc.UpdateClockRatios(clockRatios);
}

void SMPC::LoadPersistentDataFrom(std::filesystem::path path, std::error_code &error) {
    m_persistentDataPath = path;
    errno = 0;
    ReadPersistentData();
    error.assign(errno, std::generic_category());
}

void SMPC::SavePersistentDataTo(std::filesystem::path path, std::error_code &error) {
    m_persistentDataPath = path;
    errno = 0;
    WritePersistentData();
    error.assign(errno, std::generic_category());
}

uint8 SMPC::GetAreaCode() const {
    return m_areaCode;
}

void SMPC::SetAreaCode(uint8 areaCode) {
    areaCode &= 0xF;
    devlog::debug<grp::base>("Setting area code to {:X}", areaCode);
    m_areaCode = areaCode;
}

void SMPC::SaveState(state::SMPCState &state) const {
    state.IREG = IREG;
    state.OREG = OREG;
    state.COMREG = ReadCOMREG();
    state.SR = ReadSR();
    state.SF = ReadSF();
    state.PDR1 = ReadPDR1();
    state.PDR2 = ReadPDR2();
    state.DDR1 = ReadDDR1();
    state.DDR2 = ReadDDR2();
    state.IOSEL = ReadIOSEL();
    state.EXLE = ReadEXLE();

    state.intback.getPeripheralData = m_getPeripheralData;
    state.intback.optimize = m_optimize;
    state.intback.port1mode = m_port1mode;
    state.intback.port2mode = m_port2mode;
    state.intback.report = m_intbackReport;
    state.intback.reportOffset = m_intbackReportOffset;
    state.intback.inProgress = m_intbackInProgress;
    state.intback.enqueued = m_intbackEnqueued;

    state.busValue = m_busValue;
    state.resetDisable = m_resetDisable;

    m_rtc.SaveState(state);
}

bool SMPC::ValidateState(const state::SMPCState &state) const {
    if (!m_rtc.ValidateState(state)) {
        return false;
    }

    return true;
}

void SMPC::LoadState(const state::SMPCState &state) {
    IREG = state.IREG;
    OREG = state.OREG;
    WriteCOMREG<true>(state.COMREG);
    WriteSR(state.SR);
    WriteSF<true>(state.SF);
    WritePDR1<true>(state.PDR1);
    WritePDR2<true>(state.PDR2);
    WriteDDR1(state.DDR1);
    WriteDDR2(state.DDR2);
    WriteIOSEL(state.IOSEL);
    WriteEXLE(state.EXLE);

    m_getPeripheralData = state.intback.getPeripheralData;
    m_optimize = state.intback.optimize;
    m_port1mode = state.intback.port1mode;
    m_port2mode = state.intback.port2mode;
    m_intbackReport = state.intback.report;
    m_intbackReportOffset = state.intback.reportOffset;
    m_intbackInProgress = state.intback.inProgress;
    m_intbackEnqueued = state.intback.enqueued;

    m_busValue = state.busValue;
    m_resetDisable = state.resetDisable;

    m_rtc.LoadState(state);
}

void SMPC::UpdateResetNMI() {
    if (!m_resetDisable && m_resetState) {
        m_smpcOps.RaiseNMI();
    }
}

void SMPC::OnCommandEvent(core::EventContext &eventContext, void *userContext) {
    auto &smpc = *static_cast<SMPC *>(userContext);
    smpc.ProcessCommand();
    if (smpc.COMREG == Command::INTBACK) {
        if (smpc.m_intbackEnqueued && !smpc.SF) {
            // Process dual-issued INTBACK command
            smpc.m_intbackEnqueued = false;
            eventContext.Reschedule(1000);
            devlog::debug<grp::intback>("Scheduling dual-issued INTBACK command");
        } else if (smpc.m_intbackInProgress) {
            // Process continue/break request
            const bool continueFlag = bit::test<7>(smpc.IREG[0]);
            const bool breakFlag = bit::test<6>(smpc.IREG[0]);
            if (continueFlag) {
                if (!smpc.m_optimize) {
                    smpc.SF = true;
                    eventContext.Reschedule(1000);
                    devlog::debug<grp::intback>("Scheduling INTBACK continue request");
                }
            } else if (breakFlag) {
                // Delay processing by 25 microseconds
                smpc.m_scheduler.SetEventCallback(smpc.m_commandEvent, userContext, OnINTBACKBreakEvent);
                eventContext.Reschedule(100);
                devlog::debug<grp::intback>("Scheduling INTBACK break request");
            }
        }
    }
}

void SMPC::OnINTBACKBreakEvent(core::EventContext &eventContext, void *userContext) {
    auto &smpc = *static_cast<SMPC *>(userContext);
    smpc.INTBACKBreak();
    smpc.m_scheduler.SetEventCallback(smpc.m_commandEvent, userContext, ClearSFEvent);
    eventContext.Reschedule(750);
}

void SMPC::ClearSFEvent(core::EventContext &eventContext, void *userContext) {
    auto &smpc = *static_cast<SMPC *>(userContext);
    smpc.SF = false;
    smpc.m_scheduler.SetEventCallback(smpc.m_commandEvent, userContext, OnCommandEvent);
}

template <bool peek>
uint8 SMPC::Read(uint32 address) {
    if constexpr (peek) {
        switch (address) {
        case 0x01: return ReadIREG(0);
        case 0x03: return ReadIREG(1);
        case 0x05: return ReadIREG(2);
        case 0x07: return ReadIREG(3);
        case 0x09: return ReadIREG(4);
        case 0x0B: return ReadIREG(5);
        case 0x0D: return ReadIREG(6);
        case 0x1F: return ReadCOMREG();
        case 0x79: return ReadDDR1();
        case 0x7B: return ReadDDR2();
        case 0x7D: return ReadIOSEL();
        case 0x7F: return ReadEXLE();
        }
    }

    switch (address) {
    case 0x21: return ReadOREG(0);
    case 0x23: return ReadOREG(1);
    case 0x25: return ReadOREG(2);
    case 0x27: return ReadOREG(3);
    case 0x29: return ReadOREG(4);
    case 0x2B: return ReadOREG(5);
    case 0x2D: return ReadOREG(6);
    case 0x2F: return ReadOREG(7);
    case 0x31: return ReadOREG(8);
    case 0x33: return ReadOREG(9);
    case 0x35: return ReadOREG(10);
    case 0x37: return ReadOREG(11);
    case 0x39: return ReadOREG(12);
    case 0x3B: return ReadOREG(13);
    case 0x3D: return ReadOREG(14);
    case 0x3F: return ReadOREG(15);
    case 0x41: return ReadOREG(16);
    case 0x43: return ReadOREG(17);
    case 0x45: return ReadOREG(18);
    case 0x47: return ReadOREG(19);
    case 0x49: return ReadOREG(20);
    case 0x4B: return ReadOREG(21);
    case 0x4D: return ReadOREG(22);
    case 0x4F: return ReadOREG(23);
    case 0x51: return ReadOREG(24);
    case 0x53: return ReadOREG(25);
    case 0x55: return ReadOREG(26);
    case 0x57: return ReadOREG(27);
    case 0x59: return ReadOREG(28);
    case 0x5B: return ReadOREG(29);
    case 0x5D: return ReadOREG(30);
    case 0x5F: return ReadOREG(31);
    case 0x61: return ReadSR();
    case 0x63: return ReadSF();
    case 0x75: return ReadPDR1();
    case 0x77: return ReadPDR2();
    default:
        if constexpr (!peek) {
            devlog::debug<grp::regs>("Unhandled SMPC read from {:02X}", address);
        }
        return m_busValue;
    }
}

template <bool poke>
void SMPC::Write(uint32 address, uint8 value) {
    if constexpr (poke) {
        switch (address) {
        case 0x21: WriteOREG(0, value); break;
        case 0x23: WriteOREG(1, value); break;
        case 0x25: WriteOREG(2, value); break;
        case 0x27: WriteOREG(3, value); break;
        case 0x29: WriteOREG(4, value); break;
        case 0x2B: WriteOREG(5, value); break;
        case 0x2D: WriteOREG(6, value); break;
        case 0x2F: WriteOREG(7, value); break;
        case 0x31: WriteOREG(8, value); break;
        case 0x33: WriteOREG(9, value); break;
        case 0x35: WriteOREG(10, value); break;
        case 0x37: WriteOREG(11, value); break;
        case 0x39: WriteOREG(12, value); break;
        case 0x3B: WriteOREG(13, value); break;
        case 0x3D: WriteOREG(14, value); break;
        case 0x3F: WriteOREG(15, value); break;
        case 0x41: WriteOREG(16, value); break;
        case 0x43: WriteOREG(17, value); break;
        case 0x45: WriteOREG(18, value); break;
        case 0x47: WriteOREG(19, value); break;
        case 0x49: WriteOREG(20, value); break;
        case 0x4B: WriteOREG(21, value); break;
        case 0x4D: WriteOREG(22, value); break;
        case 0x4F: WriteOREG(23, value); break;
        case 0x51: WriteOREG(24, value); break;
        case 0x53: WriteOREG(25, value); break;
        case 0x55: WriteOREG(26, value); break;
        case 0x57: WriteOREG(27, value); break;
        case 0x59: WriteOREG(28, value); break;
        case 0x5B: WriteOREG(29, value); break;
        case 0x5D: WriteOREG(30, value); break;
        case 0x5F: WriteOREG(31, value); break;
        case 0x61: WriteSR(value); break;
        }
    } else {
        m_busValue = value;
    }

    devlog::trace<grp::regs>("Register write to {:02X} = {:02X}", address, value);

    switch (address) {
    case 0x01:
        WriteIREG(0, value);
        if constexpr (!poke) {
            if (m_intbackInProgress) {
                // Handle INTBACK continue/break requests
                const bool continueFlag = bit::test<7>(IREG[0]);
                const bool breakFlag = bit::test<6>(IREG[0]);
                if (continueFlag) {
                    devlog::trace<grp::intback>("INTBACK continue request");
                    SF = true;
                    if (!m_optimize) {
                        // TODO: check this; seems like it should execute after VBlank OUT
                        m_scheduler.ScheduleFromNow(m_commandEvent, 1000);
                    }
                } else if (breakFlag) {
                    devlog::trace<grp::intback>("INTBACK break request");

                    // Delay processing by 25 microseconds
                    m_scheduler.SetEventCallback(m_commandEvent, this, OnINTBACKBreakEvent);
                    m_scheduler.ScheduleFromNow(m_commandEvent, 100);
                }
            }
        }
        break;
    case 0x03: WriteIREG(1, value); break;
    case 0x05: WriteIREG(2, value); break;
    case 0x07: WriteIREG(3, value); break;
    case 0x09: WriteIREG(4, value); break;
    case 0x0B: WriteIREG(5, value); break;
    case 0x0D: WriteIREG(6, value); break;
    case 0x1F: WriteCOMREG<poke>(value); break;
    case 0x63: WriteSF<poke>(value); break;
    case 0x75: WritePDR1<poke>(value); break;
    case 0x77: WritePDR2<poke>(value); break;
    case 0x79: WriteDDR1(value); break;
    case 0x7B: WriteDDR2(value); break;
    case 0x7D: WriteIOSEL(value); break;
    case 0x7F: WriteEXLE(value); break;
    default:
        if constexpr (!poke) {
            devlog::debug<grp::regs>("Unhandled SMPC write to {:02X} = {:02X}", address, value);
        }
        break;
    }
}

void SMPC::ReadPersistentData() {
    if (m_persistentDataPath.empty()) {
        return;
    }

    // TODO: replace std iostream with custom I/O class with managed endianness
    std::ifstream in{m_persistentDataPath, std::ios::binary};
    if (!in) {
        return;
    }

    int version = in.get();
    if (version != kPersistentDataVersion || version < 0) {
        return;
    }
    in.seekg(3, std::ios::cur); // skip 3 reserved bytes

    std::array<uint8, 4> smem{};
    bool ste{};

    in.read((char *)&smem[0], sizeof(smem));
    in.read((char *)&ste, sizeof(ste));
    if (!in) {
        return;
    }
    SMEM = smem;
    m_STE = ste;

    m_rtc.ReadPersistentData(in);
}

void SMPC::WritePersistentData() {
    if (m_persistentDataPath.empty()) {
        return;
    }

    // TODO: replace std iostream with custom I/O class with managed endianness
    std::ofstream out{m_persistentDataPath, std::ios::binary};
    if (!out) {
        return;
    }

    out.put(kPersistentDataVersion);
    out.put(0x00); // reserved for future expansion
    out.put(0x00); // reserved for future expansion
    out.put(0x00); // reserved for future expansion
    out.write((const char *)&SMEM[0], sizeof(SMEM));
    out.write((const char *)&m_STE, sizeof(m_STE));

    m_rtc.WritePersistentData(out);
}

FORCE_INLINE uint8 SMPC::ReadIREG(uint8 offset) const {
    assert(offset < 7);
    return IREG[offset];
}

FORCE_INLINE uint8 SMPC::ReadCOMREG() const {
    return static_cast<uint8>(COMREG);
}

FORCE_INLINE uint8 SMPC::ReadOREG(uint8 offset) const {
    return OREG[offset & 31];
}

FORCE_INLINE uint8 SMPC::ReadSR() const {
    return SR.u8;
}

FORCE_INLINE uint8 SMPC::ReadSF() const {
    return SF | (m_busValue & 0xFE);
}

FORCE_INLINE uint8 SMPC::ReadPDR1() const {
    return (PDR1 & 0x7F) | (m_busValue & 0x80);
}

FORCE_INLINE uint8 SMPC::ReadPDR2() const {
    return (PDR2 & 0x7F) | (m_busValue & 0x80);
}

FORCE_INLINE uint8 SMPC::ReadDDR1() const {
    return DDR1;
}

FORCE_INLINE uint8 SMPC::ReadDDR2() const {
    return DDR1;
}

FORCE_INLINE uint8 SMPC::ReadIOSEL() const {
    uint8 value = 0;
    bit::deposit_into<0>(value, m_pioMode1);
    bit::deposit_into<1>(value, m_pioMode2);
    return value;
}

FORCE_INLINE uint8 SMPC::ReadEXLE() const {
    uint8 value = 0;
    bit::deposit_into<0>(value, m_extLatchEnable1);
    bit::deposit_into<1>(value, m_extLatchEnable2);
    return value;
}

FORCE_INLINE void SMPC::WriteIREG(uint8 offset, uint8 value) {
    assert(offset < 7);
    IREG[offset] = value;
}

template <bool poke>
FORCE_INLINE void SMPC::WriteCOMREG(uint8 value) {
    if constexpr (!poke) {
        // Reject if a command is already scheduled
        if (m_scheduler.IsScheduled(m_commandEvent)) {
            if (static_cast<Command>(value) == Command::INTBACK && COMREG == Command::INTBACK) {
                // INTBACK command sent while a previous INTBACK command is processing its continue request; enqueue it
                if (!m_intbackEnqueued) {
                    m_intbackEnqueued = true;
                    devlog::debug<grp::regs>("INTBACK dual-issue detected");
                    return;
                }
            }
            devlog::debug<grp::regs>("Attempted to write {:02X} to COMREG while a command is being processed; ignoring",
                                     value);
            return;
        }
    }

    COMREG = static_cast<Command>(value);

    if constexpr (!poke) {
        devlog::trace<grp::regs>("Scheduling command {:02X}", static_cast<uint8>(COMREG));
        if (COMREG == Command::SYSRES || COMREG == Command::CKCHG352 || COMREG == Command::CKCHG320) {
            // TODO: these should take ~100ms (about 2.8 million SH-2 cycles) to complete
            // Doing a shorter delay here to make it snappier
            m_scheduler.ScheduleFromNow(m_commandEvent, 200000);
        } else {
            // TODO: CDON and CDOFF execute in 40 microseconds; all other commands take 30 microseconds to complete
            // HACK: delay for longer to allow Quake (EU) to boot
            m_scheduler.ScheduleFromNow(m_commandEvent, 240);
        }
    }
}

FORCE_INLINE void SMPC::WriteOREG(uint8 offset, uint8 value) {
    OREG[offset & 31] = value;
}

FORCE_INLINE void SMPC::WriteSR(uint8 value) {
    SR.u8 = value;
}

template <bool poke>
FORCE_INLINE void SMPC::WriteSF(uint8 value) {
    if constexpr (poke) {
        SF = bit::test<0>(value);
    } else {
        SF = true;
    }
}

template <bool poke>
FORCE_INLINE void SMPC::WritePDR1(uint8 value) {
    if constexpr (poke) {
        PDR1 = value;
    } else {
        const uint8 prevPDR1 = PDR1;
        m_port1.UpdateInputs();
        PDR1 = m_port1.WritePDR(DDR1, value);
        if (m_extLatchEnable1 && bit::test<6>((prevPDR1 ^ PDR1) & DDR1)) {
            m_cbPadInterruptCallback();
        }
    }
}

template <bool poke>
FORCE_INLINE void SMPC::WritePDR2(uint8 value) {
    if constexpr (poke) {
        PDR2 = value;
    } else {
        const uint8 prevPDR2 = PDR2;
        m_port2.UpdateInputs();
        PDR2 = m_port2.WritePDR(DDR2, value);
        if (m_extLatchEnable2 && bit::test<6>((prevPDR2 ^ PDR2) & DDR2)) {
            m_cbPadInterruptCallback();
        }
    }
}

FORCE_INLINE void SMPC::WriteDDR1(uint8 value) {
    DDR1 = value;
}

FORCE_INLINE void SMPC::WriteDDR2(uint8 value) {
    DDR2 = value;
}

FORCE_INLINE void SMPC::WriteIOSEL(uint8 value) {
    m_pioMode1 = bit::test<0>(value);
    m_pioMode2 = bit::test<1>(value);
}

FORCE_INLINE void SMPC::WriteEXLE(uint8 value) {
    m_extLatchEnable1 = bit::test<0>(value);
    m_extLatchEnable2 = bit::test<1>(value);
}

void SMPC::ProcessCommand() {
    switch (COMREG) {
    case Command::MSHON: MSHON(); break;
    case Command::SSHON: SSHON(); break;
    case Command::SSHOFF: SSHOFF(); break;
    case Command::SNDON: SNDON(); break;
    case Command::SNDOFF: SNDOFF(); break;
    case Command::SYSRES: SYSRES(); break;
    case Command::CKCHG352: CKCHG352(); break;
    case Command::CKCHG320: CKCHG320(); break;
    case Command::NMIREQ: NMIREQ(); break;
    case Command::RESENAB: RESENAB(); break;
    case Command::RESDISA: RESDISA(); break;
    case Command::INTBACK: INTBACK(); break;
    case Command::SETSMEM: SETSMEM(); break;
    case Command::SETTIME: SETTIME(); break;
    default:
        devlog::debug<grp::base>("Unimplemented SMPC command {:02X}", static_cast<uint8>(COMREG));
        SF = false; // done processing
        OREG[31] = static_cast<uint8>(COMREG);
        break;
    }
}

void SMPC::INTBACKBreak() {
    devlog::trace<grp::intback>("INTBACK cancelled");
    m_intbackInProgress = false;
    SR.NPE = 0;
    SR.PDL = 0;
    // SF = false; // delayed
}

void SMPC::MSHON() {
    devlog::debug<grp::base>("Processing MSHON");

    // TODO: is this supposed to do something...?

    SF = false; // done processing

    OREG[31] = 0x00;
}

void SMPC::SSHON() {
    devlog::debug<grp::base>("Processing SSHON");

    // Turn on and reset slave SH-2
    m_smpcOps.EnableAndResetSlaveSH2();

    SF = false; // done processing

    OREG[31] = 0x02;
}

void SMPC::SSHOFF() {
    devlog::debug<grp::base>("Processing SSHOFF");

    // Turn off slave SH-2
    m_smpcOps.DisableSlaveSH2();

    SF = false; // done processing

    OREG[31] = 0x03;
}

void SMPC::SNDON() {
    devlog::debug<grp::base>("Processing SNDON");

    m_smpcOps.EnableAndResetM68K();

    SF = false; // done processing

    OREG[31] = 0x06;
}

void SMPC::SNDOFF() {
    devlog::debug<grp::base>("Processing SNDOFF");

    m_smpcOps.DisableM68K();

    SF = false; // done processing

    OREG[31] = 0x07;
}

void SMPC::SYSRES() {
    devlog::debug<grp::base>("Processing SYSRES");

    m_smpcOps.SoftResetSystem();

    SF = false; // done processing

    OREG[31] = 0x0D;
}

void SMPC::CKCHG352() {
    devlog::debug<grp::base>("Processing CKCHG352");

    ClockChange(sys::ClockSpeed::_352);

    SF = false; // done processing

    OREG[31] = 0x0E;
}
void SMPC::CKCHG320() {
    devlog::debug<grp::base>("Processing CKCHG320");

    ClockChange(sys::ClockSpeed::_320);

    SF = false; // done processing

    OREG[31] = 0x0F;
}

void SMPC::NMIREQ() {
    devlog::debug<grp::base>("Processing NMIREQ");

    m_smpcOps.RaiseNMI();

    SF = false; // done processing

    OREG[31] = 0x19;
}

void SMPC::RESENAB() {
    devlog::debug<grp::base>("Processing RESENAB");

    bool prevState = m_resetDisable;
    m_resetDisable = false;
    if (prevState != m_resetDisable) {
        UpdateResetNMI();
    }

    SF = false; // done processing

    OREG[31] = 0x19;
}

void SMPC::RESDISA() {
    devlog::debug<grp::base>("Processing RESDISA");

    bool prevState = m_resetDisable;
    m_resetDisable = true;
    if (prevState != m_resetDisable) {
        UpdateResetNMI();
    }

    SF = false; // done processing

    OREG[31] = 0x1A;
}

void SMPC::INTBACK() {
    devlog::trace<grp::intback>("Processing INTBACK {:02X} {:02X} {:02X}", IREG[0], IREG[1], IREG[2]);

    if (m_intbackInProgress) {
        WriteINTBACKPeripheralReport();
    } else {
        if (IREG[2] != 0xF0) {
            devlog::debug<grp::intback>("Unexpected INTBACK IREG2: {:02X}", IREG[2]);
            // TODO: does SMPC reject the command in this case?
        }

        const bool getStatus = bit::extract<0, 5>(IREG[0]) == 0x01;
        m_optimize = !bit::test<1>(IREG[1]); // OPE=0 means optimize
        m_getPeripheralData = bit::test<3>(IREG[1]);
        m_port1mode = bit::extract<4, 5>(IREG[1]);
        m_port2mode = bit::extract<6, 7>(IREG[1]);

        devlog::trace<grp::intback>("Init INTBACK: optimize={} status={} periph={} modes={:X} {:X}", m_optimize,
                                    getStatus, m_getPeripheralData, m_port1mode, m_port2mode);

        m_intbackInProgress = m_getPeripheralData;

        if (getStatus) {
            WriteINTBACKStatusReport();
        } else if (m_getPeripheralData) {
            WriteINTBACKPeripheralReport();
        }
    }

    if (!m_intbackInProgress) {
        SF = false; // done processing
    }

    devlog::trace<grp::intback>("Raising SMPC interrupt");
    m_cbSystemManagerInterruptCallback();
}

void SMPC::TriggerOptimizedINTBACKRead() {
    if (m_optimize) {
        m_optimize = false;

        if (SF && m_intbackInProgress && m_getPeripheralData) {
            devlog::trace<grp::intback>("Optimized INTBACK triggered");
            m_scheduler.ScheduleFromNow(m_commandEvent, 0);
        }
    }
}

void SMPC::TriggerVBlankIN() {
    // Cause pending INTBACK to timeout
    if (m_intbackInProgress) {
        devlog::trace<grp::intback>("INTBACK timed out");
        INTBACKBreak();
        m_scheduler.SetEventCallback(m_commandEvent, this, ClearSFEvent);
        m_scheduler.ScheduleFromNow(m_commandEvent, 550);
    }
}

void SMPC::ReadPeripherals() {
    m_intbackReportOffset = 0;

    const size_t port1Len = m_port1.UpdateInputs();
    const size_t port2Len = m_port2.UpdateInputs();
    m_intbackReport.resize(port1Len + port2Len);
    m_port1.Read(std::span<uint8>{m_intbackReport.begin(), port1Len});
    m_port2.Read(std::span<uint8>{m_intbackReport.begin() + port1Len, port2Len});

    /*fmt::memory_buffer buf{};
    auto out = std::back_inserter(buf);
    fmt::format_to(out, "periph data:");
    for (auto b : m_intbackReport) {
        fmt::format_to(out, " {:02X}", b);
    }
    devlog::debug<grp::intback>("{}", fmt::to_string(buf));*/
}

void SMPC::WriteINTBACKStatusReport() {
    devlog::trace<grp::intback>("INTBACK status report");

    SR.bit7 = 0;                  // fixed 0
    SR.PDL = 1;                   // fixed 1 for status report
    SR.NPE = m_getPeripheralData; // 0=no peripheral data, 1=has peripheral data
    SR.RESB = m_resetState;       // reset button state (0=off, 1=on)
    SR.P1MDn = m_port1mode;
    SR.P2MDn = m_port2mode;

    OREG[0] = (m_STE << 7) | (m_resetDisable << 6);

    if (m_rtc.IsVirtualMode()) {
        m_rtc.UpdateSysClock(m_scheduler.CurrentCount());
    }
    const auto dt = m_rtc.GetDateTime();

    OREG[1] = util::to_bcd(dt.year / 100);  // Year 1000s, Year 100s (BCD)
    OREG[2] = util::to_bcd(dt.year % 100);  // Year 10s, Year 1s (BCD)
    OREG[3] = (dt.weekday << 4) | dt.month; // Day of week (0=sun), Month (hex, 1=jan)
    OREG[4] = util::to_bcd(dt.day);         // Day (BCD)
    OREG[5] = util::to_bcd(dt.hour);        // Hour (BCD)
    OREG[6] = util::to_bcd(dt.minute);      // Minute (BCD)
    OREG[7] = util::to_bcd(dt.second);      // Second (BCD)

    // the date/time below refers to this project's very first commit
    /*OREG[1] = 0x20; // Year 1000s, Year 100s (BCD)
    OREG[2] = 0x24; // Year 10s, Year 1s (BCD)
    OREG[3] = 0x0B; // Day of week (0=sun), Month (hex, 1=jan)
    OREG[4] = 0x17; // Day (BCD)
    OREG[5] = 0x17; // Hour (BCD)
    OREG[6] = 0x01; // Minute (BCD)
    OREG[7] = 0x20; // Second (BCD)*/

    // TODO: read cartridge code from cartridge
    OREG[8] = 0x00; // Cartridge code (CTG1-0) == 0b00
    OREG[9] = m_areaCode;

    // TODO: update flags accordingly
    const bool dotsel = m_smpcOps.GetClockSpeed() == sys::ClockSpeed::_352;
    const bool mshnmi = m_smpcOps.GetNMI();
    OREG[10] = 0b00110100 | (dotsel << 6u) | (mshnmi << 3u); // System status 1 (TODO: 1=SYSRES, 0=SNDRES)
    OREG[11] = 0b00000000;                                   // System status 2 (TODO: 6=CDRES)

    OREG[12] = SMEM[0]; // SMEM 1 Saved Data
    OREG[13] = SMEM[1]; // SMEM 2 Saved Data
    OREG[14] = SMEM[2]; // SMEM 3 Saved Data
    OREG[15] = SMEM[3]; // SMEM 4 Saved Data

    OREG[31] = 0x10;
}

void SMPC::WriteINTBACKPeripheralReport() {
    const bool firstReport = m_intbackReportOffset == 0;
    devlog::trace<grp::intback>("INTBACK peripheral report - first? {}", firstReport);

    if (firstReport) {
        ReadPeripherals();
    }

    const uint8 reportLength = std::min<uint8>(32, m_intbackReport.size() - m_intbackReportOffset);
    std::copy_n(m_intbackReport.begin() + m_intbackReportOffset, reportLength, OREG.begin());
    if (reportLength < 32) {
        std::fill(OREG.begin() + reportLength, OREG.end(), 0xFF);
    }
    m_intbackReportOffset += reportLength;
    if (m_intbackReportOffset == m_intbackReport.size()) {
        m_intbackInProgress = false;
        m_intbackReportOffset = 0;
        m_optimize = false;
        devlog::trace<grp::intback>("INTBACK peripheral report ended");
    }

    SR.bit7 = 1;                  // fixed 1
    SR.PDL = firstReport;         // 1=first data, 0=second+ data
    SR.NPE = m_intbackInProgress; // 0=no remaining data, 1=more data
    SR.RESB = m_resetState;       // reset button state (0=off, 1=on)
    SR.P1MDn = m_port1mode;       // port 1 mode \  0=15 byte, 1=255 byte
    SR.P2MDn = m_port2mode;       // port 2 mode /  2=unused,  3=0 byte
}

void SMPC::SETSMEM() {
    devlog::debug<grp::base>("Processing SETSMEM {:02X} {:02X} {:02X} {:02X}", IREG[0], IREG[1], IREG[2], IREG[3]);

    SMEM[0] = IREG[0];
    SMEM[1] = IREG[1];
    SMEM[2] = IREG[2];
    SMEM[3] = IREG[3];
    m_STE = true;
    WritePersistentData();

    SF = false; // done processing

    OREG[31] = 0x17;
}

void SMPC::SETTIME() {
    devlog::debug<grp::base>("Processing SETTIME");

    util::datetime::DateTime dt{};
    dt.year = util::from_bcd((IREG[0] << 8u) + IREG[1]);
    dt.weekday = bit::extract<4, 7>(IREG[2]);
    dt.month = bit::extract<0, 3>(IREG[2]);
    dt.day = util::from_bcd(IREG[3]);
    dt.hour = util::from_bcd(IREG[4]);
    dt.minute = util::from_bcd(IREG[5]);
    dt.second = util::from_bcd(IREG[6]);

    devlog::debug<grp::base>("Setting time to {}/{:02d}/{:02d} {:02d}:{:02d}:{:02d}", dt.year, dt.month, dt.day,
                             dt.hour, dt.minute, dt.second);

    m_rtc.SetDateTime(dt);
    WritePersistentData();

    SF = false; // done processing

    OREG[31] = 0x16;
}

void SMPC::ClockChange(sys::ClockSpeed clockSpeed) {
    m_smpcOps.ClockChangeSoftReset();
    // TODO: clear VDP VRAMs?
    m_smpcOps.DisableSlaveSH2();
    m_smpcOps.RaiseNMI();
    m_smpcOps.SetClockSpeed(clockSpeed);
}

// -----------------------------------------------------------------------------
// Probe implementation

SMPC::Probe::Probe(SMPC &smpc)
    : m_smpc(smpc) {}

util::datetime::DateTime SMPC::Probe::GetRTCDateTime() const {
    return m_smpc.m_rtc.GetDateTime();
}

} // namespace ymir::smpc
