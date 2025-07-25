## Create the library target
add_library(ymir-core
    include/ymir/ymir.hpp
    include/ymir/version.hpp

    include/ymir/core/configuration.hpp
    include/ymir/core/configuration_defs.hpp
    include/ymir/core/hash.hpp
    include/ymir/core/scheduler.hpp
    include/ymir/core/scheduler_defs.hpp
    include/ymir/core/types.hpp

    include/ymir/db/game_db.hpp
    include/ymir/db/ipl_db.hpp
    include/ymir/db/rom_cart_db.hpp

    include/ymir/debug/cdblock_tracer_base.hpp
    include/ymir/debug/scu_tracer_base.hpp
    include/ymir/debug/sh2_tracer_base.hpp

    include/ymir/hw/hw_defs.hpp

    include/ymir/hw/cart/cart_base.hpp
    include/ymir/hw/cart/cart_impl_bup.hpp
    include/ymir/hw/cart/cart_impl_dram.hpp
    include/ymir/hw/cart/cart_impl_none.hpp
    include/ymir/hw/cart/cart_impl_rom.hpp
    include/ymir/hw/cart/cart_slot.hpp

    include/ymir/hw/cdblock/cdblock.hpp
    include/ymir/hw/cdblock/cdblock_buffer.hpp
    include/ymir/hw/cdblock/cdblock_defs.hpp
    include/ymir/hw/cdblock/cdblock_filter.hpp
    include/ymir/hw/cdblock/cdblock_internal_callbacks.hpp

    include/ymir/hw/m68k/m68k.hpp
    include/ymir/hw/m68k/m68k_decode.hpp
    include/ymir/hw/m68k/m68k_defs.hpp
    include/ymir/hw/m68k/m68k_disasm.hpp

    include/ymir/hw/scsp/scsp.hpp
    include/ymir/hw/scsp/scsp_callbacks.hpp
    include/ymir/hw/scsp/scsp_defs.hpp
	include/ymir/hw/scsp/scsp_midi_defs.hpp
    include/ymir/hw/scsp/scsp_dsp.hpp
    include/ymir/hw/scsp/scsp_dsp_instr.hpp
    include/ymir/hw/scsp/scsp_internal_callbacks.hpp
    include/ymir/hw/scsp/scsp_slot.hpp
    include/ymir/hw/scsp/scsp_timer.hpp

    include/ymir/hw/scu/scu.hpp
    include/ymir/hw/scu/scu_defs.hpp
    include/ymir/hw/scu/scu_dma.hpp
    include/ymir/hw/scu/scu_dsp.hpp
    include/ymir/hw/scu/scu_dsp_instr.hpp
    include/ymir/hw/scu/scu_dsp_disasm.hpp
    include/ymir/hw/scu/scu_internal_callbacks.hpp

    include/ymir/hw/sh2/sh2.hpp
    include/ymir/hw/sh2/sh2_bsc.hpp
    include/ymir/hw/sh2/sh2_cache.hpp
    include/ymir/hw/sh2/sh2_decode.hpp
    include/ymir/hw/sh2/sh2_disasm.hpp
    include/ymir/hw/sh2/sh2_divu.hpp
    include/ymir/hw/sh2/sh2_dmac.hpp
    include/ymir/hw/sh2/sh2_excpt.hpp
    include/ymir/hw/sh2/sh2_frt.hpp
    include/ymir/hw/sh2/sh2_intc.hpp
    include/ymir/hw/sh2/sh2_internal_callbacks.hpp
    include/ymir/hw/sh2/sh2_power.hpp
    include/ymir/hw/sh2/sh2_regs.hpp
    include/ymir/hw/sh2/sh2_sci.hpp
    include/ymir/hw/sh2/sh2_ubc.hpp
    include/ymir/hw/sh2/sh2_wdt.hpp

    include/ymir/hw/smpc/rtc.hpp
    include/ymir/hw/smpc/smpc.hpp
    include/ymir/hw/smpc/smpc_internal_callbacks.hpp
    
    include/ymir/hw/smpc/peripheral/peripheral_base.hpp
    include/ymir/hw/smpc/peripheral/peripheral_callbacks.hpp
    include/ymir/hw/smpc/peripheral/peripheral_defs.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_analog_pad.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_arcade_racer.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_control_pad.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_mission_stick.hpp
    include/ymir/hw/smpc/peripheral/peripheral_impl_null.hpp
    include/ymir/hw/smpc/peripheral/peripheral_port.hpp
    include/ymir/hw/smpc/peripheral/peripheral_report.hpp
    include/ymir/hw/smpc/peripheral/peripheral_state_common.hpp

    include/ymir/hw/vdp/vdp.hpp
    include/ymir/hw/vdp/vdp_callbacks.hpp
    include/ymir/hw/vdp/vdp_defs.hpp
    include/ymir/hw/vdp/vdp_internal_callbacks.hpp
    include/ymir/hw/vdp/vdp_state.hpp
    include/ymir/hw/vdp/vdp1_defs.hpp
    include/ymir/hw/vdp/vdp1_regs.hpp
    include/ymir/hw/vdp/vdp2_defs.hpp
    include/ymir/hw/vdp/vdp2_regs.hpp

    include/ymir/media/disc.hpp
    include/ymir/media/filesystem.hpp
    include/ymir/media/frame_address.hpp
    include/ymir/media/iso9660.hpp
    include/ymir/media/media_defs.hpp
    include/ymir/media/saturn_header.hpp
    include/ymir/media/subheader.hpp
    
    include/ymir/media/loader/loader.hpp
    include/ymir/media/loader/loader_bin_cue.hpp
    include/ymir/media/loader/loader_chd.hpp
    include/ymir/media/loader/loader_img_ccd_sub.hpp
    include/ymir/media/loader/loader_iso.hpp
    include/ymir/media/loader/loader_mdf_mds.hpp

    include/ymir/media/binary_reader/binary_reader.hpp
    include/ymir/media/binary_reader/binary_reader_file.hpp
    include/ymir/media/binary_reader/binary_reader_impl.hpp
    include/ymir/media/binary_reader/binary_reader_mem.hpp
    include/ymir/media/binary_reader/binary_reader_mmap.hpp
    include/ymir/media/binary_reader/binary_reader_subview.hpp

    include/ymir/state/state.hpp
    include/ymir/state/state_cdblock.hpp
    include/ymir/state/state_m68k.hpp
    include/ymir/state/state_scheduler.hpp
    include/ymir/state/state_scsp.hpp
    include/ymir/state/state_scsp_dsp.hpp
    include/ymir/state/state_scsp_slot.hpp
    include/ymir/state/state_scsp_timer.hpp
    include/ymir/state/state_scu.hpp
    include/ymir/state/state_sh2.hpp
    include/ymir/state/state_smpc.hpp
    include/ymir/state/state_system.hpp
    include/ymir/state/state_vdp.hpp

    include/ymir/sys/backup_ram.hpp
    include/ymir/sys/backup_ram_defs.hpp
    include/ymir/sys/bus.hpp
    include/ymir/sys/clocks.hpp
    include/ymir/sys/memory.hpp
    include/ymir/sys/memory_defs.hpp
    include/ymir/sys/saturn.hpp
    include/ymir/sys/system.hpp
    include/ymir/sys/system_features.hpp
    include/ymir/sys/system_internal_callbacks.hpp
    
    include/ymir/util/arith_ops.hpp
    include/ymir/util/backup_datetime.hpp
    include/ymir/util/bit_ops.hpp
    include/ymir/util/bitmask_enum.hpp
    include/ymir/util/callback.hpp
    include/ymir/util/compiler_info.hpp
    include/ymir/util/constexpr_for.hpp
    include/ymir/util/data_ops.hpp
    include/ymir/util/date_time.hpp
    include/ymir/util/dev_assert.hpp
    include/ymir/util/dev_log.hpp
    include/ymir/util/event.hpp
    include/ymir/util/function_info.hpp
    include/ymir/util/inline.hpp
    include/ymir/util/process.hpp
    include/ymir/util/scope_guard.hpp
    include/ymir/util/size_ops.hpp
    include/ymir/util/thread_name.hpp
    include/ymir/util/type_traits_ex.hpp
    include/ymir/util/unreachable.hpp


    src/ymir/ymir.cpp

    src/ymir/core/configuration.cpp
    src/ymir/core/hash.cpp

    src/ymir/db/game_db.cpp
    src/ymir/db/ipl_db.cpp
    src/ymir/db/rom_cart_db.cpp

    src/ymir/hw/cart/cart_impl_bup.cpp
    src/ymir/hw/cart/cart_slot.cpp

    src/ymir/hw/cdblock/cdblock.cpp
    src/ymir/hw/cdblock/cdblock_devlog.hpp
    src/ymir/hw/cdblock/cdblock_partition_manager.cpp
    
    src/ymir/hw/m68k/m68k.cpp
    src/ymir/hw/m68k/m68k_addr_modes.hpp
    src/ymir/hw/m68k/m68k_decode.cpp
    src/ymir/hw/m68k/m68k_disasm.cpp
    
    src/ymir/hw/scsp/scsp.cpp
    src/ymir/hw/scsp/scsp_dsp.cpp
    src/ymir/hw/scsp/scsp_slot.cpp
    
    src/ymir/hw/scu/scu.cpp
    src/ymir/hw/scu/scu_devlog.hpp
    src/ymir/hw/scu/scu_dsp.cpp
    src/ymir/hw/scu/scu_dsp_disasm.cpp

    src/ymir/hw/smpc/rtc.cpp
    src/ymir/hw/smpc/smpc.cpp

    src/ymir/hw/smpc/peripheral/peripheral_impl_analog_pad.cpp
    src/ymir/hw/smpc/peripheral/peripheral_impl_arcade_racer.cpp
    src/ymir/hw/smpc/peripheral/peripheral_impl_control_pad.cpp
    src/ymir/hw/smpc/peripheral/peripheral_impl_mission_stick.cpp

    src/ymir/hw/sh2/sh2.cpp
    
    src/ymir/hw/sh2/sh2_decode.cpp
    src/ymir/hw/sh2/sh2_disasm.cpp

    src/ymir/hw/vdp/slope.hpp
    src/ymir/hw/vdp/vdp.cpp

    src/ymir/media/filesystem.cpp
    src/ymir/media/saturn_header.cpp

    src/ymir/media/loader/loader.cpp
    src/ymir/media/loader/loader_bin_cue.cpp
    src/ymir/media/loader/loader_chd.cpp
    src/ymir/media/loader/loader_img_ccd_sub.cpp
    src/ymir/media/loader/loader_iso.cpp
    src/ymir/media/loader/loader_mdf_mds.cpp

    src/ymir/sys/backup_ram.cpp
    src/ymir/sys/memory.cpp
    src/ymir/sys/null_ipl.hpp
    src/ymir/sys/saturn.cpp

    src/ymir/util/backup_datetime.cpp
    src/ymir/util/date_time.cpp
    src/ymir/util/event.cpp
    src/ymir/util/process.cpp
)
add_library(ymir::ymir-core ALIAS ymir-core)
set_target_properties(ymir-core PROPERTIES
                      VERSION ${Ymir_core_VERSION}
                      SOVERSION ${Ymir_core_VERSION_MAJOR})
