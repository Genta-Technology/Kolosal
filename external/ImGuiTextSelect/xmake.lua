-- Copyright 2024-2025 Aidan Sun and the ImGuiTextSelect contributors
-- SPDX-License-Identifier: MIT

add_requires("imgui", { configs = { glfw = true, opengl3 = true } })
add_requires("glfw", { configs = { glfw_include = "system" } })
add_requires("opengl", "utfcpp")
add_requireconfs("imgui.glfw", { configs = { glfw_include = "system" } })

target("example")
    set_languages("c++17")
    set_encodings("utf-8")

    add_packages("imgui", "glfw", "opengl", "utfcpp")
    add_files("example/main.cpp", "textselect.cpp")
    add_includedirs(".")
