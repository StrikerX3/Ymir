#include "os_exception_handler.hpp"

#include <ymir/version.hpp>

#include <SDL3/SDL_messagebox.h>

#include <fmt/format.h>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>

    #include <iterator>
    #include <set>
#elif defined(__linux__)
    #include <signal.h>

    #include <bits/types/siginfo_t.h>
#elif defined(__FreeBSD__)
    #include <iterator>
    #include <signal.h>
#elif defined(__APPLE__)
// TODO: exception handling
#endif

#include <ymir/core/types.hpp>

namespace util {

void ShowFatalErrorDialog(const char *msg) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", msg, nullptr);
}

static void ShowExceptionDialog(const char *msg) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Exception", msg, nullptr);
}

// -----------------------------------------------------------------------------
// Windows implementation

#ifdef _WIN32

static PVOID g_vehPtr = nullptr;
bool g_allExcpt = false;

static const std::set<DWORD> kFatalErrors = {
    STATUS_ACCESS_VIOLATION,       STATUS_NO_MEMORY,      STATUS_ILLEGAL_INSTRUCTION, STATUS_ARRAY_BOUNDS_EXCEEDED,
    STATUS_PRIVILEGED_INSTRUCTION, STATUS_STACK_OVERFLOW, STATUS_HEAP_CORRUPTION,     STATUS_STACK_BUFFER_OVERRUN,
    // STATUS_NONCONTINUABLE_EXCEPTION, // usually means a bug in an exception handler
    // STATUS_ASSERTION_FAILURE, // not always a problem
    // STATUS_ENCLAVE_VIOLATION, // not used by Ymir
};

static const std::set<DWORD> kAllowedExceptions = {
    // Old method of setting thread name
    0x406D1388,

    // Windows Runtime Originate Error, usually triggered by interactions with file dialogs
    0x40080201,

    // Let C++ exceptions go through. They are handled by try-catch blocks in many places.
    0xE06D7363,
    0xE04D5343,
};

