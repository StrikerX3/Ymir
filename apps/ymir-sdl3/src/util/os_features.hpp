#pragma once

#include <SDL3/SDL_video.h>

#include <filesystem>

namespace util::os {

// Changes window decorations depending on the operating system:
// - Windows 11: disables rounded corners
void ConfigureWindowDecorations(SDL_Window *window);

// Changes the hidden attribute of a file.
// Only applies to Windows.
void SetFileHidden(std::filesystem::path path, bool hidden);

} // namespace util::os
