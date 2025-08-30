## Create the executable target
add_executable(ymir-sdl3
    src/main.cpp
    src/winmain.cpp

    src/app/app.cpp
    src/app/app.hpp
    src/app/audio_system.cpp
    src/app/audio_system.hpp
    src/app/cmdline_opts.hpp
    src/app/message.hpp
    src/app/profile.cpp
    src/app/profile.hpp
    src/app/rewind_buffer.cpp
    src/app/rewind_buffer.hpp
    src/app/rom_manager.cpp
    src/app/rom_manager.hpp
    src/app/settings.cpp
    src/app/settings.hpp
    src/app/shared_context.cpp
    src/app/shared_context.hpp
    src/app/stb_implementations.cpp

    src/app/debug/cdblock_tracer.cpp
    src/app/debug/cdblock_tracer.hpp
    src/app/debug/scsp_tracer.cpp
    src/app/debug/scsp_tracer.hpp
    src/app/debug/scu_tracer.cpp
    src/app/debug/scu_tracer.hpp
    src/app/debug/sh2_tracer.cpp
    src/app/debug/sh2_tracer.hpp
    
    src/app/events/emu_debug_event_factory.cpp
    src/app/events/emu_debug_event_factory.hpp
    src/app/events/emu_event.hpp
    src/app/events/emu_event_factory.cpp
    src/app/events/emu_event_factory.hpp
    src/app/events/gui_event.hpp
    src/app/events/gui_event_factory.hpp

    src/app/input/input_action.hpp
    src/app/input/input_backend_sdl3.cpp
    src/app/input/input_backend_sdl3.hpp
    src/app/input/input_bind.hpp
    src/app/input/input_context.cpp
    src/app/input/input_context.hpp
    src/app/input/input_events.cpp
    src/app/input/input_events.hpp
    src/app/input/input_primitives.cpp
    src/app/input/input_primitives.hpp
    src/app/input/input_utils.hpp
    
    src/app/ui/window_base.cpp
    src/app/ui/window_base.hpp

    src/app/ui/fonts/IconsMaterialSymbols.h

    src/app/ui/state/debug/memory_viewer_state.hpp

    src/app/ui/views/backup_memory_view.cpp
    src/app/ui/views/backup_memory_view.hpp

    src/app/ui/views/debug/cdblock_cmd_trace_view.cpp
    src/app/ui/views/debug/cdblock_cmd_trace_view.hpp
    src/app/ui/views/debug/cdblock_filters_view.cpp
    src/app/ui/views/debug/cdblock_filters_view.hpp
    src/app/ui/views/debug/debug_output_view.cpp
    src/app/ui/views/debug/debug_output_view.hpp
    src/app/ui/views/debug/scsp_kyonex_trace_view.cpp
    src/app/ui/views/debug/scsp_kyonex_trace_view.hpp
    src/app/ui/views/debug/scsp_output_view.cpp
    src/app/ui/views/debug/scsp_output_view.hpp
    src/app/ui/views/debug/scsp_slots_view.cpp
    src/app/ui/views/debug/scsp_slots_view.hpp
    src/app/ui/views/debug/scu_dma_registers_view.cpp
    src/app/ui/views/debug/scu_dma_registers_view.hpp
    src/app/ui/views/debug/scu_dma_state_view.cpp
    src/app/ui/views/debug/scu_dma_state_view.hpp
    src/app/ui/views/debug/scu_dma_trace_view.cpp
    src/app/ui/views/debug/scu_dma_trace_view.hpp
    src/app/ui/views/debug/scu_dsp_data_ram_view.cpp
    src/app/ui/views/debug/scu_dsp_data_ram_view.hpp
    src/app/ui/views/debug/scu_dsp_disassembly_view.cpp
    src/app/ui/views/debug/scu_dsp_disassembly_view.hpp
    src/app/ui/views/debug/scu_dsp_dma_registers_view.cpp
    src/app/ui/views/debug/scu_dsp_dma_registers_view.hpp
    src/app/ui/views/debug/scu_dsp_dma_trace_view.cpp
    src/app/ui/views/debug/scu_dsp_dma_trace_view.hpp
    src/app/ui/views/debug/scu_dsp_registers_view.cpp
    src/app/ui/views/debug/scu_dsp_registers_view.hpp
    src/app/ui/views/debug/scu_interrupt_trace_view.cpp
    src/app/ui/views/debug/scu_interrupt_trace_view.hpp
    src/app/ui/views/debug/scu_interrupts_view.cpp
    src/app/ui/views/debug/scu_interrupts_view.hpp
    src/app/ui/views/debug/scu_registers_view.cpp
    src/app/ui/views/debug/scu_registers_view.hpp
    src/app/ui/views/debug/scu_timers_view.cpp
    src/app/ui/views/debug/scu_timers_view.hpp
    src/app/ui/views/debug/sh2_breakpoints_view.cpp
    src/app/ui/views/debug/sh2_breakpoints_view.hpp
    src/app/ui/views/debug/sh2_cache_entries_view.cpp
    src/app/ui/views/debug/sh2_cache_entries_view.hpp
    src/app/ui/views/debug/sh2_cache_register_view.cpp
    src/app/ui/views/debug/sh2_cache_register_view.hpp
    src/app/ui/views/debug/sh2_debug_toolbar_view.cpp
    src/app/ui/views/debug/sh2_debug_toolbar_view.hpp
    src/app/ui/views/debug/sh2_disassembly_view.cpp
    src/app/ui/views/debug/sh2_disassembly_view.hpp
    src/app/ui/views/debug/sh2_divu_registers_view.cpp
    src/app/ui/views/debug/sh2_divu_registers_view.hpp
    src/app/ui/views/debug/sh2_divu_statistics_view.cpp
    src/app/ui/views/debug/sh2_divu_statistics_view.hpp
    src/app/ui/views/debug/sh2_divu_trace_view.cpp
    src/app/ui/views/debug/sh2_divu_trace_view.hpp
    src/app/ui/views/debug/sh2_dmac_channel_view.cpp
    src/app/ui/views/debug/sh2_dmac_channel_view.hpp
    src/app/ui/views/debug/sh2_dmac_registers_view.cpp
    src/app/ui/views/debug/sh2_dmac_registers_view.hpp
    src/app/ui/views/debug/sh2_dmac_trace_view.cpp
    src/app/ui/views/debug/sh2_dmac_trace_view.hpp
    src/app/ui/views/debug/sh2_exception_vectors_view.cpp
    src/app/ui/views/debug/sh2_exception_vectors_view.hpp
    src/app/ui/views/debug/sh2_frt_view.cpp
    src/app/ui/views/debug/sh2_frt_view.hpp
    src/app/ui/views/debug/sh2_interrupt_trace_view.cpp
    src/app/ui/views/debug/sh2_interrupt_trace_view.hpp
    src/app/ui/views/debug/sh2_interrupts_view.cpp
    src/app/ui/views/debug/sh2_interrupts_view.hpp
    src/app/ui/views/debug/sh2_power_view.cpp
    src/app/ui/views/debug/sh2_power_view.hpp
    src/app/ui/views/debug/sh2_registers_view.cpp
    src/app/ui/views/debug/sh2_registers_view.hpp
    src/app/ui/views/debug/sh2_watchpoints_view.cpp
    src/app/ui/views/debug/sh2_watchpoints_view.hpp
    src/app/ui/views/debug/sh2_wdt_view.cpp
    src/app/ui/views/debug/sh2_wdt_view.hpp
    src/app/ui/views/debug/vdp1_registers_view.cpp
    src/app/ui/views/debug/vdp1_registers_view.hpp
    src/app/ui/views/debug/vdp2_layer_params_view.cpp
    src/app/ui/views/debug/vdp2_layer_params_view.hpp
    src/app/ui/views/debug/vdp2_layer_visibility_view.cpp
    src/app/ui/views/debug/vdp2_layer_visibility_view.hpp
    src/app/ui/views/debug/vdp2_vram_delay_view.cpp
    src/app/ui/views/debug/vdp2_vram_delay_view.hpp

    src/app/ui/views/settings/analog_pad_config_view.cpp
    src/app/ui/views/settings/analog_pad_config_view.hpp
    src/app/ui/views/settings/arcade_racer_config_view.cpp
    src/app/ui/views/settings/arcade_racer_config_view.hpp
    src/app/ui/views/settings/audio_settings_view.cpp
    src/app/ui/views/settings/audio_settings_view.hpp
    src/app/ui/views/settings/cartridge_settings_view.cpp
    src/app/ui/views/settings/cartridge_settings_view.hpp
    src/app/ui/views/settings/cdblock_settings_view.cpp
    src/app/ui/views/settings/cdblock_settings_view.hpp
    src/app/ui/views/settings/control_pad_config_view.cpp
    src/app/ui/views/settings/control_pad_config_view.hpp
    src/app/ui/views/settings/general_settings_view.cpp
    src/app/ui/views/settings/general_settings_view.hpp
    src/app/ui/views/settings/gui_settings_view.cpp
    src/app/ui/views/settings/gui_settings_view.hpp
    src/app/ui/views/settings/hotkeys_settings_view.cpp
    src/app/ui/views/settings/hotkeys_settings_view.hpp
    src/app/ui/views/settings/input_settings_view.cpp
    src/app/ui/views/settings/input_settings_view.hpp
    src/app/ui/views/settings/ipl_settings_view.cpp
    src/app/ui/views/settings/ipl_settings_view.hpp
    src/app/ui/views/settings/mission_stick_config_view.cpp
    src/app/ui/views/settings/mission_stick_config_view.hpp
    src/app/ui/views/settings/settings_view_base.cpp
    src/app/ui/views/settings/settings_view_base.hpp
    src/app/ui/views/settings/system_settings_view.cpp
    src/app/ui/views/settings/system_settings_view.hpp
    src/app/ui/views/settings/video_settings_view.cpp
    src/app/ui/views/settings/video_settings_view.hpp
    src/app/ui/views/settings/tweaks_settings_view.cpp
    src/app/ui/views/settings/tweaks_settings_view.hpp

    src/app/ui/widgets/audio_widgets.cpp
    src/app/ui/widgets/audio_widgets.hpp
    src/app/ui/widgets/cartridge_widgets.cpp
    src/app/ui/widgets/cartridge_widgets.hpp
    src/app/ui/widgets/common_widgets.cpp
    src/app/ui/widgets/common_widgets.hpp
    src/app/ui/widgets/datetime_widgets.cpp
    src/app/ui/widgets/datetime_widgets.hpp
    src/app/ui/widgets/input_widgets.cpp
    src/app/ui/widgets/input_widgets.hpp
    src/app/ui/widgets/peripheral_widgets.cpp
    src/app/ui/widgets/peripheral_widgets.hpp
    src/app/ui/widgets/savestate_widgets.cpp
    src/app/ui/widgets/savestate_widgets.hpp
    src/app/ui/widgets/settings_widgets.cpp
    src/app/ui/widgets/settings_widgets.hpp
    src/app/ui/widgets/system_widgets.cpp
    src/app/ui/widgets/system_widgets.hpp
    src/app/ui/widgets/unbound_actions_widget.cpp
    src/app/ui/widgets/unbound_actions_widget.hpp

    src/app/ui/windows/about_window.cpp
    src/app/ui/windows/about_window.hpp
    src/app/ui/windows/backup_ram_manager_window.cpp
    src/app/ui/windows/backup_ram_manager_window.hpp
    src/app/ui/windows/peripheral_config_window.cpp
    src/app/ui/windows/peripheral_config_window.hpp
    src/app/ui/windows/settings_window.cpp
    src/app/ui/windows/settings_window.hpp
    src/app/ui/windows/system_state_window.cpp
    src/app/ui/windows/system_state_window.hpp

    src/app/ui/windows/debug/cdblock_cmd_trace_window.cpp
    src/app/ui/windows/debug/cdblock_cmd_trace_window.hpp
    src/app/ui/windows/debug/cdblock_filters_window.cpp
    src/app/ui/windows/debug/cdblock_filters_window.hpp
    src/app/ui/windows/debug/cdblock_window_base.cpp
    src/app/ui/windows/debug/cdblock_window_base.hpp
    src/app/ui/windows/debug/cdblock_window_set.hpp
    src/app/ui/windows/debug/debug_output_window.cpp
    src/app/ui/windows/debug/debug_output_window.hpp
    src/app/ui/windows/debug/memory_viewer_window.cpp
    src/app/ui/windows/debug/memory_viewer_window.hpp
    src/app/ui/windows/debug/scsp_kyonex_trace_window.cpp
    src/app/ui/windows/debug/scsp_kyonex_trace_window.hpp
    src/app/ui/windows/debug/scsp_output_window.cpp
    src/app/ui/windows/debug/scsp_output_window.hpp
    src/app/ui/windows/debug/scsp_slots_window.cpp
    src/app/ui/windows/debug/scsp_slots_window.hpp
    src/app/ui/windows/debug/scsp_window_base.cpp
    src/app/ui/windows/debug/scsp_window_base.hpp
    src/app/ui/windows/debug/scsp_window_set.hpp
    src/app/ui/windows/debug/scu_dma_trace_window.cpp
    src/app/ui/windows/debug/scu_dma_trace_window.hpp
    src/app/ui/windows/debug/scu_dma_window.cpp
    src/app/ui/windows/debug/scu_dma_window.hpp
    src/app/ui/windows/debug/scu_dsp_window.cpp
    src/app/ui/windows/debug/scu_dsp_window.hpp
    src/app/ui/windows/debug/scu_interrupt_trace_window.cpp
    src/app/ui/windows/debug/scu_interrupt_trace_window.hpp
    src/app/ui/windows/debug/scu_registers_window.cpp
    src/app/ui/windows/debug/scu_registers_window.hpp
    src/app/ui/windows/debug/scu_window_set.hpp
    src/app/ui/windows/debug/sh2_breakpoints_window.cpp
    src/app/ui/windows/debug/sh2_breakpoints_window.hpp
    src/app/ui/windows/debug/sh2_cache_window.cpp
    src/app/ui/windows/debug/sh2_cache_window.hpp
    src/app/ui/windows/debug/sh2_debugger_window.cpp
    src/app/ui/windows/debug/sh2_debugger_window.hpp
    src/app/ui/windows/debug/sh2_divu_window.cpp
    src/app/ui/windows/debug/sh2_divu_window.hpp
    src/app/ui/windows/debug/sh2_dmac_trace_window.cpp
    src/app/ui/windows/debug/sh2_dmac_trace_window.hpp
    src/app/ui/windows/debug/sh2_dmac_window.cpp
    src/app/ui/windows/debug/sh2_dmac_window.hpp
    src/app/ui/windows/debug/sh2_exception_vectors_window.cpp
    src/app/ui/windows/debug/sh2_exception_vectors_window.hpp
    src/app/ui/windows/debug/sh2_interrupt_trace_window.cpp
    src/app/ui/windows/debug/sh2_interrupt_trace_window.hpp
    src/app/ui/windows/debug/sh2_interrupts_window.cpp
    src/app/ui/windows/debug/sh2_interrupts_window.hpp
    src/app/ui/windows/debug/sh2_power_window.cpp
    src/app/ui/windows/debug/sh2_power_window.hpp
    src/app/ui/windows/debug/sh2_timers_window.cpp
    src/app/ui/windows/debug/sh2_timers_window.hpp
    src/app/ui/windows/debug/sh2_watchpoints_window.cpp
    src/app/ui/windows/debug/sh2_watchpoints_window.hpp
    src/app/ui/windows/debug/sh2_window_base.cpp
    src/app/ui/windows/debug/sh2_window_base.hpp
    src/app/ui/windows/debug/sh2_window_set.hpp
    src/app/ui/windows/debug/vdp_window_base.cpp
    src/app/ui/windows/debug/vdp_window_base.hpp
    src/app/ui/windows/debug/vdp_window_set.hpp
    src/app/ui/windows/debug/vdp1_registers_window.cpp
    src/app/ui/windows/debug/vdp1_registers_window.hpp
    src/app/ui/windows/debug/vdp2_layer_params_window.cpp
    src/app/ui/windows/debug/vdp2_layer_params_window.hpp
    src/app/ui/windows/debug/vdp2_layer_visibility_window.cpp
    src/app/ui/windows/debug/vdp2_layer_visibility_window.hpp
    src/app/ui/windows/debug/vdp2_vram_delay_window.cpp
    src/app/ui/windows/debug/vdp2_vram_delay_window.hpp

    src/serdes/cereal_archive_vector.hpp
    src/serdes/state_cereal.hpp

    src/util/file_loader.cpp
    src/util/file_loader.hpp
    src/util/ipl_rom_loader.cpp
    src/util/ipl_rom_loader.hpp
    src/util/math.hpp
    src/util/os_exception_handler.cpp
    src/util/os_exception_handler.hpp
    src/util/regions.cpp
    src/util/regions.hpp
    src/util/ring_buffer.hpp
    src/util/sdl_file_dialog.hpp
    src/util/std_lib.cpp
    src/util/std_lib.hpp
)
add_executable(ymir::ymir-sdl3 ALIAS ymir-sdl3)
set_target_properties(ymir-sdl3 PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})
target_include_directories(ymir-sdl3
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_link_libraries(ymir-sdl3 PRIVATE
    ymir::ymir-core
    ymir-sdl3::rc
    SDL3::SDL3
    fmt
    cxxopts
    imgui::imgui
    tomlplusplus::tomlplusplus
    cereal::cereal
    lz4::lz4
    stb::stb
    rtmidi
)
target_compile_features(ymir-sdl3 PUBLIC cxx_std_20)

## Create embedded resource library
cmrc_add_resource_library(ymir-sdl3-rc
    ALIAS ymir-sdl3::rc
    NAMESPACE Ymir_sdl3_rc
    WHENCE res/embed

    res/embed/fonts/MaterialSymbolsOutlined_Filled-Regular.ttf
    res/embed/fonts/SplineSans-Bold.ttf
    res/embed/fonts/SplineSans-Medium.ttf
    res/embed/fonts/SplineSansMono-Bold.ttf
    res/embed/fonts/SplineSansMono-Medium.ttf
    res/embed/fonts/ZenDots-Regular.ttf
    
    res/embed/images/ymir.png
)

cmrk_copy_runtime_dlls(ymir-sdl3)

## Generate MacOS mig IPC implementation
if (APPLE)
    find_path(MACH_EXC_DEFS_DIR "mach/mach_exc.defs")
    message(STATUS "mach/mach_exc.defs location: ${MACH_EXC_DEFS_DIR}")
    if(MACH_EXC_DEFS_DIR)        
        execute_process(
            COMMAND
                mkdir -p "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/x64"
            COMMAND mig
                -arch x86_64
                -server "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/x64/mach_exc_server.c"
                -sheader "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/x64/mach_exc_server.h"
                "${MACH_EXC_DEFS_DIR}/mach/mach_exc.defs"
        )
        execute_process(
            COMMAND
                mkdir -p "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/a64"
            COMMAND mig
                -arch arm64
                -server "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/a64/mach_exc_server.c"
                -sheader "${CMAKE_CURRENT_SOURCE_DIR}/src/util/mig/a64/mach_exc_server.h"
                "${MACH_EXC_DEFS_DIR}/mach/mach_exc.defs"
        )
        target_sources(ymir-sdl3 PRIVATE
            src/util/mig/macos_mig.c
        )
    endif()
endif()

## Add icon
if (WIN32)
    target_sources(ymir-sdl3 PRIVATE res/ymir.rc)
endif ()

## Make it a Win32 executable (without a console window) on Windows
if (WIN32 AND NOT Ymir_ENABLE_DEVLOG)
    set_target_properties(ymir-sdl3 PROPERTIES WIN32_EXECUTABLE ON)
endif ()

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-sdl3")
    set_property(TARGET ymir-sdl3 PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## Define additional macros
target_compile_definitions(ymir-sdl3 PUBLIC Ymir_ENABLE_IMGUI_DEMO=$<BOOL:${Ymir_ENABLE_IMGUI_DEMO}>)
target_compile_definitions(ymir-sdl3 PUBLIC Ymir_ORGANIZATION_NAME="StrikerX3")
target_compile_definitions(ymir-sdl3 PUBLIC Ymir_APP_NAME="${PROJECT_NAME}")

## Apply performance options
if (Ymir_AVX2)
    if (MSVC)
        target_compile_options(ymir-sdl3 PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymir-sdl3 PUBLIC "-mavx2")
        target_compile_options(ymir-sdl3 PUBLIC "-mfma")
        target_compile_options(ymir-sdl3 PUBLIC "-mbmi")
    endif ()
endif ()

## Configure Visual Studio solution
if (MSVC)
    vs_set_filters(TARGET ymir-sdl3)
    set_target_properties(ymir-sdl3 PROPERTIES FOLDER "Ymir")
    set_target_properties(ymir-sdl3-rc PROPERTIES FOLDER "Ymir")
endif ()

## Include the install rules if requested
if (Ymir_INCLUDE_PACKAGING)
    add_subdirectory(packaging)
endif ()
