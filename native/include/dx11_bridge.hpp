// SPDX-License-Identifier: MIT
#pragma once
#include "sl_runtime.hpp"
#include "tgfg_interop_policy.hpp"
#include <d3d11_4.h>
#include <memory>
namespace tgfg {
// S_OK = wrapped; S_FALSE = not a supported candidate (call original unchanged).
// A failed HRESULT is logged and the original creation path may be used only
// while no replacement HWND swapchain remains alive.
HRESULT try_create_dx11_bridge(IUnknown* device,IDXGIFactory* clientFactory,HWND,
    const DXGI_SWAP_CHAIN_DESC1&,const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,IDXGIOutput*,IDXGISwapChain1**);
bool is_game_window(HWND,UINT,UINT);
bool capture_dx11_inputs(uint32_t,ID3D11DeviceContext*,ID3D11Resource*,ID3D11Resource*,
    RectU,RectU,const sl::Constants&);
// The same capture on the game device's immediate context, for Unity render-thread events.
bool capture_dx11_inputs_immediate(uint32_t,ID3D11Resource*,ID3D11Resource*,RectU,RectU,const sl::Constants&);
void finish_dx11_capture(uint32_t,bool);
int capture_dx11_hudless(uint32_t,ID3D11DeviceContext*,ID3D11Resource*);
int capture_dx11_hudless_frame(const TGFG_Hudless&);
void shutdown_dx11_bridge();
} // namespace tgfg
