#include "app/ApplicationHost.h"

#include <cstdlib>
#include <string>

#include <windows.h>

namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"OpenVR Overlay Template could not start.";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\OpenVROverlayTemplate.Instance");
    if (singleInstance == nullptr) {
        return EXIT_FAILURE;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleInstance);
        return EXIT_SUCCESS;
    }

    flickvr::app::ApplicationHost application;
    std::string error;
    if (!application.Initialize(&error)) {
        const std::wstring message = Utf8ToWide(error);
        MessageBoxW(nullptr, message.c_str(), L"OpenVR Overlay Template", MB_OK | MB_ICONERROR);
        CloseHandle(singleInstance);
        return EXIT_FAILURE;
    }
    const int result = application.Run();
    application.Shutdown();
    CloseHandle(singleInstance);
    return result;
}
