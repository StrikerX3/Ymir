#pragma once

#include <imgui.h>

namespace app::ui::widgets {

// Creates a "(?)" element with a simple text explanation.
void ExplanationTooltip(const char *explanation, float scale, bool sameLine = true);

// Creates a "(!)" element with a text message and the given color.
// Useful for warnings or errors.
void ImportantTooltip(ImVec4 color, const char *message, float scale, bool sameLine = true);

} // namespace app::ui::widgets
