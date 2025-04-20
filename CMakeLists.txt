# Minimum CMake version and project definition
cmake_minimum_required(VERSION 3.14)
project(KolosalDesktop)

include(${CMAKE_SOURCE_DIR}/cmake/ucm.cmake)

# Static link the runtime libraries
ucm_set_runtime(STATIC)

# Global settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

ucm_print_flags()

# Define source directories
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)
set(IMGUI_DIR ${EXTERNAL_DIR}/imgui)
set(FONT_FOLDER_PATH ${EXTERNAL_DIR}/fonts)

# Set the options
option(DEBUG "Build with debugging information" OFF)

# ==== External Dependencies ====

# utfcpp
include(FetchContent)
FetchContent_Declare(
    utfcpp
    GIT_REPOSITORY https://github.com/nemtrif/utfcpp.git
    GIT_TAG v4.0.6
)
FetchContent_MakeAvailable(utfcpp)

# Freetype (./external/freetype/lib ./external/freetype/include ./external/freetype/bin)
set(CMAKE_PREFIX_PATH "${EXTERNAL_DIR}/freetype" ${CMAKE_PREFIX_PATH})
find_package(Freetype REQUIRED)
if(NOT FREETYPE_FOUND)
    message(FATAL_ERROR "Freetype not found")
endif()
message(STATUS "Found Freetype: ${FREETYPE_INCLUDE_DIRS}")
message(STATUS "    - Freetype Libraries: ${FREETYPE_LIBRARIES}")
set(FREETYPE_DLL_DIR "${FREETYPE_INCLUDE_DIRS}/../bin")
message(STATUS "    - Freetype DLL Directory: ${FREETYPE_DLL_DIR}")

# OpenSSL
find_package(OpenSSL REQUIRED)
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not found")
endif()
message(STATUS "Found OpenSSL: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "    - OpenSSL Libraries: ${OPENSSL_LIBRARIES}")

set(OPENSSL_DLL_DIR "${OPENSSL_INCLUDE_DIR}/../bin")
message(STATUS "    - OpenSSL DLL Directory: ${OPENSSL_DLL_DIR}")

# DirectX SDK (using Windows SDK)
# DirectX 10 components are included in Windows SDK, no need for separate find_package

# Native File Dialog Extended
add_subdirectory(${EXTERNAL_DIR}/nativefiledialog-extended)

# CURL
set(CMAKE_PREFIX_PATH "${EXTERNAL_DIR}/curl" ${CMAKE_PREFIX_PATH})

find_package(CURL REQUIRED)
if(NOT CURL_FOUND)
    message(FATAL_ERROR "CURL not found")
endif()
message(STATUS "Found CURL: ${CURL_INCLUDE_DIR}")

# genta personal engine library
find_library(INFERENCE_ENGINE_LIB        NAMES InferenceEngineLib       PATHS ${EXTERNAL_DIR}/genta-personal/lib)
find_library(INFERENCE_ENGINE_LIB_VULKAN NAMES InferenceEngineLibVulkan PATHS ${EXTERNAL_DIR}/genta-personal/lib)
if(NOT INFERENCE_ENGINE_LIB OR NOT INFERENCE_ENGINE_LIB_VULKAN)
    message(FATAL_ERROR "Inference Engine library not found")
endif()
message(STATUS "Found InferenceEngine libraries:")
message(STATUS "  - OpenBLAS: ${INFERENCE_ENGINE_LIB}")
message(STATUS "  - Vulkan:   ${INFERENCE_ENGINE_LIB_VULKAN}")

# MD4C, ImGuiTextEdit and imgui_md
set(MARKDOWN_RENDERER
    ${EXTERNAL_DIR}/md4c/src/md4c.c
    ${EXTERNAL_DIR}/imgui_md/imgui_md.cpp
)

# ImGuiTextSelect
set(IMGUI_TEXT_SELECT
    ${EXTERNAL_DIR}/ImGuiTextSelect/textselect.cpp
)

# ==== ImGui Configuration ====
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx10.cpp  # Changed from OpenGL3 to DirectX 10
)

# ==== Font Definitions ====
set(FONT_DEFINITIONS
    IMGUI_FONT_PATH_INTER_REGULAR="fonts/Inter-Regular.ttf"
    IMGUI_FONT_PATH_INTER_BOLD="fonts/Inter-Bold.ttf"
    IMGUI_FONT_PATH_INTER_BOLDITALIC="fonts/Inter-BoldItalic.ttf"
    IMGUI_FONT_PATH_INTER_ITALIC="fonts/Inter-Italic.ttf"
    IMGUI_FONT_PATH_FIRACODE_REGULAR="fonts/FiraCode-Regular.ttf"
    IMGUI_FONT_PATH_CODICON="fonts/codicon.ttf"
    IMGUI_FONT_PATH_NOTO_EMOJI="fonts/seguiemj-1.35-flat.ttf"
)

