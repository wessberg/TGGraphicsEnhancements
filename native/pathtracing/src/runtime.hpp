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
void shader_progress(TGPT_Diagnostics&);
// For conditions that can repeat every frame: an identical message at most every 5 s.
void log_throttled(const std::string&);
std::wstring native_root();

// The texture atlas: every registered texture as one 64x64 tile of a shared 4096x4096 RGBA8 texture,
// sRGB-encoded color and linear alpha. Rays read materials from it; HDRP's own GBuffer serves what the
// camera sees directly, so the tiles only color bounced light and test alpha.
constexpr uint32_t kAtlasSize=4096, kTileSize=64, kTilesPerRow=kAtlasSize/kTileSize, kMaxTiles=kTilesPerRow*kTilesPerRow;
// Detail tiles (TGPT_RegisterDetailTexture): 512x512, in rows of their own below the 64x64 tiles (the atlas is taller by
// them), for textures the camera sees from the atlas itself (see-through glass, the eyes, hair). Their numbers follow the
// 64x64 tiles'. 128 of them (1 MB each: 16 rows, pt_common.hlsli DETAIL_TILES): 32 were gone after a dozen characters,
// each of whose hair, beard and brows is a material of its own (29 of 53 hair materials in one run fell back to 64x64
// tiles and off the hair layer, and jars' and eyes' textures after them: PT-35, PT-13). They are a pool (take_detail_tile):
// hair of the same maps and values shares one, hair gives its back when its last material lets go of it
// (TGPT_ReleaseHairColor, or colored anew), and hair leaves the last kDetailReserve to see-through glass and the eyes,
// which are few and keep theirs until the scene is cleared.
constexpr uint32_t kDetailTileSize=512, kDetailTilesPerRow=kAtlasSize/kDetailTileSize, kDetailRows=16, kMaxDetailTiles=kDetailTilesPerRow*kDetailRows;
constexpr uint32_t kDetailReserve=16;
constexpr uint32_t kAtlasHeight=kAtlasSize+kDetailRows*kDetailTileSize;
static_assert(kAtlasHeight<=16384, "a Direct3D texture is at most 16384 texels tall");
// The alpha atlas: the alpha of alpha-tested materials' textures as 256x256 tiles of a shared 4096x16384 R8 texture, 1,024
// of them (PT-50: until 2026-10-07 4096x4096, 256 tiles, past which a material's alpha came from its 64x64 color tile and
// straw and leaves turned to blocks; 64 MB now, 16 MB then).
constexpr uint32_t kAlphaTileSize=256, kAlphaTilesPerRow=kAtlasSize/kAlphaTileSize, kAlphaAtlasHeight=4*kAtlasSize;
constexpr uint32_t kMaxAlphaTiles=kAlphaTilesPerRow*(kAlphaAtlasHeight/kAlphaTileSize);
static_assert(kAlphaAtlasHeight<=16384, "a Direct3D texture is at most 16384 texels tall");
// Lights' cookies: 64x64 tiles of a 1024x1024 atlas (pt_common.hlsli COOKIE_*).
constexpr uint32_t kCookieTileSize=64, kCookieAtlasSize=1024, kCookieTilesPerRow=kCookieAtlasSize/kCookieTileSize, kMaxCookies=kCookieTilesPerRow*kCookieTilesPerRow;
constexpr uint32_t kSkyWidth=512, kSkyHeight=256;
// HDRP's sky at the mips its fog color is read at (PT-37, pt_common.hlsli FOG_SKY_*): its sky texture's mips 3 to 6 (HDRP's
// fog reads mip 6, ENVCONSTANTS_CONVOLUTION_MIP_COUNT - 1, up close, down to 3 by its Max Mip of 0.5), each a 128x64 tile
// stacked from top to bottom.
constexpr uint32_t kFogSkyWidth=128, kFogSkyTile=64, kFogSkyFirstMip=3, kFogSkyLevels=4;
// Characters' skinned positions on their way from the game's device: one shared texture per frame parity,
// a vertex a texel (xyz, 1), 4096 a row.
// 2M vertices (PT-50: until 2026-10-07 524,288, which the harness's busiest scenes nearly filled): 32 MB a texture.
constexpr uint32_t kSkinWidth=4096, kSkinRows=512, kSkinTexels=kSkinWidth*kSkinRows;
// Lights at most (TGPT_SetLights): far past what the game loads at once (985 in the harness's busiest scenes); a light
// index stays under the emitters' (kEmitterBase). Until PT-50 it was 1,024.
constexpr uint32_t kMaxLights=65536;
constexpr uint32_t kMaxInstances=1u<<20;

