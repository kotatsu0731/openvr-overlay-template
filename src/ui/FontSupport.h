#pragma once

struct ImFont;

namespace flickvr::ui {

// Loads a Japanese UI font into the current ImGui context and merges Windows
// fallback fonts for rare ideographs, symbols, and supplementary characters.
// The returned font is owned by ImGui's font atlas.
ImFont* LoadOverlayFont(float pixels, bool preferBold = false);

}  // namespace flickvr::ui
