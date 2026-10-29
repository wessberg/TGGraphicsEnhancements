// SPDX-License-Identifier: MIT
// TG Path Tracing as a TG Graphics Bridge extension, and the plugin's C API. It needs the bridge's
// shared D3D12 device and the game's real D3D11 device, and never touches the game's swapchain.
#include "runtime.hpp"
#include "texture_share.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstddef>
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
        try {
            log("TG Path Tracing 1.2.1 extension bootstrap (TG Graphics Bridge); PID="+std::to_string(GetCurrentProcessId()));
            return true;
        }
        catch(...) { return false; }
    }
    void d3d12_device_ready(ID3D12Device* device, uint32_t vendor) noexcept override {
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
    if(!out || out->size!=sizeof(TGPT_Status) || out->abi!=TGPT_ABI) return -1;
    try {
        { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); *out=e.status; }
        // Four times a second, path tracing on or off: textures the game freed let go of on the D3D12 side too (PT-45).
        share::collect();
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
        if(d->normalStream<4 && source->vertexBuffers[d->normalStream] && valid_format(d->normalFormat, d->normalDimension) && d->normalDimension>=3) {
            source->normalStream=d->normalStream; source->normalOffset=d->normalOffset; source->normalFormat=d->normalFormat; source->normalDimension=d->normalDimension;
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
        e.newMeshes.clear(); e.textureTiles.clear(); e.detailTiles.clear(); e.newTextures.clear(); e.newHairBakes.clear(); e.newHairLobeBakes.clear(); e.newEyeBakes.clear(); e.alphaTiles.clear(); e.newAlphaTextures.clear();
        e.detailTilesMade=0; e.detailTilesFree.clear(); e.detailTilesGivenBack.clear(); e.hairContents.clear(); e.hairKeys.clear(); // the pool starts over
        e.materials.clear(); e.instances.clear(); e.materialIndices.clear(); e.lights.clear();
        e.skinSource.Reset(); e.skinStride=0; e.skinned.clear(); // their meshes went with the scene
        e.winds.clear(); // their materials went with the scene
        e.textureSwaps.clear(); // the map records went with the scene
        e.materialsDirty=e.instancesDirty=e.lightsDirty=true;
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
        slot.skinSource=e.skinSource; slot.skinStride=e.skinStride; slot.skinned=e.skinned;
        slot.windTime=e.windTime; slot.winds=e.winds;
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
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetFog(uint32_t sequence, uint64_t color) {
    if(!color) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported || e.status.disabled) return -6;
        e.fog.sequence=sequence; e.fog.color=color; e.fog.pending=true;
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
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetWaterShade(uint32_t sequence, const TGPT_WaterShade* shade) {
    if(!shade || shade->size!=sizeof(TGPT_WaterShade) || shade->abi!=TGPT_ABI) return -2;
    if(!shade->target || !shade->depth || !shade->gbuffer[0] || !shade->gbuffer[1] || !shade->gbuffer[2] || !shade->gbuffer[3]) return -1;
    for(const auto& row:shade->extinction) for(float v:row) if(!std::isfinite(v)) return -1;
    for(const auto& row:shade->foam) for(float v:row) if(!std::isfinite(v)) return -1;
    try {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(!e.status.supported || e.status.disabled) return -6;
        auto& slot=e.waterShade[sequence%e.waterShade.size()];
        slot.sequence=sequence; slot.desc=*shade; slot.pending=true;
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
