#pragma once

#include <stdio.h>

namespace util {

/// @brief Base class of stdio suppressors.
class StdioSuppressor {
public:
    virtual ~StdioSuppressor();

protected:
    StdioSuppressor(FILE *file);

private:
    FILE *const m_file;
    int m_fd_prev;
};

/// @brief Temporarily suppresses stdout for the lifetime of the object.
class StdOutSuppressor final : public StdioSuppressor {
public:
    StdOutSuppressor()
        : StdioSuppressor(stdout) {}
};

/// @brief Temporarily suppresses stderr for the lifetime of the object.
class StdErrSuppressor final : public StdioSuppressor {
public:
    StdErrSuppressor()
        : StdioSuppressor(stderr) {}
};

} // namespace util
