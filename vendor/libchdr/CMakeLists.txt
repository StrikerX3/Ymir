find_package(ZLIB REQUIRED)
find_package(zstd REQUIRED)
add_subdirectory(libchdr/deps/lzma-24.05 EXCLUDE_FROM_ALL)

list(APPEND CHDR_INCLUDES ${CMAKE_CURRENT_BINARY_DIR}/include)

add_library(chdr-static STATIC
    libchdr/src/libchdr_bitstream.c
    libchdr/src/libchdr_cdrom.c
    libchdr/src/libchdr_chd.c
    libchdr/src/libchdr_flac.c
    libchdr/src/libchdr_huffman.c
)
target_include_directories(chdr-static PRIVATE ${CHDR_INCLUDES} PUBLIC libchdr/include)
target_link_libraries(chdr-static PRIVATE lzma ZLIB::ZLIB zstd::libzstd_static)
if (MSVC)
    target_compile_definitions(chdr-static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
