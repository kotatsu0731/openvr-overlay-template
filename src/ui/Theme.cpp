#include "ui/Theme.h"

#include <imgui.h>

namespace flickvr::ui {
namespace {

ImVec4 ToImVec4(const Color& color) noexcept {
    return {color.r, color.g, color.b, color.a};
}

}  // namespace

void ApplyTheme(const ThemePalette& colors, ImGuiStyle& style) noexcept {
    style.WindowRounding = 28.0F;
    style.ChildRounding = 22.0F;
    style.FrameRounding = 20.0F;
    style.GrabRounding = 16.0F;
    style.PopupRounding = 18.0F;
    style.TabRounding = 16.0F;
    style.WindowBorderSize = 2.0F;
    style.FrameBorderSize = 2.0F;
    style.WindowPadding = {28.0F, 24.0F};
    style.FramePadding = {24.0F, 16.0F};
    style.ItemSpacing = {18.0F, 16.0F};
    style.TouchExtraPadding = {8.0F, 8.0F};

    auto& c = style.Colors;
    c[ImGuiCol_WindowBg] = ToImVec4(colors.backdrop);
    c[ImGuiCol_ChildBg] = ToImVec4(colors.surface);
    c[ImGuiCol_PopupBg] = ToImVec4(colors.surface);
    c[ImGuiCol_PopupBg].w = 1.0F;
    const ImVec4 titleBackground{colors.surfaceRaised.r, colors.surfaceRaised.g,
                                colors.surfaceRaised.b, 1.0F};
    c[ImGuiCol_TitleBg] = titleBackground;
    c[ImGuiCol_TitleBgActive] = titleBackground;
    c[ImGuiCol_TitleBgCollapsed] = titleBackground;
    c[ImGuiCol_ModalWindowDimBg] = {0.0F, 0.0F, 0.0F, 0.45F};
    c[ImGuiCol_Border] = ToImVec4(colors.outline);
    c[ImGuiCol_Text] = ToImVec4(colors.text);
    c[ImGuiCol_TextDisabled] = ToImVec4(colors.textMuted);
    c[ImGuiCol_FrameBg] = ToImVec4(colors.surfaceRaised);
    c[ImGuiCol_FrameBgHovered] = ToImVec4(colors.accent);
    c[ImGuiCol_FrameBgActive] = ToImVec4(colors.accentPressed);
    c[ImGuiCol_Button] = ToImVec4(colors.surfaceRaised);
    c[ImGuiCol_ButtonHovered] = ToImVec4(colors.accent);
    c[ImGuiCol_ButtonActive] = ToImVec4(colors.accentPressed);
    c[ImGuiCol_CheckMark] = ToImVec4(colors.accent);
    c[ImGuiCol_SliderGrab] = ToImVec4(colors.accent);
    c[ImGuiCol_SliderGrabActive] = ToImVec4(colors.accentPressed);
    c[ImGuiCol_Header] = ToImVec4(colors.surfaceRaised);
    c[ImGuiCol_HeaderHovered] = ToImVec4(colors.accent);
    c[ImGuiCol_HeaderActive] = ToImVec4(colors.accentPressed);
    c[ImGuiCol_Tab] = ToImVec4(colors.surfaceRaised);
    c[ImGuiCol_TabHovered] = ToImVec4(colors.accent);
    c[ImGuiCol_TabSelected] = ToImVec4(colors.accentPressed);
}

}  // namespace flickvr::ui
