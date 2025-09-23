#pragma once

#include "cdblock_defs.hpp"

#include "cdblock_internal_callbacks.hpp"
#include "ygr_internal_callbacks.hpp"
#include <ymir/hw/sh1/sh1_internal_callbacks.hpp>

#include <ymir/sys/bus.hpp>

#include <ymir/util/inline.hpp>

#include <ymir/core/types.hpp>

#include <array>
#include <cassert>

namespace ymir::cdblock {

struct YGR {
    YGR();

    void Reset();

    void MapCallbacks(sh1::CBAssertIRQ assertIRQ6, sh1::CBAssertIRQ assertIRQ7, sh1::CBSetDREQn setDREQ0n,
                      sh1::CBSetDREQn setDREQ1n, CBTriggerExternalInterrupt0 triggerExternalInterrupt0) {
        m_cbAssertIRQ6 = assertIRQ6;
        m_cbAssertIRQ7 = assertIRQ7;
        m_cbSetDREQ0n = setDREQ0n;
        m_cbSetDREQ1n = setDREQ1n;
        m_cbTriggerExternalInterrupt0 = triggerExternalInterrupt0;
    }

    void MapMemory(sys::SH2Bus &mainBus, sys::SH1Bus &cdbBus);

    // -------------------------------------------------------------------------
    // CD-block bus

    uint16 CDBReadWord(uint32 address) const;
    void CDBWriteWord(uint32 address, uint16 value);

    // -------------------------------------------------------------------------
    // Host bus

    template <bool peek>
    uint16 HostReadWord(uint32 address) const;
    template <bool poke>
    void HostWriteWord(uint32 address, uint16 value);

    uint8 HostPeekByte(uint32 address) const;
    void HostPokeByte(uint32 address, uint8 value);

private:
    sh1::CBAssertIRQ m_cbAssertIRQ6;
    sh1::CBAssertIRQ m_cbAssertIRQ7;
    sh1::CBSetDREQn m_cbSetDREQ0n;
    sh1::CBSetDREQn m_cbSetDREQ1n;
    CBTriggerExternalInterrupt0 m_cbTriggerExternalInterrupt0;

    // Legend:
    // xx r/w   yy r/w   code        name
    //
    // xx = CD Block register address
    // yy = Host register address
    // r/w = Readable/writable indicators
    //       W& means writes are ANDed with the current value (i.e. write 0 to clear value).
    //       W| means writes are ORed with the current value (i.e. write 1 to set value).
    // code = Register code
    // name = Register name

    // 00 R/W   00 R/W   DATA        Data transfer
    // Used for data transfers between host and CD Block. Uses an internal FIFO.

    struct FIFO {
        std::array<uint16, 4096> data; // TODO: should be much smaller than this (8 entries only)
        mutable uint32 readPos;
        uint32 writePos;
        mutable uint32 count;

        FORCE_INLINE bool IsEmpty() const {
            return count == 0;
        }

        FORCE_INLINE bool IsFull() const {
            return count == data.size();
        }

        FORCE_INLINE void Clear() {
            readPos = 0;
            writePos = 0;
            count = 0;
        }

        template <bool peek>
        FORCE_INLINE uint16 Read() const {
            const uint16 value = data[readPos];
            if constexpr (!peek) {
                readPos = (readPos + 1) % data.size();
                if (count > 0) {
                    --count;
                }
            }
            return value;
        }

        template <bool poke>
        FORCE_INLINE void Write(uint16 value) {
            assert(count < data.size());
            data[writePos] = value;
            if constexpr (!poke) {
                writePos = (writePos + 1) % data.size();
                ++count;
            }
        }
    } m_fifo;

