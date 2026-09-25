// SPDX-License-Identifier: MIT
#pragma once
#include <dxgi1_6.h>
#include <string>

namespace tgb {
// One fact about this session, as key=value for TGGraphicsBridge_ReadDiagnostics. Thread-safe.
void set_fact(const std::string& key, const std::string& value);
std::string fact(const std::string& key);
std::string all_facts();
// Once at bootstrap: the Windows product, version and build.
void note_windows();
// When the game's D3D11 device is first seen (core mutex held): its adapter, and which adapter
// drives the display its window is on.
void note_adapter(IDXGIAdapter* adapter, const DXGI_ADAPTER_DESC& desc);
void note_display(IDXGIAdapter* game, const DXGI_ADAPTER_DESC& gameDesc, HWND window);
// The same device's swapchain on a new window: the display watcher follows it.
void note_display_window(HWND window);
// The NVIDIA driver settings that change frame pacing, read on a thread of their own and again
// every minute. Started once the game's adapter is known to be an NVIDIA one.
void start_driver_settings();
}
