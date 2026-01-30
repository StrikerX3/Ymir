#include "stdio_suppress.hpp"

#if defined(_WIN32)
    #include <io.h>

    #define dup _dup
    #define dup2 _dup2
    #define fileno _fileno
    #define close _close

constexpr auto kNullFile = "NUL";
#else
    #include <unistd.h>

constexpr auto kNullFile = "/dev/null";
#endif

#include <stdio.h>

namespace util {

StdioSuppressor::StdioSuppressor(FILE *file)
    : m_file(file) {

    fflush(file);
    m_fd_prev = dup(fileno(file));
#ifdef _WIN32
    freopen_s(&file, kNullFile, "w", file);
#else
    freopen(kNullFile, "w", file);
#endif
}

StdioSuppressor::~StdioSuppressor() {
    fflush(m_file);
    dup2(m_fd_prev, fileno(m_file));
    close(m_fd_prev);
}

} // namespace util
