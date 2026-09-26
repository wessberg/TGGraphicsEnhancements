// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>

namespace tgb {
// Windows' d3d12.dll reads which Direct3D 12 runtime to use from the game's exe (the D3D12SDKVersion and
// D3D12SDKPath exports). Where Windows' own runtime is older than that request, it loads the exe's
// D3D12Core.dll instead and refuses every device (0x887E0003) unless that file's version matches the
// request exactly. The game ships a different version from the one it names, so the request is set to
// the version it ships. d3d12.dll reads the request when it loads, so this runs before anything in the
// process loads d3d12.dll. Returns what it found, for the log.
//
// An extension that needs a newer runtime than Windows' and the game's (TG Path Tracing, for DirectX
// Raytracing 1.2 and Shader Model 6.9) ships Microsoft's D3D12Core.dll in D3D12 beside its native DLL.
// The newest such retail runtime (6xx; 7xx previews need developer mode) inside the game folder, signed
// by Microsoft, becomes the request instead: its version and its folder, relative to the exe.
std::string align_agility_sdk(const std::vector<std::wstring>& extensions);
// After the shared D3D12 device was, or was not, created. Should Direct3D 12 fail on an extension's runtime
// (a Windows build it does not support, say), the next start does not use that runtime again while it and
// Windows' own are the same: Windows' or the game's runtime serves, as without the extension. A start that
// creates the device with it clears the note.
void note_agility_device(bool created);
}
