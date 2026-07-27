add_library(stb OBJECT
    stb/stb_image.h
    stb/stb_image_write.h
    stb/stb_vorbis.c

    # Add more features as needed

    ymir/stb_implementations.cpp
)
add_library(stb::stb ALIAS stb)

target_include_directories(stb
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/ymir>"
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/stb>"
)
target_compile_features(stb PUBLIC cxx_std_20)
