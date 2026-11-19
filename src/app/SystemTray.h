#pragma once

#include <string>

#include <windows.h>
#include <shellapi.h>

namespace flickvr::app {

class SystemTray final {
public:
    enum class Action { None, ResetOverlayPosition, Exit };

    SystemTray() = default;
    ~SystemTray();
    SystemTray(const SystemTray&) = delete;
    SystemTray& operator=(const SystemTray&) = delete;

    [[nodiscard]] bool Initialize(std::string* error);
    [[nodiscard]] Action TakeAction() noexcept;
    void Shutdown() noexcept;

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    bool AddIcon() noexcept;
    void ShowMenu();

    HWND window_{};
    NOTIFYICONDATAW icon_{};
    UINT taskbarCreatedMessage_{};
    Action pendingAction_{Action::None};
};

}  // namespace flickvr::app
