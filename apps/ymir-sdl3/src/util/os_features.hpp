#pragma once

#include <SDL3/SDL_video.h>

namespace util::os {

// Changes window decorations depending on the operating system:
// - Windows 11: disables rounded corners
void ConfigureWindowDecorations(SDL_Window *window);

} // namespace util::os