target_include_directories(ymir-core
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_compile_features(ymir-core PUBLIC cxx_std_20)

## Add dependencies
target_link_libraries(ymir-core PUBLIC
    fmt
    mio
    concurrentqueue
    xxHash::xxHash
    chdr-static
)
if (WIN32)
    ## synchronization.lib required for WaitOnAddress and WakeByAddressAll
    target_link_libraries(ymir-core PUBLIC
        synchronization
    )
endif ()


# Build precompiled header
target_precompile_headers(ymir-core
    PUBLIC
        <algorithm>
        <array>
        <atomic>
        <bit>
        <cassert>
        <charconv>
        <cinttypes>
        <climits>
        <cmath>
        <concepts>
        <condition_variable>
        <cstddef>
        <cstdint>
        <cstdlib>
        <deque>
        <filesystem>
        <fstream>
        <functional>
        <initializer_list>
        <iosfwd>
        <iostream>
        <limits>
        <memory>
        <mutex>
        <new>
        <numeric>
        <optional>
        <queue>
        <set>
        <span>
        <sstream>
        <string>
        <thread>
        <type_traits>
        <unordered_map>
        <unordered_set>
        <utility>
        <variant>
        <vector>

        <fmt/format.h>
        <fmt/std.h>
        <fmt/xchar.h>

        <mio/mmap.hpp>
)

## Define version macros
if (DEFINED Ymir_VERSION_PRERELEASE AND NOT Ymir_VERSION_PRERELEASE STREQUAL "")
    set(Ymir_VERSION "${Ymir_VERSION}-${Ymir_VERSION_PRERELEASE}")
endif ()
if (DEFINED Ymir_VERSION_BUILD AND NOT Ymir_VERSION_BUILD STREQUAL "")
    set(Ymir_VERSION "${Ymir_VERSION}+${Ymir_VERSION_BUILD}")
endif ()
message(STATUS "Ymir version string: ${Ymir_VERSION}")
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION="${Ymir_VERSION}")
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_MAJOR=${Ymir_VERSION_MAJOR})
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_MINOR=${Ymir_VERSION_MINOR})
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_PATCH=${Ymir_VERSION_PATCH})
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_PRERELEASE="${Ymir_VERSION_PRERELEASE}")
target_compile_definitions(ymir-core PUBLIC Ymir_VERSION_BUILD="${Ymir_VERSION_BUILD}")

