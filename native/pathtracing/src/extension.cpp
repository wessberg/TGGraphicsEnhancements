// SPDX-License-Identifier: MIT
// TG Path Tracing as a TG Graphics Bridge extension, and the plugin's C API. It needs the bridge's
// shared D3D12 device and the game's real D3D11 device, and never touches the game's swapchain.
#include "runtime.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace tgpt {
namespace {
tgb::Host* bridgeHost=nullptr;
HMODULE self=nullptr;
std::wstring root; // BepInEx/TGPathTracing/Native

class PathTracingExtension final : public tgb::Extension {
public:
    const char* name() noexcept override { return "TG Path Tracing"; }
    uint32_t needs() noexcept override { return tgb::NeedD3D12; }
    bool bootstrap() noexcept override {
        try { log("TG Path Tracing 1.2.0 extension bootstrap (TG Graphics Bridge); PID="+std::to_string(GetCurrentProcessId())); return true; }
        catch(...) { return false; }
    }
    void d3d12_device_ready(ID3D12Device* device, uint32_t vendor) noexcept override {
        try { tgpt::device_ready(device, vendor); }
        catch(const std::exception& e) { log(std::string("Device check: ")+e.what()); }
        catch(...) {}
    }
};

bool is_buffer(uint64_t pointer) {
    if(!pointer) return false;
    ComPtr<ID3D11Buffer> buffer;
    return SUCCEEDED(reinterpret_cast<IUnknown*>(uintptr_t(pointer))->QueryInterface(IID_PPV_ARGS(&buffer)));
}
bool valid_format(uint32_t format, uint32_t dimension) { return format<=TGPT_VF_SNORM16 && dimension>=1 && dimension<=4; }
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
    if(!out || out->size!=sizeof(TGPT_Status) || out->abi!=TGPT_ABI) return -1;
    try { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); *out=e.status; return 0; } catch(...) { return -2; }
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
        if(!d->vertexCount || !d->submeshCount || !d->submeshes || d->submeshCount>256 || d->positionStream>3) return -2;
        if(d->indexFormat!=16 && d->indexFormat!=32) return -3;
        if(!valid_format(d->positionFormat, d->positionDimension) || d->positionDimension<3) return -4;
        if(!is_buffer(d->vertexBuffers[d->positionStream]) || !is_buffer(d->indexBuffer)) return -5;
        auto source=std::make_unique<MeshSource>();
        source->key=d->key;
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
        if(count && (stride<12 || stride%4 || stride>256)) return refuse(-2);
        ComPtr<ID3D11Buffer> source;
        if(count && FAILED(reinterpret_cast<IUnknown*>(uintptr_t(buffer))->QueryInterface(IID_PPV_ARGS(&source)))) return refuse(-3);
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.skinSource=source; e.skinStride=count ? stride : 0;
        e.skinned.assign(instances, instances+std::min(count, TGPT_MAX_SKINNED));
        return 0;
    } catch(...) { return refuse(-10); }
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
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetScene(const TGPT_Instance* instances, uint32_t count, const uint32_t* materialIndices, uint32_t materialIndexCount) {
    if((!instances && count) || (!materialIndices && materialIndexCount)) return -1;
    if(count>kMaxInstances || materialIndexCount>(1u<<24)) return -2;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.instances.assign(instances, instances+count);
        e.materialIndices.assign(materialIndices, materialIndices+materialIndexCount);
        e.instancesDirty=true; e.status.instances=count;
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
TGPT_EXPORT int32_t TGPT_CALL TGPT_ClearScene() {
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.newMeshes.clear(); e.textureTiles.clear(); e.newTextures.clear(); e.alphaTiles.clear(); e.newAlphaTextures.clear();
        e.materials.clear(); e.instances.clear(); e.materialIndices.clear(); e.lights.clear();
        e.skinSource.Reset(); e.skinStride=0; e.skinned.clear(); // their meshes went with the scene
        e.materialsDirty=e.instancesDirty=e.lightsDirty=true;
        e.status.textures=e.status.materials=e.status.instances=e.status.lights=0;
        ++e.clearGeneration;
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SubmitFrame(const TGPT_Frame* f) {
    if(!f || f->size!=sizeof(TGPT_Frame) || f->abi!=TGPT_ABI) return -1;
    if(!f->gbuffer0 || !f->normal || !f->depth || !f->renderWidth || !f->renderHeight || f->renderWidth>16384 || f->renderHeight>16384) return -2;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported || e.status.disabled) return -6;
        auto& slot=e.frames[f->sequence%e.frames.size()];
        slot=FrameSlot{}; slot.sequence=f->sequence; slot.occupied=true; slot.frame=*f;
        slot.skinSource=e.skinSource; slot.skinStride=e.skinStride; slot.skinned=e.skinned;
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
TGPT_EXPORT void* TGPT_CALL TGPT_GetRenderEvent() { return reinterpret_cast<void*>(&tgpt::render_event); }
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetShadowDetail(uint32_t request, uint64_t color, float strength) {
    if(!color || !(strength>0.f) || !(strength<1e30f)) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& slot=e.shadowDetail[request%e.shadowDetail.size()];
        slot.request=request; slot.color=color; slot.strength=strength<1.f ? strength : 1.f; slot.pending=true;
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetWater(uint32_t sequence, uint64_t reflections, uint64_t depth, uint64_t normal, float reflectionMultiplier) {
    if(!reflections || !depth || !normal || !(reflectionMultiplier>=0.f) || !(reflectionMultiplier<1e30f)) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported || e.status.disabled) return -6;
        auto& slot=e.water[sequence%e.water.size()];
        slot.sequence=sequence; slot.reflections=reflections; slot.depth=depth; slot.normal=normal;
        slot.multiplier=reflectionMultiplier; slot.pending=true;
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetDiffusionProfiles(const float* profiles, uint32_t count) {
    if(count>16 || (count && !profiles)) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.sssProfiles.fill(0.f); e.sss=false;
        for(uint32_t i=0;i<count*4;++i) {
            const float v=profiles[i];
            e.sssProfiles[i]=(v>0.f && v<1e30f) ? v : 0.f;
        }
        for(uint32_t i=0;i<count;++i) if(e.sssProfiles[i*4+3]>0.f) e.sss=true;
        return 0;
    } catch(...) { return -10; }
}
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSkinTransmission(const float* profiles, uint32_t count) {
    if(count>16 || (count && !profiles)) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.transmission.fill(0.f);
        for(uint32_t i=0;i<count*8;++i) {
            const float v=profiles[i];
            e.transmission[i]=(v>=0.f && v<1e30f) ? v : 0.f;
        }
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
