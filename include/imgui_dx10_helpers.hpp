#pragma once

// Forward declarations for DirectX ImGui functions we need to call
#ifdef __cplusplus
extern "C" {
#endif

	// Declare these ImGui_ImplDX10 functions that are defined in imgui_impl_dx10.cpp
	// but not exposed in the header file
	extern void ImGui_ImplDX10_InvalidateDeviceObjects();
	extern bool ImGui_ImplDX10_CreateDeviceObjects();

#ifdef __cplusplus
}
#endif