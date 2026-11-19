cmake_minimum_required(VERSION 3.28)

project(OpenVROverlayTemplate VERSION 1.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "OpenVROverlayTemplate requires Windows.")
endif()

option(OVRTPL_FETCH_DEPENDENCIES "Fetch OpenVR, Dear ImGui, and toml++ at configure time" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)
ovrtpl_configure_dependencies()

add_executable(OpenVROverlayTemplate WIN32
    src/app/ApplicationHost.cpp
    src/app/SystemTray.cpp
    src/app/main.cpp
    src/config/Settings.cpp
    src/diagnostics/Logger.cpp
    src/ui/FontSupport.cpp
    src/ui/OverlayRenderer.cpp
    src/ui/SmoothScroll.cpp
    src/ui/Theme.cpp
    src/vr/OpenVRRuntime.cpp
    src/vr/OverlayDragger.cpp
    src/vr/OverlayManager.cpp
    src/vr/PointerInteractor.cpp
    src/vr/PointerProjection.cpp
    src/vr/SteamVRInput.cpp
)

target_include_directories(OpenVROverlayTemplate PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(OpenVROverlayTemplate PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
if(MSVC)
    target_compile_options(OpenVROverlayTemplate PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
else()
    target_compile_options(OpenVROverlayTemplate PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(OpenVROverlayTemplate PRIVATE
    tomlplusplus::tomlplusplus
    ovrtpl_imgui
    openvr_api
    d3d11
    d3dcompiler
    dxgi
    dwmapi
    ole32
    shell32
    user32
    imm32
)

set_target_properties(OpenVROverlayTemplate PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:OpenVROverlayTemplate>"
)

add_custom_command(TARGET OpenVROverlayTemplate POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${OVRTPL_OPENVR_TARGET}>"
            "$<TARGET_FILE_DIR:OpenVROverlayTemplate>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:OpenVROverlayTemplate>/resources"
    VERBATIM
)