# ==== Kolosal Library ====
add_library(kolosal_lib STATIC
    ${IMGUI_SOURCES}
    ${MARKDOWN_RENDERER}
    ${IMGUI_TEXT_SELECT}
)

# Set definitions
target_compile_definitions(kolosal_lib PUBLIC
    CHAT_HISTORY_DIRECTORY="${CMAKE_SOURCE_DIR}/chat_history"
    PRESETS_DIRECTORY="${CMAKE_SOURCE_DIR}/presets"
    CONFIG_PATH="${CMAKE_SOURCE_DIR}/config.json"
    $<$<BOOL:${DEBUG}>:DEBUG>
)

target_include_directories(kolosal_lib PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${EXTERNAL_DIR}/icons
    ${EXTERNAL_DIR}/nlohmann
    ${EXTERNAL_DIR}/nativefiledialog-extended/src/include
    ${EXTERNAL_DIR}/genta-personal/include
    ${EXTERNAL_DIR}/stb
    ${EXTERNAL_DIR}/md4c/src
    ${EXTERNAL_DIR}/imgui_md
    ${EXTERNAL_DIR}/imspinner
    ${EXTERNAL_DIR}/ImGuiTextSelect
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/assets
    ${CMAKE_SOURCE_DIR}/kolosal-server/include
    ${FREETYPE_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIR}
)

# Platform-specific library dependencies
if(WIN32)
    target_link_libraries(kolosal_lib PUBLIC
        nfd
        Dwmapi
        UxTheme
        Shcore
        d3d10        # DirectX 10 library
        dxgi         # DXGI library
        user32
        gdi32
        shell32
        OpenSSL::SSL
        utf8cpp
        ${FREETYPE_LIBRARIES}
        ${CURL_LIBRARIES}
    )
    
    target_compile_definitions(kolosal_lib PUBLIC
        UNICODE
        ${FONT_DEFINITIONS}
        KOLOSAL_LOGO_PATH="assets/logo.png"
    )
else()
    target_link_libraries(kolosal_lib PUBLIC
        nfd
        OpenSSL::SSL
    )

    target_compile_definitions(kolosal_lib PUBLIC
        ${FONT_DEFINITIONS}
        KOLOSAL_LOGO_PATH="assets/logo.png"
    )
endif()

# ==== Kolosal Server Shared Library ====
add_subdirectory(${CMAKE_SOURCE_DIR}/kolosal-server)

# ==== Main Executable ====
if (DEBUG)
    add_executable(KolosalDesktop
        source/main.cpp 
        assets/resource.rc
    )
else()
    add_executable(KolosalDesktop WIN32
        source/main.cpp 
        assets/resource.rc
    )
endif()

# Link both the engine (kolosal_lib) and the Kolosal server shared library.
target_link_libraries(KolosalDesktop PRIVATE 
    kolosal_lib
    kolosal_server
)

# ==== Post-Build Commands ====
# Copy fonts
add_custom_command(
    TARGET KolosalDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${FONT_FOLDER_PATH}" "$<TARGET_FILE_DIR:KolosalDesktop>/fonts"
)

# Copy assets
add_custom_command(
    TARGET KolosalDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:KolosalDesktop>/assets"
)

# Copy OpenSSL DLLs
add_custom_command(
    TARGET KolosalDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${OPENSSL_DLL_DIR}/libcrypto-3-x64.dll"
        "${OPENSSL_DLL_DIR}/libssl-3-x64.dll"
        "$<TARGET_FILE_DIR:KolosalDesktop>"
    COMMENT "Copying OpenSSL DLLs to output directory"
)

# Copy models folder
add_custom_command(
    TARGET KolosalDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/models" "$<TARGET_FILE_DIR:KolosalDesktop>/models"
)

# Copy curl DLLs from external/curl/bin
add_custom_command(
    TARGET KolosalDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${EXTERNAL_DIR}/curl/bin" "$<TARGET_FILE_DIR:KolosalDesktop>"
)

# Copy Kolosal Server DLL
add_custom_command(
    TARGET KolosalDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:kolosal_server>"
        "$<TARGET_FILE_DIR:KolosalDesktop>"
    COMMENT "Copying Kolosal Server DLL to output directory"
)

# Copy Inference Engine DLLs
add_custom_command(
    TARGET KolosalDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EXTERNAL_DIR}/genta-personal/bin/InferenceEngineLib.dll"
        "${EXTERNAL_DIR}/genta-personal/bin/InferenceEngineLibVulkan.dll"
        "$<TARGET_FILE_DIR:KolosalDesktop>"
    COMMENT "Copying Inference Engine DLLs to output directory"
)

# Copy Freetype DLLs
add_custom_command(
    TARGET KolosalDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FREETYPE_DLL_DIR}/freetype.dll"
        "$<TARGET_FILE_DIR:KolosalDesktop>"
    COMMENT "Copying Freetype DLLs to output directory"
)