#pragma once

struct ImGuiStyle;

namespace flickvr::ui {

struct Color {
    float r{};
    float g{};
    float b{};
    float a{1.0F};
};

struct ThemePalette {
    Color backdrop;
    Color surface;
    Color surfaceRaised;
    Color outline;
    Color controlOutline;
    Color inputSurface;
    Color text;
    Color textMuted;
    Color textOnAccent;
    Color accent;
    Color accentSecondary;
    Color accentPressed;
    Color accentSurfaceTint;
    Color surfaceTint;
    Color backgroundGlow;
    Color noiseTint;
    Color success;
    Color warning;
    Color danger;
};

void ApplyTheme(const ThemePalette& colors, ImGuiStyle& style) noexcept;

}  // namespace flickvr::ui
