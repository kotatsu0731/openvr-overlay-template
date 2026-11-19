#pragma once

#include "ui/Theme.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;
struct ImGuiContext;

namespace flickvr::ui {

struct PointerFrame {
    bool visible{};
    float xPixels{};
    float yPixels{};
    bool primaryHeld{};
    bool primaryPressed{};
    bool primaryReleased{};
    float wheelX{};
    float wheelY{};
};

struct VrPointerVisual {
    bool visible{};
    bool pressed{};
    bool offSurface{};
    float xPixels{};
    float yPixels{};
};

class OverlayRenderer final {
public:
    OverlayRenderer();
    ~OverlayRenderer();

    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;

    [[nodiscard]] bool Initialize(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        std::uint32_t width,
        std::uint32_t height,
        float fontPixels,
        bool boldFont,
        const ThemePalette& palette,
        std::string* error = nullptr);
    // Recreates only the overlay render target. The ImGui context, font atlas,
    // theme and text scale are preserved across a live layout resize.
    [[nodiscard]] bool Resize(
        std::uint32_t width,
        std::uint32_t height,
        std::string* error = nullptr);
    // Call only after the newly resized texture has been submitted to OpenVR.
    // Until then, the compositor may still reference the previous texture.
    void ReleaseRetiredTextures() noexcept;
    void Shutdown() noexcept;

    void BeginFrame(float deltaSeconds, const PointerFrame& pointer);
    void DrawVrPointers(std::span<const VrPointerVisual> pointers);
    void EndFrame(const Color& clearColor);
    void SetTheme(const ThemePalette& palette) noexcept;
    void SetTextScale(float scale) noexcept;

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
    [[nodiscard]] ID3D11Texture2D* texture() const noexcept;
    [[nodiscard]] ID3D11ShaderResourceView* shaderResourceView() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t width_{};
    std::uint32_t height_{};
};

}  // namespace flickvr::ui
