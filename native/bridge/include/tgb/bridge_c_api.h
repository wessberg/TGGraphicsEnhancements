// SPDX-License-Identifier: MIT
// The TG Graphics Bridge C API, used by its BepInEx preloader and its gameplay plugin.
#pragma once
#include <stdint.h>
#ifdef _WIN32
#define TGB_EXPORT extern "C" __declspec(dllexport)
#define TGB_CALL __cdecl
#else
#define TGB_EXPORT extern "C"
#define TGB_CALL
#endif
#define TGB_ABI 0x00010100u
#pragma pack(push,4)
struct TGB_Status {
    uint32_t size, abi;
    uint32_t bootstrapped;           // 0 not yet, 1 hooks installed, 2 failed
    uint32_t extensionsRegistered, extensionsActive, extensionsFailed;
    uint32_t needs;                  // union of the active extensions' needs
    int32_t presentationProvider;    // index of the extension replacing the game's swapchain, -1 none
    int32_t deviceCreator;           // index of the extension that created the D3D12 device, -1 the bridge
    uint32_t gameDeviceObserved, gameDeviceGeneration;
    uint32_t d3d12DeviceReady, gpuVendor;
    uint32_t swapchainsSeen, swapchainsProvided; // game-window swapchains; how many a provider replaced
    int32_t lastError;               // last failing HRESULT, 0 when none
    uint32_t nativeLoadPid;
};
// CPU, GPU and video memory use for the performance overlay (TGGraphicsBridge_ReadPerformance).
struct TGB_Performance {
    uint32_t size, abi;
    uint32_t valid;                              // bit 0 CPU, bit 1 GPU, bit 2 video memory
    float cpuProcess, cpuSystem;                 // percent of all logical processors: this process, the system
    float gpuProcess, gpuSystem;                 // percent busy of the busiest GPU engine type, as Task Manager shows it
    uint32_t reserved;
    uint64_t vramProcess, vramBudget, vramTotal; // bytes: this process on the game's adapter, Windows' budget for it, dedicated memory
};
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGB_Status)==68, "TG Graphics Bridge status ABI");
static_assert(sizeof(TGB_Performance)==56, "TG Graphics Bridge performance ABI");
#endif
TGB_EXPORT uint32_t TGB_CALL TGGraphicsBridge_GetAbiVersion();
// Before bootstrap only: the full path of an extension DLL. 0 accepted; negative refused.
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_AddExtension(const wchar_t* path);
// Synchronously from the preloader, outside DllMain. 0 success.
TGB_EXPORT uint32_t __stdcall TGGraphicsBridge_Bootstrap(void* unused);
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_ReadStatus(TGB_Status* status);
// Name of registered extension index (UTF-8, NUL-terminated); length written, or negative.
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_GetExtensionName(uint32_t index, char* buffer, uint32_t size);
// 1 active, 0 inactive or failed, negative out of range.
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_GetExtensionState(uint32_t index);
// The latest of the samples taken twice a second from the first call on. 0 success.
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_ReadPerformance(TGB_Performance* performance);
// Facts about this session as UTF-8 key=value lines (Windows build, Direct3D 12 runtime, adapters and
// displays, NVIDIA driver settings). Writes at most size-1 bytes and a NUL; returns the full length.
TGB_EXPORT int32_t TGB_CALL TGGraphicsBridge_ReadDiagnostics(char* buffer, uint32_t size);
// One line in TGGraphicsBridge.Native.log, so a mod's own events (a troubleshooting run's steps) line
// up with the bridge's.
TGB_EXPORT void TGB_CALL TGGraphicsBridge_LogNote(const char* text);