    mutable struct Registers {
        // 02 R/W   -- -     TRCTL       Transfer control
        union RegTRCTL {
            uint16 u16;
            struct {
                uint16 DIR : 1;  // 0  R/W  -    Transfer direction (0=CDB->host, 1=host->CDB)
                uint16 RES : 1;  // 1  R/W  -    Reset FIFO (1=reset)
                uint16 TE : 1;   // 2  R/W  -    Transfer enable (1=enable; start transfer to FIFO)
                uint16 unk3 : 1; // 3  R/W  -    Unknown
            };
        } TRCTL;

        // 04 R/W  -- -     CDIRQL      Lower CD IRQ bits
        union RegCDIRQL {
            uint16 u16;
            struct {
                uint16 CMD : 1;  // 0  R/W  -    Command - set when host has written to CRn registers
                uint16 RESP : 1; // 1  R/W  -    Response read - set when host has read from RRn
            };
        } CDIRQL;

        // 06 R/W  -- -     CDIRQU      Upper CD IRQ bits
        union RegCDIRQU {
            uint16 u16;
            struct {
                uint16 : 4;
                uint16 DET : 1;  // 4  R/W& -    CD Block detected - set when CD Block header is detected
                uint16 unk5 : 1; // 5  R/W& -    Unknown
                uint16 unk6 : 1; // 6  R/W& -    Unknown
            };
        } CDIRQU;

        // 08 R/W  -- -     CDMSKL      Lower CD IRQ mask
        union RegCDMSKL {
            uint16 u16;
            struct {
                uint16 CMD : 1;  // 0  R/W  -    Command IRQ enabled (1=enabled, 0=masked)
                uint16 RESP : 1; // 1  R/W  -    Response read IRQ enabled (1=enabled, 0=masked)
            };
        } CDMSKL;

        // 0A R/W  -- -     CDMSKU      Upper CD IRQ mask
        union RegCDMSKU {
            uint16 u16;
            struct {
                uint16 : 4;
                uint16 DET : 1;  // 4  R/W  -    CD Block detected IRQ enabled (1=enabled, 0=masked)
                uint16 unk5 : 1; // 5  R/W  -    Unknown IRQ enabled (1=enabled, 0=masked)
                uint16 unk6 : 1; // 6  R/W  -    Unknown IRQ enabled (1=enabled, 0=masked)
            };
        } CDMSKU;

        // 0C R/W  -- -     REG0C      Unknown register 0C
        union RegREG0C {
            uint16 u16;
            struct {
                uint16 unk0 : 1; // 0  R/W  -    Unknown
                uint16 unk1 : 1; // 1  R/W  -    Unknown
            };
        } REG0C;

        // 0E R/W  -- -     REG0E      Unknown register 0E
        // All bits readable and writable.
        uint16 REG0E;

        // 10 R/W  18 R/W   CR1/RR1    Command/Response register 1
        // 12 R/W  1C R/W   CR2/RR2    Command/Response register 2
        // 14 R/W  20 R/W   CR3/RR3    Command/Response register 3
        // 16 R/W  24 R/W   CR4/RR4    Command/Response register 4
        //
        // Host reads from RR, writes to CR.
        // SH-1 reads from CR, writes to RR.
        std::array<uint16, 4> CR; // 10-16  R/W  18-26  R/W  Command registers (CR1-4)
        std::array<uint16, 4> RR; // 10-16  R/W  18-26  R/W  Response registers (RR1-4)

        // 18 R/W  -- -     REG18      Unknown register 18
        union RegREG18 {
            uint16 u16;
            struct {
                uint16 unk0 : 6; // 0-5  -    R/W  Unknown
            };
        } REG18;

        // 1A R/W  -- -     REG1A      Unknown register 1A
        union RegREG1A {
            uint16 u16;
            struct {
                uint16 unk0 : 1; // 0  -    R/W  Unknown
                uint16 unk1 : 1; // 1  -    R/W  Unknown
                uint16 unk2 : 1; // 2  -    R/W  Unknown
                uint16 : 1;
                uint16 unk4 : 1; // 4  -    R/W  Unknown
                uint16 : 1;
                uint16 unk6 : 1; // 6  -    R/W  Unknown
                uint16 DWE : 1;  // 7  -    R/W  CD Block data write enable - set when CD Block header is detected.
                                 //              Enables DREQ0 signal for DMAC0 request.
            };
        } REG1A;

        // 1C R    -- -     REG1C      Unknown register 1C
        union RegREG1C {
            uint16 u16;
            struct {
                uint16 unk0 : 8; // 0-7  R    -    Unknown
            };
        } REG1C;

        // 1E R/W| 08 R/W&  HIRQ       Host IRQ bits
        // Writes from SH-1 can only set these bits.
        // Writes from host can only clear these bits.
        HIRQFlags HIRQ;

        // --  -   0C R/W   HIRQMASK   Host IRQ mask
        HIRQFlags HIRQMASK;
    } m_regs;

    void UpdateInterrupts();
    void UpdateFIFODREQ() const;

    void DiscChanged();
    void SectorTransferDone();

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const CBDiscChanged CbDiscChanged = util::MakeClassMemberRequiredCallback<&YGR::DiscChanged>(this);
    const CBSectorTransferDone CbSectorTransferDone =
        util::MakeClassMemberRequiredCallback<&YGR::SectorTransferDone>(this);
};

} // namespace ymir::cdblock
