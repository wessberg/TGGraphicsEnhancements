// SPDX-License-Identifier: MIT
// TG Path Tracing as a TG Graphics Bridge extension, and the plugin's C API. It needs the bridge's
// shared D3D12 device and the game's real D3D11 device, and never touches the game's swapchain.
#include "runtime.hpp"
#include "camera_shift.hpp"
#include "texture_share.hpp"
#include "vfx_observer.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace tgpt {
namespace {
tgb::Host* bridgeHost=nullptr;
HMODULE self=nullptr;
std::wstring root; // BepInEx/TGPathTracing/Native

// For a test run in the game alone (PT-87, PT-99; the harness's runner writes the file and takes it away again, and no
// package holds it): the lines NAME=VALUE of test-environment.txt beside this DLL are set in the process's environment
// before anything of the tracer reads it. Steam starts the game, so a run cannot hand it a variable, and the trials that
// decide at the tracer's start (TGPT_TEST_PLAIN_PASSES, TGPT_TEST_SHARE_COUNT_ONCE, TGPT_TEST_ATLAS_SIDES) are read from
// the environment before the plugin exists. Only names that begin with TGPT_TEST_ are taken; each is logged.
void apply_test_environment() {
    if(root.empty()) return;
    std::ifstream file(std::filesystem::path(root)/L"test-environment.txt");
    if(!file) return;
    std::string line;
    while(std::getline(file, line)) {
        while(!line.empty() && (line.back()=='\r' || line.back()==' ' || line.back()=='\t')) line.pop_back();
        const size_t equals=line.find('=');
        if(equals==std::string::npos || line.rfind("TGPT_TEST_", 0)!=0) continue;
        const std::string name=line.substr(0, equals), value=line.substr(equals+1);
        if(SetEnvironmentVariableA(name.c_str(), value.c_str())) log("Test environment (test-environment.txt beside the tracer's DLL: a test run's, never a player's): "+name+"="+value);
    }
}

class PathTracingExtension final : public tgb::Extension {
public:
    const char* name() noexcept override { return "TG Path Tracing"; }
    uint32_t needs() noexcept override { return tgb::NeedD3D12; }
    bool bootstrap() noexcept override {
        try {
            log("TG Path Tracing 1.2.4 extension bootstrap (TG Graphics Bridge); PID="+std::to_string(GetCurrentProcessId()));
            apply_test_environment();
            return true;
        }
        catch(...) { return false; }
    }
    void d3d12_device_ready(ID3D12Device* device, uint32_t vendor) noexcept override {
        // Diagnostic hooks must see shader creation even with PT Off. No hooks during normal play.
        try { if(vfx::requested(host())) vfx::install(host(),vfx::simulation_requested(host())); } catch(...) {}
        try { tgpt::device_ready(device, vendor); }
        catch(const std::exception& e) { log(std::string("Device check: ")+e.what()); }
        catch(...) {}
        // Inside the game's swapchain creation, before it loads a texture: the earliest the game's device is seen.
        try {
            bool supported=false;
            { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); supported=e.status.supported!=0; }
            if(supported) share::game_device_ready(host().game_device(), device);
        } catch(...) {}
    }
};

bool is_buffer(uint64_t pointer) {
    if(!pointer) return false;
    ComPtr<ID3D11Buffer> buffer;
    return SUCCEEDED(reinterpret_cast<IUnknown*>(uintptr_t(pointer))->QueryInterface(IID_PPV_ARGS(&buffer)));
}
bool valid_format(uint32_t format, uint32_t dimension) { return format<=TGPT_VF_SNORM16 && dimension>=1 && dimension<=4; }

// A detail tile from the pool (Engine::mutex held): one given back, else the next never handed out; -1 when none is left.
// For hair, none is left once only the last kDetailReserve are: see-through glass and the eyes take those (hair's tiles come
// and go with the characters, theirs stay until the scene is cleared, and a jar found none after a dozen characters).
int32_t take_detail_tile(Engine& e, bool hair) {
    const uint32_t held=e.detailTilesMade-uint32_t(e.detailTilesFree.size());
    if(held>=kMaxDetailTiles-(hair ? kDetailReserve : 0u)) return -1;
    if(!e.detailTilesFree.empty()) { const int32_t tile=e.detailTilesFree.back(); e.detailTilesFree.pop_back(); return tile; }
    return int32_t(kMaxTiles+e.detailTilesMade++);
}
// What a hair material's tile is made of: TGPT_HairColor's maps and values as its bytes (everything past size and abi).
std::string hair_content(const TGPT_HairColor& hair) {
    const auto* bytes=reinterpret_cast<const char*>(&hair);
    return std::string(bytes+offsetof(TGPT_HairColor, diffuseMap), sizeof(TGPT_HairColor)-offsetof(TGPT_HairColor, diffuseMap));
}
// A hair material lets go of the detail tile it read (Engine::mutex held). The last one gives the tile back to the pool:
// its content is forgotten, a bake still waiting for it dropped (it would overwrite whatever takes the tile next).
void release_hair_tile(Engine& e, const Engine::HairUse& use) {
    if(use.tile<int32_t(kMaxTiles)) return;
    const auto content=e.hairContents.find(use.content);
    if(content==e.hairContents.end() || content->second.tile!=use.tile) return;
    if(content->second.users>1) { --content->second.users; return; }
    e.hairContents.erase(content);
    e.newHairBakes.erase(std::remove_if(e.newHairBakes.begin(), e.newHairBakes.end(), [&](const HairBake& b) { return b.tile==use.tile; }), e.newHairBakes.end());
    e.detailTilesFree.push_back(use.tile);
    e.detailTilesGivenBack.push_back(use.tile);
}
} // namespace

tgb::Host& host() { return *bridgeHost; }
std::wstring native_root() { return root; }
Engine& engine() { static Engine* instance=new Engine(); return *instance; } // never destroyed: the render thread may outlive statics
void log(const std::string& message) {
    static std::mutex logMutex;
    try {
        std::lock_guard<std::mutex> guard(logMutex);
        const auto line="[TGPT] "+message+"\n";
        OutputDebugStringA(line.c_str());
        if(!root.empty()) {
            std::ofstream file(std::filesystem::path(root)/L"TGPathTracing.Native.log", std::ios::app);
            file<<GetTickCount64()<<" "<<line;
        }
    } catch(...) {}
}
void log_throttled(const std::string& message) {
    static std::mutex throttleMutex; static std::string last; static uint64_t lastTime=0;
    try {
        std::lock_guard<std::mutex> guard(throttleMutex);
        const uint64_t now=GetTickCount64();
        if(message==last && now-lastTime<5000) return;
        last=message; lastTime=now;
    } catch(...) { return; }
    log(message);
}
} // namespace tgpt

