// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#define TGNR_ABI 0x00010100u
#define TGNR_EXPORT extern "C" __declspec(dllexport)
#pragma pack(push, 4)
enum TGNR_State : uint32_t { TGNR_WAITING, TGNR_MISSING, TGNR_REJECTED, TGNR_UNSUPPORTED, TGNR_READY, TGNR_FAILED };
enum TGNR_Flags : uint32_t { TGNR_REVERSED_DEPTH=1, TGNR_RESET=2, TGNR_SRGB_VIEW=4, TGNR_HDR_INPUT=8 };
// Guides are captured at BeforePostProcess; color at that stage (HDR_INPUT) or AfterPostProcess. Both carry
// the same frame AND camera. Pointers are retained until their queued render event completes.
struct TGNR_Frame {
    uint32_t size, abi, frame, camera;
    uint64_t color, depth, motion;
    uint32_t width, height, renderWidth, renderHeight, flags;
    float strength, jitterX, jitterY, nearPlane, farPlane;
    // 310.8 profile: A/B/C select DLSSNR.Style 0/1/2; separate from the weight-set hint.
    uint32_t model=0;
    float localTone=1, localStructure=1, skinStructure=1;
    uint32_t autoMask=0;
};
struct TGNR_Status {
    uint32_t size, abi, state, lastResult;
    uint64_t evaluated, skipped, failed;
    uint32_t width, height, lastFrame, resets;
    double gpuMilliseconds;
    uint64_t transferBytes;
};
#pragma pack(pop)
static_assert(sizeof(TGNR_Frame)==100, "TGNR frame ABI");
static_assert(sizeof(TGNR_Status)==72, "TGNR status ABI");