void RegisterExceptionHandler(bool allExceptions) {
    g_allExcpt = allExceptions;
    g_vehPtr = AddVectoredExceptionHandler(1, [](_EXCEPTION_POINTERS *ExceptionInfo) -> LONG {
        if (kAllowedExceptions.contains(ExceptionInfo->ExceptionRecord->ExceptionCode)) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const bool isFatal = kFatalErrors.contains(ExceptionInfo->ExceptionRecord->ExceptionCode);
        if (!g_allExcpt && !isFatal) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        fmt::memory_buffer buf{};
        auto out = std::back_inserter(buf);

        if (isFatal) {
            fmt::format_to(out, "Ymir encountered a fatal error.\n\n");
        } else {
            fmt::format_to(out, "Ymir encountered an exception.\n\n");
        }
        fmt::format_to(out, "Ymir version " Ymir_FULL_VERSION "\n\n");

        DWORD threadId = GetCurrentThreadId();
        PWSTR threadDesc = nullptr;
        HRESULT descOK = GetThreadDescription(GetCurrentThread(), &threadDesc);
        fmt::format_to(out, "Exception code=0x{:X} address={} flags=0x{:X}\n",
                       ExceptionInfo->ExceptionRecord->ExceptionCode, ExceptionInfo->ExceptionRecord->ExceptionAddress,
                       ExceptionInfo->ExceptionRecord->ExceptionFlags, threadId);

        fmt::format_to(out, "Thread ID: 0x{:X}", threadId);
        if (SUCCEEDED(descOK)) {
            int bufferSize = WideCharToMultiByte(CP_UTF8, 0, threadDesc, -1, NULL, 0, NULL, NULL);
            if (bufferSize > 0) {
                std::string threadDescBuf(bufferSize, '\0');
                int result =
                    WideCharToMultiByte(CP_UTF8, 0, threadDesc, -1, threadDescBuf.data(), bufferSize, NULL, NULL);
                auto nullPos = threadDescBuf.find_first_of('\0');
                if (nullPos != std::string::npos) {
                    threadDescBuf.resize(nullPos);
                }
                if (result > 0) {
                    fmt::format_to(out, ", name: {}", threadDescBuf);
                }
            }
        }

        fmt::format_to(out, "\n\n");

        auto *cr = ExceptionInfo->ContextRecord;
        fmt::format_to(out, "Content information:\n");

    #if defined(_M_X64) || defined(__x86_64__)

        fmt::format_to(out, "RAX={:016X} RBX={:016X} RCX={:016X} RDX={:016X}\n", cr->Rax, cr->Rbx, cr->Rcx, cr->Rdx);
        fmt::format_to(out, "RSP={:016X} RBP={:016X} RSI={:016X} RDI={:016X}\n", cr->Rsp, cr->Rbp, cr->Rsi, cr->Rdi);
        fmt::format_to(out, "R8={:016X} R9={:016X} R10={:016X} R11={:016X}\n", cr->R8, cr->R9, cr->R10, cr->R11);
        fmt::format_to(out, "R12={:016X} R13={:016X} R14={:016X} R15={:016X}\n", cr->R12, cr->R13, cr->R14, cr->R15);
        fmt::format_to(out, "CS={:02X} DS={:02X} ES={:02X} FS={:02X} GS={:02X} SS={:02X}\n", cr->SegCs, cr->SegDs,
                       cr->SegEs, cr->SegFs, cr->SegGs, cr->SegSs);
        fmt::format_to(out, "RIP={:016X} EFlags={:08X} MXCSR={:08X} ContextFlags={:08X}", cr->Rip, cr->EFlags,
                       cr->MxCsr, cr->ContextFlags);

    #elif defined(_M_ARM64) || defined(__aarch64__)

        fmt::format_to(out, " X0={:016X}  X1={:016X}  X2={:016X}  X3={:016X}\n", cr->X0, cr->X1, cr->X2, cr->X3);
        fmt::format_to(out, " X4={:016X}  X5={:016X}  X6={:016X}  X7={:016X}\n", cr->X4, cr->X5, cr->X6, cr->X7);
        fmt::format_to(out, " X8={:016X}  X9={:016X} X10={:016X} X11={:016X}\n", cr->X8, cr->X9, cr->X10, cr->X11);
        fmt::format_to(out, "X12={:016X} X13={:016X} X14={:016X} X15={:016X}\n", cr->X12, cr->X13, cr->X14, cr->X15);
        fmt::format_to(out, "X16={:016X} X17={:016X} X18={:016X} X19={:016X}\n", cr->X16, cr->X17, cr->X18, cr->X19);
        fmt::format_to(out, "X20={:016X} X21={:016X} X22={:016X} X23={:016X}\n", cr->X20, cr->X21, cr->X22, cr->X23);
        fmt::format_to(out, "X24={:016X} X25={:016X} X26={:016X} X27={:016X}\n", cr->X24, cr->X25, cr->X26, cr->X27);
        fmt::format_to(out, "X28={:016X}  FP={:016X}  LR={:016X}  SP={:016X}\n", cr->X28, cr->Fp, cr->Lr, cr->Sp);
        fmt::format_to(out, "PC={:X} CPSR={:X}", cr->Pc, cr->Cpsr);

    #endif

        std::string errMsg = fmt::to_string(buf);

        if (isFatal) {
            ShowFatalErrorDialog(errMsg.c_str());
        } else {
            ShowExceptionDialog(errMsg.c_str());
        }

        return EXCEPTION_CONTINUE_SEARCH;
    });
}

/*void UnregisterExceptionHandler() {
    RemoveVectoredExceptionHandler(g_vehPtr);
    g_vehPtr = nullptr;
}*/

#elif defined(__linux__) || defined(__FreeBSD__)

// -----------------------------------------------------------------------------
// Linux and FreeBSD implementation

// static struct sigaction s_oldAction;

