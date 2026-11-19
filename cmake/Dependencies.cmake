include(FetchContent)

function(ovrtpl_configure_dependencies)
    if(NOT OVRTPL_FETCH_DEPENDENCIES)
        find_package(tomlplusplus CONFIG REQUIRED)
        find_package(OpenVR CONFIG REQUIRED)
        find_package(imgui CONFIG REQUIRED)
        add_library(ovrtpl_imgui ALIAS imgui::imgui)
        set(OVRTPL_OPENVR_TARGET openvr_api PARENT_SCOPE)
        set(OVRTPL_OPENVR_LICENSE "" PARENT_SCOPE)
        set(OVRTPL_IMGUI_LICENSE "" PARENT_SCOPE)
        return()
    endif()

    FetchContent_Declare(
        tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG v3.4.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(tomlplusplus)

    set(BUILD_SHARED ON CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        openvr
        GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
        GIT_TAG v2.15.6
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(openvr)
    if(TARGET openvr_api64)
        add_library(openvr_api ALIAS openvr_api64)
        set(OVRTPL_OPENVR_TARGET openvr_api64 PARENT_SCOPE)
        if(MINGW)
            target_compile_definitions(openvr_api64 PRIVATE WIN32)
        endif()
    elseif(TARGET openvr_api)
        set(OVRTPL_OPENVR_TARGET openvr_api PARENT_SCOPE)
    else()
        message(FATAL_ERROR "The fetched OpenVR SDK did not create an API target")
    endif()
    set(OVRTPL_OPENVR_LICENSE "${openvr_SOURCE_DIR}/LICENSE" PARENT_SCOPE)

    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9
        GIT_SHALLOW TRUE
        GIT_SUBMODULES ""
    )
    FetchContent_MakeAvailable(imgui)

    add_library(ovrtpl_imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    )
    target_include_directories(ovrtpl_imgui PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends"
    )
    target_compile_definitions(ovrtpl_imgui PUBLIC
        IMGUI_DISABLE_OBSOLETE_FUNCTIONS
        IMGUI_USE_WCHAR32)
    target_link_libraries(ovrtpl_imgui PUBLIC d3d11 dxgi)
    set(OVRTPL_IMGUI_LICENSE "${imgui_SOURCE_DIR}/LICENSE.txt" PARENT_SCOPE)
endfunction()
