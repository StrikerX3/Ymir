/**
 * Copyright L. Spiro 2025
 *
 * Written by: Shawn (L. Spiro) Wilcoxen
 *
 * Description: Functions for disabling/enabling floating-point denormals.
 */

#pragma once

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM)
    #include <cstdint>
#endif

#if defined(_MSC_VER)
    #include <float.h>
#endif

#if defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
    #include <immintrin.h>
    #include <xmmintrin.h>
#endif

namespace lsn {

/**
 * Returns true if this build target supports explicit subnormal control.
 */
constexpr bool HasNoSubnormalsControl() {
#if defined(__aarch64__) || defined(_M_ARM64) || defined(__arm__) || defined(_M_ARM) || defined(__SSE__) || \
    defined(__x86_64__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
    return true;
#else
    return false;
#endif
}

#if defined(__aarch64__) || defined(_M_ARM64)
static inline std::uint64_t GetFPCR() {
    #if __has_builtin(__builtin_aarch64_get_fpcr)
    return __builtin_aarch64_get_fpcr();
    #elif defined(_MSC_VER)
    return _ReadStatusReg(ARM64_SYSREG(0b11, 0b011, 0b0100, 0b100, 0b000));
    #else
    std::uint64_t ullFpcr;
    __asm__ __volatile__("MRS %[fpcr], fpcr" : [fpcr] "=r"(ullFpcr));
    return ullFpcr;
    #endif
}

static inline void SetFPCR(std::uint64_t ullFpcr) {
    #if __has_builtin(__builtin_aarch64_set_fpcr)
    __builtin_aarch64_set_fpcr(ullFpcr);
    #elif defined(_MSC_VER)
    _WriteStatusReg(ARM64_SYSREG(0b11, 0b011, 0b0100, 0b100, 0b000), ullFpcr);
    #else
    __asm__ __volatile__("MSR fpcr, %[fpcr]" : : [fpcr] "r"(ullFpcr));
    #endif
}
#elif defined(__arm__) || defined(_M_ARM)
static inline std::uint32_t GetFPSCR() {
    #if __has_builtin(__builtin_arm_get_fpscr) && __has_builtin(__builtin_arm_set_fpscr)
    return __builtin_arm_get_fpscr();
    #else
    std::uint32_t uiFpscr;
    __asm__ __volatile__("VMRS %[fpscr], fpscr" : [fpscr] "=r"(uiFpscr));
    return uiFpscr;
    #endif
}

static inline void SetFPSCR(std::uint32_t uiFpscr) {
    #if __has_builtin(__builtin_arm_get_fpscr) && __has_builtin(__builtin_arm_set_fpscr)
    __builtin_arm_set_fpscr(uiFpscr);
    #else
    __asm__ __volatile__("VMSR fpscr, %[fpscr]" : : [fpscr] "r"(uiFpscr));
    #endif
}
#endif

/**
 * Enables "no subnormals" for the current thread.
 * x86/x64 -> MXCSR: set FTZ (bit 15) and DAZ (bit 6).
 * ARM/AArch64 -> FPCR/FPSCR: set FZ (bit 24) and (where available) FZ16 (bit 19).
 */
static inline void EnableNoSubnormals() {
#if defined(__aarch64__) || defined(_M_ARM64)
    std::uint64_t ullFpcr = GetFPCR();
    ullFpcr |= (1ULL << 24) | (1ULL << 19);
    SetFPCR(ullFpcr);
#elif defined(__arm__) || defined(_M_ARM)
    std::uint32_t uiFpscr = GetFPSCR();
    uiFpscr |= (1U << 24);
    SetFPSCR(uiFpscr);
#elif defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
    unsigned int uiMxcsr = _mm_getcsr();
    uiMxcsr |= 0x8000U; // FTZ.
    uiMxcsr |= 0x0040U; // DAZ.
    _mm_setcsr(uiMxcsr);
#else
    // No-op.
#endif
}

/**
 * Disables "no subnormals" for the current thread (restores gradual underflow).
 */
static inline void DisableNoSubnormals() {
#if defined(__aarch64__) || defined(_M_ARM64)
    std::uint64_t ullFpcr = GetFPCR();
    ullFpcr &= ~((1ULL << 24) | (1ULL << 19));
    SetFPCR(ullFpcr);
#elif defined(__arm__) || defined(_M_ARM)
    std::uint32_t uiFpscr = GetFPSCR();
    uiFpscr &= ~(1U << 24);
    SetFPSCR(uiFpscr);
#elif defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
    unsigned int uiMxcsr = _mm_getcsr();
    uiMxcsr &= ~0x8000U; // Clear FTZ.
    uiMxcsr &= ~0x0040U; // Clear DAZ.
    _mm_setcsr(uiMxcsr);
#else
    // No-op.
#endif
}

/**
 * Optional (Windows): also request CRT/x87 flush-to-zero for legacy code paths.
 * On x64 MSVC, SSE/AVX controls above are authoritative.
 */
static inline void EnableNoSubnormalsX87() {
#if defined(_MSC_VER)
    unsigned int uiOld{};
    _controlfp_s(&uiOld, _DN_FLUSH, _MCW_DN);
#endif
}

/**
 * RAII scope that enables "no subnormals" for the current thread and restores
 * the prior mode on destruction.
 */
class CScopedNoSubnormals {
public:
    /**
     * Captures the current control register and enables "no subnormals".
     */
    CScopedNoSubnormals() {
#if defined(__aarch64__) || defined(_M_ARM64)
        m_ullSaved = GetFPCR();
        EnableNoSubnormals();
#elif defined(__arm__) || defined(_M_ARM)
        m_uiSaved = GetFPSCR();
        EnableNoSubnormals();
#elif defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
        m_uiSaved = _mm_getcsr();
        EnableNoSubnormals();
#else
        // Nothing to save.
#endif
    }

    /**
     * Restores the previously captured control register.
     */
    ~CScopedNoSubnormals() {
#if defined(__aarch64__) || defined(_M_ARM64)
        SetFPCR(m_ullSaved);
#elif defined(__arm__) || defined(_M_ARM)
        SetFPSCR(m_uiSaved);
#elif defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
        _mm_setcsr(m_uiSaved);
#else
        // Nothing to restore.
#endif
    }

    CScopedNoSubnormals(const CScopedNoSubnormals &_sOther) = delete;
    CScopedNoSubnormals &operator=(const CScopedNoSubnormals &_sOther) = delete;

private:
#if defined(__aarch64__) || defined(_M_ARM64)
    std::uint64_t m_ullSaved{};
#elif defined(__arm__) || defined(_M_ARM)
    std::uint32_t m_uiSaved{};
#elif defined(__SSE__) || defined(__x86_64__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
    unsigned int m_uiSaved{};
#endif
};

} // namespace lsn