void RegisterExceptionHandler(bool allExceptions) {
    struct sigaction action;
    action.sa_sigaction = [](int sig, siginfo_t *info, void *ucontext) -> void {
        const auto addr = reinterpret_cast<uintptr_t>(info->si_addr);
        auto *context = static_cast<ucontext_t *>(ucontext);
        auto &mcontext = context->uc_mcontext;

        fmt::memory_buffer buf{};
        auto out = std::back_inserter(buf);

        fmt::format_to(out, "Ymir encountered a fatal error.\n\n");
        fmt::format_to(out, "Ymir version " Ymir_FULL_VERSION "\n\n");
        fmt::format_to(out, "signo=0x{:X} code=0x{:X} address=0x{:X}\n", info->si_signo, info->si_code, addr);

        // TODO: signal-specific info
        fmt::format_to(out, "\n");

        fmt::format_to(out, "Content information:\n");

    #if defined(__linux__)

        #if defined(_M_X64) || defined(__x86_64__)

        auto *gregs = mcontext.gregs;
        fmt::format_to(out, "RAX={:016X} RBX={:016X} RCX={:016X} RDX={:016X}\n", gregs[REG_RAX], gregs[REG_RBX],
                       gregs[REG_RCX], gregs[REG_RDX]);
        fmt::format_to(out, "RSP={:016X} RBP={:016X} RSI={:016X} RDI={:016X}\n", gregs[REG_RSP], gregs[REG_RBP],
                       gregs[REG_RSI], gregs[REG_RDI]);
        fmt::format_to(out, "R8={:016X} R9={:016X} R10={:016X} R11={:016X}\n", gregs[REG_R8], gregs[REG_R9],
                       gregs[REG_R10], gregs[REG_R11]);
        fmt::format_to(out, "R12={:016X} R13={:016X} R14={:016X} R15={:016X}\n", gregs[REG_R12], gregs[REG_R13],
                       gregs[REG_R14], gregs[REG_R15]);
        fmt::format_to(out, "CSFSGS={:016X} RIP={:016X} EFlags={:08X}", gregs[REG_CSGSFS], gregs[REG_RIP],
                       gregs[REG_EFL]);

        #elif defined(_M_ARM64) || defined(__aarch64__)

        auto *regs = mcontext.regs;
        fmt::format_to(out, "R0={:016X} R1={:016X} R2={:016X} R3={:016X}\n", regs[0], regs[1], regs[2], regs[3]);
        fmt::format_to(out, "R4={:016X} R5={:016X} R6={:016X} R7={:016X}\n", regs[4], regs[5], regs[6], regs[7]);
        fmt::format_to(out, "R8={:016X} R9={:016X} R10={:016X} R11={:016X}\n", regs[8], regs[9], regs[10], regs[11]);
        fmt::format_to(out, "R12={:016X} R13={:016X} R14={:016X} R15={:016X}\n", regs[12], regs[13], regs[14],
                       regs[15]);
        fmt::format_to(out, "R16={:016X} R17={:016X} R18={:016X} R19={:016X}\n", regs[16], regs[17], regs[18],
                       regs[19]);
        fmt::format_to(out, "R20={:016X} R21={:016X} R22={:016X} R23={:016X}\n", regs[20], regs[21], regs[22],
                       regs[23]);
        fmt::format_to(out, "R24={:016X} R25={:016X} R26={:016X} R27={:016X}\n", regs[24], regs[25], regs[26],
                       regs[27]);
        fmt::format_to(out, "R28={:016X} R29={:016X} R30={:016X}\n", regs[28], regs[29], regs[30]);
        fmt::format_to(out, "SP={:X} PC={:X} pstate={:X}", mcontext.sp, mcontext.pc, mcontext.pstate);

        #endif

    #else // __FreeBSD__

        #if defined(_M_X64) || defined(__x86_64__)

        fmt::format_to(out, "RAX={:016X} RBX={:016X} RCX={:016X} RDX={:016X}\n", mcontext.mc_rax, mcontext.mc_rbx,
                       mcontext.mc_rcx, mcontext.mc_rdx);
        fmt::format_to(out, "RSP={:016X} RBP={:016X} RSI={:016X} RDI={:016X}\n", mcontext.mc_rsp, mcontext.mc_rbp,
                       mcontext.mc_rsi, mcontext.mc_rdi);
        fmt::format_to(out, "R8={:016X} R9={:016X} R10={:016X} R11={:016X}\n", mcontext.mc_r8, mcontext.mc_r9,
                       mcontext.mc_r10, mcontext.mc_r11);
        fmt::format_to(out, "R12={:016X} R13={:016X} R14={:016X} R15={:016X}\n", mcontext.mc_r12, mcontext.mc_r13,
                       mcontext.mc_r14, mcontext.mc_r15);
        fmt::format_to(out, "CS={:02X} DS={:02X} ES={:02X} FS={:02X} GS={:02X} SS={:02X}\n", mcontext.mc_cs,
                       mcontext.mc_ds, mcontext.mc_es, mcontext.mc_fs, mcontext.mc_gs, mcontext.mc_ss);
        fmt::format_to(out, "RIP={:016X} RFlags={:016X}", mcontext.mc_rip, mcontext.mc_rflags);

        #elif defined(_M_ARM64) || defined(__aarch64__)

                // TODO: implement

        #endif

    #endif

        std::string errMsg = fmt::to_string(buf);

        ShowFatalErrorDialog(errMsg.c_str());

        /*if (s_oldAction.sa_handler) {
            s_oldAction.sa_handler(sig);
        } else if (s_oldAction.sa_sigaction) {
            s_oldAction.sa_sigaction(sig, info, ucontext);
        }*/
        abort();
    };
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGILL, &action, nullptr);
    // sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
}

#elif defined(__APPLE__)

// -----------------------------------------------------------------------------
// macOS implementation

void RegisterExceptionHandler(bool allExceptions) {
    // TODO: implement
}

#endif

} // namespace util
