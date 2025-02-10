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

# OpenGL
find_package(OpenGL REQUIRED)
if(NOT OpenGL_FOUND)
    message(FATAL_ERROR "OpenGL not found")
endif()
message(STATUS "Found OpenGL: ${OpenGL_INCLUDE_DIR}")

# OpenSSL
find_package(OpenSSL REQUIRED)
if(NOT OpenSSL_FOUND)
    message(FATAL_ERROR "OpenSSL not found")
endif()
message(STATUS "Found OpenSSL: ${OPENSSL_INCLUDE_DIR}")
message(STATUS "    - OpenSSL Libraries: ${OPENSSL_LIBRARIES}")

set(OPENSSL_DLL_DIR "${OPENSSL_INCLUDE_DIR}/../bin")
message(STATUS "    - OpenSSL DLL Directory: ${OPENSSL_DLL_DIR}")

# GLAD
add_library(glad STATIC ${EXTERNAL_DIR}/glad/src/glad.c)
target_include_directories(glad PUBLIC ${EXTERNAL_DIR}/glad/include)

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

# ==== ImGui Configuration ====
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

# ==== Font Definitions ====
set(FONT_DEFINITIONS
    IMGUI_FONT_PATH_INTER_REGULAR="fonts/Inter-Regular.ttf"
    IMGUI_FONT_PATH_FIRACODE_REGULAR="fonts/FiraCode-Regular.ttf"
    IMGUI_FONT_PATH_INTER_BOLD="fonts/Inter-Bold.ttf"
    IMGUI_FONT_PATH_INTER_BOLDITALIC="fonts/Inter-BoldItalic.ttf"
    IMGUI_FONT_PATH_INTER_ITALIC="fonts/Inter-Italic.ttf"
    IMGUI_FONT_PATH_CODICON="fonts/codicon.ttf"
)

# ==== Kolosal Library ====
add_library(kolosal_lib STATIC
    ${IMGUI_SOURCES}
    ${MARKDOWN_RENDERER}
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
    ${OpenGL_INCLUDE_DIR}
    ${EXTERNAL_DIR}/glad/include
    ${EXTERNAL_DIR}/icons
    ${EXTERNAL_DIR}/nlohmann
    ${EXTERNAL_DIR}/nativefiledialog-extended/src/include
    ${EXTERNAL_DIR}/genta-personal/include
    ${EXTERNAL_DIR}/stb
    ${EXTERNAL_DIR}/md4c/src
    ${EXTERNAL_DIR}/imgui_md
    ${EXTERNAL_DIR}/imspinner
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/assets
    ${CURL_INCLUDE_DIR}
)

# Platform-specific library dependencies
if(WIN32)
    target_link_libraries(kolosal_lib PUBLIC
        ${OpenGL_LIBRARIES}
        glad
        nfd
        Dwmapi
        UxTheme
        Shcore
        opengl32
        user32
        gdi32
        shell32
        OpenSSL::SSL
        ${CURL_LIBRARIES}
    )
    
    target_compile_definitions(kolosal_lib PUBLIC
        IMGUI_IMPL_OPENGL_LOADER_GLAD
        UNICODE
        ${FONT_DEFINITIONS}
        KOLOSAL_LOGO_PATH="assets/logo.png"
    )
else()
    target_link_libraries(kolosal_lib PUBLIC
        ${OpenGL_LIBRARIES}
        glad
        nfd
        OpenSSL::SSL
    )
    
    target_compile_definitions(kolosal_lib PUBLIC
        IMGUI_IMPL_OPENGL_LOADER_GLAD
        ${FONT_DEFINITIONS}
        KOLOSAL_LOGO_PATH="assets/logo.png"
    )
endif()

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

target_link_libraries(KolosalDesktop PRIVATE kolosal_lib)

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

# Copy Inference Engine DLLs
add_custom_command(
    TARGET KolosalDesktop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EXTERNAL_DIR}/genta-personal/bin/InferenceEngineLib.dll"
        "${EXTERNAL_DIR}/genta-personal/bin/InferenceEngineLibVulkan.dll"
        "$<TARGET_FILE_DIR:KolosalDesktop>"
    COMMENT "Copying Inference Engine DLLs to output directory"
)