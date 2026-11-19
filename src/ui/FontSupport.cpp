#include "ui/FontSupport.h"

#include <array>
#include <filesystem>

#include <imgui.h>
#include <windows.h>

namespace flickvr::ui {

ImFont* LoadOverlayFont(float pixels, bool preferBold) {
    std::array<char, MAX_PATH> windowsDirectory{};
    if (GetWindowsDirectoryA(windowsDirectory.data(), static_cast<UINT>(windowsDirectory.size())) == 0) {
        return nullptr;
    }

    const std::filesystem::path fonts = std::filesystem::path(windowsDirectory.data()) / "Fonts";
    const std::array regularCandidates{
        fonts / "YuGothM.ttc",
        fonts / "meiryo.ttc",
        fonts / "msgothic.ttc",
    };
    const std::array boldCandidates{
        fonts / "YuGothB.ttc",
        fonts / "meiryob.ttc",
        fonts / "YuGothM.ttc",
    };
    const std::array fallbackCandidates{
        fonts / "meiryo.ttc",
        fonts / "msgothic.ttc",
        fonts / "seguisym.ttf",
        fonts / "seguiemj.ttf",
    };

    ImGuiIO& io = ImGui::GetIO();
    ImFont* primaryFont = nullptr;
    std::filesystem::path primaryPath;
    const auto& primaryCandidates = preferBold ? boldCandidates : regularCandidates;
    for (const auto& candidate : primaryCandidates) {
        if (!std::filesystem::exists(candidate)) {
            continue;
        }
        // Dear ImGui 1.92 streams glyphs on demand, so a range list is no
        // longer required. The selected Windows font supplies Japanese glyphs.
        primaryFont = io.Fonts->AddFontFromFileTTF(candidate.string().c_str(), pixels);
        if (primaryFont != nullptr) {
            primaryPath = candidate;
            break;
        }
    }
    if (primaryFont == nullptr) {
        return nullptr;
    }

    // Conversion candidates may contain characters outside the primary
    // Japanese font (rare ideographs, symbols, or emoji). In ImGui 1.92 merged
    // sources are queried in order for each glyph, keeping Japanese metrics
    // while filling only code points absent from the primary font.
    for (const auto& candidate : fallbackCandidates) {
        if (candidate == primaryPath || !std::filesystem::exists(candidate)) {
            continue;
        }
        ImFontConfig mergeConfig{};
        mergeConfig.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(
            candidate.string().c_str(), pixels, &mergeConfig);
    }
    return primaryFont;
}

}  // namespace flickvr::ui
