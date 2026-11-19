#include "ui/OverlayRenderer.h"

#include "ui/FontSupport.h"

#include <algorithm>
#include <cfloat>
#include <utility>
#include <vector>

#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <wrl/client.h>

namespace flickvr::ui {
namespace {

using Microsoft::WRL::ComPtr;

}  // namespace

struct OverlayRenderer::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    ComPtr<ID3D11ShaderResourceView> shaderResource;
    std::vector<ComPtr<ID3D11Texture2D>> retiredTextures;
    ImGuiContext* imgui{};
};

OverlayRenderer::OverlayRenderer() = default;

OverlayRenderer::~OverlayRenderer() {
    Shutdown();
}

bool OverlayRenderer::Initialize(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    std::uint32_t width,
    std::uint32_t height,
    float fontPixels,
    bool boldFont,
    const ThemePalette& palette,
    std::string* error) {
    Shutdown();
    if (device == nullptr || context == nullptr || width == 0 || height == 0) {
        if (error != nullptr) {
            *error = "OverlayRenderer requires a D3D11 device, context, and non-zero size";
        }
        return false;
    }

    auto next = std::make_unique<Impl>();
    next->device = device;
    next->context = context;

    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = width;
    textureDescription.Height = height;
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_DEFAULT;
    textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT result = device->CreateTexture2D(&textureDescription, nullptr, &next->texture);
    if (FAILED(result)) {
        if (error != nullptr) {
            *error = "Failed to create the overlay D3D11 texture";
        }
        return false;
    }
    result = device->CreateRenderTargetView(next->texture.Get(), nullptr, &next->renderTarget);
    if (FAILED(result)) {
        if (error != nullptr) {
            *error = "Failed to create the overlay render target";
        }
        return false;
    }
    result = device->CreateShaderResourceView(next->texture.Get(), nullptr, &next->shaderResource);
    if (FAILED(result)) {
        if (error != nullptr) {
            *error = "Failed to create the overlay shader resource";
        }
        return false;
    }

    next->imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(next->imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = {static_cast<float>(width), static_cast<float>(height)};
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Font size is supplied by each overlay and remains unchanged when only
    // the keyboard texture height is resized.
    LoadOverlayFont(std::clamp(fontPixels, 24.0F, 64.0F), boldFont);
    ApplyTheme(palette, ImGui::GetStyle());

    if (!ImGui_ImplDX11_Init(device, context)) {
        ImGui::DestroyContext(next->imgui);
        if (error != nullptr) {
            *error = "Dear ImGui DirectX 11 backend initialization failed";
        }
        return false;
    }

    width_ = width;
    height_ = height;
    impl_ = std::move(next);
    return true;
}

bool OverlayRenderer::Resize(
    std::uint32_t width,
    std::uint32_t height,
    std::string* error) {
    if (impl_ == nullptr || width == 0U || height == 0U) {
        if (error != nullptr) {
            *error = "OverlayRenderer resize requires an initialized renderer and non-zero size";
        }
        return false;
    }
    if (width == width_ && height == height_) {
        if (error != nullptr) error->clear();
        return true;
    }

    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = width;
    textureDescription.Height = height;
    textureDescription.MipLevels = 1;
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_DEFAULT;
    textureDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11RenderTargetView> renderTarget;
    ComPtr<ID3D11ShaderResourceView> shaderResource;
    HRESULT result = impl_->device->CreateTexture2D(
        &textureDescription, nullptr, &texture);
    if (SUCCEEDED(result)) {
        result = impl_->device->CreateRenderTargetView(
            texture.Get(), nullptr, &renderTarget);
    }
    if (SUCCEEDED(result)) {
        result = impl_->device->CreateShaderResourceView(
            texture.Get(), nullptr, &shaderResource);
    }
    if (FAILED(result)) {
        if (error != nullptr) {
            *error = "Failed to resize the overlay D3D11 render target";
        }
        return false;
    }

    // The old target may still be bound after the previous frame. Unbind it
    // before swapping resources. Keep the old texture alive until OpenVR has
    // received the new one; its API permits the compositor to retain the
    // previously submitted D3D texture until that point.
    impl_->context->OMSetRenderTargets(0, nullptr, nullptr);
    impl_->retiredTextures.push_back(impl_->texture);
    impl_->texture = std::move(texture);
    impl_->renderTarget = std::move(renderTarget);
    impl_->shaderResource = std::move(shaderResource);
    width_ = width;
    height_ = height;
    if (error != nullptr) error->clear();
    return true;
}

void OverlayRenderer::ReleaseRetiredTextures() noexcept {
    if (impl_ != nullptr) impl_->retiredTextures.clear();
}

void OverlayRenderer::Shutdown() noexcept {
    if (impl_ == nullptr) {
        return;
    }
    if (impl_->imgui != nullptr) {
        ImGui::SetCurrentContext(impl_->imgui);
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext(impl_->imgui);
    }
    impl_.reset();
    width_ = 0;
    height_ = 0;
}

void OverlayRenderer::BeginFrame(float deltaSeconds, const PointerFrame& pointer) {
    if (impl_ == nullptr) {
        return;
    }
    ImGui::SetCurrentContext(impl_->imgui);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(width_), static_cast<float>(height_)};
    io.DeltaTime = std::clamp(deltaSeconds, 1.0F / 240.0F, 0.1F);
    io.AddMousePosEvent(
        pointer.visible ? pointer.xPixels : -FLT_MAX,
        pointer.visible ? pointer.yPixels : -FLT_MAX);
    if (pointer.primaryPressed) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    }
    if (pointer.primaryReleased) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    }
    if (!pointer.primaryPressed && !pointer.primaryReleased) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, pointer.primaryHeld);
    }
    if (pointer.wheelX != 0.0F || pointer.wheelY != 0.0F) {
        io.AddMouseWheelEvent(pointer.wheelX, pointer.wheelY);
    }
    io.MouseDrawCursor = pointer.visible;
    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
}

