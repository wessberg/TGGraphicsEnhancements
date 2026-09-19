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
#define TGFG_ABI 0x00040000u
#pragma pack(push,4)
struct TGFG_Request {
    uint32_t size, abi;
    uint64_t sequence;
    uint32_t mode, fixedMultiplier; // Off=0, Fixed=1, Dynamic=2; total multiplier
    float dynamicTargetFps, reflexBaseFpsLimit;
    uint32_t suspensionMask, sourceGraphicsApi;
    uint32_t reflexMode; // 0=Off, 1=On (low latency), 2=On + Boost. Forced to at least On while FG is active.
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
};
// Matrices are row-vector / row-major, transposed from Unity's column-vector matrices.
// Packet is copied synchronously. No managed texture pointer is retained or guessed.
struct TGFG_Camera {
    uint32_t size, abi, frameId, resetHistory, width, height, cameraId, reserved;
    float viewToClip[16], clipToView[16], clipToPrev[16], prevToClip[16];
    float position[3], up[3], right[3], forward[3];
    float nearPlane, farPlane, fovRadians, aspect;
};
#pragma pack(pop)
static_assert(sizeof(TGFG_Request)==44, "request ABI");
static_assert(sizeof(TGFG_Status)==152, "status ABI");
static_assert(sizeof(TGFG_Camera)==352, "camera ABI");
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
