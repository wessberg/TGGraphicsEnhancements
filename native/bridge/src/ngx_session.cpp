// SPDX-License-Identifier: MIT
#include <mutex>
namespace { std::recursive_mutex ngxMutex; }
extern "C" __declspec(dllexport) void __cdecl TGGraphicsBridge_LockNgx() { ngxMutex.lock(); }
extern "C" __declspec(dllexport) void __cdecl TGGraphicsBridge_UnlockNgx() { ngxMutex.unlock(); }
