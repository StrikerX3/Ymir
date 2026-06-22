#pragma once

#include <ymir/hw/vdp/vdp2_defs.hpp>

#include <fmt/format.h>

namespace app::ui {

struct WindowSetPrinter {
    WindowSetPrinter(ymir::vdp::WindowLogic logic)
        : m_logic(logic) {}

    void AppendWindow(const char *name, bool enabled, bool inverted) {
        auto out = std::back_inserter(m_buf);
        if (!enabled) {
            return;
        }
        if (m_hasAnyWindow) {
            if (m_logic == ymir::vdp::WindowLogic::And) {
                fmt::format_to(out, " & ");
            } else {
                fmt::format_to(out, " | ");
            }
        }
        if (inverted) {
            fmt::format_to(out, "~");
        }
        fmt::format_to(out, "{}", name);
        m_hasAnyWindow = true;
    }

    std::string ToString() const {
        return m_hasAnyWindow ? fmt::to_string(m_buf) : "-";
    }

private:
    const ymir::vdp::WindowLogic m_logic;
    bool m_hasAnyWindow = false;
    fmt::memory_buffer m_buf{};
};

} // namespace app::ui
