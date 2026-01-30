#pragma once

#include <filesystem>

struct SDL_Window;

namespace util::os {

/// @brief Changes window decorations depending on the operating system:
/// - Windows 11: disables rounded corners
/// @param[in] window the SDL window to manipulate
void ConfigureWindowDecorations(SDL_Window *window);

/// @brief Changes the hidden attribute of a file.
/// Only applies to Windows.
/// @param[in] path the path to the file
/// @param[in] hidden the value of the hidden attribute
void SetFileHidden(std::filesystem::path path, bool hidden);

} // namespace util::os
