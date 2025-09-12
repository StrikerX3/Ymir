if(APPLE)
    # Install the .app bundle on macOS
    install(TARGETS ymir-sdl3 EXPORT Ymir_Targets
            BUNDLE DESTINATION . COMPONENT Ymir_Runtime
            RUNTIME DESTINATION . COMPONENT Ymir_Runtime
            LIBRARY COMPONENT Ymir_Runtime
            NAMELINK_COMPONENT Ymir_Development
            ARCHIVE COMPONENT Ymir_Development
            INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
else()
    # Standard installation for other platforms
    install(TARGETS ymir-sdl3 EXPORT Ymir_Targets
            RUNTIME COMPONENT Ymir_Runtime
            LIBRARY COMPONENT Ymir_Runtime
            NAMELINK_COMPONENT Ymir_Development
            ARCHIVE COMPONENT Ymir_Runtime
            INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