using namespace tgpt;

extern "C" __declspec(dllexport) tgb::Extension* __cdecl TGGraphicsBridge_CreateExtension(tgb::Host* host, uint32_t abi) {
    static PathTracingExtension* extension=nullptr;
    if(!host || abi!=TGB_EXTENSION_ABI || host->abi()!=TGB_EXTENSION_ABI) return nullptr;
    try {
        if(!extension) {
            bridgeHost=host;
            std::vector<wchar_t> buffer(MAX_PATH);
            for(;;) {
                const DWORD length=GetModuleFileNameW(self, buffer.data(), DWORD(buffer.size()));
                if(length && length<buffer.size()) { root=std::filesystem::path(std::wstring(buffer.data(), length)).parent_path().wstring(); break; }
                if(!length || buffer.size()>=32768) break;
                buffer.resize(buffer.size()*2);
            }
            extension=new PathTracingExtension();
        }
    } catch(...) { return nullptr; }
    return extension;
}

TGPT_EXPORT uint32_t TGPT_CALL TGPT_GetAbiVersion() { return TGPT_ABI; }
TGPT_EXPORT int32_t TGPT_CALL TGPT_ReadStatus(TGPT_Status* out) {
    // The record grew at its end (PT-98): a plugin from before asks with the size it knows and gets that much of it.
    if(!out || (out->size!=sizeof(TGPT_Status) && out->size!=TGPT_STATUS_SIZE_BEFORE_ARRIVAL) || out->abi!=TGPT_ABI) return -1;
    try {
        const uint32_t size=out->size;
        { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); std::memcpy(out, &e.status, size); }
        out->size=size;
        // Four times a second, path tracing on or off: textures the game freed let go of on the D3D12 side too (PT-45).
        share::collect();
        return 0;
    } catch(...) { return -2; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetYardstick(TGPT_Yardstick* yardstick) {
    try {
        if(!yardstick || yardstick->size!=sizeof(TGPT_Yardstick) || yardstick->abi!=TGPT_ABI) return -1;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        *yardstick=e.yardstick;
        return 0;
    } catch(...) { return -2; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetWalkPixels(uint32_t* frameIndex, uint32_t* onSubsurface, uint32_t* walked) {
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(frameIndex) *frameIndex=e.walkPixels[0];
        if(onSubsurface) *onSubsurface=e.walkPixels[1];
        if(walked) *walked=e.walkPixels[2];
        return 0;
    } catch(...) { return -2; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetStageTimes(char* buffer, uint32_t size) {
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(buffer && size) { const size_t n=std::min<size_t>(e.stageTimes.size(), size-1); std::memcpy(buffer, e.stageTimes.data(), n); buffer[n]=0; }
        return int32_t(e.stageTimes.size());
    } catch(...) { return -2; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetUnsupportedReason(char* buffer, uint32_t size) {
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(buffer && size) { const size_t n=std::min<size_t>(e.unsupported.size(), size-1); std::memcpy(buffer, e.unsupported.data(), n); buffer[n]=0; }
        return int32_t(e.unsupported.size());
    } catch(...) { return -2; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterMesh(const TGPT_MeshDesc* d) {
    if(!d || d->size!=sizeof(TGPT_MeshDesc) || d->abi!=TGPT_ABI) return -1;
    try {
        if(!d->vertexCount || !d->submeshCount || !d->submeshes || d->positionStream>3) return -2;
        if(d->indexFormat!=16 && d->indexFormat!=32) return -3;
        if(!valid_format(d->positionFormat, d->positionDimension) || d->positionDimension<3) return -4;
        if(!is_buffer(d->vertexBuffers[d->positionStream]) || !is_buffer(d->indexBuffer)) return -5;
        auto source=std::make_unique<MeshSource>();
        source->key=d->key; source->decalAttributes=(d->reserved&1u)!=0;
        for(int i=0;i<4;++i) {
            if(d->vertexBuffers[i] && !is_buffer(d->vertexBuffers[i])) return -5;
            source->vertexBuffers[size_t(i)]=reinterpret_cast<ID3D11Buffer*>(uintptr_t(d->vertexBuffers[i])); // referenced here
            source->strides[size_t(i)]=d->strides[i];
        }
        source->positionStream=d->positionStream; source->positionOffset=d->positionOffset;
        source->positionFormat=d->positionFormat; source->positionDimension=d->positionDimension;
        if(d->uvStream<4 && source->vertexBuffers[d->uvStream] && valid_format(d->uvFormat, d->uvDimension) && d->uvDimension>=2) {
            source->uvStream=d->uvStream; source->uvOffset=d->uvOffset; source->uvFormat=d->uvFormat; source->uvDimension=d->uvDimension;
        }
        if(d->colorStream<4 && source->vertexBuffers[d->colorStream] && valid_format(d->colorFormat, d->colorDimension) && d->colorDimension>=3) {
            source->colorStream=d->colorStream; source->colorOffset=d->colorOffset; source->colorFormat=d->colorFormat; source->colorDimension=d->colorDimension;
        }
        if(d->normalStream<4 && source->vertexBuffers[d->normalStream] && valid_format(d->normalFormat, d->normalDimension) && d->normalDimension>=3) {
            source->normalStream=d->normalStream; source->normalOffset=d->normalOffset; source->normalFormat=d->normalFormat; source->normalDimension=d->normalDimension;
        }
        if(d->uv2Stream<4 && source->vertexBuffers[d->uv2Stream] && valid_format(d->uv2Format, d->uv2Dimension) && d->uv2Dimension>=2) {
            source->uv2Stream=d->uv2Stream; source->uv2Offset=d->uv2Offset; source->uv2Format=d->uv2Format; source->uv2Dimension=d->uv2Dimension;
        }
        if(d->tangentStream<4 && source->vertexBuffers[d->tangentStream] && valid_format(d->tangentFormat, d->tangentDimension) && d->tangentDimension==4) {
            source->tangentStream=d->tangentStream; source->tangentOffset=d->tangentOffset; source->tangentFormat=d->tangentFormat; source->tangentDimension=d->tangentDimension;
        }
        source->indexBuffer=reinterpret_cast<ID3D11Buffer*>(uintptr_t(d->indexBuffer));
        source->indexFormat=d->indexFormat; source->vertexCount=d->vertexCount;
        source->submeshes.assign(d->submeshes, d->submeshes+d->submeshCount);
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        const int32_t handle=e.nextMesh++;
        e.newMeshes.emplace_back(handle, std::move(source));
        ++e.status.meshesRegistered;
        return handle;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterCpuMesh(const TGPT_CpuMeshDesc* d) {
    if(!d || d->size!=sizeof(TGPT_CpuMeshDesc) || d->abi!=TGPT_ABI) return -1;
    try {
        if(!d->positions || !d->indices || !d->vertexCount || d->indexCount<3 || d->indexCount%3) return -2;
        for(uint32_t i=0;i<d->indexCount;++i) if(d->indices[i]>=d->vertexCount) return -3;
        auto source=std::make_unique<MeshSource>();
        source->key=d->key; source->cpu=true; source->vertexCount=d->vertexCount;
        source->positions.assign(d->positions, d->positions+size_t(d->vertexCount)*3);
        if(d->uvs) source->uvs.assign(d->uvs, d->uvs+size_t(d->vertexCount)*2);
        if(d->colors) source->colors.assign(d->colors, d->colors+d->vertexCount);
        source->indices.assign(d->indices, d->indices+d->indexCount);
        source->submeshes.push_back(TGPT_Submesh{0, d->indexCount, 0, 0});
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        const int32_t handle=e.nextMesh++;
        e.newMeshes.emplace_back(handle, std::move(source));
        ++e.status.meshesRegistered;
        return handle;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterSkinnedMesh(const TGPT_CpuMeshDesc* d, const uint32_t* submeshStarts, uint32_t submeshCount) {
    if(!d || d->size!=sizeof(TGPT_CpuMeshDesc) || d->abi!=TGPT_ABI) return -1;
    try {
        if(!d->indices || !d->vertexCount || d->indexCount<3 || d->indexCount%3) return -2;
        for(uint32_t i=0;i<d->indexCount;++i) if(d->indices[i]>=d->vertexCount) return -3;
        if(submeshCount>TGPT_SKINNED_MATERIALS || (submeshCount && !submeshStarts)) return -4;
        auto source=std::make_unique<MeshSource>();
        source->key=d->key; source->cpu=true; source->skinned=true; source->vertexCount=d->vertexCount;
        if(d->positions) source->positions.assign(d->positions, d->positions+size_t(d->vertexCount)*3);
        else source->positions.assign(size_t(d->vertexCount)*3, 0.f);
        if(d->uvs) source->uvs.assign(d->uvs, d->uvs+size_t(d->vertexCount)*2);
        source->indices.assign(d->indices, d->indices+d->indexCount);
        // Submeshes (one material each) as consecutive index ranges; none given: one.
        const uint32_t n=std::max(submeshCount, 1u);
        for(uint32_t s=0;s<n;++s) {
            const uint32_t start=submeshCount ? submeshStarts[s] : 0, end=s+1<submeshCount ? submeshStarts[s+1] : d->indexCount;
            if(start>end || end>d->indexCount || (end-start)%3) return -5;
            source->submeshes.push_back(TGPT_Submesh{start, end-start, 0, 0});
        }
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        const int32_t handle=e.nextMesh++;
        e.newMeshes.emplace_back(handle, std::move(source));
        ++e.status.meshesRegistered;
        return handle;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSkinned(uint64_t buffer, uint32_t stride, const TGPT_SkinnedInstance* instances, uint32_t count) {
    // A refused list leaves no characters rather than the last one, whose vertex offsets may no longer hold.
    auto refuse=[](int32_t code) {
        try { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.skinSource.Reset(); e.skinStride=0; e.skinned.clear(); } catch(...) {}
        return code;
    };
    try {
        if(count && (!buffer || !instances)) return refuse(-1);
        // ON TRIAL (PT-92): where a vertex holds its skinned normal, in the stride's bits 16-23 (kept with it in skinStride).
        const uint32_t normalAt=(stride>>TGPT_SKINNED_NORMAL_SHIFT)&0xFFu, bytes=stride&TGPT_SKINNED_STRIDE_MASK;
        if(count && (stride>>24 || bytes<12 || bytes%4 || bytes>256)) return refuse(-2);
        if(count && normalAt && (normalAt<12 || normalAt%4 || normalAt+4>bytes)) return refuse(-2);
        ComPtr<ID3D11Buffer> source;
        if(count && FAILED(reinterpret_cast<IUnknown*>(uintptr_t(buffer))->QueryInterface(IID_PPV_ARGS(&source)))) return refuse(-3);
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.skinSource=source; e.skinStride=count ? stride : 0;
        e.skinned.assign(instances, instances+std::min(count, TGPT_MAX_SKINNED));
        return 0;
    } catch(...) { return refuse(-10); }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetClothWind(float time, const TGPT_ClothWind* winds, uint32_t count) {
    try {
        if(count && !winds) return -1;
        if(!std::isfinite(time)) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.windTime=time;
        e.winds.clear();
        for(uint32_t i=0;i<std::min(count, TGPT_MAX_CLOTH_WIND);++i) {
            const auto& w=winds[i];
            const float values[8]={w.speed, w.wind, w.noiseScale, w.intensity[0], w.intensity[1], w.intensity[2], w.direction[0], w.direction[1]};
            if(std::all_of(std::begin(values), std::end(values), [](float v) { return std::isfinite(v); })) e.winds.push_back(w);
        }
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetPlantWind(const TGPT_PlantWindFrame* frame, const TGPT_PlantWind* winds, uint32_t count) {
    try {
        if(!frame || (count && !winds)) return -1;
        const float shared[6]={frame->time, frame->speed, frame->direction, frame->forward[0], frame->forward[1], frame->forward[2]};
        if(!std::all_of(std::begin(shared), std::end(shared), [](float v) { return std::isfinite(v); })) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.plantFrame=*frame;
        e.plantWinds.clear();
        for(uint32_t i=0;i<std::min(count, TGPT_MAX_PLANT_WIND);++i) {
            const auto& w=winds[i];
            const float values[5]={w.strength, w.bendHeight, w.pivotOffsetY, w.fade[0], w.fade[1]};
            if((w.kind==TGPT_PLANT_GRASS || w.kind==TGPT_PLANT_LEAVES) && std::all_of(std::begin(values), std::end(values), [](float v) { return std::isfinite(v); }))
                e.plantWinds.push_back(w);
        }
        return 0;
    } catch(...) { return -10; }
}
// PT-112: the game's fog boxes for the frames submitted from now on (tgpt_api.h TGPT_FogBox). A record with a number that
// is not finite, or no size, is left out.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetFogBoxes(const TGPT_FogBox* boxes, uint32_t count) {
    try {
        if(count && !boxes) return -1;
        std::vector<TGPT_FogBox> next;
        next.reserve(count);
        for(uint32_t i=0;i<count;++i) {
            const TGPT_FogBox& b=boxes[i];
            const float* v=reinterpret_cast<const float*>(&b);
            bool finite=true;
            for(size_t k=0;k<sizeof(TGPT_FogBox)/4;++k) if(k!=15 && k!=19 && !std::isfinite(v[k])) finite=false; // (flags and mask are ints)
            if(!finite || !(b.extinction>0.f) || !(b.extents[0]>0.f && b.extents[1]>0.f && b.extents[2]>0.f) || !(b.maskMost>0.f)) continue;
            next.push_back(b);
        }
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.fogBoxes.swap(next);
        return 0;
    } catch(...) { return -10; }
}
// PT-108, on trial (docs/agents/rain-1.md): the rain's wetness for the frames submitted from now on. Numbers that are not
// finite, or out of what the game's own pass can hold, are dry.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetRain(const TGPT_Rain* rain) {
    try {
        TGPT_Rain next{};
        if(rain) {
            if(rain->size!=sizeof(TGPT_Rain) || rain->abi!=TGPT_ABI) return -1;
            const float values[3]={rain->moisture, rain->soaked, rain->screenNoise};
            if(!std::all_of(std::begin(values), std::end(values), [](float v) { return std::isfinite(v); })) return -2;
            next=*rain;
            next.moisture=std::clamp(next.moisture, 0.f, 1.f); next.soaked=std::clamp(next.soaked, 0.f, 1.f); next.screenNoise=std::clamp(next.screenNoise, 0.f, 1.f);
        }
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.rain=next;
        return 0;
    } catch(...) { return -10; }
}
// PT-112, the sky, on trial (docs/agents/sky-2.md): the sky for the frames submitted from now on. A record with a number that is
// not finite, or a mode the tracer does not know, is HDRP's sky (mode 0).
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSkyModel(const TGPT_Sky* sky) {
    try {
        TGPT_Sky next{};
        if(sky) {
            if(sky->size!=sizeof(TGPT_Sky) || sky->abi!=TGPT_ABI) return -1;
            next=*sky;
            const float* v=reinterpret_cast<const float*>(&next);
            // Every float of the record is finite (its integers are skipped by their places: the header, the bodies' flags and
            // textures, the layers' maps, distortion, flowmaps and switches, the size at the end).
            auto integer=[](size_t word) {
                if(word<4) return true;                                   // size, abi, mode, flags
                if(word==41 || word==42 || word==43) return true;         // cloudsHash, bodyCount, reserved0
                if(word>=44 && word<44+4*28) { const size_t w=(word-44)%28; return w==7 || w==23; } // a body's flags and texture
                if(word>=160 && word<200) { const size_t w=(word-160)%20; return w==11 || w==15 || w==17 || w==18; } // a layer's map, distortion, flowmap, on
                return word>=200;                                         // cloudWidth, cloudHeight, reserved1
            };
            bool finite=true;
            for(size_t k=0;k<sizeof(TGPT_Sky)/4;++k) if(!integer(k) && !std::isfinite(v[k])) finite=false;
            if(!finite || next.mode>TGPT_SKY_OWN || !(next.planetRadius>1000.f) || !(next.atmosphereDepth>0.f) ||
               !(next.airScaleHeight>0.f) || !(next.aerosolScaleHeight>0.f)) next=TGPT_Sky{};
            next.bodyCount=std::min(next.bodyCount, TGPT_SKY_MAX_BODIES);
            next.cloudWidth=std::min(next.cloudWidth, 8192u); next.cloudHeight=std::min(next.cloudHeight, 8192u);
            next.size=sizeof(TGPT_Sky); next.abi=TGPT_ABI;
        }
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.skyModel=next;
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetWindNoise(uint32_t key, uint32_t width, uint32_t height, uint32_t flags, const uint8_t* texels) {
    try {
        if(!key || !texels || !width || !height || width>4096 || height>4096) return -1;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        // A new set with this map in it (frames already submitted keep the set they were given). A key sent again takes the
        // place of its map when the size is the same, a new place otherwise.
        auto next=std::make_shared<WindNoise>();
        if(e.windNoise) *next=*e.windNoise;
        const size_t bytes=size_t(width)*height;
        auto found=next->maps.find(key);
        if(found==next->maps.end() || found->second.width!=width || found->second.height!=height) {
            WindNoise::Map map; map.offset=uint32_t((next->bytes.size()+3)/4*4); map.width=width; map.height=height;
            next->bytes.resize(size_t(map.offset)+bytes, 0);
            found=next->maps.insert_or_assign(key, map).first;
        }
        found->second.flags=flags;
        std::memcpy(next->bytes.data()+found->second.offset, texels, bytes);
        e.windNoise=std::move(next);
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterTexture(uint64_t key, uint64_t texture) {
    if(!texture) return -1;
    try {
        ComPtr<ID3D11Texture2D> t2d;
        if(FAILED(reinterpret_cast<IUnknown*>(uintptr_t(texture))->QueryInterface(IID_PPV_ARGS(&t2d)))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        const auto found=e.textureTiles.find(key);
        if(found!=e.textureTiles.end()) return found->second;
        if(e.textureTiles.size()>=kMaxTiles) return -3;
        const int32_t tile=int32_t(e.textureTiles.size());
        e.textureTiles.emplace(key, tile);
        TextureBake bake; bake.tile=tile; bake.key=key; bake.texture=t2d; // referenced until baked
        e.newTextures.push_back(std::move(bake));
        e.status.textures=uint32_t(e.textureTiles.size());
        return tile;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterDetailTexture(uint64_t key, uint64_t texture) {
    if(!texture) return -1;
    try {
        ComPtr<ID3D11Texture2D> t2d;
        if(FAILED(reinterpret_cast<IUnknown*>(uintptr_t(texture))->QueryInterface(IID_PPV_ARGS(&t2d)))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        const auto found=e.detailTiles.find(key);
        if(found!=e.detailTiles.end()) return found->second;
        const int32_t tile=take_detail_tile(e, false);
        if(tile<0) return -3;
        e.detailTiles.emplace(key, tile);
        TextureBake bake; bake.tile=tile; bake.key=key; bake.texture=t2d; // referenced until baked
        e.newTextures.push_back(std::move(bake));
        return tile;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterAlphaTexture(uint64_t key, uint64_t texture, float cutoff) {
    if(!texture) return -1;
    try {
        ComPtr<ID3D11Texture2D> t2d;
        if(FAILED(reinterpret_cast<IUnknown*>(uintptr_t(texture))->QueryInterface(IID_PPV_ARGS(&t2d)))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        const auto found=e.alphaTiles.find(key);
        if(found!=e.alphaTiles.end()) return found->second;
        if(e.alphaTiles.size()>=kMaxAlphaTiles) return -3;
        const int32_t tile=int32_t(e.alphaTiles.size());
        e.alphaTiles.emplace(key, tile);
        TextureBake bake; bake.tile=tile; bake.key=key; bake.texture=t2d; // referenced until baked
        bake.cutoff=std::isfinite(cutoff) ? cutoff : 0.5f;
        e.newAlphaTextures.push_back(std::move(bake));
        return tile;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterMaskTexture(uint64_t key, uint64_t texture, uint32_t channels) {
    if(!texture) return -1;
    try {
        ComPtr<ID3D11Texture2D> t2d;
        if(FAILED(reinterpret_cast<IUnknown*>(uintptr_t(texture))->QueryInterface(IID_PPV_ARGS(&t2d)))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        // The color tiles' atlas under keys of their own (a texture can be one material's color and another's mask), one a
        // texture and channels.
        const uint64_t own=(key^(uint64_t(channels&0xFu)<<59))|(1ull<<63);
        const auto found=e.textureTiles.find(own);
        if(found!=e.textureTiles.end()) return found->second;
        // Color first: a material without its mask map keeps its constants, one without its texture loses its colors.
        if(e.textureTiles.size()>=kMaxTiles/4*3) return -3;
        const int32_t tile=int32_t(e.textureTiles.size());
        e.textureTiles.emplace(own, tile);
        TextureBake bake; bake.tile=tile; bake.key=own; bake.texture=t2d; bake.mask=true; bake.channels=channels&0xFu; // referenced until baked
        e.newTextures.push_back(std::move(bake));
        e.status.textures=uint32_t(e.textureTiles.size());
        return tile;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterHairColor(uint64_t key, const TGPT_HairColor* hair) {
    if(!hair || hair->size!=sizeof(TGPT_HairColor) || hair->abi!=TGPT_ABI) return -1;
    try {
        HairBake bake; bake.desc=*hair;
        const uint64_t maps[5]={hair->diffuseMap, hair->rootMap, hair->idMap, hair->maskMap, hair->blendMap};
        for(int i=0;i<5;++i)
            if(maps[i] && FAILED(reinterpret_cast<IUnknown*>(uintptr_t(maps[i]))->QueryInterface(IID_PPV_ARGS(&bake.maps[i])))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        // Under keys of their own (bit 62: a hair material's; mask maps' have bit 63): a detail tile while hair may take one
        // (take_detail_tile), as the camera sees hair's color from its tile (a 64x64 one blurred every strand into flat clumps,
        // 2026-09-28), else a 64x64 tile, which it then keeps. A detail tile is shared by every hair material made of the same
        // maps and values (the game clones a hair material for each character: a tile each, a dozen characters took all 32).
        // The same key again (the game colored the hair since): its tile baked again while no other material reads it, else
        // the tile that holds the new colors, or a tile of its own; the tile it leaves goes back to the pool with its last
        // material. Where none is free then, it keeps the tile it had.
        const uint64_t own=(key&~(3ull<<62))|(1ull<<62);
        const std::string content=hair_content(*hair);
        int32_t tile=-1;
        bool baked=false; // its tile holds these colors already, or will from a bake that is waiting
        const auto known=e.hairKeys.find(own);
        const auto shared=e.hairContents.find(content);
        if(known!=e.hairKeys.end() && known->second.tile<int32_t(kMaxTiles)) tile=known->second.tile; // its 64x64 tile, baked again
        else if(known!=e.hairKeys.end() && known->second.content==content) { tile=known->second.tile; baked=true; }
        else if(known!=e.hairKeys.end()) {
            auto& use=known->second;
            const auto old=e.hairContents.find(use.content);
            if(shared!=e.hairContents.end()) {
                release_hair_tile(e, use);
                tile=shared->second.tile; ++shared->second.users; baked=true;
            } else if(old!=e.hairContents.end() && old->second.users==1) {
                // Its own tile, baked again in place.
                tile=use.tile;
                e.hairContents.erase(old);
                e.hairContents[content]=Engine::HairContent{tile, 1};
            } else if(const int32_t fresh=take_detail_tile(e, true); fresh>=0) {
                release_hair_tile(e, use);
                tile=fresh;
                e.hairContents[content]=Engine::HairContent{tile, 1};
            } else return use.tile; // no tile free for the new colors: the old ones stay
            use=Engine::HairUse{tile, content};
        } else if(shared!=e.hairContents.end()) {
            tile=shared->second.tile; ++shared->second.users; baked=true;
            e.hairKeys[own]=Engine::HairUse{tile, content};
        } else if(const int32_t fresh=take_detail_tile(e, true); fresh>=0) {
            tile=fresh;
            e.hairContents[content]=Engine::HairContent{tile, 1};
            e.hairKeys[own]=Engine::HairUse{tile, content};
        } else {
            if(const auto found=e.textureTiles.find(own); found!=e.textureTiles.end()) tile=found->second;
            else {
                if(e.textureTiles.size()>=kMaxTiles) return -3;
                tile=int32_t(e.textureTiles.size());
                e.textureTiles.emplace(own, tile);
                e.status.textures=uint32_t(e.textureTiles.size());
            }
            e.hairKeys[own]=Engine::HairUse{tile, std::string()};
        }
        if(baked) return tile;
        bake.tile=tile;
        // A bake still waiting for this tile takes the newer colors.
        for(auto& waiting:e.newHairBakes) if(waiting.tile==tile) { waiting=std::move(bake); return tile; }
        e.newHairBakes.push_back(std::move(bake));
        return tile;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_ReleaseHairColor(uint64_t key) {
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        const uint64_t own=(key&~(3ull<<62))|(1ull<<62);
        const auto known=e.hairKeys.find(own);
        if(known==e.hairKeys.end()) return 0;
        const size_t free=e.detailTilesFree.size();
        release_hair_tile(e, known->second);
        e.hairKeys.erase(known);
        return e.detailTilesFree.size()>free ? 1 : 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterHairLobes(uint64_t key, const TGPT_HairLobes* lobes) {
    if(!lobes || lobes->size!=sizeof(TGPT_HairLobes) || lobes->abi!=TGPT_ABI) return -1;
    if(!std::isfinite(lobes->smoothnessPower) || !std::isfinite(lobes->flowFlipGreen)) return -1;
    try {
        HairLobeBake bake; bake.desc=*lobes;
        const uint64_t maps[3]={lobes->flowMap, lobes->maskMap, lobes->specularMap};
        for(int i=0;i<3;++i)
            if(maps[i] && FAILED(reinterpret_cast<IUnknown*>(uintptr_t(maps[i]))->QueryInterface(IID_PPV_ARGS(&bake.maps[i])))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        // A 64x64 color tile under a key of its own (bit 61 alone of the top three: hair colors have 62, eyes 61 and 62, mask
        // maps 63), read texel by texel (pt_common hair_lobe). The same key again bakes it again.
        const uint64_t own=(key&~(7ull<<61))|(1ull<<61);
        int32_t tile;
        if(const auto found=e.textureTiles.find(own); found!=e.textureTiles.end()) tile=found->second;
        else {
            if(e.textureTiles.size()>=kMaxTiles) return -3;
            tile=int32_t(e.textureTiles.size());
            e.textureTiles.emplace(own, tile);
            e.status.textures=uint32_t(e.textureTiles.size());
        }
        bake.tile=tile;
        for(auto& waiting:e.newHairLobeBakes) if(waiting.tile==tile) { waiting=std::move(bake); return tile; }
        e.newHairLobeBakes.push_back(std::move(bake));
        return tile;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterEyeColor(uint64_t key, const TGPT_EyeColor* eye) {
    if(!eye || eye->size!=sizeof(TGPT_EyeColor) || eye->abi!=TGPT_ABI) return -1;
    try {
        EyeBake bake; bake.desc=*eye;
        const uint64_t maps[2]={eye->scleraMap, eye->irisMap};
        for(int i=0;i<2;++i)
            if(maps[i] && FAILED(reinterpret_cast<IUnknown*>(uintptr_t(maps[i]))->QueryInterface(IID_PPV_ARGS(&bake.maps[i])))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        // Under keys of their own (bits 61 and 62): a detail tile while there are any, else a 64x64 tile.
        const uint64_t own=(key&~(7ull<<61))|(3ull<<61);
        const auto detail=e.detailTiles.find(own);
        if(detail!=e.detailTiles.end()) return detail->second;
        const auto color=e.textureTiles.find(own);
        if(color!=e.textureTiles.end()) return color->second;
        int32_t tile=take_detail_tile(e, false);
        if(tile>=0) e.detailTiles.emplace(own, tile);
        else if(e.textureTiles.size()<kMaxTiles) { tile=int32_t(e.textureTiles.size()); e.textureTiles.emplace(own, tile); e.status.textures=uint32_t(e.textureTiles.size()); }
        else return -3;
        bake.tile=tile;
        e.newEyeBakes.push_back(std::move(bake));
        return tile;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterCookie(uint64_t key, uint64_t texture) {
    if(!texture) return -1;
    try {
        ComPtr<ID3D11Texture2D> t2d;
        if(FAILED(reinterpret_cast<IUnknown*>(uintptr_t(texture))->QueryInterface(IID_PPV_ARGS(&t2d)))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported) return -6;
        const auto found=e.cookieTiles.find(key);
        if(found!=e.cookieTiles.end()) return found->second;
        if(e.cookieTiles.size()>=kMaxCookies) return -3;
        const int32_t tile=int32_t(e.cookieTiles.size());
        e.cookieTiles.emplace(key, tile);
        TextureBake bake; bake.tile=tile; bake.key=key; bake.texture=t2d; // referenced until baked
        e.newCookies.push_back(std::move(bake));
        return tile;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetMaterials(uint32_t first, const TGPT_Material* materials, uint32_t count) {
    if(!materials && count) return -1;
    if(first>(1u<<20) || count>(1u<<20)) return -2;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(e.materials.size()<size_t(first)+count) e.materials.resize(size_t(first)+count, TGPT_Material{});
        std::copy(materials, materials+count, e.materials.begin()+first);
        e.materialsDirty=true; e.status.materials=uint32_t(e.materials.size());
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetMaterialPrograms(uint32_t first, const uint32_t* rows, uint32_t count) {
    if(!rows && count) return -1;
    if(first>TGPT_PROGRAM_MAX_ROWS || count>TGPT_PROGRAM_MAX_ROWS || first+count>TGPT_PROGRAM_MAX_ROWS) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!first && !count) e.programRows.clear();
        else {
            // Rows never sent read as a record of no program (its first word no TGPT_PROGRAM_*).
            if(e.programRows.size()<(size_t(first)+count)*4) e.programRows.resize((size_t(first)+count)*4, 0xFFFFFFFFu);
            std::copy(rows, rows+size_t(count)*4, e.programRows.begin()+size_t(first)*4);
        }
        e.programsDirty=true;
        return 0;
    } catch(...) { return -10; }
}

TGPT_EXPORT int32_t TGPT_CALL TGPT_SetDecals(const TGPT_Decal* decals, uint32_t count) {
    if(!decals && count) return -1;
    try {
        auto& e=tgpt::engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(e.decals.size()==count && (!count || std::memcmp(e.decals.data(),decals,size_t(count)*sizeof(TGPT_Decal))==0)) return 0;
        if(count) e.decals.assign(decals, decals+count); else e.decals.clear();
        e.decalsDirty=true;
        return 0;
    } catch(...) { return -2; }
}

TGPT_EXPORT int32_t TGPT_CALL TGPT_SetScene(const TGPT_Instance* instances, uint32_t count, const uint32_t* materialIndices, uint32_t materialIndexCount) {
    if((!instances && count) || (!materialIndices && materialIndexCount)) return -1;
    if(count>kMaxInstances || materialIndexCount>(1u<<24)) return -2;
    try {
        auto list=std::make_shared<SceneList>();
        list->instances.assign(instances, instances+count);
        list->materialIndices.assign(materialIndices, materialIndices+materialIndexCount);
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.scene=std::move(list); e.status.instances=count;
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetMoving(const TGPT_Moving* things, uint32_t count, const uint32_t* materialIndices, uint32_t materialIndexCount,
                                             const uint32_t* replaced, uint32_t replacedCount) {
    if((!things && count) || (!materialIndices && materialIndexCount) || (!replaced && replacedCount)) return -1;
    if(count>kMaxInstances || replacedCount>kMaxInstances || materialIndexCount>(1u<<24)) return -2;
    try {
        std::shared_ptr<MovingList> list;
        if(count || replacedCount) {
            list=std::make_shared<MovingList>();
            list->things.assign(things, things+count);
            list->materialIndices.assign(materialIndices, materialIndices+materialIndexCount);
            list->replaced.assign(replaced, replaced+replacedCount);
        }
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.moving=std::move(list);
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetLights(const TGPT_Light* lights, uint32_t count) {
    if(!lights && count) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.lights.assign(lights, lights+std::min(count, kMaxLights));
        e.lightsDirty=true; e.status.lights=uint32_t(e.lights.size());
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSky(uint64_t texture) {
    try {
        ComPtr<ID3D11Resource> resource;
        if(texture && FAILED(reinterpret_cast<IUnknown*>(uintptr_t(texture))->QueryInterface(IID_PPV_ARGS(&resource)))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!tgb::same_object(e.sky.Get(), resource.Get())) { e.sky=resource; ++e.skyVersion; }
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_ReadDiagnostics(TGPT_Diagnostics* diagnostics) {
    if(!diagnostics || diagnostics->size!=sizeof(TGPT_Diagnostics) || diagnostics->abi!=TGPT_ABI) return -1;
    auto& e=engine(); { std::lock_guard<std::mutex> lock(e.mutex); *diagnostics=e.diagnostics; }
    shader_progress(*diagnostics); return 0;
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_ReleaseMesh(int32_t handle) {
    if(handle<=0) return -1;
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    e.retiredMeshes.push_back(handle); return 0;
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_ClearScene() {
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.retiredMeshes.clear(); e.newMeshes.clear(); e.textureTiles.clear(); e.detailTiles.clear(); e.newTextures.clear(); e.newHairBakes.clear(); e.newHairLobeBakes.clear(); e.newEyeBakes.clear(); e.alphaTiles.clear(); e.newAlphaTextures.clear();
        e.detailTilesMade=0; e.detailTilesFree.clear(); e.detailTilesGivenBack.clear(); e.hairContents.clear(); e.hairKeys.clear(); // the pool starts over
        e.materials.clear(); e.scene=std::make_shared<const SceneList>(); e.lights.clear();
        e.moving.reset(); // their meshes and materials went with the scene
        e.programRows.clear(); e.programsDirty=true; // their textures went with the scene
        e.decals.clear(); e.decalsDirty=true;
        e.skinSource.Reset(); e.skinStride=0; e.skinned.clear(); // their meshes went with the scene
        e.winds.clear(); // their materials went with the scene
        e.plantWinds.clear(); e.windNoise.reset();
        e.rain=TGPT_Rain{}; // its noise texture's handle went with the scene: dry until the plugin's next TGPT_SetRain
        e.fogBoxes.clear(); // their masks' handles went with the scene
        e.skyModel=TGPT_Sky{}; // its textures' handles went with the scene: HDRP's sky until the plugin's next TGPT_SetSkyModel
        e.textureSwaps.clear(); // the map records went with the scene
        e.materialsDirty=e.lightsDirty=true;
        e.status.textures=e.status.materials=e.status.instances=e.status.lights=0;
        ++e.clearGeneration;
    } catch(...) { return -10; }
    try { share::clear(); } catch(...) { return -10; } // outside the engine's lock
    return 0;
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SubmitFrame(const TGPT_Frame* f) {
    if(!f || f->size!=sizeof(TGPT_Frame) || f->abi!=TGPT_ABI) return -1;
    if(!f->gbuffer0 || !f->normal || !f->depth || !f->renderWidth || !f->renderHeight || f->renderWidth>16384 || f->renderHeight>16384) return -2;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported || e.status.disabled) return -6;
        auto& slot=e.frames[f->sequence%e.frames.size()];
        slot=FrameSlot{}; slot.sequence=f->sequence; slot.occupied=true; slot.frame=*f;
        // The camera's rays carry the sub-pixel shift the upscaler is told (camera_shift.hpp, PT-84): said once a session
        // for each way the matrices came.
        {
            const CameraShiftCheck shift=ensure_camera_shift(slot.frame);
            static bool said[4]{};
            const int state=int(shift.state);
            if(shift.state!=CameraShift::None && !said[state]) {
                said[state]=true;
                char at[160];
                std::snprintf(at, sizeof at, "%.3f, %.3f", double(f->jitter[0]), double(f->jitter[1]));
                if(shift.state==CameraShift::Held) log(std::string("The camera's rays carry HDRP's sub-pixel shift (this frame's: ")+at+" render pixels), as its depth and its upscaler do");
                else if(shift.state==CameraShift::Applied) log(std::string("HDRP's camera matrices came without the frame's sub-pixel shift (")+at+" render pixels): the tracer put it into its camera rays itself");
                else {
                    char held[160];
                    std::snprintf(held, sizeof held, "%.3f, %.3f", double(shift.held[0]), double(shift.held[1]));
                    log(std::string("HDRP's camera matrices hold a sub-pixel shift of ")+held+" render pixels where the frame says "+at+": the camera's rays follow the matrices");
                }
            }
        }
        slot.skinSource=e.skinSource; slot.skinStride=e.skinStride; slot.skinned=e.skinned;
        slot.windTime=e.windTime; slot.winds=e.winds;
        slot.plantFrame=e.plantFrame; slot.plantWinds=e.plantWinds; slot.windNoise=e.windNoise;
        slot.rain=e.rain;
        slot.fogBoxes=e.fogBoxes;
        slot.skyModel=e.skyModel;
        slot.scene=e.scene; slot.moving=e.moving;
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetComposite(uint32_t sequence, uint64_t color) {
    if(!color) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& slot=e.frames[sequence%e.frames.size()];
        if(!slot.occupied || slot.sequence!=sequence) return -2;
        slot.color=color;
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetDepthPyramid(uint32_t sequence, uint64_t pyramid) {
    if(!pyramid) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& slot=e.frames[sequence%e.frames.size()];
        if(!slot.occupied || slot.sequence!=sequence) return -2;
        slot.pyramid=pyramid;
        return 0;
    } catch(...) { return -10; }
}
static void __stdcall observed_render_event(int eventId, void* data) {
    if(eventId==1) vfx::trace_marker();
    tgpt::render_event(eventId, data);
}
TGPT_EXPORT void* TGPT_CALL TGPT_GetRenderEvent() { return reinterpret_cast<void*>(&observed_render_event); }
// Additive diagnostic exports; no production ABI struct, version or option bit changes.
// Diagnostic control is independent of TGPT_Frame: it must work while Mode is Off. No option bit.
TGPT_EXPORT int32_t TGPT_CALL TGPT_VfxSimulationSetEnabled(uint32_t enabled) {
    if(!vfx::state().installed || !vfx::sim::data().armed) return enabled ? -1:0;
    vfx::sim::data().enabled=enabled!=0;
    if(!enabled) {
        auto& data=vfx::sim::data(); std::lock_guard<std::mutex> lock(data.mutex);
        data.latest.clear(); data.current.clear();
    }
    return 0;
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_VfxDiagnosticConfigure(const wchar_t* directory) { return vfx::configure(directory); }
TGPT_EXPORT void* TGPT_CALL TGPT_VfxDiagnosticGetRenderEvent() { return reinterpret_cast<void*>(&vfx::frame); }
TGPT_EXPORT int32_t TGPT_CALL TGPT_VfxDiagnosticCompletedFrame() { return vfx::state().completedFrame.load(); }
TGPT_EXPORT int32_t TGPT_CALL TGPT_MainClock(uint32_t sequence, uint32_t frameCount, const int64_t* clocks, uint32_t count) {
    if(!clocks || count!=6) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& m=e.mainClocks[sequence%e.mainClocks.size()];
        m.sequence=sequence; m.frameCount=frameCount; for(uint32_t i=0;i<count;++i) m.at[i]=clocks[i];
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetWaterCaustics(const TGPT_WaterCaustics* surfaces, uint32_t count) {
    if(count>TGPT_MAX_WATER || (count && !surfaces)) return -1;
    for(uint32_t i=0;i<count;++i) if(surfaces[i].size!=sizeof(TGPT_WaterCaustics) || surfaces[i].abi!=TGPT_ABI) return -2;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported || e.status.disabled) return -6;
        for(auto& c:e.caustics) c=Engine::Caustics{};
        for(uint32_t i=0;i<count;++i) {
            const auto& d=surfaces[i];
            bool finite=std::isfinite(d.top) && std::isfinite(d.bottom) && std::isfinite(d.maxDisplacement);
            for(float v:d.worldToWater) finite=finite && std::isfinite(v);
            for(const auto& b:d.bands) for(float v:b) finite=finite && std::isfinite(v);
            for(const auto& f:d.fades) for(float v:f) finite=finite && std::isfinite(v);
            for(float v:d.extinction) finite=finite && std::isfinite(v) && v>=0.f;
            for(float v:d.bounds) finite=finite && std::isfinite(v);
            // A slot that could not be mapped stays empty: no light off it rather than a wrong one. Waves need both buffers.
            if(!finite || d.top<d.bottom || d.bounds[2]<d.bounds[0] || d.bounds[3]<d.bounds[1] || d.bandCount>3 || (!d.displacement)!=(!d.normals)) continue;
            e.caustics[i].displacement=reinterpret_cast<ID3D11Resource*>(uintptr_t(d.displacement));
            e.caustics[i].normals=reinterpret_cast<ID3D11Resource*>(uintptr_t(d.normals));
            e.caustics[i].desc=d;
            e.caustics[i].live=true;
        }
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetProfileReflectance(const float* fresnel0, uint32_t count) {
    if(count>16 || (count && !fresnel0)) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.profileF0.fill(0.f);
        for(uint32_t i=0;i<count;++i) {
            const float v=fresnel0[i];
            e.profileF0[i]=(v>0.f && v<=1.f) ? v : 0.f; // a reflectance; anything else is none (a dielectric's 0.04)
        }
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterSharedTexture(uint64_t texture, TGPT_SharedTexture* info) {
    if(info && (info->size!=sizeof(TGPT_SharedTexture) || info->abi!=TGPT_ABI)) return -1;
    try {
        if(info) { *info=TGPT_SharedTexture{}; info->size=sizeof(TGPT_SharedTexture); info->abi=TGPT_ABI; info->handle=-1; }
        ComPtr<ID3D12Device> device;
        {
            auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
            if(!e.status.supported) return TGPT_SHARE_UNSUPPORTED;
            device=e.sharedDevice;
        }
        return share::register_texture(device.Get(), texture, info);
    } catch(...) { return -10; }
}
// Mip streaming made a hair or eye map anew: the bakes still waiting take the new texture (they hold the old one, so its
// address is still its own), and the render thread points the map records baked from it at the new one (apply_texture_swaps).
TGPT_EXPORT int32_t TGPT_CALL TGPT_SwapSharedTexture(uint64_t oldTexture, uint64_t newTexture) {
    if(!newTexture) return -1;
    try {
        ComPtr<ID3D11Resource> replacement;
        if(FAILED(reinterpret_cast<IUnknown*>(uintptr_t(newTexture))->QueryInterface(IID_PPV_ARGS(&replacement)))) return -2;
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto follow=[&](ComPtr<ID3D11Resource>& map) { if(oldTexture && map && uint64_t(uintptr_t(map.Get()))==oldTexture) map=replacement; };
        for(auto& b:e.newHairBakes) for(auto& map:b.maps) follow(map);
        for(auto& b:e.newHairLobeBakes) for(auto& map:b.maps) follow(map);
        for(auto& b:e.newEyeBakes) for(auto& map:b.maps) follow(map);
        e.textureSwaps.emplace_back(oldTexture, uint64_t(uintptr_t(replacement.Get())));
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetGuides(TGPT_Guides* guides) {
    if(!guides || guides->size!=sizeof(TGPT_Guides) || guides->abi!=TGPT_ABI) return -1;
    try {
        auto& e=tgpt::engine(); std::lock_guard<std::mutex> lock(e.mutex);
        *guides=e.guides;
        return 0;
    } catch(...) { return -10; }
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    // No DisableThreadLibraryCalls: the static C runtime needs thread notifications.
    if(reason==DLL_PROCESS_ATTACH) tgpt::self=module;
    return TRUE;
}
