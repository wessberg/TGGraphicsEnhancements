// SPDX-License-Identifier: MIT
#pragma once
#include <string>

namespace tgb {
// Windows' d3d12.dll reads which Direct3D 12 runtime to use from the game's exe (the D3D12SDKVersion and
// D3D12SDKPath exports). Where Windows' own runtime is older than that request, it loads the exe's
// D3D12Core.dll instead and refuses every device (0x887E0003) unless that file's version matches the
// request exactly. The game ships a different version from the one it names, so the request is set to
// the version it ships. d3d12.dll reads the request when it loads, so this runs before anything in the
// process loads d3d12.dll. Returns what it found, for the log.
std::string align_agility_sdk();
}