// A mesh as registered, before and after its read-back. The game's buffers are referenced from
// registration until the render thread has copied them.
struct MeshSource {
    uint64_t key=0;
    bool cpu=false, skinned=false, decalAttributes=false;
    std::array<ComPtr<ID3D11Buffer>, 4> vertexBuffers;
    std::array<uint32_t, 4> strides{};
    uint32_t positionStream=0, positionOffset=0, positionFormat=0, positionDimension=3;
    uint32_t uvStream=kInvalid, uvOffset=0, uvFormat=0, uvDimension=2;
    uint32_t colorStream=kInvalid, colorOffset=0, colorFormat=0, colorDimension=4;
    uint32_t normalStream=kInvalid, normalOffset=0, normalFormat=0, normalDimension=3;
    uint32_t uv2Stream=kInvalid, uv2Offset=0, uv2Format=0, uv2Dimension=2; // the fourth set of texture coordinates (PT-64)
    uint32_t tangentStream=kInvalid, tangentOffset=0, tangentFormat=0, tangentDimension=4;
    ComPtr<ID3D11Buffer> indexBuffer;
    uint32_t indexFormat=16, vertexCount=0;
    std::vector<TGPT_Submesh> submeshes;
    // CPU meshes arrive converted already.
    std::vector<float> positions, uvs;
    std::vector<uint32_t> colors, indices;
};
// mask: a mask map (TGPT_RegisterMaskTexture), baked linear, its metallic and smoothness channels as channels names them.
struct TextureBake { int32_t tile=-1; uint64_t key=0; ComPtr<ID3D11Resource> texture; float cutoff=0.5f; bool mask=false; uint32_t channels=0; };
// A hair material's color for its tile (TGPT_RegisterHairColor): its maps (referenced until baked) and values.
struct HairBake { int32_t tile=-1; ComPtr<ID3D11Resource> maps[5]; TGPT_HairColor desc{}; };
// A hair material's strands for its lobe tile (TGPT_RegisterHairLobes): its flow, mask and specular maps (referenced until
// baked) and values.
struct HairLobeBake { int32_t tile=-1; ComPtr<ID3D11Resource> maps[3]; TGPT_HairLobes desc{}; };
// An eye's color for its tile (TGPT_RegisterEyeColor): its sclera and iris maps (referenced until baked) and values.
struct EyeBake { int32_t tile=-1; ComPtr<ID3D11Resource> maps[2]; TGPT_EyeColor desc{}; };
// The scene's instance list as TGPT_SetScene handed it over (a new one each call): a frame holds the one that was the newest
// when it was submitted, which its TGPT_SetMoving indices name, however many scans the plugin makes before it is traced.
struct SceneList { std::vector<TGPT_Instance> instances; std::vector<uint32_t> materialIndices; };
// The things the game moves in one frame (TGPT_SetMoving): where each is and was, their material indices, and the scene's
// instances they stand in for that frame.
struct MovingList { std::vector<TGPT_Moving> things; std::vector<uint32_t> materialIndices; std::vector<uint32_t> replaced; };

// One requested frame, from TGPT_SubmitFrame to the composite.
// The winds' noise maps (TGPT_SetWindNoise), one byte a texel, the row at v = 0 first: every map's bytes one after another
// (each starting on a multiple of four), and where each key's lie. A new set is made whenever a map is sent; frames hold
// the set they were submitted with.
struct WindNoise {
    struct Map { uint32_t offset=0, width=0, height=0, flags=0; };
    std::vector<uint8_t> bytes;
    std::unordered_map<uint32_t, Map> maps;
};

