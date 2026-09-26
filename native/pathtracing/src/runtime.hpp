// SPDX-License-Identifier: MIT
// TG Path Tracing internals, shared by the extension glue, the scene and the tracer.
//
// Threads: the plugin's calls (scene, frames) come from Unity's main thread and only fill the pending
// state below under Engine::mutex. Everything that touches a device runs on Unity's render thread in
// the two render events: it takes what is pending, reads meshes back from the game's D3D11 device,
// builds acceleration structures and traces on the bridge's D3D12 device, and composites on D3D11.
#pragma once
#include "gpu.hpp"
#include "tgpt_api.h"
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace tgpt {
tgb::Host& host();
void log(const std::string&);
// For conditions that can repeat every frame: an identical message at most every 5 s.
void log_throttled(const std::string&);
std::wstring native_root();

// The texture atlas: every registered texture as one 64x64 tile of a shared 4096x4096 RGBA8 texture,
// sRGB-encoded color and linear alpha. Rays read materials from it; HDRP's own GBuffer serves what the
// camera sees directly, so the tiles only color bounced light and test alpha.
constexpr uint32_t kAtlasSize=4096, kTileSize=64, kTilesPerRow=kAtlasSize/kTileSize, kMaxTiles=kTilesPerRow*kTilesPerRow;
// The alpha atlas: the alpha of alpha-tested materials' textures as 256x256 tiles of a shared 4096x4096 R8 texture.
constexpr uint32_t kAlphaTileSize=256, kAlphaTilesPerRow=kAtlasSize/kAlphaTileSize, kMaxAlphaTiles=kAlphaTilesPerRow*kAlphaTilesPerRow;
constexpr uint32_t kSkyWidth=512, kSkyHeight=256;
// Characters' skinned positions on their way from the game's device: one shared texture per frame parity,
// a vertex a texel (xyz, 1), 4096 a row.
constexpr uint32_t kSkinWidth=4096, kSkinRows=128, kSkinTexels=kSkinWidth*kSkinRows;
constexpr uint32_t kMaxLights=1024;
constexpr uint32_t kMaxInstances=1u<<20;

// A mesh as registered, before and after its read-back. The game's buffers are referenced from
// registration until the render thread has copied them.
struct MeshSource {
    uint64_t key=0;
    bool cpu=false, skinned=false;
    std::array<ComPtr<ID3D11Buffer>, 4> vertexBuffers;
    std::array<uint32_t, 4> strides{};
    uint32_t positionStream=0, positionOffset=0, positionFormat=0, positionDimension=3;
    uint32_t uvStream=kInvalid, uvOffset=0, uvFormat=0, uvDimension=2;
    uint32_t colorStream=kInvalid, colorOffset=0, colorFormat=0, colorDimension=4;
    ComPtr<ID3D11Buffer> indexBuffer;
    uint32_t indexFormat=16, vertexCount=0;
    std::vector<TGPT_Submesh> submeshes;
    // CPU meshes arrive converted already.
    std::vector<float> positions, uvs;
    std::vector<uint32_t> colors, indices;
};
struct TextureBake { int32_t tile=-1; uint64_t key=0; ComPtr<ID3D11Resource> texture; };

// One requested frame, from TGPT_SubmitFrame to the composite.
struct FrameSlot {
    uint32_t sequence=0;
    bool occupied=false, traced=false;
    TGPT_Frame frame{};
    uint64_t color=0;      // HDRP's camera color buffer, from TGPT_SetComposite
    uint64_t retire=0;     // the consumer fence value the D3D12 work signals, for the composite's wait
    // This frame's characters (TGPT_SetSkinned before TGPT_SubmitFrame).
    ComPtr<ID3D11Buffer> skinSource; uint32_t skinStride=0;
    std::vector<TGPT_SkinnedInstance> skinned;
};

struct Engine {
    std::mutex mutex;
    TGPT_Status status{sizeof(TGPT_Status), TGPT_ABI};
    std::string unsupported;
    ComPtr<ID3D12Device> sharedDevice; uint32_t vendor=0;
    // Pending scene, main thread.
    int32_t nextMesh=1;
    std::vector<std::pair<int32_t, std::unique_ptr<MeshSource>>> newMeshes;
    std::unordered_map<uint64_t, int32_t> textureTiles;
    std::vector<TextureBake> newTextures;
    std::unordered_map<uint64_t, int32_t> alphaTiles;
    std::vector<TextureBake> newAlphaTextures;
    std::vector<TGPT_Material> materials; bool materialsDirty=false;
    std::vector<TGPT_Instance> instances; std::vector<uint32_t> materialIndices; bool instancesDirty=false;
    std::vector<TGPT_Light> lights; bool lightsDirty=false;
    ComPtr<ID3D11Resource> sky; uint32_t skyVersion=0;
    ComPtr<ID3D11Buffer> skinSource; uint32_t skinStride=0; std::vector<TGPT_SkinnedInstance> skinned;
    uint32_t clearGeneration=0;
    std::array<FrameSlot, 8> frames;
    TGPT_Guides guides{sizeof(TGPT_Guides), TGPT_ABI}; // the render thread's current guide textures
};
Engine& engine();

// Unity's render thread. TGPT_GetRenderEvent returns render_event.
void __stdcall render_event(int eventId, void* data);
void device_ready(ID3D12Device* device, uint32_t vendor);
} // namespace tgpt
