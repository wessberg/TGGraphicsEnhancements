// SPDX-License-Identifier: MIT
// Decisions of the upscaler that need no GPU, kept free of Windows headers so they are tested on
// every build host.
#pragma once
#include "tgup_api.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace tgup {
// XeSS quality presets (xess_quality_settings_t). Since XeSS 1.3 each preset accepts any input
// resolution between native and its own ratio, so the game's continuous render scale maps to the
// narrowest preset whose range still contains it.
enum class XessPreset : int32_t { UltraPerformance=100, Performance=101, Balanced=102, Quality=103, UltraQuality=104, UltraQualityPlus=105, NativeAA=106 };
struct XessRange { XessPreset preset; float maxRatio; };
inline constexpr XessRange kXessRanges[]={
    {XessPreset::NativeAA, 1.0f}, {XessPreset::UltraQualityPlus, 1.3f}, {XessPreset::UltraQuality, 1.5f},
    {XessPreset::Quality, 1.7f}, {XessPreset::Balanced, 2.0f}, {XessPreset::Performance, 2.3f},
    {XessPreset::UltraPerformance, 3.0f}};
inline float upscale_ratio(uint32_t renderWidth, uint32_t renderHeight, uint32_t outputWidth, uint32_t outputHeight) {
    if(!renderWidth || !renderHeight) return 0.f;
    return std::max(float(outputWidth)/float(renderWidth), float(outputHeight)/float(renderHeight));
}
// Index into kXessRanges of the narrowest preset for this ratio, starting at from (to step past a
// preset the runtime rejected); -1 when none fits.
inline int xess_preset_index(float ratio, int from=0) {
    if(!(ratio>0.f) || !std::isfinite(ratio)) return -1;
    constexpr float tolerance=0.01f; // the game's own 2.99 divisor and rounding of odd sizes
    for(int i=std::max(0, from); i<int(sizeof(kXessRanges)/sizeof(kXessRanges[0])); ++i)
        if(ratio<=kXessRanges[i].maxRatio+tolerance) return i;
    return -1;
}
inline const char* xess_preset_name(XessPreset p) {
    switch(p) {
    case XessPreset::UltraPerformance: return "Ultra Performance";
    case XessPreset::Performance: return "Performance";
    case XessPreset::Balanced: return "Balanced";
    case XessPreset::Quality: return "Quality";
    case XessPreset::UltraQuality: return "Ultra Quality";
    case XessPreset::UltraQualityPlus: return "Ultra Quality Plus";
    case XessPreset::NativeAA: return "Native Anti-Aliasing";
    }
    return "?";
}
inline bool finite_all(std::initializer_list<float> values) {
    for(float v:values) if(!std::isfinite(v)) return false;
    return true;
}
// The mode a request names: XeSS takes its preset (or 0 to choose by ratio); FSR has no mode input.
inline bool valid_mode(uint32_t backend, uint32_t mode) {
    if(backend==TGUP_BACKEND_XESS) return mode==0 || (mode>=uint32_t(XessPreset::UltraPerformance) && mode<=uint32_t(XessPreset::NativeAA));
    if(backend==TGUP_BACKEND_RAY_RECONSTRUCTION) return mode<=5; // NGX: MaxPerf, Balanced, MaxQuality, UltraPerformance, UltraQuality, DLAA
    return mode==0;
}
// The request the plugin sends; anything outside these bounds is refused before the GPU sees it.
inline bool valid(const TGUP_Frame& f) {
    if(f.size!=sizeof(TGUP_Frame) || f.abi!=TGUP_ABI) return false;
    if(f.backend!=TGUP_BACKEND_FSR && f.backend!=TGUP_BACKEND_XESS && f.backend!=TGUP_BACKEND_RAY_RECONSTRUCTION) return false;
    // The output may follow in TGUP_SetOutput, from the pass that declares HDRP's destination; Ray
    // Reconstruction's comes with the request, as do its guides.
    if(!f.color || !f.depth || !f.motion) return false;
    const bool rr=f.backend==TGUP_BACKEND_RAY_RECONSTRUCTION;
    if(rr && (!f.output || !f.diffuseAlbedo || !f.specularAlbedo || !f.normalRoughness)) return false;
    for(int i=0;i<16 && rr;++i) if(!std::isfinite(f.worldToView[i]) || !std::isfinite(f.viewToClip[i])) return false;
    if(!rr && (f.diffuseAlbedo || f.specularAlbedo || f.normalRoughness)) return false;
    if(!f.renderWidth || !f.renderHeight || !f.outputWidth || !f.outputHeight) return false;
    if(f.outputWidth>16384 || f.outputHeight>16384) return false;
    if(f.renderWidth>f.outputWidth || f.renderHeight>f.outputHeight) return false; // upscaling, never down
    if(!finite_all({f.jitterX, f.jitterY, f.motionScaleX, f.motionScaleY, f.cameraNear, f.cameraFar, f.fovY,
                    f.frameTimeMs, f.sharpness, f.preExposure})) return false;
    if(std::fabs(f.jitterX)>1.f || std::fabs(f.jitterY)>1.f) return false;
    if(!(f.cameraNear>0.f) || !(f.cameraFar>f.cameraNear) || !(f.fovY>0.f) || !(f.fovY<3.1416f)) return false;
    if(f.sharpness<0.f || f.sharpness>1.f || !(f.preExposure>0.f)) return false;
    if(f.flags&~31u || !valid_mode(f.backend, f.mode)) return false;
    return true;
}
// The newest of the FSR versions AMD's runtime offers, for the log beside the one AMD chose. Versions
// compare by their numbers ("4.0.2" > "3.1.5" > "2.3.4"); a name without numbers ranks below every
// numbered one. -1 when there is none.
inline std::vector<uint32_t> version_numbers(const std::string& name) {
    std::vector<uint32_t> parts;
    size_t i=0;
    while(i<name.size() && !(name[i]>='0' && name[i]<='9')) ++i;
    while(i<name.size() && name[i]>='0' && name[i]<='9') {
        uint32_t value=0;
        while(i<name.size() && name[i]>='0' && name[i]<='9') { value=value*10u+uint32_t(name[i]-'0'); if(value>100000u) value=100000u; ++i; }
        parts.push_back(value);
        if(i+1<name.size() && name[i]=='.' && name[i+1]>='0' && name[i+1]<='9') ++i; else break;
    }
    return parts;
}
inline int newest_version(const std::vector<std::string>& names) {
    int best=-1; std::vector<uint32_t> bestParts;
    for(size_t i=0;i<names.size();++i) {
        auto parts=version_numbers(names[i]);
        if(best<0 || std::lexicographical_compare(bestParts.begin(), bestParts.end(), parts.begin(), parts.end())) {
            best=int(i); bestParts=std::move(parts);
        }
    }
    return best;
}

