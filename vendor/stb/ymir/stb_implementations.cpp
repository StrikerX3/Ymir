#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// stb_vorbis.c is backwards and provides the implementation by default, with a macro to disable it.
// Ymir's CMakeLists.txt for stb already defines it publicly so that library consumers can simply include the header and
// not worry about duplicate implementations or defining the header-only macro.
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