struct FrameSlot {
    uint32_t sequence=0;
    bool occupied=false, traced=false;
    TGPT_Frame frame{};
    uint64_t color=0;      // HDRP's camera color buffer, from TGPT_SetComposite
    uint64_t pyramid=0;    // PT-112: HDRP's depth pyramid (its fog and transparents read it), from TGPT_SetDepthPyramid
    uint64_t retire=0;     // the consumer fence value the D3D12 work signals, for the composite's wait
    // This frame's characters (TGPT_SetSkinned before TGPT_SubmitFrame).
    ComPtr<ID3D11Buffer> skinSource; uint32_t skinStride=0;
    std::vector<TGPT_SkinnedInstance> skinned;
    // The game's shader time and the wind on its swaying cloth then (TGPT_SetClothWind before TGPT_SubmitFrame).
    float windTime=0; std::vector<TGPT_ClothWind> winds;
    // The wind on its grass and leaves then (TGPT_SetPlantWind), and the noise maps as they were (TGPT_SetWindNoise).
    TGPT_PlantWindFrame plantFrame{}; std::vector<TGPT_PlantWind> plantWinds; std::shared_ptr<const WindNoise> windNoise;
    // The rain's wetness then (TGPT_SetRain; PT-108, on trial): all 0 is dry.
    TGPT_Rain rain{};
    // The game's fog boxes then (TGPT_SetFogBoxes, PT-112).
    std::vector<TGPT_FogBox> fogBoxes;
    // The sky then (TGPT_SetSkyModel, PT-112 the sky; on trial): mode TGPT_SKY_HDRP is HDRP's sky as before.
    TGPT_Sky skyModel{};
    // The scene's instance list then (TGPT_SetScene) and the things the game moves in it (TGPT_SetMoving).
    std::shared_ptr<const SceneList> scene; std::shared_ptr<const MovingList> moving;
};

