#include <ymir/hw/scsp/scsp_dsp.hpp>

#include <ostream>

namespace ymir::scsp {

DSP::DSP(uint8 *ram)
    : m_WRAM(ram) {
    Reset();
}

void DSP::Reset() {
    program.fill({.u64 = 0});
    tempMem.fill(0);
    soundMem.fill(0);
    coeffs.fill(0);
    addrs.fill(0);
    mixStack.fill(0);
    effectOut.fill(0);
    audioInOut.fill(0);

    ringBufferLeadAddress = 0;
    ringBufferLength = 0;
    UpdateRBP();
    UpdateRBL();

    // DSP program is aligned to operation 7, which processes slot i-6.
    // 4 DSP program steps per slot -> -6*4 = -24 = 104 (or 0x68) in modulo 128
    PC = 0x68;

    m_programLength = 0;

    INPUTS = 0;

    SFT_REG = 0;
    FRC_REG = 0;
    Y_REG = 0;
    ADRS_REG = 0;

    MDEC_CT = 0;

    m_mixStackGen = 0;
    m_mixStackNull = 0xFFFF;

    m_readPending = false;
    m_readNOFL = false;
    m_readValue = 0;

    m_writePending = false;
    m_writeValue = 0;

    m_readWriteAddr = 0;
}

void DSP::UpdateProgramLength(uint8 writeIndex) {
    const bool wroteNOP = program[writeIndex].u64 == 0;
    if (wroteNOP && writeIndex == m_programLength - 1) {
        // If writing a NOP to the last instruction, shrink the program
        while (m_programLength > 0 && program[m_programLength - 1].u64 == 0) {
            --m_programLength;
        }
    } else if (!wroteNOP && writeIndex >= m_programLength) {
        // If writing anything other than a NOP past the current program length, increase program length
        m_programLength = writeIndex + 1;
    }

    // Run one extra NOP to ensure side-effects are carried out
    if (m_programLength < program.size()) {
        ++m_programLength;
    }
}

void DSP::DumpRegs(std::ostream &out) const {
    auto write = [&](const auto &reg) { out.write((const char *)&reg, sizeof(reg)); };
    write(ringBufferLeadAddress);
    write(ringBufferLength);
    write(PC);
    write(INPUTS);

    write(SFT_REG);
    write(FRC_REG);
    write(Y_REG);
    write(ADRS_REG);

    write(MDEC_CT);

    write(m_readPending);
    write(m_readNOFL);
    write(m_readValue);

    write(m_writePending);
    write(m_writeValue);

    write(m_readWriteAddr);
}

void DSP::SaveState(state::SCSPDSP &state) const {
    for (size_t i = 0; i < program.size(); i++) {
        state.MPRO[i] = program[i].u64;
    }
    state.TEMP = tempMem;
    state.MEMS = soundMem;
    state.COEF = coeffs;
    state.MADRS = addrs;
    state.MIXS = mixStack;
    state.EFREG = effectOut;
    state.EXTS = audioInOut;

    state.MIXSGen = m_mixStackGen;
    state.MIXSNull = m_mixStackNull;

    state.RBP = ringBufferLeadAddress;
    state.RBL = ringBufferLength;

    state.PC = PC;
    state.INPUTS = INPUTS;

    state.SFT_REG = SFT_REG;
    state.FRC_REG = FRC_REG;
    state.Y_REG = Y_REG;
    state.ADRS_REG = ADRS_REG;

    state.MDEC_CT = MDEC_CT;

    state.readPending = m_readPending;
    state.readNOFL = m_readNOFL;
    state.readValue = m_readValue;

    state.writePending = m_writePending;
    state.writeValue = m_writeValue;

    state.readWriteAddr = m_readWriteAddr;
}

bool DSP::ValidateState(const state::SCSPDSP &state) const {
    return true;
}

void DSP::LoadState(const state::SCSPDSP &state) {
    m_programLength = 0;
    for (size_t i = 0; i < program.size(); i++) {
        program[i].u64 = state.MPRO[i];
        if (program[i].u64 != 0) {
            m_programLength = i + 1;
        }
    }
    if (m_programLength < program.size()) {
        ++m_programLength;
    }

    tempMem = state.TEMP;
    soundMem = state.MEMS;
    coeffs = state.COEF;
    addrs = state.MADRS;
    mixStack = state.MIXS;
    effectOut = state.EFREG;
    audioInOut = state.EXTS;

    m_mixStackGen = state.MIXSGen & 0x10;
    m_mixStackNull = state.MIXSNull;

    ringBufferLeadAddress = state.RBP;
    ringBufferLength = state.RBL;

    PC = state.PC;
    INPUTS = state.INPUTS;

    SFT_REG = state.SFT_REG;
    FRC_REG = state.FRC_REG;
    Y_REG = state.Y_REG;
    ADRS_REG = state.ADRS_REG;

    MDEC_CT = state.MDEC_CT;

    m_readPending = state.readPending;
    m_readNOFL = state.readNOFL;
    m_readValue = state.readValue;

    m_writePending = state.writePending;
    m_writeValue = state.writeValue;

    m_readWriteAddr = state.readWriteAddr;

    for (sint32 &value : tempMem) {
        value = bit::sign_extend<24>(value);
    }
    for (sint32 &value : soundMem) {
        value = bit::sign_extend<24>(value);
    }

    UpdateRBP();
    UpdateRBL();
}

} // namespace ymir::scsp
