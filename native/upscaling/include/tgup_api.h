// SPDX-License-Identifier: MIT
// TG Better Upscaling native ABI, shared with the BepInEx plugin (NativeUpscaler.cs).
#pragma once
#include <stdint.h>
#ifdef _WIN32
#define TGUP_EXPORT extern "C" __declspec(dllexport)
#define TGUP_CALL __cdecl
#else
#define TGUP_EXPORT extern "C"
#define TGUP_CALL
#endif
#define TGUP_ABI 0x00010200u

// Upscaling technologies, values shared with the plugin.
#define TGUP_BACKEND_NONE 0u
#define TGUP_BACKEND_FSR 1u
#define TGUP_BACKEND_XESS 2u

// TGUP_Frame.flags
#define TGUP_FLAG_RESET 1u              // history is invalid: camera cut, first frame, resolution change
#define TGUP_FLAG_DEPTH_INVERTED 2u     // reversed-Z depth
#define TGUP_FLAG_FSR_AUTO_EXPOSURE 4u  // FSR computes its own exposure instead of taking HDRP's
#define TGUP_FLAG_FSR_DEBUG_CHECK 8u    // FSR's input checker; its messages go to the native log
#define TGUP_FLAG_FSR_DEBUG_VIEW 16u    // FSR's debug view instead of the upscaled image

// Render events (Unity's IssuePluginEventAndData; the data is the value named here).
#define TGUP_EVENT_UPSCALE 1            // TGUP_Frame.sequence: inputs, upscale and, with the output known, the write
#define TGUP_EVENT_OUTPUT 2             // TGUP_Frame.sequence: the write into the output from TGUP_SetOutput

#pragma pack(push,4)
// One upscaling request, taken where HDRP would run STP. The textures are HDRP's own render textures
// (Texture.GetNativeTexturePtr), valid from (0,0) over the render and output sizes. Render event 1
// with this sequence number upscales on the render thread; render event 2 writes the result into
// output, which TGUP_SetOutput supplies when the destination is only known to a later pass. With
// output set here, event 1 does both.
struct TGUP_Frame {
    uint32_t size, abi, sequence, backend;
    uint64_t color, depth, motion, output;          // ID3D11Resource*; output may follow in TGUP_SetOutput
    uint32_t renderWidth, renderHeight, outputWidth, outputHeight;
    float jitterX, jitterY;                         // HDCamera.taaJitter.xy, render pixels (negated for the SDKs)
    float motionScaleX, motionScaleY;               // HDRP motion vectors to render pixels, current to previous
    float cameraNear, cameraFar, fovY, frameTimeMs; // world units, radians, milliseconds
    float sharpness, preExposure;                   // FSR's own sharpening 0..1 (0 off); HDRP's exposure baked into the color (1 = none)
    uint32_t flags;                                 // TGUP_FLAG_*
    uint32_t mode;                                  // XeSS: the chosen xess_quality_settings_t (100..106); otherwise 0
};
// A quality mode as the runtime reports it: the vendor's id and its per-axis upscale ratio (output /
// render). FSR: FfxApiUpscaleQualityMode, 0 (Native AA) to 4 (Ultra Performance). XeSS:
// xess_quality_settings_t, 100 (Ultra Performance) to 106 (Native Anti-Aliasing).
struct TGUP_Mode {
    uint32_t id;
    float ratio;
};
// The DLSS Super Resolution overrides in the NVIDIA driver's settings profile for the game (what the
// NVIDIA App's DLSS override writes), for the performance overlay.
struct TGUP_DlssOverride {
    uint32_t known;           // 1 once read from the driver's profile; 0 without an NVIDIA driver or before the first read
    uint32_t presetOverride;  // "Override DLSS-SR presets": 0 none, 1..15 preset A..O, 0xFFFFFF the latest
    uint32_t overrideEnabled; // "Enable DLSS-SR override": 1 on
    uint32_t modeOverride;    // "Override DLSS-SR performance mode": 3 the game's own, 0xFFFFFFFF not set
    uint32_t dlaaOverride;    // "Override DLSS mode to be DLAA": 1 forces DLAA
    uint32_t scalingRatio;    // "Override scaling ratio for DLSS-SR": render scale 33..100 percent, 0 not set
};
struct TGUP_Status {
    uint32_t size, abi;
    uint32_t available;                             // bit (1<<backend) per usable technology
    uint32_t disabled;                              // bit (1<<backend): switched off for the session after failures
    uint32_t fsrVersionCount, xessVersion;          // XeSS packed major<<16|minor<<8|patch
    uint32_t activeBackend, activeFsrVersion, xessPreset; // activeFsrVersion: index of the version FSR runs
    uint32_t renderWidth, renderHeight, outputWidth, outputHeight;
    uint32_t gpuVendor, deviceReady;
    int32_t lastResult;                             // last failing SDK or HRESULT code, 0 when none
    uint64_t framesUpscaled, framesFailed, framesFallback;
};
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGUP_Frame)==112, "TG Better Upscaling frame ABI");
static_assert(sizeof(TGUP_Status)==88, "TG Better Upscaling status ABI");
static_assert(sizeof(TGUP_Mode)==8, "TG Better Upscaling mode ABI");
static_assert(sizeof(TGUP_DlssOverride)==24, "TG Better Upscaling DLSS override ABI");
#endif

TGUP_EXPORT uint32_t TGUP_CALL TGUP_GetAbiVersion();
TGUP_EXPORT int32_t TGUP_CALL TGUP_ReadStatus(TGUP_Status* status);
// Display name of FSR version index (UTF-8). Returns the length written, or negative.
TGUP_EXPORT int32_t TGUP_CALL TGUP_GetFsrVersionName(uint32_t index, char* buffer, uint32_t size);
// Main thread, while recording the frame: the request render event 1 then carries out.
TGUP_EXPORT int32_t TGUP_CALL TGUP_SubmitFrame(const TGUP_Frame* frame);
// Main thread: HDRP's destination for a submitted request, before render event 2.
TGUP_EXPORT int32_t TGUP_CALL TGUP_SetOutput(uint32_t sequence, uint64_t output);
TGUP_EXPORT void* TGUP_CALL TGUP_GetRenderEvent();
// The quality modes of one technology (TGUP_BACKEND_*), from the highest render scale down, once its
// runtime was probed on the shared device. Returns how many there are (at most capacity are written), or
// negative.
TGUP_EXPORT int32_t TGUP_CALL TGUP_GetModes(uint32_t backend, TGUP_Mode* modes, uint32_t capacity);
// The DLSS Super Resolution DLL NVIDIA's runtime loaded for the game's DLSS, as file versions
// (major.minor.build.revision, 16 bits each): the one loaded, the one it asked for, the bundled one.
// Returns TGUP_DLSS_*.
#define TGUP_DLSS_NONE 0u     // not loaded yet (or not an NVIDIA GPU)
#define TGUP_DLSS_BUNDLED 1u  // the bundled copy, in place of the older file it asked for
#define TGUP_DLSS_KEPT 2u     // the file it asked for, not older than the bundled copy
TGUP_EXPORT int32_t TGUP_CALL TGUP_GetDlssDll(uint64_t* loaded, uint64_t* requested, uint64_t* bundled);
// The driver profile's DLSS overrides, read on a thread of its own from the first call on and again
// every 30 seconds. 0 success.
TGUP_EXPORT int32_t TGUP_CALL TGUP_GetDlssOverride(TGUP_DlssOverride* dlssOverride);
