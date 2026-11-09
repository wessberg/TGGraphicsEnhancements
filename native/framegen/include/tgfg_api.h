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
#define TGFG_ABI 0x00010100u
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
// The gameplay camera's depth and motion vectors straight from HDRP, for frames without an
// upscaler that exposes them (TAA, STP, no anti-aliasing). The resources are the camera's own
// render textures (Texture.GetNativeTexturePtr); they are copied on the render thread by render
// event 3 of the same frame, while HDRP still owns them. Motion vectors follow HDRP: current minus
// previous position in UV units, without jitter.
struct TGFG_HdrpInputs {
    uint32_t size, abi, frameId, flags; // flags: bit 0 = reversed-Z depth
    uint64_t depth, motion;             // ID3D11Resource*
    uint32_t renderWidth, renderHeight; // valid region from (0,0): HDCamera.actualWidth/actualHeight
    float jitterX, jitterY;             // HDCamera.taaJitter.xy, in render pixels
};
// HDRP AfterPostProcess color, still owned by HDRP until render event 4 consumes it.
struct TGFG_Hudless {
    uint32_t size, abi, frameId, flags; // 1 flip Y; 2 linear nits; 4 Rec2020 (otherwise Rec709); 8 sRGB SRV
    uint64_t color;
    uint32_t width, height; // valid viewport from (0,0), not the RTHandle allocation size
};
// CPU time the presentation bridge spends in Present, in microseconds, summed since the last read
// with reset (TGFG_ReadTimings). gpuWait: waiting for the D3D12 queue to free a buffer; sdkPresent:
// the presenting swapchain's own Present (DLSS-G, FSR or XeSS pacing, VSync); handoff: returning the
// frame to D3D11. interval: time between presents; slowIntervals counts those over 50 ms.
struct TGFG_Timing {
    uint32_t size, abi;
    uint32_t presents, slowIntervals;
    uint64_t totalUs, gpuWaitUs, sdkPresentUs, handoffUs, intervalUs;
    uint32_t totalMaxUs, gpuWaitMaxUs, sdkPresentMaxUs, intervalMaxUs;
};
#pragma pack(pop)
static_assert(sizeof(TGFG_Timing)==72, "timing ABI");
static_assert(sizeof(TGFG_Hudless)==32, "HUD-less ABI");
static_assert(sizeof(TGFG_Request)==48, "request ABI");
static_assert(sizeof(TGFG_Status)==188, "status ABI");
static_assert(sizeof(TGFG_Camera)==416, "camera ABI");
static_assert(sizeof(TGFG_HdrpInputs)==48, "HDRP inputs ABI");
TGFG_EXPORT uint32_t TGFG_CALL TGFG_GetAbiVersion();
TGFG_EXPORT int TGFG_CALL TGFG_Submit(const TGFG_Request*);
TGFG_EXPORT int TGFG_CALL TGFG_ReadStatus(TGFG_Status*);
TGFG_EXPORT int TGFG_CALL TGFG_BeginSimulation(uint32_t frameId);
TGFG_EXPORT int TGFG_CALL TGFG_EndSimulation(uint32_t frameId);
TGFG_EXPORT int TGFG_CALL TGFG_SetCamera(const TGFG_Camera*);
TGFG_EXPORT int TGFG_CALL TGFG_SetHdrpInputs(const TGFG_HdrpInputs*);
TGFG_EXPORT int TGFG_CALL TGFG_SetHudless(const TGFG_Hudless*);
TGFG_EXPORT void* TGFG_CALL TGFG_GetRenderEvent();
// Optional game/RenoDX integration point. Must run on the immediate render
// context after final scene effects and before HUD, for this exact frame.
TGFG_EXPORT int TGFG_CALL TGFG_CaptureHudlessD3D11(uint32_t frameId, void* context, void* colorResource);
// Enable verification and use of the AfterPostProcess capture supplied at render event 4.
TGFG_EXPORT int TGFG_CALL TGFG_SetHudlessCapture(uint32_t enabled);
// Troubleshooting overlays and checks of the frame generation SDKs, from the plugin's advanced config.
// Bits: 1 tear lines, 2 reset indicators, 4 debug view, 8 pacing lines, 16 the SDK's input checker (FSR),
// 32 one three-frame color/depth/motion capture after verified HUD separation and camera movement.
TGFG_EXPORT int TGFG_CALL TGFG_SetDebugFlags(uint32_t flags);
// Optional, for the plugin's troubleshooting report. Facts about this session as UTF-8 key=value
// lines (Streamline, DLSS-G, Reflex, FSR, XeSS and Anti-Lag 2 availability, the last failure); writes
// at most size-1 bytes and a NUL and returns the full length.
TGFG_EXPORT int TGFG_CALL TGFG_ReadDiagnostics(char* buffer, uint32_t size);
// The Present timings since the last read; reset != 0 starts a new window.
TGFG_EXPORT int TGFG_CALL TGFG_ReadTimings(TGFG_Timing* timing, uint32_t reset);
// One line in TGFG.Native.log. Flags: 1 ends the current pipeline summary window at the next present,
// 2 (with 1) holds the 5 s summaries until the next call with 1, so one summary covers one window.
TGFG_EXPORT int TGFG_CALL TGFG_LogNote(const char* text, uint32_t flags);
// enabled != 0: present without DXGI_PRESENT_ALLOW_TEARING, whatever the game passes. The plugin sets it
// while it holds the game's VSync off for DLSS Frame Generation on another GPU's screen: that fixes the
// latency NVIDIA warns of where Windows composes the frames, and keeps a laptop that flips them directly
// (hybrid independent flip, cross-adapter scan-out) from tearing.
TGFG_EXPORT int TGFG_CALL TGFG_SetNoTearing(uint32_t enabled);
// DLSS Frame Generation's preset: 0 NVIDIA's default, 3 Preset C (lighter, a little less exact).
TGFG_EXPORT int TGFG_CALL TGFG_SetDlssPreset(uint32_t preset);
