#pragma once

#include <ymir/util/dev_log.hpp>

namespace ymir::cdblock::grp {

// -----------------------------------------------------------------------------
// Dev log groups

// Hierarchy:
//
// base
//   regs
//   cmd
//   play_init
//   play
//   xfer
//   part_mgr
//   ygr
//     ygr_regs
//     ygr_cr
//   lle
//     lle_cd
//       lle_cd_cmd
//       lle_cd_status

struct base {
    static constexpr bool enabled = true;
    static constexpr devlog::Level level = devlog::level::debug;
    static constexpr std::string_view name = "CDBlock";
};

struct regs : public base {
    static constexpr std::string_view name = "CDBlock-Regs";
};

struct cmd : public base {
    static constexpr std::string_view name = "CDBlock-Command";
};

struct play_init : public base {
    static constexpr std::string_view name = "CDBlock-PlayInit";
};

struct play : public base {
    static constexpr std::string_view name = "CDBlock-Play";
};

struct xfer : public base {
    static constexpr std::string_view name = "CDBlock-Transfer";
};

struct part_mgr : public base {
    static constexpr std::string_view name = "CDBlock-PartMgr";
};

struct ygr : public base {
    static constexpr std::string_view name = "CDBlock-YGR";
};

struct ygr_regs : public ygr {
    static constexpr std::string_view name = "CDBlock-YGR-Regs";
};

struct ygr_fifo : public ygr {
    static constexpr devlog::Level level = devlog::level::trace;
    static constexpr std::string_view name = "CDBlock-YGR-FIFO";
};

struct ygr_cr : public ygr {
    // static constexpr devlog::Level level = devlog::level::trace;
    static constexpr std::string_view name = "CDBlock-YGR-CR";
};

struct lle : public base {
    static constexpr std::string_view name = "CDB-LLE";
};

struct lle_cd : public lle {
    static constexpr std::string_view name = "CDB-LLE-CD";
};

struct lle_cd_cmd : public lle_cd {
    // static constexpr devlog::Level level = devlog::level::trace;
    static constexpr std::string_view name = "CDB-LLE-Command";
};

struct lle_cd_status : public lle_cd {
    // static constexpr devlog::Level level = devlog::level::trace;
    static constexpr std::string_view name = "CDB-LLE-Status";
};

} // namespace ymir::cdblock::grp
