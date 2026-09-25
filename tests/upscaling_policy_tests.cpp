// SPDX-License-Identifier: MIT
// Portable checks of the upscaler's decisions: which XeSS preset serves a render scale, which FSR
// version is the newest, which requests are refused, the SDK conventions derived from HDRP's values,
// and which DLSS loads get the bundled nvngx_dlss.dll.
#include "tgup_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int failures=0;
static void expect(bool condition, const char* what) {
    if(!condition) { std::printf("FAIL: %s\n", what); ++failures; }
}
static TGUP_Frame frame() {
    TGUP_Frame f{}; f.size=sizeof(f); f.abi=TGUP_ABI; f.sequence=1; f.backend=TGUP_BACKEND_FSR;
    f.color=f.depth=f.motion=f.output=0x1000;
    f.renderWidth=1920; f.renderHeight=1080; f.outputWidth=3840; f.outputHeight=2160;
    f.jitterX=0.25f; f.jitterY=-0.125f; f.motionScaleX=-1920; f.motionScaleY=-1080;
    f.cameraNear=0.1f; f.cameraFar=5000; f.fovY=1.0f; f.frameTimeMs=16.6f; f.sharpness=0.5f; f.preExposure=1;
    f.flags=TGUP_FLAG_DEPTH_INVERTED; f.mode=0;
    return f;
}
int main() {
    using namespace tgup;
    // The game's quality slider spans divisors 2.99 to 1.0 (UpScaling.UpScalingQualityDivisors).
    struct Case { float ratio; XessPreset preset; } cases[]={
        {1.0f, XessPreset::NativeAA}, {1.2f, XessPreset::UltraQualityPlus}, {1.3f, XessPreset::UltraQualityPlus},
        {1.5f, XessPreset::UltraQuality}, {1.7f, XessPreset::Quality}, {2.0f, XessPreset::Balanced},
        {2.2f, XessPreset::Performance}, {2.99f, XessPreset::UltraPerformance}, {3.0f, XessPreset::UltraPerformance}};
    for(const auto& c:cases) {
        const int index=xess_preset_index(c.ratio);
        expect(index>=0 && kXessRanges[index].preset==c.preset, "narrowest XeSS preset for a ratio");
    }
    expect(xess_preset_index(3.5f)==-1, "no XeSS preset beyond 3x");
    expect(xess_preset_index(0.f)==-1 && xess_preset_index(-1.f)==-1, "non-positive ratios have no preset");
    // Stepping past a preset the runtime rejected moves to the next wider one.
    const int balanced=xess_preset_index(2.0f);
    expect(xess_preset_index(2.0f, balanced+1)>balanced, "next wider preset after a rejection");
    // Rounding of odd output sizes stays inside the preset.
    expect(kXessRanges[xess_preset_index(upscale_ratio(2560, 1440, 3841, 2161))].preset==XessPreset::UltraQuality,
        "a pixel of rounding does not change the preset");
    expect(kXessRanges[xess_preset_index(upscale_ratio(1280, 720, 2561, 1441))].preset==XessPreset::Balanced,
        "a pixel of rounding at 2x stays Balanced");
    expect(upscale_ratio(1920, 1080, 3840, 2160)==2.f, "per-axis ratio");

    auto f=frame();
    expect(valid(f), "a complete request is valid");
    f=frame(); f.size=0; expect(!valid(f), "wrong size refused");
    f=frame(); f.abi=0x50000; expect(!valid(f), "wrong ABI refused");
    f=frame(); f.backend=TGUP_BACKEND_NONE; expect(!valid(f), "no technology refused");
    f=frame(); f.backend=7; expect(!valid(f), "unknown technology refused");
    f=frame(); f.depth=0; expect(!valid(f), "missing texture refused");
    f=frame(); f.output=0; expect(valid(f), "the destination may follow in TGUP_SetOutput");
    f=frame(); f.renderWidth=4000; expect(!valid(f), "downscaling refused");
    f=frame(); f.jitterX=2.f; expect(!valid(f), "jitter beyond a pixel refused");
    f=frame(); f.cameraFar=0.05f; expect(!valid(f), "far before near refused");
    f=frame(); f.fovY=0.f; expect(!valid(f), "zero field of view refused");
    f=frame(); f.sharpness=1.5f; expect(!valid(f), "sharpness above one refused");
    f=frame(); f.preExposure=0.f; expect(!valid(f), "zero pre-exposure refused");
    f=frame(); f.flags=64u; expect(!valid(f), "unknown flag refused");
    f=frame(); f.mode=3; expect(!valid(f), "FSR takes no mode");
    f=frame(); f.backend=TGUP_BACKEND_XESS; f.mode=101; expect(valid(f), "XeSS names its preset");
    f=frame(); f.backend=TGUP_BACKEND_XESS; f.mode=0; expect(valid(f), "XeSS without a mode chooses by ratio");
    f=frame(); f.backend=TGUP_BACKEND_XESS; f.mode=99; expect(!valid(f), "unknown XeSS preset refused");
    f=frame(); f.backend=TGUP_BACKEND_XESS; f.mode=107; expect(!valid(f), "XeSS preset past Native AA refused");
    f=frame(); f.motionScaleX=std::strtof("nan", nullptr); expect(!valid(f), "NaN refused");

    // HDRP's DLSS pass gives NGX -taaJitter; FSR and XeSS use the same convention.
    f=frame(); float jx=0, jy=0; sdk_jitter(f, jx, jy);
    expect(jx==-0.25f && jy==0.125f, "SDK jitter is HDRP's taaJitter negated");
    float n=0, fa=0; fsr_near_far(f, n, fa);
    expect(n==5000.f && fa==0.1f, "inverted depth swaps near and far for FSR");
    f.flags=0; fsr_near_far(f, n, fa);
    expect(n==0.1f && fa==5000.f, "standard depth keeps near and far");
    expect(frame_time_ms(0.f)==1.f && frame_time_ms(1000.f)==200.f && frame_time_ms(16.6f)==16.6f, "frame time bounds");

    // The newest version AMD's runtime lists, in whatever order it lists them.
    expect(newest_version({"3.1.5", "2.3.4"})==0, "FSR 3.1.5 over 2.3.4");
    expect(newest_version({"2.3.4", "4.0.2", "3.1.5"})==1, "FSR 4 over FSR 3.1 wherever it is listed");
    expect(newest_version({"3.1.10", "3.1.9"})==0, "versions compare by number, not text");
    expect(newest_version({"FSR 3.1.4", "3.1.5"})==1, "a prefix before the number is ignored");
    expect(newest_version({"?", "2.3.4"})==1, "an unnumbered name ranks below a numbered one");
    expect(newest_version({})==-1, "no versions, no choice");
    expect(version_numbers("4.0.2 (ML)")==std::vector<uint32_t>({4, 0, 2}), "trailing text after the number");

    // Which loads get the bundled DLSS: nvngx_dlss.dll by full path only, and only over an older file.
    expect(is_dlss_sr_path(L"C:\\Games\\FoA/nvngx_dlss.dll"), "NGX's own path, with its forward slash");
    expect(is_dlss_sr_path(L"D:\\FoA\\NVNGX_DLSS.DLL"), "file names compare without case");
    expect(is_dlss_sr_path(L"\\\\?\\C:\\FoA\\nvngx_dlss.dll"), "extended-length and UNC paths");
    expect(!is_dlss_sr_path(L"nvngx_dlss.dll"), "a bare name is left to the loader's search");
    expect(!is_dlss_sr_path(L"C:\\FoA\\nvngx_dlssd.dll") && !is_dlss_sr_path(L"C:\\FoA\\nvngx_dlssg.dll"),
        "ray reconstruction and frame generation are other DLLs");
    expect(!is_dlss_sr_path(L"C:\\FoA\\my_nvngx_dlss.dll"), "only the exact file name");
    expect(!is_dlss_sr_path(nullptr) && !is_dlss_sr_path(L""), "no path");
    constexpr uint64_t v3810=(3ull<<48)|(8ull<<32)|(10ull<<16), v31091=(310ull<<48)|(9ull<<32)|(1ull<<16), v31080=(310ull<<48)|(8ull<<32);
    expect(use_bundled_dlss(v3810, v31091), "the game's 3.8.10 gets the bundled 310.9.1");
    expect(use_bundled_dlss(v31080, v31091), "a DLSS mod's older 310.8 gets the bundled 310.9.1");
    expect(!use_bundled_dlss(v31091, v31091), "the same version is kept");
    expect(!use_bundled_dlss((311ull<<48), v31091), "a newer file is kept");
    expect(!use_bundled_dlss(0, v31091), "a file without a version is kept");
    expect(!use_bundled_dlss(v3810, 0), "no bundled copy, nothing replaced");
    expect(file_version_text(v31091)=="310.9.1.0" && file_version_text(v3810)=="3.8.10.0", "version text");
    // A DLSS mod's set in the game folder is kept whole; the game's lone nvngx_dlss.dll is not a set.
    constexpr uint64_t v310_7_129=(310ull<<48)|(7ull<<32)|(129ull<<16);
    expect(part_of_dlss_set(v31080, true, {}), "DLSS neural rendering beside it (DLSS 5): kept");
    expect(part_of_dlss_set(v31080, false, {v310_7_129, v31080}), "frame generation of the same release: kept");
    expect(!part_of_dlss_set(v31080, false, {v310_7_129}), "only ray reconstruction of another release: not a set");
    expect(!part_of_dlss_set(v3810, false, {}), "the game's own nvngx_dlss.dll alone: replaced");
    expect(!part_of_dlss_set(v3810, false, {0, 0}), "siblings without versions: not a set");

    if(failures) { std::printf("%d upscaling policy checks failed\n", failures); return 1; }
    std::printf("Upscaling policy checks passed\n");
    return 0;
}
