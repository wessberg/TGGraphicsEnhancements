// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#ifdef _WIN32
#define TGFG_EXPORT extern "C" __declspec(dllexport)
#define TGFG_CALL __cdecl
#else
#define TGFG_EXPORT extern "C"
#define TGFG_CALL
#endif
#define TGFG_ABI 0x00050000u
#pragma pack(push,4)
struct TGFG_Request {
    uint32_t size, abi;
    uint64_t sequence;
    uint32_t mode, fixedMultiplier; // Off=0, Fixed=1, Dynamic=2 (DLSS only); total multiplier, clamped to the backend's maximum
    float dynamicTargetFps, baseFpsLimit;
    uint32_t suspensionMask, sourceGraphicsApi;
    uint32_t latencyMode; // 0 Off, 1 NVIDIA Reflex, 2 Reflex + Boost, 3 Intel XeLL, 4 AMD Anti-Lag 2
    uint32_t backend;     // 0 none (frame generation set to Off), 1 DLSS, 2 FSR, 3 XeSS
};
struct TGFG_Status {
    uint32_t size, abi;
    uint64_t requestedSequence, appliedSequence, successfulInputDispatches;
    uint32_t requestedMode, lastAcceptedMode, maxGeneratedFrames, dynamicSupported, reason;
    int32_t lastSetOptionsResult, lastGetStateResult;
    uint32_t dlssgStatus, swapchainFormat, flags, presentedSinceQuery, requestAgeMs;
    float requestedDynamicTargetFps, requestedBaseFpsLimit;
    uint32_t sourceApi, presentationApi, bridgeFlags;
    int32_t lastInteropHresult;
    uint64_t sourcePresents, outputFramesReported, copiedFrames, capturedNgxCalls;
    uint32_t sourceFormat, outputColorSpace, nativeLoadPid, completionFenceUsed;
    // Bit (1<<backend) per usable frame generation backend; maxima are what each runtime reports.
    uint32_t backendsAvailable, fsrMaxGenerated, xessMaxGenerated;
    // Bit (1<<latencyMode) per usable latency technology. Intel XeLL paired with XeSS Frame
    // Generation is implied by the XeSS backend bit; the XeLL bit means XeLL works on its own.
    uint32_t latencyAvailable;
    uint32_t activeBackend, activeLatency; // presenting swapchain (0 standard/DLSS-G, 2 FSR, 3 XeSS); latency technology in effect
    uint32_t gpuVendor, backendFailedMask; // PCI vendor ID; bit (1<<backend) disabled for this session after a runtime failure
    int32_t backendResult; // last failing FSR/XeSS/XeLL result code, 0 when none
};
// Matrices are row-vector / row-major, transposed from Unity's column-vector matrices.
// Packet is copied synchronously. No managed texture pointer is retained or guessed.
struct TGFG_Camera {
    uint32_t size, abi, frameId, resetHistory, width, height, cameraId, reserved;
    float viewToClip[16], clipToView[16], clipToPrev[16], prevToClip[16];
    float position[3], up[3], right[3], forward[3];
    float nearPlane, farPlane, fovRadians, aspect;
    float worldToView[16]; // Unity worldToCameraMatrix (view space looks down -Z), for XeSS frame constants
};
#pragma pack(pop)
static_assert(sizeof(TGFG_Request)==48, "request ABI");
static_assert(sizeof(TGFG_Status)==188, "status ABI");
static_assert(sizeof(TGFG_Camera)==416, "camera ABI");
TGFG_EXPORT uint32_t TGFG_CALL TGFG_GetAbiVersion();
TGFG_EXPORT int TGFG_CALL TGFG_Submit(const TGFG_Request*);
TGFG_EXPORT int TGFG_CALL TGFG_ReadStatus(TGFG_Status*);
TGFG_EXPORT int TGFG_CALL TGFG_BeginSimulation(uint32_t frameId);
TGFG_EXPORT int TGFG_CALL TGFG_EndSimulation(uint32_t frameId);
TGFG_EXPORT int TGFG_CALL TGFG_SetCamera(const TGFG_Camera*);
TGFG_EXPORT void* TGFG_CALL TGFG_GetRenderEvent();
// Optional game/RenoDX integration point. Must run on the immediate render
// context after final scene effects and before HUD, for this exact frame.
TGFG_EXPORT int TGFG_CALL TGFG_CaptureHudlessD3D11(uint32_t frameId, void* context, void* colorResource);