## Define additional macros
target_compile_definitions(ymir-core PUBLIC "Ymir_ENABLE_DEVLOG=$<BOOL:${Ymir_ENABLE_DEVLOG}>")
target_compile_definitions(ymir-core PUBLIC "Ymir_DEV_ASSERTIONS=$<BOOL:${Ymir_ENABLE_DEV_ASSERTIONS}>")
target_compile_definitions(ymir-core PUBLIC "Ymir_DEV_BUILD=$<BOOL:${Ymir_DEV_BUILD}>")
target_compile_definitions(ymir-core PUBLIC "Ymir_EXTRA_INLINING=$<BOOL:${Ymir_EXTRA_INLINING}>")
target_compile_definitions(ymir-core PUBLIC "TOML_EXCEPTIONS=0")

## Generate the export header and attach it to the target
include(GenerateExportHeader)
generate_export_header(ymir-core EXPORT_FILE_NAME include/ymir/export.h)
target_compile_definitions(ymir-core PUBLIC "$<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:Ymir_core_STATIC_DEFINE>")
target_include_directories(ymir-core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-core")
    set_property(TARGET ymir-core PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## Apply performance options
if (Ymir_AVX2)
    target_compile_definitions(ymir-core PUBLIC ${PROJECT_NAME}_AVX2=1)
    if (MSVC)
        target_compile_options(ymir-core PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymir-core PUBLIC "-mavx2")
        target_compile_options(ymir-core PUBLIC "-mfma")
        target_compile_options(ymir-core PUBLIC "-mbmi")
    endif ()
endif ()

## Enable unity build
#set_target_properties(ymir-core PROPERTIES UNITY_BUILD ON)
#set_target_properties(ymir-core PROPERTIES UNITY_BUILD_BATCH_SIZE 0)

## Configure Visual Studio filters
if (MSVC)
    vs_set_filters(TARGET ymir-core)
    set_target_properties(ymir-core PROPERTIES FOLDER "Ymir")
endif ()