void OverlayRenderer::DrawVrPointers(
    std::span<const VrPointerVisual> pointers) {
    if (impl_ == nullptr) {
        return;
    }
    ImGui::SetCurrentContext(impl_->imgui);
    ImDrawList& draw = *ImGui::GetForegroundDrawList();
    constexpr float kMargin = 24.0F;
    constexpr float kRingRadius = 16.0F;
    constexpr float kFillRadius = 13.0F;
    constexpr float kCenterRadius = 3.0F;
    constexpr ImU32 kHalo = IM_COL32(8, 12, 20, 185);
    constexpr ImU32 kOutline = IM_COL32(8, 12, 20, 230);
    constexpr ImU32 kWhite = IM_COL32(255, 255, 255, 255);
    constexpr ImU32 kAccent = IM_COL32(132, 112, 242, 235);
    constexpr ImU32 kPressedFill = IM_COL32(111, 97, 220, 120);

    for (const VrPointerVisual& pointer : pointers) {
        if (!pointer.visible) {
            continue;
        }

        const ImVec2 center{
            std::clamp(pointer.xPixels, kMargin, static_cast<float>(width_) - kMargin),
            std::clamp(pointer.yPixels, kMargin, static_cast<float>(height_) - kMargin),
        };
        const ImU32 ringColor = pointer.pressed ? kAccent : kWhite;
        if (pointer.pressed) {
            draw.AddCircleFilled(center, kFillRadius, kPressedFill, 32);
        }
        draw.AddCircle(center, kRingRadius, kHalo, 32, 8.0F);
        draw.AddCircle(center, kRingRadius, ringColor, 32, 3.0F);
        draw.AddCircleFilled(center, kCenterRadius + 1.5F, kOutline, 20);
        draw.AddCircleFilled(center, kCenterRadius, kWhite, 20);

        if (!pointer.offSurface) {
            continue;
        }
        const ImU32 markerColor = pointer.pressed ? kAccent : kWhite;
        if (pointer.xPixels < 0.0F) {
            draw.AddTriangleFilled(
                {center.x - 21.0F, center.y},
                {center.x - 9.0F, center.y - 9.0F},
                {center.x - 9.0F, center.y + 9.0F},
                kOutline);
            draw.AddTriangleFilled(
                {center.x - 19.0F, center.y},
                {center.x - 10.0F, center.y - 7.0F},
                {center.x - 10.0F, center.y + 7.0F},
                markerColor);
        } else if (pointer.xPixels > static_cast<float>(width_)) {
            draw.AddTriangleFilled(
                {center.x + 21.0F, center.y},
                {center.x + 9.0F, center.y - 9.0F},
                {center.x + 9.0F, center.y + 9.0F},
                kOutline);
            draw.AddTriangleFilled(
                {center.x + 19.0F, center.y},
                {center.x + 10.0F, center.y - 7.0F},
                {center.x + 10.0F, center.y + 7.0F},
                markerColor);
        }
        if (pointer.yPixels < 0.0F) {
            draw.AddTriangleFilled(
                {center.x, center.y - 21.0F},
                {center.x - 9.0F, center.y - 9.0F},
                {center.x + 9.0F, center.y - 9.0F},
                kOutline);
            draw.AddTriangleFilled(
                {center.x, center.y - 19.0F},
                {center.x - 7.0F, center.y - 10.0F},
                {center.x + 7.0F, center.y - 10.0F},
                markerColor);
        } else if (pointer.yPixels > static_cast<float>(height_)) {
            draw.AddTriangleFilled(
                {center.x, center.y + 21.0F},
                {center.x - 9.0F, center.y + 9.0F},
                {center.x + 9.0F, center.y + 9.0F},
                kOutline);
            draw.AddTriangleFilled(
                {center.x, center.y + 19.0F},
                {center.x - 7.0F, center.y + 10.0F},
                {center.x + 7.0F, center.y + 10.0F},
                markerColor);
        }
    }
}

void OverlayRenderer::EndFrame(const Color& clearColor) {
    if (impl_ == nullptr) {
        return;
    }
    ImGui::SetCurrentContext(impl_->imgui);
    ImGui::Render();

    const float clear[4]{clearColor.r, clearColor.g, clearColor.b, clearColor.a};
    impl_->context->OMSetRenderTargets(1, impl_->renderTarget.GetAddressOf(), nullptr);
    impl_->context->ClearRenderTargetView(impl_->renderTarget.Get(), clear);
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width_);
    viewport.Height = static_cast<float>(height_);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    impl_->context->RSSetViewports(1, &viewport);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void OverlayRenderer::SetTheme(const ThemePalette& palette) noexcept {
    if (impl_ != nullptr) {
        ImGui::SetCurrentContext(impl_->imgui);
        ApplyTheme(palette, ImGui::GetStyle());
    }
}

void OverlayRenderer::SetTextScale(float scale) noexcept {
    if (impl_ != nullptr) {
        ImGui::SetCurrentContext(impl_->imgui);
        ImGui::GetStyle().FontScaleMain = std::clamp(scale, 0.5F, 3.0F);
    }
}

bool OverlayRenderer::initialized() const noexcept {
    return impl_ != nullptr;
}

ID3D11Texture2D* OverlayRenderer::texture() const noexcept {
    return impl_ != nullptr ? impl_->texture.Get() : nullptr;
}

ID3D11ShaderResourceView* OverlayRenderer::shaderResourceView() const noexcept {
    return impl_ != nullptr ? impl_->shaderResource.Get() : nullptr;
}

}  // namespace flickvr::ui
