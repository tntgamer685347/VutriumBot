#pragma once

// Define before including Windows.h to prevent macro conflicts
#define NOMINMAX

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "kiero/kiero.h" // Adjust path if needed
#include "imgui/imgui.h" // Adjust path if needed
#include "imgui/imgui_impl_win32.h" // Adjust path if needed
#include "imgui/imgui_impl_dx11.h" // Adjust path if needed

// Kiero typedefs
typedef HRESULT(__stdcall* Present) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef uintptr_t PTR; // Optional PTR definition if used elsewhere