struct Engine {
    std::mutex mutex;
    // cameraLight negative until the camera's light is first measured.
    TGPT_Status status=[] { TGPT_Status s{sizeof(TGPT_Status), TGPT_ABI}; s.cameraLight=-1.f; return s; }();
    TGPT_Diagnostics diagnostics{sizeof(TGPT_Diagnostics), TGPT_ABI};
    std::string unsupported;
    std::string stageTimes; // the GPU time of a frame's stages over about the last second (TGPT_GetStageTimes)
    TGPT_Yardstick yardstick{sizeof(TGPT_Yardstick), TGPT_ABI}; // the last frame counted (TGPT_GetYardstick)
    uint32_t walkPixels[3]{}; // of that frame (TGPT_GetWalkPixels): its index, the pixels on materials light walks inside, those that walked
    ComPtr<ID3D12Device> sharedDevice; uint32_t vendor=0;
    // Pending scene, main thread.
    int32_t nextMesh=1;
    std::vector<int32_t> retiredMeshes;
    std::vector<std::pair<int32_t, std::unique_ptr<MeshSource>>> newMeshes;
    std::unordered_map<uint64_t, int32_t> textureTiles;
    std::unordered_map<uint64_t, int32_t> detailTiles; // TGPT_RegisterDetailTexture's and the eyes', numbered from kMaxTiles
    // The detail tiles' pool (take_detail_tile): how many were ever handed out, and those given back since.
    uint32_t detailTilesMade=0; std::vector<int32_t> detailTilesFree;
    // Hair's colors in detail tiles, by what they are made of (TGPT_HairColor's maps and values, as its bytes): materials
    // with the same share one tile, counted. And each hair material's key: the tile it reads and that content (none: a
    // 64x64 tile of its own, which it keeps).
    struct HairContent { int32_t tile=-1; uint32_t users=0; };
    std::unordered_map<std::string, HairContent> hairContents;
    struct HairUse { int32_t tile=-1; std::string content; };
    std::unordered_map<uint64_t, HairUse> hairKeys;
    // Detail tiles hair gave back since the render thread last looked: their strand rows are cleared there (bake_hair), so
    // the next hair on one isn't drawn with the last one's strands before its own are baked.
    std::vector<int32_t> detailTilesGivenBack;
    std::vector<TextureBake> newTextures;
    std::vector<HairBake> newHairBakes; // TGPT_RegisterHairColor's, into color tiles
    std::vector<HairLobeBake> newHairLobeBakes; // TGPT_RegisterHairLobes's, into color tiles
    std::vector<EyeBake> newEyeBakes;   // TGPT_RegisterEyeColor's, into detail tiles or color tiles
    // TGPT_SwapSharedTexture's (old ID3D11Resource*, new): hair's and eyes' map records baked from the old texture read the new.
    std::vector<std::pair<uint64_t, uint64_t>> textureSwaps;
    std::unordered_map<uint64_t, int32_t> alphaTiles;
    std::vector<TextureBake> newAlphaTextures;
    std::unordered_map<uint64_t, int32_t> cookieTiles; // kept across scene clears: its atlas is never cleared
    std::vector<TextureBake> newCookies;
    std::vector<TGPT_Decal> decals; bool decalsDirty=false;
    std::vector<TGPT_Material> materials; bool materialsDirty=false;
    std::vector<uint32_t> programRows; bool programsDirty=false; // TGPT_SetMaterialPrograms' rows, four words each (PT-64)
    std::shared_ptr<const SceneList> scene=std::make_shared<const SceneList>();   // TGPT_SetScene's newest
    std::shared_ptr<const MovingList> moving;                                       // TGPT_SetMoving's, kept until its next call
    std::vector<TGPT_Light> lights; bool lightsDirty=false;
    ComPtr<ID3D11Resource> sky; uint32_t skyVersion=0;
    ComPtr<ID3D11Buffer> skinSource; uint32_t skinStride=0; std::vector<TGPT_SkinnedInstance> skinned;
    float windTime=0; std::vector<TGPT_ClothWind> winds; // TGPT_SetClothWind's, kept until its next call
    TGPT_PlantWindFrame plantFrame{}; std::vector<TGPT_PlantWind> plantWinds; // TGPT_SetPlantWind's, kept until its next call
    std::shared_ptr<const WindNoise> windNoise;                               // TGPT_SetWindNoise's maps, until the scene is cleared
    TGPT_Rain rain{};                                                         // TGPT_SetRain's, kept until its next call (all 0: dry)
    std::vector<TGPT_FogBox> fogBoxes;                                        // TGPT_SetFogBoxes', kept until its next call
    TGPT_Sky skyModel{};                                                      // TGPT_SetSkyModel's, kept until its next call (mode 0: HDRP's)
    uint32_t clearGeneration=0;
    std::array<FrameSlot, 8> frames;
    TGPT_Guides guides{sizeof(TGPT_Guides), TGPT_ABI}; // the render thread's current guide textures
    // PT-107 (R19): the game's main thread on the CPU's clock (QueryPerformanceCounter), a frame's record by its sequence
    // (TGPT_MainClock): the frame's Update, HDRP's rendering begun, the capture pass's start and end, HDRP's rendering done,
    // the frame's LateUpdate; the tracer's State::CpuClock reads them.
    struct MainClock { uint32_t sequence=0, frameCount=0; std::array<int64_t, 6> at{}; };
    std::array<MainClock, 16> mainClocks{};
    // Caustics (TGPT_SetWaterCaustics): each slot's wave buffers (referenced while they are set; none for still water), HDRP's
    // numbers for them and the water's extent. live: the slot holds water.
    struct Caustics { ComPtr<ID3D11Resource> displacement, normals; TGPT_WaterCaustics desc{}; bool live=false; };
    std::array<Caustics, TGPT_MAX_WATER> caustics;
    // A mirror reflectance per diffusion profile (TGPT_SetProfileReflectance): HDRP's Fresnel0 of its index of refraction, 0 none.
    std::array<float, 16> profileF0{};
};
Engine& engine();

// Unity's render thread. TGPT_GetRenderEvent returns render_event.
void __stdcall render_event(int eventId, void* data);
void device_ready(ID3D12Device* device, uint32_t vendor);
} // namespace tgpt
