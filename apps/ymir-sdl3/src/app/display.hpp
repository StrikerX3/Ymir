#pragma once

#include <SDL3/SDL_pixels.h>

namespace app::display {

struct DisplayMode {
    int width;
    int height;
    SDL_PixelFormat pixelFormat;
    float refreshRate;
    float pixelDensity;

    bool operator==(const DisplayMode &) const = default;

    bool IsValid() const {
        return width != 0 && height != 0 && pixelFormat != SDL_PIXELFORMAT_UNKNOWN && refreshRate != 0.0f &&
               pixelDensity != 0.0f;
    }
};

} // namespace app::display
