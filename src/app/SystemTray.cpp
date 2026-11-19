#include "app/SystemTray.h"

#include <utility>

namespace flickvr::app {
namespace {

constexpr wchar_t kWindowClass[] = L"OpenVROverlayTemplate.SystemTray";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kResetPosition = 1;
constexpr UINT kExit = 2;

}  // namespace

SystemTray::~SystemTray() {
    Shutdown();
}

bool SystemTray::Initialize(std::string* error) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    if (!RegisterClassW(&windowClass)) {
        if (error) *error = "Could not register the system tray window.";
        return false;
    }

    // A hidden top-level window receives TaskbarCreated after Explorer restarts.
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    window_ = CreateWindowExW(0, kWindowClass, L"OpenVR Overlay Template", WS_OVERLAPPED,
        0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!window_) {
        if (error) *error = "Could not create the system tray window.";
        Shutdown();
        return false;
    }

    icon_.cbSize = sizeof(icon_);
    icon_.hWnd = window_;
    icon_.uID = 1;
    icon_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    icon_.uCallbackMessage = kTrayMessage;
    icon_.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    if (!icon_.hIcon) {
        icon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    wcscpy_s(icon_.szTip, L"OpenVR Overlay Template");
    if (!icon_.hIcon || !AddIcon()) {
        if (error) *error = "Could not display the system tray icon.";
        Shutdown();
        return false;
    }
    if (error) error->clear();
    return true;
}

bool SystemTray::AddIcon() noexcept {
    if (!Shell_NotifyIconW(NIM_ADD, &icon_)) return false;
    icon_.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &icon_)) {
        Shell_NotifyIconW(NIM_DELETE, &icon_);
        return false;
    }
    return true;
}

SystemTray::Action SystemTray::TakeAction() noexcept {
    return std::exchange(pendingAction_, Action::None);
}

void SystemTray::ShowMenu() {
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, kResetPosition, L"オーバーレイ位置をリセット");
    AppendMenuW(menu, MF_STRING, kExit, L"終了");
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(window_);
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, window_, nullptr);
    PostMessageW(window_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK SystemTray::WindowProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SystemTray*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<SystemTray*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self) {
        if (self->taskbarCreatedMessage_ != 0 && message == self->taskbarCreatedMessage_) {
            self->AddIcon();
            return 0;
        }
        if (message == kTrayMessage && LOWORD(lParam) == WM_CONTEXTMENU) {
            self->ShowMenu();
            return 0;
        }
        if (message == WM_COMMAND) {
            if (LOWORD(wParam) == kExit) self->pendingAction_ = Action::Exit;
            else if (LOWORD(wParam) == kResetPosition && self->pendingAction_ != Action::Exit) {
                self->pendingAction_ = Action::ResetOverlayPosition;
            }
            return 0;
        }
        if (message == WM_CLOSE) {
            self->pendingAction_ = Action::Exit;
            return 0;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void SystemTray::Shutdown() noexcept {
    if (icon_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &icon_);
        icon_ = {};
    }
    if (window_) {
        DestroyWindow(window_);
        window_ = nullptr;
    }
    UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
    pendingAction_ = Action::None;
}

}  // namespace flickvr::app