// HDRP jitters the projection by +taaJitter pixels; DLSS, FSR and XeSS all take the offset of the
// samples, which HDRP's own DLSS pass passes as -taaJitter.
inline void sdk_jitter(const TGUP_Frame& f, float& x, float& y) { x=-f.jitterX; y=-f.jitterY; }
// FSR sorts near and far itself; AMD's integrations pass them swapped for inverted depth.
inline void fsr_near_far(const TGUP_Frame& f, float& nearPlane, float& farPlane) {
    const bool inverted=(f.flags&TGUP_FLAG_DEPTH_INVERTED)!=0;
    nearPlane=inverted ? f.cameraFar : f.cameraNear;
    farPlane=inverted ? f.cameraNear : f.cameraFar;
}
// Frame time for the SDKs: HDRP's own delta, bounded so a hitch or a paused frame cannot feed an
// absurd value into the temporal accumulation.
inline float frame_time_ms(float ms) { return std::clamp(std::isfinite(ms) ? ms : 16.6f, 1.f, 200.f); }

// NVIDIA DLSS Super Resolution. A load of nvngx_dlss.dll by full path (drive or UNC), whatever the
// folder: next to the game, a DLSS mod's, or the driver's download. Checked on every LoadLibraryExW
// call, so it allocates nothing.
inline bool is_dlss_sr_path(const wchar_t* path) {
    if(!path) return false;
    constexpr wchar_t name[]=L"nvngx_dlss.dll";
    constexpr size_t nameLength=sizeof(name)/sizeof(name[0])-1;
    size_t length=0;
    while(path[length] && length<32768) ++length;
    if(length<nameLength+3) return false;
    const bool drive=((path[0]|0x20)>=L'a' && (path[0]|0x20)<=L'z') && path[1]==L':' && (path[2]==L'\\' || path[2]==L'/');
    const bool unc=(path[0]==L'\\' || path[0]==L'/') && (path[1]==L'\\' || path[1]==L'/');
    if(!drive && !unc) return false;
    const wchar_t* tail=path+length-nameLength;
    if(tail[-1]!=L'\\' && tail[-1]!=L'/') return false;
    for(size_t i=0;i<nameLength;++i) {
        wchar_t c=tail[i];
        if(c>=L'A' && c<=L'Z') c=wchar_t(c+32);
        if(c!=name[i]) return false;
    }
    return true;
}
// Newest wins: the bundled copy replaces only a file whose version is known and older. The game's
// own file, a newer one from a DLSS mod or the driver, or one without a version is kept.
inline bool use_bundled_dlss(uint64_t requested, uint64_t bundled) { return requested && bundled && requested<bundled; }
// A DLSS mod that installs a set of NVIDIA's DLLs is kept whole, whatever its nvngx_dlss.dll's version:
// its other DLLs, and add-ons built on them, work with the nvngx_dlss.dll it ships. A set is DLSS
// neural rendering beside it (nvngx_dlssnr.dll, which the RenoDX DLSS 5 add-on runs; with our newer
// nvngx_dlss.dll its frame rate stopped following the render resolution), or ray reconstruction or
// frame generation (nvngx_dlssd.dll, nvngx_dlssg.dll) of the same major.minor release. The game itself
// ships nvngx_dlss.dll alone.
inline bool same_dlss_release(uint64_t a, uint64_t b) { return a && b && (a>>32)==(b>>32); }
inline bool part_of_dlss_set(uint64_t requested, bool neuralRendering, std::initializer_list<uint64_t> siblings) {
    if(neuralRendering) return true;
    for(uint64_t sibling:siblings) if(same_dlss_release(requested, sibling)) return true;
    return false;
}
// A packed file version (major.minor.build.revision, 16 bits each) as text.
inline std::string file_version_text(uint64_t v) {
    return std::to_string(v>>48)+"."+std::to_string((v>>32)&0xFFFF)+"."+std::to_string((v>>16)&0xFFFF)+"."+std::to_string(v&0xFFFF);
}
} // namespace tgup
