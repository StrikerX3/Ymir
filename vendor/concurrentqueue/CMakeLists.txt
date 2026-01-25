add_library(concurrentqueue INTERFACE)

target_include_directories(concurrentqueue
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/concurrentqueue>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/concurrentqueue/>
)
