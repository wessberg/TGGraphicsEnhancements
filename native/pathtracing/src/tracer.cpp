// SPDX-License-Identifier: MIT
// Path traced frames on Unity's render thread. A frame's traced light is shown in that same frame (PT-84):
//
//   Render event 1 of frame N (recorded by the plugin right after HDRP wrote its GBuffer)
//     1. D3D11, the game's immediate context in an isolated pipeline state: mesh read-backs, material
//        textures into the atlas, the sky into a latitude-longitude map, and HDRP's GBuffer, depth,
//        stencil, motion vectors and exposure into frame N's set of textures shared with D3D12.
//     2. D3D11 signals the producer fence; the private D3D12 compute queue waits for it on the GPU.
//     3. D3D12: uploads and acceleration structure builds, ReSTIR DI, ReSTIR GI and reflections, the
//        denoisers, and frame N's traced light (divided by the diffuse color) into
//        frame N's output textures. It runs alongside HDRP's shadows and lighting and signals the
//        consumer fence when done.
//   Render event 2 of frame N (a custom pass after opaque lighting and the sky, before fog)
//     4. D3D11 waits on the GPU for frame N's own traced light (the consumer fence) and lays it into HDRP's
//        color buffer, each pixel its own texel. So what the upscaler gets was traced through the very points
//        HDRP drew frame N's depth at, under the sub-pixel shift it is told for frame N, still or moving.
//        (Until 2026-10-08 the composite showed frame N-1's light, moved by frame N's motion vectors and matched
//        by HDRP's depth, so that D3D11 never waited for D3D12: a frame late, and under another frame's shift.)
//
// Each fence is signalled by one API only, so its value never goes backwards. Inputs are double-buffered by
// frame parity (a frame's trace also reads the frame before's); before D3D11 overwrites a set, D3D12 has finished
// the frame that read it. The traced light has one set of textures: D3D12 writes it in a frame's trace, D3D11
// reads it in that frame's composite after waiting for the trace, and the next trace starts only at D3D11's
// producer signal, which comes after that composite in D3D11's order. (Until 2026-10-08 two sets, by parity:
// D3D11 read the frame before's light while D3D12 wrote this frame's.)
#include "scene.hpp"
#include "ray_regeneration.hpp"
#include "nrd_denoiser.hpp"
#include "nrd_confidence.hpp"
#include "primary_surface_replacement.hpp"
#include "texture_share.hpp"
#include "camera_shift.hpp"
#include "d3d11_shader.hpp"
#include "pt_shaders.hpp"
// NVIDIA RTXDI (fetched beside the repository, compiled in): its ReSTIR DI and GI parameter blocks and helpers.
#include <Rtxdi/DI/ReSTIRDIParameters.h>
#include <Rtxdi/GI/ReSTIRGIParameters.h>
#include <Rtxdi/RtxdiUtils.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <cmath>
#include <cstring>

#include "pass_cache.hpp"

namespace tgpt {
namespace {
constexpr uint32_t kFrames=3;
// What each stage of a frame costs the GPU (the F8 report, TGPT_GetStageTimes, and the log): a timestamp at the frame's start
// and after each stage, and a pair around the water's reflections (a submission of their own), after the frames' marks.
// The bounced light's paths (with their temporal reuse, pt_gi) and its spatial reuse (with the boiling filter) apart: the
// spatial reuse's visibility rays grow with Spatial Samples, the paths with Rays Per Pixel and Bounces.
constexpr uint32_t kStages=14;
constexpr const char* kStageNames[kStages]={"scene", "sky", "caustics", "prepare", "bounced light", "bounced light spatial reuse", "direct light",
                                            "reflections", "radiance cache", "denoising", "fog cells", "fog walk", "fog lamps", "composite"};
constexpr uint32_t kMarks=kStages+1, kQueries=kFrames*kMarks;
// The reflections' stage by its name, for the sort's chooser (PT-128: a fixed number went stale when the sky's stage came
// in before it, 2026-10-11, and the chooser timed the direct light).
constexpr uint32_t stage_named(const char* name) {
    for(uint32_t s=0;s<kStages;++s) { const char* a=kStageNames[s]; const char* b=name; while(*a && *a==*b) { ++a; ++b; } if(*a==*b) return s; }
    return kStages;
}
constexpr uint32_t kReflectionsStage=stage_named("reflections");
static_assert(kReflectionsStage<kStages, "the reflections' stage is named in kStageNames");
// The stage the chooser reads the scene's own cost by (a stage the reflections' hint does not touch): the bounced light's.
constexpr uint32_t kSceneCostStage=stage_named("bounced light");
static_assert(kSceneCostStage<kStages, "the bounced light's stage is named in kStageNames");
constexpr uint32_t kSortEvery=128, kSortProbe=8; // the reflections' sort (State::ReflectionsSort): a probe of 8 frames every 128
// PT-109 (docs/agents/megageo-1.md): the "scene" stage's acceleration structure work by kind, for the log's "PT structures"
// line: a timestamp where each kind's commands end on the list (GpuScene::structureStamp), in a query heap of their own.
constexpr uint32_t kStructMarks=24;
constexpr const char* kStructureNames[GpuScene::kStructureKinds]={"the rest of the scene stage", "skinning", "new meshes", "compaction", "decals", "cloth",
                                                                    "plants", "water", "characters", "micromaps", "top level"};
constexpr uint32_t kHeapSize=1u<<18;
constexpr uint64_t kRingSize=48ull<<20;
// Tags the game's immediate context. ReShade makes a texture's GetDevice return its proxy device, whose
// COM identity differs from the device the bridge hands out; the proxy forwards private data, so the tag
// recognises the game's device through it (as TG Better Upscaling does). An unrelated device still fails.
const GUID kContextTag={0x3f6d2c81,0x57a4,0x4b0e,{0xa1,0x9c,0x2e,0x64,0xd8,0x0b,0x71,0x5a}};

// Kept in step with FrameConstants in pt_common.hlsli.
struct FrameConstants {
    float invViewProj[16], viewProj[16], prevViewProj[16];
    float cameraPos[4], prevCameraPos[4], prevOriginDelta[4], sunDir[4], sunColor[4];
    uint32_t size[4], settings[4], settings2[4];
    float params[4];
    uint32_t inputs0[4], inputs1[4], scene0[4], scene1[4], surface[4], material[4], signals[4], denoise0[4], denoise1[4], denoise2[4];
    float gridMin[4];
    uint32_t grid[4];
    uint32_t extra[4];      // the alpha atlas (SRV), the previous frame's material (UAV), RTXDI's neighbour offsets (SRV)
    RTXDI_RuntimeParameters rtxdiRuntime;
    RTXDI_ReservoirBufferParameters rtxdiReservoirs;
    RTXDI_DITemporalResamplingParameters diTemporal;
    RTXDI_BoilingFilterParameters diBoiling;
    RTXDI_DISpatialResamplingParameters diSpatial;
    uint32_t rtxdi[4];      // reservoir arrays: DI this frame, DI previous, GI this frame, GI previous
    RTXDI_GITemporalResamplingParameters giTemporal;
    RTXDI_GISpatialResamplingParameters giSpatial;
    RTXDI_BoilingFilterParameters giBoiling;
    float radiance[4];      // the reference radiance (stored light), the previous frame's, the clamps' reference, 1 with swaying instances
    uint32_t cache[4];      // the radiance cache: its keys, sums and light (raw UAVs); 1 while it is on
    float cacheOrigin[4];   // the scene origin in the world, in metres (the cache's cells are the world's)
    uint32_t emissive[4];   // emissive triangles (scene.cpp build_emitters): the emitter records (SRV), the previous frame's, their counts
    uint32_t emissive2[4];  // the instance table (SRV), the static instances it covers, the clusters sampled as lights, the first one's index
    float fog[4];           // HDRP's height fog (TGPT_Frame.fog): extinction per metre, base height (world), 1 / scale height; the player's exposure factor
    float waterScatter[4][4];
    float waterPrevious[4][13][4]; uint32_t waterPreviousTextures[4];
    float waterFoam[4][6][4]; int32_t waterFoamMaps[4][4];
    uint32_t waterGeometry[4]; // base-water transforms and mesh records; shared displaced intersections
    uint32_t waterTextures[4]; // caustics (TGPT_SetWaterCaustics): each slot's displacement array (SRV), kInvalid for still water
    float water[240];       // per slot, 15 rows (kWaterRows): per band its x and z texture coordinate rows of the position relative to
                            // the scene origin and (amplitude, fade a, fade b); the world-to-water rotation's rows; (bands, band
                            // size, max displacement, 1 when the slot holds water); its extent (x, z min, max); its absorption
    float waterTop[4];      // per slot: its water's highest point, relative to the scene origin (-1e30 when empty)
    float waterBottom[4];   // and its lowest (1e30 when empty)
    uint32_t hair[4];       // hair's own a pixel (pt_prepare, UAV); 1 while the scene holds hair camera rays find (pt_primary); the strand atlas (SRV);
                            // an upscaler's mip bias (a float's bits)
    float strandTiles[2*kMaxDetailTiles][4]; // per detail tile, two rows: what its hair reads its strands with (bake_strands)
    float profileF0[16];    // per diffusion profile: HDRP's mirror reflectance of its index of refraction, 0 none (TGPT_SetProfileReflectance)
    uint32_t textures[4];   // PT-46: the shared textures' range in the heap (sharedBase); hair's and eyes' map records (SRV); unused
    float fogColor[4];      // PT-37: what HDRP's fog scatters of its color (TGPT_Frame.fogColor's rgb); HDRP's maximum fog distance
    float fogMip[4];        // HDRP's mip fog (near, far, max mip); the fog's sky (SRV, as a uint's bits; kInvalid for a constant color)
    uint32_t waterNormals[4]; // caustics: each slot's surface gradient array (SRV), kInvalid for still water
    uint32_t caustics[4];   // the caustics' light (pt_caustics.hlsl): its triangles' light (raw UAV; kInvalid: none this frame),
                            // their acceleration structure (SRV), the counters (raw UAV), the triangles' capacity
    float causticGrid[4];   // the finest cell (metres), the triangles' corners (raw UAV, bits), the levels, a random number of the frame (bits)
    float causticLevels[32]; // per level: its grid's corner (x, z relative to the scene origin), the finer level's square in its cells
    float motionViewProj[16]; // this frame's view-projection without jitter, camera-relative: HDRP's motion vectors' current one
    uint32_t decals[4];     // records, ordered box BVH, node count, layer filtering
    uint32_t primary[4];    // the tracer's own camera rays (pt_primary; UAVs): each pixel's own motion, its hit's emission, the yardstick;
                            // SRV: the moving things' previous transforms (GpuScene::movingPreviousSrv)
    uint32_t wet[4];        // where the water has been (pt_wet.hlsl, PT-52): its cells (raw UAV; kInvalid: none), its levels, the
                            // frame's clock (the game's shader time, a float's bits), the clock of the store's last update (bits)
    float wetGrid[4];       // the finest cell (metres), the scene origin's height in the world, the height above which nothing is wet;
                            // w: ON TRIAL, the small-lamp switches (TGPT_Frame.jitter's w; pt_common lamp_trial), 0 off
    float wetLevels[32];    // per level: its square's corner (x, z relative to the scene origin), that cell's world index (ints' bits)
    float look[4];          // x: the hold's W this frame (TGPT_Frame.highlightKept; 0: the shaders' built-in); y, z: where the
                            // previous frame's pixel grid lies against this one's (render pixels: this frame's sub-pixel shift
                            // less the previous one's; pt_common previous_place, PT-85); w: that on trial: 0 off (y and z
                            // stay 0), 2 the tracer's own histories alone (TGPT_Frame.jitter's z 1), 1 ReSTIR's reuse too (its 2)
    float sunGrid[8][4];    // the water's kept wave corners (pt_sun_grid.hlsl), two rows a slot: its grid's two steps (x, z of each; 0: no
                            // grid), then corner (0, 0)'s x and z, the level plane's height and the slot's first byte in the buffer (bits)
    float rain[4];          // the rain's wetness (TGPT_SetRain, pt_common rain_roughness; PT-108, on trial): the game's moisture (0: dry),
                            // a soaked surface's smoothness at the camera, the screen noise's stand-in, the noise texture's number (bits)
    float waterCeiling[4];  // x: the highest corner of the scene's own water of every slot, relative to the scene origin (-1e30: none;
                            // pt_water_surface water_ceiling, round 20); y, z, w: 0
    float fogMedium[4];     // the fog as a medium (TGPT_Frame.fogMedium: the Fog volume's albedo, its anisotropy; pt_medium.hlsli)
    uint32_t fogBoxes[4];   // the game's fog boxes (TGPT_SetFogBoxes, PT-112): x their records' view (kInvalid: none), y how many
    // PT-112, the sky, on trial (fill_sky; pt_common.hlsli FrameConstants.sky, pt_sky.hlsli): the atmosphere's numbers, the
    // celestial bodies (kSkyBodyRows rows each), the cloud layers (kSkyLayerRows rows each, then the layer's opacity), the
    // tables and the map (SRVs), and the baked clouds (SRV), their ambient light (UAV), mode | flags << 8 and the map's mips
    // when it is the fog's sky. All 0 and the mode 0 (HDRP's sky) unless the plugin's hidden "Sky trial" asks.
    float sky[11][4];
    float skyBodies[32][4];
    float skyClouds[11][4];
    uint32_t skyMaps[4];
    uint32_t skyMaps2[4];
    uint32_t psr[4]; // PT-127 (pt_psr_record.hlsli): the mirrors' records for NRD (UAV), none, a test's view, the frame's serial
};
static_assert(sizeof(FrameConstants)==9024, "frame constants layout");
// The yardstick's counters (pt_yardstick.hlsl, pt_common YARD_COUNTERS, YARD_MATERIALS and YARD_PER_MATERIAL): a summary, then
// six a material (its pixels, those that differ, and of those: another place, colour, normal, roughness).
constexpr uint32_t kYardCounters=64, kYardMaterials=4096, kYardPerMaterial=6, kYardCount=kYardCounters+kYardPerMaterial*kYardMaterials;
// The caustics (pt_caustics.hlsl): the finest grid cell over the water, the cells a level's side, its levels at most, a landed
// triangle's corners (three float4s: the vertices its acceleration structure is built from) and light (three float3s and its
// tolerance), and the counters (pt_caustics COUNTERS uints: the triangles written, their largest tolerance, per slot what
// the waves are where the tracer reads them, the sun's light as it landed, and from kCausticTileCounts on the tiles that
// ran, that had a corner on their slot's water, that a light reached and that wrote a triangle, per level and slot; in
// words 2 and 3 the written triangles' checksum).
constexpr float kCausticCell=0.02f;
constexpr uint32_t kCausticGrid=512, kCausticLevels=8, kCausticCorners=48, kCausticLight=48, kCausticCounters=256, kWaterRows=15;
constexpr uint32_t kCausticTileCounts=128, kCausticTileKinds=4, kCausticChecksum=2; // pt_caustics TILE_COUNTS, TILE_*, CHECKSUM
// TGPT_TEST_CAUSTIC_PARTS (a test's; PT-114, docs/agents/caustgrid.md): what of the caustics' stage is left out, for its
// time by part. Bits 0 to 3: where the pass's threads stop (a measuring build's pass alone reads it: pt_caustics.hlsl's
// "stop"); 0x100: no pass for where the water has been (pt_wet); 0x200: none for the water's kept wave corners
// (pt_sun_grid); 0x400: the room past the frame's triangles is not made inactive; 0x800: the landed triangles' structure
// is not built; 0x1000: the caustics' own tiles are not dispatched. Every one of them changes the picture.
// (Read once a frame, as the tests' other switches are: a test changes it between its runs.)
uint32_t caustic_parts() { char text[16]{}; return GetEnvironmentVariableA("TGPT_TEST_CAUSTIC_PARTS", text, sizeof text)>0 ? uint32_t(std::strtoul(text, nullptr, 0)) : 0u; }
// TGPT_TEST_CAUSTIC_CUTS (a test's): parts of PT-114's trial left out while its bit is on, for checking each alone.
// 1: every light of a tile is traced, whether or not its rays on can land in view (as without the trial's bit 26); 2:
// every corner of a tile is worked out, and 32: every tile the pass would run is listed, water under it or not (both as
// without the trial's bit 25); 16: the pick has no pass of its own (each tile's own threads pick, as before).
uint32_t caustic_cuts_off() { char text[16]{}; return GetEnvironmentVariableA("TGPT_TEST_CAUSTIC_CUTS", text, sizeof text)>0 ? uint32_t(std::strtoul(text, nullptr, 0)) : 0u; }
// TGPT_LOG_CAUSTICS_COUNTERS (a test's): a log line a frame with the caustics' counters (the triangles written, the tiles,
// the checksum), for comparing two runs frame by frame.
bool caustic_counters_logged() { return GetEnvironmentVariableA("TGPT_LOG_CAUSTICS_COUNTERS", nullptr, 0)>0; }
#ifdef TGPT_SPEED_MEASURE
// A measuring build's counters of shade_path's two special branches, glass followed and the water's own path (PT-131,
// docs/agents/campspeed.md; pt_common speed_count_flush): a block of 48 words (pt_common SPEED_COUNTERS) for the bounced
// light's pass, one for the reflections'.
constexpr uint32_t kSpeedBlock=48, kSpeedCounters=2*kSpeedBlock;
#endif
// Where the water has been (pt_wet.hlsl, PT-52): the finest cell, the cells a level's side, its levels at most, a cell's record
// (pt_common.hlsli WET_GRID, WET_CELL_BYTES): 32 MB for all eight levels.
constexpr float kWetCell=0.02f;
constexpr uint32_t kWetGrid=256, kWetLevels=8, kWetCellBytes=64;
// The water's wave corners kept for the sun's question inside the water (pt_sun_grid.hlsl, pt_water_sun.hlsli SUN_CORNERS,
// SUN_CORNER_BYTES, SUN_TILE, SUN_RECORD_BYTES): the corners a side of a slot's grid, a corner's bytes (the wave's point and
// its normal), the cells a side of a tile of corners and a tile's record's bytes, and how many slots have a grid (the
// first that can: 28 MB each: its corners, a record a tile, a record a row of tiles, one for the whole grid).
// kSunCellShare: the grid's cell over the cell a patch wants (half a texel of the slot's finest band), a little under 1
// so that a patch whose plane is turned a little still finds the grid's cells no larger than it wants.
constexpr uint32_t kSunCorners=1024, kSunCornerBytes=24, kSunGrids=2;
constexpr uint32_t kSunTile=4, kSunTiles=kSunCorners/kSunTile, kSunRecordBytes=64;
constexpr uint32_t kSunSlotBytes=kSunCorners*kSunCorners*kSunCornerBytes+kSunTiles*kSunTiles*kSunRecordBytes+kSunTiles*kSunRecordBytes+kSunRecordBytes;
constexpr double kSunCellShare=1.0/1.03, kWaterIndex=1.3333;
// The radiance cache's cells (pt_common.hlsli CACHE_SLOTS): a key, a frame's sums (16 bytes) and the light (16 bytes) each.
constexpr uint32_t kCacheSlots=1u<<20;
// PT-112, the sky, on trial (pt_sky.hlsli SKY_*): a body's and a cloud layer's rows in the constants, the transmittance table
// (Bruneton's: over the distance to the top and the height), the multiple scattering's (Hillaire's: 32 x 32), the ground's
// irradiance (over the sun's cosine) and the sky map (latitude-longitude, 256 texels to a quarter turn as HDRP's 256-texel
// cube map, with every mip down to 1 x 1).
constexpr uint32_t kSkyBodyRows=8, kSkyLayerRows=5, kSkyBodies=4;
constexpr uint32_t kSkyTransmittanceW=256, kSkyTransmittanceH=64, kSkyMultiple=32, kSkyGround=256;
constexpr uint32_t kSkyMapW=1024, kSkyMapH=512, kSkyMapMips=11;
constexpr uint32_t kSkyAmbientGroups=32; // the clouds' ambient light: 32 groups of 64 directions (pt_sky.hlsl SKY_AMBIENT_GROUPS)
static_assert(kSkyBodies==TGPT_SKY_MAX_BODIES && kSkyBodies*kSkyBodyRows==32 && 2*kSkyLayerRows+1==11, "the sky's rows (FrameConstants, pt_sky.hlsli)");
// RTXDI's spatial reuse walks a low-discrepancy set of offsets in the unit disc (rtxdi::FillNeighborOffsetBuffer).
constexpr uint32_t kNeighborOffsets=8192;
// Hair's strands at their textures' own size (bake_strands): an R8 atlas with its mips, in blocks of kStrandBlock texels a side
// (16 of them) and quarters of those down to an eighth.
constexpr uint32_t kStrandAtlas=8192, kStrandBlock=2048, kStrandLevels=14;

struct Shared { tgb::SharedTexture t; uint32_t srv=kInvalid, uav=kInvalid; };
// PT-87 (docs/agents/memory-4.md): the atlases, which the game's device writes now and then (a bake) and the tracer's device
// reads every frame. Windows counts a texture once for each device that uses it, so an atlas both hold is in its count
// twice: 261 MB of second count (347 with hair's strands) for nothing on the card. So the game's device holds its side of
// an atlas only around a bake. The tracer keeps an NT handle of the texture; after kSideIdleMs without a bake into it, or
// a second without any traced frame, the game's device lets go of its texture object and its views (let_go_side), and
// the next bake opens them again by the handle (take_up_side) before it takes anything from its queue (hold_sides). The
// tracer's own side, its view in the heap and what was baked are never touched: the picture does not know. Where the way
// back cannot be shown to work, the side stays held, as before (GameSide::stay).
// By the clock, as what a frame takes in goes since PT-98: two seconds are 120 frames at 60 a second, the number the
// other idle things of PT-87 go by (kIdleFrames), and 16 frames at the 8 a second a town ran at, where 120 frames would
// be a quarter of a minute and a walk through a town would hardly ever see a side let go.
constexpr uint64_t kSideIdleMs=2000;
enum : uint32_t { kSideColour, kSideAlpha, kSideCookies, kSideStrands, kSides };
struct GameSide {
    HANDLE handle=nullptr;    // the texture's NT handle, kept while the atlas lives: the game's device's way back
    uint32_t idle=0;          // traced frames in a row in which the game's device did not write the atlas
    uint64_t wroteAt=0;       // when it last did, or took its side up (GetTickCount64)
    bool written=false;       // it did in this frame (hold_sides and the cleared scene's clears say so; tend_sides counts)
    bool stay=false;          // held from now on, as before: no handle, a second opening that failed, or a re-open that failed once
    uint32_t gone=0, back=0;  // times let go, and taken up again
    // Of its last move, for the log: the frames and the seconds since its last bake when it was let go (0 frames: no
    // frame was traced for a second), and how long the game's device took to open it again.
    uint32_t idleThen=0; double idleSeconds=0, openMs=0;
};
// The sides that moved together (in one frame, or one call of the maintenance event) and whose log line waits until
// Windows' count has followed: the game's device frees what it let go of at its next flush, and is counted again for a
// texture when it first uses it. direction: -1 let go, +1 taken up, 0 none waiting; mask: which atlases; before: the
// count before the first of them moved.
struct SideMoves { int32_t direction=0; uint32_t mask=0, waited=0; double before=0; };
struct D11Constants { uint32_t c0[4]; float c1[4]; };
// Kept in step with the Composite cbuffer in d3d11.hlsl.
// The guides pass reads profileF0 too (TGPT_SetProfileReflectance: a leaf's reflectance); the composite's cbuffer stops before it.
struct CompositeConstants { float invViewProj[16]; float cameraDelta[4]; uint32_t frameSize[4]; uint32_t extras[4]; float profileF0[16]; };
static_assert(sizeof(CompositeConstants)==176, "composite constants layout");
// Kept in step with the Hair cbuffer in d3d11.hlsl (TGPT_HAIR): TGPT_HairColor's values from its tiling on, as they are.
struct HairConstants { float v[48]; };
static_assert(sizeof(HairConstants)==192 && sizeof(TGPT_HairColor)==48+sizeof(HairConstants), "hair constants layout");
// Kept in step with the Eye cbuffer in d3d11.hlsl (TGPT_EYE): TGPT_EyeColor's values from its sclera tint on, as they are.
struct EyeConstants { float v[16]; };
static_assert(sizeof(EyeConstants)==64 && sizeof(TGPT_EyeColor)==24+sizeof(EyeConstants), "eye constants layout");
// One frame's HDRP textures as D3D12 reads them, and its traced light as its composite reads it.
struct Inputs { Shared gb0, nrm, gb2, gb3, depth, motion, exposure; };
// c: RGB specular reflectance and A reflection hit distance, for Ray Reconstruction; g0, g1: its guides where the tracer knows the pixel better
// than HDRP's GBuffer (a surface drawn forward, the hair a pixel's own ray crossed: pt_primary write_guides, pt_compose
// traced_guides, d3d11.hlsl traced_guides).
// (dm and mv, PT-112: the frame's own device depth (R32) and motion (R16G16) for HDRP's buffers. Two textures, in formats
// both devices share: an R32G32 one could not be made shareable on the game's device, 0x80070057, run 20261011-000324.)
struct Outputs { Shared a, b, c, g0, g1, dm, mv; };
// A frame's traced light for its own composite: the consumer fence's value once it is in the output textures, and their size.
struct Result {
    bool valid=false;
    uint64_t retire=0;
    uint32_t width=0, height=0;
};

struct State {
    // D3D11
    uint32_t generation=0;
    ComPtr<ID3D11Device5> d11;
    ComPtr<ID3D11DeviceContext4> c11;
    ComPtr<ID3DDeviceContextState> scratch;
    ComPtr<ID3D11ComputeShader> csAlbedo, csFloat4, csDepth, csMotion, csExposure, csSkyCube, csSkyArray, csAtlas, csAlpha, csComposite, csGuides;
    ComPtr<ID3D11ComputeShader> csCookieCube, csCookie2D; // lights' cookies into their tiles
    ComPtr<ID3D11ComputeShader> csHair; ComPtr<ID3D11Buffer> hairConstants; // RealHair's colors into their tiles
    ComPtr<ID3D11ComputeShader> csHairLobes;                                // and its strands into their lobe tiles
    ComPtr<ID3D11ComputeShader> csStrandAlpha, csStrandMip;                 // and their alpha at its own size (the strand atlas)
    ComPtr<ID3D11ComputeShader> csEye; ComPtr<ID3D11Buffer> eyeConstants;   // the eyes' colors into theirs
    // DLSS Ray Reconstruction's guides (D3D11, the render size), and replaced ones kept alive a while.
    struct Guide { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11UnorderedAccessView> uav; };
    std::array<Guide, 5> guides; uint32_t guideWidth=0, guideHeight=0; // 3, 4: written by the composite
    std::vector<std::pair<uint64_t, ComPtr<ID3D11Texture2D>>> retiredGuides;
    ComPtr<ID3D11VertexShader> vsComposite; ComPtr<ID3D11PixelShader> psComposite;
    // PT-112 (A): the frame's own depth and motion into HDRP's buffers (write_depth_motion).
    ComPtr<ID3D11VertexShader> vsDepthMotion; ComPtr<ID3D11PixelShader> psDepthMotion, psPyramid; ComPtr<ID3D11DepthStencilState> depthAlways;
    uint32_t summaryOwnDepth=0, summaryOwnPyramid=0, summaryOwnAsked=0; // (the 10-second line: frames that asked, wrote, wrote the pyramid too)
    ComPtr<ID3D11BlendState> compositeBlend; ComPtr<ID3D11RasterizerState> raster; ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11Buffer> constants, compositeConstants;
    ComPtr<ID3D11SamplerState> linearClamp, linearWrap;
    bool d3d11Ready=false, d3d12Ready=false; // set once everything ensure_d3d11 / ensure_d3d12 make exists
    bool recording=false;                    // a command list is being recorded (trace)
    // D3D12
    ComPtr<ID3D12Device> sharedDevice;
    bool dxr12=false;       // Shader Model 6.9 and raytracing tier 1.2 (device_ready)
    bool serReorders=false; // and Shader Execution Reordering does reorder on this GPU
    ComPtr<ID3D12Device5> d12;
    ComPtr<ID3D12CommandQueue> queue;
    tgb::SharedFence producer, consumer;
    std::array<ComPtr<ID3D12CommandAllocator>, kFrames> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList4>, kFrames> lists;
    std::array<uint64_t, kFrames> retired{};
    std::array<UploadRing, kFrames> rings;
    uint32_t listIndex=0;
    DescriptorHeap heap;
    ComPtr<ID3D12DescriptorHeap> decalSamplers;
    uint32_t sharedBase=kInvalid; // the game's own textures' views (texture_share.hpp), a range of the heap from its creation on
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> meter; // the traced light's histogram for its gain (pt_meter.hlsl)
    ComPtr<ID3D12PipelineState> prepare, probe, gi, giBoil, giSpatial, di, diSpatial, specular, temporal, atrous, compose, ommBake, rrgGuides, skinUnpack, walk, cache;
    ComPtr<ID3D12PipelineState> giReuse; // ReSTIR GI's reuse across frames (pt_gi_reuse), right after the bounced light's pass, in every frame
    // The game's fog, traced (PT-112 B, PT-118; TGPT_FRAME_TRACED_FOG; fog_froxel.inl): the camera's fog in cells. Its
    // three passes; the cells' light, this frame's and the frame before's taking turns; and the camera's fog as the
    // composite reads it (at the render size: the fog's light toward the camera, the way's transmittance), made when a
    // frame first asks for it. RGBA32F, not the 16-bit floats the other targets have: the fog's light over a short way
    // is a small part of a picture's light, and light is stored relative to the reference radiance (a white wall in
    // full sun, or 1 m from the scene's brightest light). Where one light is far brighter than the sun (the game's
    // vista: a reference of 5.7e8) the fog in front of a near rock was 0 to 8 of the SMALLEST steps a 16-bit float
    // has, each half a percent of white on the screen: it showed in steps, and the per-pixel route's blend of a tenth
    // of a frame into the last rounded to nothing (docs/agents/fogrule2.md, 2026-10-12).
    ComPtr<ID3D12PipelineState> fogFroxel, fogIntegrate, fogTail;
    Texture fogGrid[2], tracedFog; uint32_t fogGridFrame=0; float fogGridReference=1.f;
    uint32_t fogGridScene=0, fogGridMode=0;
    ComPtr<ID3D12PipelineState> sway; // swaying cloth's pose (pt_sway)
    ComPtr<ID3D12PipelineState> waterMesh; // the water's waves as triangles: their pose (pt_water_mesh)
    ComPtr<ID3D12PipelineState> emissive; // the emitter records (pt_emissive.hlsl)
    // The radiance cache (pt_common.hlsli): its keys, a frame's sums and its light, for kCacheSlots cells; whether it ran
    // last frame (turned back on, it starts empty), and the scene it holds the light of (a clear's generation).
    Buffer cacheKeys, cacheSums, cacheLight; uint32_t cacheKeysUav=kInvalid, cacheSumsUav=kInvalid, cacheLightUav=kInvalid;
    bool cacheRan=false; uint32_t cacheScene=0;
    // DirectX Raytracing 1.2: the Shader Model 6.9 passes (their ray queries accept opacity micromaps),
    // and the GI and reflection passes as ray generation shaders that reorder their threads (SER).
    bool useDxr12=false;
    ComPtr<ID3D12StateObject> giSer, specularSer; Buffer serTable;
    // PT-99 (the hidden "Speed trial", bit 8; trace_event sets it each frame): the two passes as plain compute passes.
    bool plainPasses=false;
    ComPtr<ID3D12QueryHeap> queries; Buffer queryReadback; double ticksPerMs=0;
    // The light at the camera (pt_probe.hlsl): its two sums, and a read-back slot per frame in flight.
    Buffer probeBuffer, probeReadback; uint32_t probeUav=kInvalid;
    // The traced light's histogram (pt_meter.hlsl, bins 0 to 63) and that of the bounced light handed to NRD (pt_nrd_prep,
    // bins 64 to 127), and a read-back slot per frame in flight.
    Buffer meterBuffer, meterReadback; uint32_t meterUav=kInvalid;
    static constexpr uint32_t kMeterBins=128;
    std::array<bool, kFrames> queried{};
    // PT-99 (R19, docs/rounds/round-19.md 4): how the reflections' pass regroups its threads (pt_specular.hlsl): by the
    // material each pixel's ray met (16 bits), or with no hint at all (the frame word's bit 4). The same picture either way;
    // which is faster depends on the scene (run 20261010-175041, ms of the reflections' stage: the beach 91 to 93 by the
    // material, 75.5 with no hint; a town 22.9 and 30.6; the tavern 9.3 and 10.0). So the tracer measures: it runs its
    // choice, tries the other for kSortProbe frames every kSortEvery, and keeps the faster by 5 %. The hidden "Speed trial"
    // 16 holds the material, 32 no hint (for measuring).
    struct ReflectionsSort {
        bool noHint=false;                  // the choice
        uint32_t frames=0, probeLeft=0;     // frames since the last probe ended; frames of a probe still to run
        std::array<uint8_t, kFrames> used{}; // a list slot's frame: 0 not counted, 1 / 2 the choice by the material / no hint, 3 / 4 a probe's
        std::vector<double> mine, other;    // the stage's time with the choice (the frames before a probe) and in the probe
        std::vector<double> mineScene, otherScene; // the bounced light's time in those frames: the same scene, or no verdict
        uint32_t switches=0, probes=0; double lastMine=0, lastOther=0;
        // Probes in a row that kept the choice: each doubles the wait for the next, up to 8 times kSortEvery (a probe runs the
        // slower variant for its frames: every 128 frames that cost 0.4 to 0.8 ms a frame on average where one choice keeps
        // winning, docs/agents/review-perf-astra.md 11). A switch or another view starts over at kSortEvery.
        uint32_t kept=0;
    } sort;
    // How far the GPU got (WriteBufferImmediate, each written once the work before it is done, into memory the CPU still
    // reads after the device is lost), for the log when the GPU stops (report_gpu_stop): slot 0 a frame's last mark done
    // (its number << 8 | the mark: 0 its start, k the end of stage k - 1, kStageNames), slot 1 the water's (<< 8 | 1 begun,
    // 2 done), slot 2 the scene stage's structures (<< 8 | 1 + the kind whose commands just ended, kStructureNames: the
    // scene stage hung once, 2026-10-11, with no finer mark), slot 3 that stamp's number within its frame (<< 8 | n:
    // "the rest of the scene stage" is stamped at seven places, and three hangs of 2026-10-11 and -12 named only it;
    // with the kinds the CPU recorded for that frame, structKinds, the number names the kind whose work did not end),
    // slots 4 and 5 the plants' own steps (the last one done and its frame: GpuScene::structureNote, PT-125);
    // and the last of each recorded, to tell a stage that never finished from a frame that never started.
    Buffer crumbs; bool crumbsOn=false; uint32_t crumbsRecorded=0;
    DeferredRelease release;
    // Shared with D3D11.
    Shared atlas, alphaAtlas, sky, cookieAtlas;
    Shared fogSky; // HDRP's sky at the mips its fog color is read at (PT-37: update_sky, pt_common.hlsli fog_sky)
    // Characters: their skinned positions a frame, per parity (D3D11 writes one while D3D12 may read the other).
    std::array<Shared, 2> skin;
    struct SkinWork { uint32_t firstTexel=0, count=0; TGPT_SkinnedInstance instance{}; };
    std::array<std::vector<SkinWork>, 2> skinWork;
    // ON TRIAL (PT-92): this parity's texture holds each vertex's skinned normal in its alpha (copy_skinned; pt_skin.hlsl).
    std::array<bool, 2> skinNormals{};
    // The D3D11 copy pass, compiled for the skinning output's layout (raw, or structured of a given stride).
    std::map<uint32_t, ComPtr<ID3D11ComputeShader>> skinCs;
    ComPtr<ID3D11Buffer> skinSourceBuffer; ComPtr<ID3D11ShaderResourceView> skinSourceSrv; uint32_t skinSourceKey=0;
    bool skinLogged=false;
    std::array<Inputs, 2> inputs;
    Outputs outputs; // the frame's traced light: written by its trace, read by its composite
    std::array<uint64_t, 2> inputsRetire{}; // the consumer value after which D3D12 no longer reads each input set
    uint32_t width=0, height=0;
    // D3D12 only, per render size.
    std::array<Texture, 2> surface, normal, material;
    // RTXDI's DI and GI reservoirs: a buffer each, two arrays (this frame's and the previous frame's) in its block-linear layout.
    Buffer diRes, giRes; uint32_t diUav=kInvalid, giUav=kInvalid, rtxdiRowPitch=0, rtxdiArrayPitch=0;
    Buffer neighborOffsets; uint32_t neighborOffsetsSrv=kInvalid;
    Texture diffuseRaw, sunRaw, specRaw, tmpA, tmpB, tmpC, tmpD, directTmp, directOut;
    std::array<Texture, 2> diffuseHist, diffuseMoments, directHist, directMoments, specHist, specMoments;
    // What HDRP shades itself: this frame's shares of its light the traced shadows leave, and their history (whether it ran
    // last frame: turned back on, it starts over).
    // Hair's highlights (pt_prepare, pt_di_spatial): a hair pixel's strand direction and its material, this frame's alone.
    Texture hair;
    // What each pixel's reflection shows, for the reflections' filter (pt_common pack_seen; PT-80).
    Texture specSeen;
    // The tracer's own camera rays (pt_primary, PT-59): each pixel's motion from its hit's own previous position, its hit's
    // emission, and the yardstick's word for it (that hit against HDRP's G-buffer).
    Texture ownMotion, ownEmission, yardstick;
    // The yardstick's counters (pt_yardstick.hlsl), read back as the meter's are: the traced frame each read-back slot counted
    // (0: none), and when its line was last logged.
    ComPtr<ID3D12PipelineState> yardPass;
    Buffer yardBuffer, yardReadback; uint32_t yardUav=kInvalid;
    std::array<uint32_t, kFrames> yardSlot{}; std::array<bool, kFrames> yardSlotOwn{}; uint64_t yardLogged=0;
    // Hair's strands at their textures' own size (bake_strands): their diffuse maps' alpha in blocks of an R8 atlas with its mips,
    // shared with D3D12 (each level's views on D3D11, to bake a block and make its mips); the blocks still free by size
    // (kStrandBlock, then each quarter of it down to an eighth: a corner each, x | y << 16), the block each map took (the map
    // held, so that no other texture takes its address), and per detail tile what its hair reads (FrameConstants.strandTiles).
    Shared strandAtlas;
    std::vector<ComPtr<ID3D11UnorderedAccessView>> strandUavs;
    std::vector<ComPtr<ID3D11ShaderResourceView>> strandSrvs;
    std::array<std::vector<uint32_t>, 4> strandFree;
    struct StrandBlock { ComPtr<ID3D11Resource> map; uint32_t corner=0, size=0; };
    std::vector<StrandBlock> strandBlocks;
    float strandTiles[2*kMaxDetailTiles][4]{};
    // PT-87 (memory4): the game's side of the colour, alpha, cookie and strand atlases, held only around a bake (GameSide);
    // how often a side was let go and taken up again since the tracer's start, and the tracer's own shared textures as
    // the memory line last summed them (ownShared), for what of them Windows counts twice now (tell_own_shared).
    std::array<GameSide, kSides> sides{};
    SideMoves sideMoves; uint32_t sideGroups=0; // the moves whose line waits, and how many such groups there were (say_side_moves)
    uint32_t sidesGone=0, sidesBack=0; uint64_t ownSharedBytes=0;
    // AMD FSR Ray Regeneration (Radeon RX 9000): its guides at the render size and its context. A failure
    // leaves the built-in denoiser for the rest of the session.
    Texture rrgDepth, rrgMotion, rrgNormals, rrgDiffuse, rrgSpecular;
    RayRegeneration rrg;
    bool rrgFailed=false, rrgActive=false;
    double rrgScale=0; // fixed radiance units for AMD history, independent of the easing storage reference
    uint32_t rrgLastFrame=0; bool rrgHavePrevious=false;
    // NVIDIA NRD (RELAX, or REBLUR on trial): its inputs and outputs at the render size (made when it first denoises), its
    // texture pools, and the last frame's camera for its previous-frame matrices. A failure leaves the built-in denoiser for
    // the rest of the session.
    Texture nrdViewZ, nrdMotion, nrdNormalRoughness, nrdDiffuseIn, nrdDiffuseOut, nrdSpecularIn, nrdSpecularOut;
    NrdConfidenceResources nrdConfidence;
    // PT-127: what NRD is told of a pixel that shows a still pure mirror (primary surface replacement), one record a
    // pixel while NRD denoises with the tracer's own camera rays; the serial a frame's records are stamped with.
    PrimarySurfaceReplacementResources psr;
    uint32_t psrSerial=0;
    ComPtr<ID3D12PipelineState> nrdConfidenceTrace, nrdConfidenceFilter;
    Nrd nrd;
    bool nrdFailed=false, nrdActive=false, nrdHavePrevious=false;
    double nrdScale=0; // the radiance (HDRP's units) NRD's 1 stands for: kept until its history starts over
    // The real frame rate NRD's history is set by (kinds 1 and 2, on trial: NrdFrame::frameRate): the time between the frames
    // NRD ran, eased; 0 until two of them ran in a row.
    std::chrono::steady_clock::time_point nrdLastAt{}; double nrdFrameSeconds=0;
    // The bounced light's median as NRD was handed it, in HDRP's units, from the histogram a few frames back (0: none yet),
    // and the scale each frame in flight handed it over with (0: NRD did not run in that frame).
    double nrdMedian=0, nrdSlotScale[8]{};
    float nrdView[16]{}, nrdProjection[16]{}, nrdJitter[2]{}; double nrdCamera[3]{};
    bool denoisedLast=false; // the built-in denoiser ran last frame (its history is current)
    bool ownDenoisedLast=false; // the direct light's and the reflections' own filters ran last frame (built-in denoiser or NRD)
    // PT-87 (docs/agents/memory-1.md): the built-in filters' textures exist only while frames run the filter they serve (the
    // bounced light's: the built-in denoiser alone; the direct light's and the reflections': it and NRD), and a denoiser's
    // own (NRD's inputs and pools, Ray Regeneration's guides and context) only while it is the one that runs: the traced
    // frames in a row that went without each (kIdleFrames of them: let go; ensure_filter_textures, release_idle_denoisers).
    uint32_t diffuseFilterIdle=0, ownFilterIdle=0, nrdIdle=0, rrgIdle=0;
    uint64_t lastTraceTick=0; // when a frame was last handed over (GetTickCount64): release_frame_memory
    double memoryCamera[3]{}; bool haveMemoryCamera=false; // the last traced frame's camera in the world: the memory's count by distance
    uint32_t parity=0;
    GpuScene scene;
    // Bookkeeping.
    bool broken=false;
    uint64_t frame=0;
    double origin[3]{}, prevOrigin[3]{};
    bool haveOrigin=false, resetNext=true;
    // The last traced frame's index (a gap means frames went untraced: their history no longer fits) and the reference
    // radiance its light was stored against.
    bool haveFrameIndex=false; uint32_t lastFrameIndex=0; double reference=0, clampReference=0;
    // The last traced frame's sub-pixel shift as its camera rays carried it (camera_shift.hpp held_camera_shift): what
    // the next frame reuses of it lies in a pixel grid shifted by that (FrameConstants.look's y and z).
    float cameraShift[2]{}; bool haveCameraShift=false;
    uint32_t skyVersion=0; uint64_t skyFrame=0; bool skyValid=false;
    bool sceneResourcesDirty=false;
    uint64_t accountedBytes=0, accountedTransfer=0; // the memory line's sum, and its rings and read-backs (publish_memory)
    uint64_t lastSummary=0, summaryFrames=0; double summaryGpu=0, summaryCpu=0;
    // The stages' GPU time (kStageNames): summed for the log's summary, and over about a second for TGPT_GetStageTimes.
    std::array<double, kStages> summaryStage{}, recentStage{};
    uint32_t summaryStageFrames=0, recentFrames=0; uint64_t recentStart=0;
    // PT-109: the structures' work by kind (kStructureNames). Each list slot's stamps and their kinds; the GPU time summed for
    // the log, the frames it was read of, and what the scene counted (builds, refits, triangles, bytes) of every frame recorded.
    ComPtr<ID3D12QueryHeap> structQueries; Buffer structReadback;
    std::array<uint32_t, kFrames> structCount{}, structResolved{}; std::array<std::array<uint8_t, kStructMarks>, kFrames> structKinds{};
    std::array<uint32_t, kFrames> structFrame{}; // the frame each list's stamps are of (report_gpu_stop)
    std::array<double, GpuScene::kStructureKinds> summaryStruct{}; uint32_t summaryStructFrames=0, summaryStructRecorded=0;
    std::array<GpuScene::StructureWork, GpuScene::kStructureKinds> summaryStructWork{};
    // (PT-109, megageo-5: the same over about a second, for TGPT_GetStageTimes, which the harness's walk reads a second at a time.)
    std::array<double, GpuScene::kStructureKinds> recentStruct{}; uint32_t recentStructFrames=0, recentStructRecorded=0;
    std::array<GpuScene::StructureWork, GpuScene::kStructureKinds> recentStructWork{};
    std::array<double, 4> phase{}, summaryPhase{}; // event 1: D3D11 work, flush, waiting for a free list, recording
    uint32_t failures=0;
    uint64_t composited=0, compositeWaits=0, compositeSkipped=0;
    // PT-107 (R19): frames whose render event 1 found the frame before's trace already done on the card (the card then
    // waits for the CPU: the CPU is not ahead), of the frames counted.
    uint32_t summaryCpuLate=0, summaryCpuLateOf=0;
    // PT-107 (R19): the game's device's own GPU clock at the tracer's two events, so that a frame splits into what that device
    // does around the trace (the composite waits for the frame's own trace: the two devices take turns). A slot a frame, by its
    // sequence: [0] render event 1 begins, [1] the inputs are signalled to the D3D12 queue, [2] the composite before its wait
    // for the trace, [3] after it, [4] the next frame's event 1, [5] the composite's end, [6] and [7] Fog Adaptation's event
    // (render event 5) begins and ends. Read some frames later, never waited for (DONOTFLUSH).
    // (R19, later: [8] the next frame's Update mark, where the render thread has queued all of this frame; [9] the next
    // frame's HDRP rendering, its first command: render event 8's marks 0 and 1, d11_stamp_open.)
    // (PT-112, measure-waits only: [10] the next frame's BeforeRendering, [11] its AfterOpaqueDepthAndNormal, [12] this
    // frame's BeforeTransparent, [13] BeforePostProcess, [14] AfterPostProcess: HDRP's pass groups.)
    struct D11Stamps { ComPtr<ID3D11Query> disjoint; std::array<ComPtr<ID3D11Query>, 15> at; uint32_t sequence=0, reached=0; bool open=false, pending=false; };
    std::array<D11Stamps, 4> d11Stamps; int d11Open=-1; bool d11StampsFailed=false;
    // a frame's means: the tracer's inputs, the game to the composite, the wait, the composite, Fog Adaptation, the game after
    std::array<double, 6> summaryD11{}; uint32_t summaryD11Frames=0;
    double summaryD11ToFog=0; uint32_t summaryD11FogFrames=0; // of the game after: from the composite to its fog (frames with Fog Adaptation's event)
    // of the game after, by [8] and [9]: Fog Adaptation's end to the frame's end, the frame's end to the next frame's
    // rendering, that rendering to the next frame's trace (frames with both marks)
    std::array<double, 3> summaryD11Next{}; uint32_t summaryD11NextFrames=0;
    // PT-112: HDRP's pass groups (frames with all five injection points): the fog's end to BeforeTransparent, the
    // transparents, post-processing, after it to the frame's end, the gap, the next frame's start to BeforeRendering,
    // the depth prepass, after it to our trace
    std::array<double, 8> summaryHdrp{}; uint32_t summaryHdrpFrames=0;
    // PT-107 (R19): the CPU's clock (QueryPerformanceCounter) at the same events, a record a frame by its sequence: [0] to [7]
    // as the stamps above, [8] the inputs flushed. The frame's D3D12 trace is put on that clock by the queue's calibration
    // (calGpu, calCpu: taken when its list slot is recorded), when its stamps are read.
    struct CpuClock { uint32_t sequence=0, reached=0; std::array<int64_t, 9> at{}; };
    std::array<CpuClock, 16> cpuClocks;
    std::array<uint32_t, kFrames> listSequence{}; std::array<uint64_t, kFrames> calGpu{}, calCpu{}; uint32_t traceSequence=0;
    // a frame's means (ms): the trace's start after the inputs' flush; the CPU at the composite, at Fog Adaptation and at the
    // next frame's trace, each after the trace's end (below 0: before it); the CPU from the composite's end to Fog Adaptation,
    // and from Fog Adaptation to the next frame's trace
    std::array<double, 6> summaryCpuClock{}; uint32_t summaryCpuClockFrames=0;
    // And the next frame's start (Engine::MainClock, render event 8), each after the trace's end: the main thread's Update,
    // the render thread reaching that Update's mark, HDRP's rendering begun on the main thread and its first command on the
    // render thread, the main thread's capture pass begun and done, HDRP's rendering done on the main thread.
    // (R19, later: the render thread at LateUpdate's mark and at a mark queued at once when HDRP's rendering begins; and the
    // main thread's LateUpdate.)
    struct RenderMark { uint32_t frameCount=0; std::array<int64_t, 4> at{}; };
    std::array<RenderMark, 16> renderMarks{}; // by Unity's frame count: render event 8's clock
    std::array<double, 10> summaryNextFrame{}; uint32_t summaryNextFrames=0;
    // Caustics (TGPT_SetWaterCaustics): each slot's wave buffers (HDRP's displacement and additional data arrays, their top mip),
    // copied on D3D11 into textures shared with D3D12 per input parity, and HDRP's numbers for them. live: the slot holds water
    // (waves: its buffers copied; still water without).
    struct WaveCopy { ComPtr<ID3D11Texture2D> d11; ComPtr<ID3D12Resource> d12; uint32_t srv=kInvalid; UINT size=0, slices=0; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN; };
    struct Caustics { WaveCopy displacement, normals; bool live=false, waves=false; TGPT_WaterCaustics desc{}; };
    std::array<std::array<Caustics, TGPT_MAX_WATER>, 2> caustics;
    // The caustics' light where it lands (pt_caustics.hlsl): its triangles' corners (kCausticCorners bytes each: the vertices of
    // an acceleration structure built over them every frame, caustic_light's search) and light (kCausticLight bytes each), the
    // structure's bottom and top levels and their scratch (the top level's one instance in each frame's ring), and the counters;
    // a read-back slot per frame in flight for the counters, by which the room grows.
    Buffer causticCorners, causticLight, causticBlas, causticBlasScratch, causticTlas, causticTlasScratch, causticCounters, causticReadback;
    uint32_t causticCornersUav=kInvalid, causticLightUav=kInvalid, causticTlasSrv=kInvalid, causticCountersUav=kInvalid, causticCapacity=0, causticWanted=0;
    // The triangles the frame being recorded builds its structure over (the first causticBuilt of the room: half as many again
    // as the last frames wrote, so a build doesn't walk a room that a busy scene once filled: 10 ms over 16 million unused
    // ones), and what the last frames wrote.
    uint32_t causticBuilt=0, causticRecent=0;
    // PT-87 (ensure_caustic_room): the triangles the buffers hold (causticCapacity is the most a frame may write, a number),
    // the triangles of the structure the top level names (the last build's), the frames in a row that built over half
    // the room or less, and those that ran no caustics pass.
    uint32_t causticRoom=0, causticStructure=0, causticRoomIdle=0, causticIdle=0;
    // PT-114, on trial (TGPT_FRAME_CAUSTICS_GRID; caustic_tile_list): the tiles listed for the frame being recorded, per
    // frame slot the view of them in that slot's upload ring (as the fog boxes' records have one), and since the last status
    // line: the frames with a list, the tiles listed and those left out with no water of their slot under them.
    std::vector<uint32_t> causticTileList;
    std::array<uint32_t, kFrames> causticTileSrv{kInvalid, kInvalid, kInvalid};
    // And the listed tiles' picks of their lights (pt_caustics.hlsl pick_tile: 16 bytes a tile, room for every tile of every
    // level and slot, 0.6 MB), which the pick's own pass writes and the tiles' pass reads in the same frame.
    Buffer causticPicks; uint32_t causticPicksUav=kInvalid;
    uint64_t causticListFrames=0, causticListed=0, causticDry=0;
    double causticListMs=0; // (what the list took on the CPU, summed)
    int causticListSaid=-1;
    // What the waves are where the tracer reads them (pt_caustics's counters), per slot since the last summary: the highest a
    // wave moved the surface (m), the steepest slope, and the last sample (a grid point's x and z, the surface there, its
    // normal: relative to the scene origin of its frame, kept with it) and that origin.
    struct WaveSeen { float high=0, steep=0; float sample[8]{}; double origin[3]{}; bool sampled=false; uint64_t frames=0; };
    std::array<WaveSeen, TGPT_MAX_WATER> causticWaves;
    std::array<std::array<double, 3>, kFrames> causticOrigin{};
    std::array<bool, kFrames> causticRead{};
#ifdef TGPT_SPEED_MEASURE
    // A measuring build's counters (kSpeedCounters): the buffer the two big passes add to (running sums, never emptied:
    // a frame's own counts are its read-back less the frame before's), a read-back slot per frame in flight, the sums
    // last seen, and the counts since the last line in the log.
    Buffer speedCounters, speedReadback; uint32_t speedCountersUav=kInvalid;
    std::array<bool, kFrames> speedRead{}; bool speedHaveLast=false, speedSaidNone=false;
    std::array<uint32_t, kSpeedCounters> speedLast{}; std::array<uint64_t, kSpeedCounters> speedSums{};
    uint32_t speedFrames=0; uint64_t speedLogged=0;
#endif
    ComPtr<ID3D12PipelineState> causticPass;
    // Where the water has been (pt_wet.hlsl): the store of the world's cells round the camera, emptied before its first frame and
    // with another scene; the game's shader time of the frame being traced (TGPT_SetClothWind's), its clock.
    Buffer wetStore; uint32_t wetStoreUav=kInvalid; ComPtr<ID3D12PipelineState> wetPass;
    bool wetRan=false; uint32_t wetScene=0; float wetClock=0.f;
    // The highest any water stood since the store was emptied (relative to the scene origin): no ground above it was ever wet.
    float wetCeiling=-1e30f;
    // The rain's wetness of the frame being traced (TGPT_SetRain as its TGPT_SubmitFrame found it; PT-108, on trial).
    TGPT_Rain rain{};
    // The game's fog boxes of the frame being traced (TGPT_SetFogBoxes, PT-112), and per frame slot the view of their
    // records in that slot's upload ring (written again each frame the slot comes round: its GPU work is done by then).
    std::vector<TGPT_FogBox> fogBoxes;
    std::array<uint32_t, kFrames> fogBoxSrv{kInvalid, kInvalid, kInvalid};
    uint32_t fogBoxesSaid=UINT32_MAX;
    // PT-112, the sky, on trial (docs/agents/sky-2.md; pt_sky.hlsl, record_sky): the sky of the frame being traced
    // (TGPT_SetSkyModel as its TGPT_SubmitFrame found it); the atmosphere's tables, made again when their numbers change
    // (skyTablesKey), the clouds baked again when HDRP would (skyCloudsKey: its baking hash, the maps, the size), their ambient
    // light (a texel) and the map, every frame; all made when a frame first asks for them (ensure_sky_textures). skyOwn: this
    // frame's constants name them.
    TGPT_Sky skyModel{};
    struct MipTexture {
        ComPtr<ID3D12Resource> resource; D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON; uint32_t srv=kInvalid, mips=0;
        std::array<uint32_t, 16> uav; MipTexture() { uav.fill(kInvalid); }
    } skyMap;
    Texture skyTransmittance, skyMultiple, skyGround, skyClouds, skyAmbient;
    uint64_t skyTablesKey=0, skyCloudsKey=0, skyTablesMade=0, skyCloudsBaked=0;
    bool skyTablesPending=false, skyCloudsPending=false, skyOwn=false, skyFailed=false;
    uint32_t skyModeSaid=UINT32_MAX;
    ComPtr<ID3D12PipelineState> skyPass;
    // The water's kept wave corners (pt_sun_grid.hlsl): the buffer, made when a slot first has a grid; the pass; which slots
    // have a grid this frame (a bit a slot: the pass runs once for each).
    Buffer sunGrid; uint32_t sunGridUav=kInvalid; ComPtr<ID3D12PipelineState> sunGridPass; uint32_t sunGridSlots=0;
    uint64_t causticFrames=0, causticTriangleSum=0; uint32_t causticMost=0;
    // The sun's light through the water as it landed since the last status line (pt_caustics.hlsl's counters [48 + 8 i + l],
    // its SUM_*, per level l): 0 what the triangles' corners carried, 1 the part on triangles whose corners met different
    // surfaces, 2 what the trial (TGPT_OPTION_CAUSTICS_SPLIT) landed of that part; 3 to 7 the rippling light where it
    // landed (the landed area across the beam, the light, the light times its brightness, the rays' lengths, the count);
    // 8 and 9 the waves' slopes squared and the corners.
    std::array<std::array<uint64_t, kCausticLevels>, 10> causticSums{};
    // The caustics' tiles since the last status line (pt_caustics.hlsl's counters from kCausticTileCounts on), per level and
    // slot (4 l + s): 0 the tiles that ran, 1 those with a corner on their slot's water, 2 those a light reached, 3 those
    // that wrote a triangle (PT-114). And the frame each read-back slot's counters are of.
    std::array<std::array<uint64_t, kCausticLevels*TGPT_MAX_WATER>, kCausticTileKinds> causticTiles{};
    std::array<uint32_t, kFrames> causticFrame{};
} st;
void crumb(ID3D12GraphicsCommandList4* list, uint32_t slot, uint32_t value);

// The passes, made on a thread of their own from the moment the shared device exists (device_ready, start_passes), while the
// game goes on starting: the driver compiles each D3D12 pass (after a new version of the mod or of the driver, until its
// shader cache has them: 13 to 22 s in round 3) and the D3D11 ones are compiled from d3d11.hlsl (a second or two), and the
// first traced frame waited for all of it on the game's render thread, the game frozen meanwhile. Until they are made,
// TGPT_Status.starting holds the plugin back and a frame handed over anyway goes untraced (not a failure).
struct Passes {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> nrdConfidenceTrace, nrdConfidenceFilter;
    bool dxr12=false; // the Shader Model 6.9 set, else 6.6
    ComPtr<ID3D12PipelineState> prepare, probe, gi, giReuse, giSpatial, di, diSpatial, specular, temporal, atrous, compose, caustics, wet;
    ComPtr<ID3D12PipelineState> fogFroxel, fogIntegrate, fogTail; // PT-112 B, PT-118: the camera's fog, traced, in cells
    ComPtr<ID3D12PipelineState> ommBake, rrgGuides, giBoil, meter, walk, cache, skinUnpack, emissive, sway, waterMesh, yardstick, sunGrid;
    ComPtr<ID3D12PipelineState> sky; // PT-112, the sky, on trial (pt_sky.hlsl)
    ComPtr<ID3D12StateObject> giSer, specularSer;
};
struct Warmup {
    std::mutex mutex;
    bool started=false, done=false;
    std::unique_ptr<Passes> passes;                 // once done: made, or null after a failure (ensure_d3d12 then makes them)
    std::map<std::string, ComPtr<ID3DBlob>> d3d11;  // compiled D3D11 passes by shader_key
} warm;
bool passes_starting() { std::lock_guard<std::mutex> g(warm.mutex); return warm.started && !warm.done; }

// PT-87 (docs/agents/memory-2.md, "memory3"): the tracer's start in what Windows counts for the process on the card. In the
// game's log of 2026-10-09 20:25 about 2 GB of that count arrived with the tracer and were in no figure of "PT memory
// bytes". These lines say where: "PT memory at start: <step> +N MB (Windows counts M MB)" for each step of the start, and
// "PT memory since the tracer's start" for what comes after, split by whether a call of the tracer's was running when it
// came (start_summary). Windows counts for the whole process: while the game loads, a step's move is the game's too (a
// step takes milliseconds, so little of it). Read only: nothing here changes what is made, or when.
struct CountNow { double local=0, other=0; bool known=false; }; // MB: the card's own memory; the PC's memory the card uses beside it
CountNow count_now(ID3D12Device* device) {
    uint64_t local=0, other=0; CountNow c;
    c.known=share::video_memory_now(local, other, device); c.local=double(local)/1048576.0; c.other=double(other)/1048576.0;
    return c;
}
// The first of the two alone (one query): what the process's budget is weighed against. 0: not known.
double windows_counts_mb(ID3D12Device* device) {
    uint64_t budget=0, counted=0;
    return share::video_memory(budget, counted, device) ? double(counted)/1048576.0 : 0.0;
}
// What came after the start, on the render thread (every event of the tracer's runs there): the count's moves while a call
// of the tracer's ran, by the call's part, and with none running. For ten minutes from the start (start_summary ends it):
// a frame's call reads the count five times meanwhile.
struct StartTally {
    bool on=false;
    uint64_t began=0; uint32_t frames=0;
    double base=0, baseOther=0;                 // the two counts when the start began
    double stepsDevice=0, stepsPasses=0;        // the start's steps (their own lines): those of a frame's D3D11 side, those among its passes
    double d3d11=0, flush=0, passes=0, rest=0;  // a frame's call: its D3D11 side, the flush and the wait for it, recording and running the passes, what is left
    double others=0;                            // the maintenance, composite and fog calls
    double between=0;                           // with no call of the tracer's running
    double lastExit=-1;                         // the count when the tracer's last call returned (-1: none yet)
    share::OpenCount opens0{};                  // the opens' sums when the start began (texture_share.hpp)
    int64_t opensInCalls=0;                     // of the opens' moves since, what came while a call ran (the plugin opens on its own thread)
    uint32_t summaries=0;                       // start_summary's lines so far
} tally;
// A chain of the start's steps on one thread: a step's line says what the count did since the step before it.
class StartSteps {
    ID3D12Device* device_; double* into_; CountNow first_, last_;
public:
    // into: the tally's sum these steps belong to (render thread), or null (another thread, or before the tally began).
    StartSteps(ID3D12Device* device, double* into) : device_(device), into_(into), first_(count_now(device)), last_(first_) {}
    const CountNow& first() const { return first_; }
    // The count's move since the last step or lap, with no line (for a sum over a loop).
    CountNow lap() {
        const CountNow now=count_now(device_);
        const CountNow moved{now.local-last_.local, now.other-last_.other, now.known && last_.known};
        last_=now; return moved;
    }
    // own: the tracer's own figure for what the step made, in bytes (UINT64_MAX: none to name). at: the counts at the
    // step's end where that is not the last step's or lap's.
    void say(const std::string& what, const CountNow& moved, uint64_t own=UINT64_MAX, bool whole=false, const CountNow* at=nullptr) const {
        if(!moved.known) return;
        if(into_ && !whole) *into_+=moved.local;
        const CountNow& now=at ? *at : last_;
        char figure[96]{}; if(own!=UINT64_MAX) std::snprintf(figure, sizeof figure, "; the tracer's own figure for it %.0f MB", double(own)/1048576.0);
        char text[768];
        std::snprintf(text, sizeof text, "PT memory at start: %s %+.0f MB (Windows counts %.0f MB; the PC's own memory beside it %+.0f MB, now %.0f MB%s)",
                      what.c_str(), moved.local, now.local, moved.other, now.other, figure);
        log(text);
    }
    void step(const std::string& what, uint64_t own=UINT64_MAX) { say(what, lap(), own); }
    // All the chain's steps together, after the last of them.
    void whole(const std::string& what, uint64_t own=UINT64_MAX) const {
        say(what, CountNow{last_.local-first_.local, last_.other-first_.other, first_.known && last_.known}, own, true);
    }
};
CountNow operator+(const CountNow& a, const CountNow& b) { return CountNow{a.local+b.local, a.other+b.other, a.known && b.known}; }
// A call of the tracer's on the render thread, for the tally: what came since the last call returned goes to `between`,
// what the call moves to its part (mark ends a part of a frame's call).
struct TallyCall {
    bool on=false; double at=0; int64_t opens=0;
    void enter(ID3D12Device* device) {
        on=tally.on && device; if(!on) return;
        at=windows_counts_mb(device); opens=share::open_count().moved;
        if(tally.lastExit>=0) tally.between+=at-tally.lastExit;
    }
    // The first frame's call began before the tally did (ensure_d3d12 begins it): it counts from the tally's base.
    void adopt() { if(!on && tally.on) { on=true; at=tally.base; opens=tally.opens0.moved; } }
    void mark(ID3D12Device* device, double& part) { if(!on || !device) return; const double now=windows_counts_mb(device); part+=now-at; at=now; }
    void leave(ID3D12Device* device, double& part) {
        if(!on || !device) return;
        mark(device, part); tally.lastExit=at; tally.opensInCalls+=share::open_count().moved-opens;
    }
};
// The line for what came since the start. total, transfer: the memory line's own sum and its rings and read-backs, bytes.
void start_summary(ID3D12Device* device, uint64_t total, uint64_t transfer) {
    if(!tally.on || !device) return;
    const CountNow now=count_now(device); if(!now.known) return;
    const share::OpenCount opens=share::open_count();
    const double mb=1.0/1048576.0, steps=tally.stepsDevice+tally.stepsPasses;
    const double openMoved=double(opens.moved-tally.opens0.moved)*mb, openInCalls=double(tally.opensInCalls)*mb, openBetween=openMoved-openInCalls;
    const double calls=tally.d3d11+tally.flush+tally.passes+tally.rest+tally.others;
    char text[1536];
    std::snprintf(text, sizeof text,
        "PT memory since the tracer's start: %u frames, %.0f s: Windows' count moved by %+.0f MB to %.0f MB (the PC's own memory beside it by %+.0f MB to %.0f MB). "
        "While a call of the tracer's ran: %+.0f MB (the start's steps %+.0f; the rest of the frames' D3D11 side %+.0f; their flush and the wait for it %+.0f; the rest of "
        "recording and running the passes %+.0f; the end of a frame's call %+.0f; maintenance, composite and fog %+.0f), of it %+.0f MB while the game's textures were "
        "being opened on another thread. Opening %llu of the game's textures (%.0f MB as the tracer counts them) moved the count by %+.0f MB. With no call of the tracer's "
        "running: %+.0f MB, of it %+.0f MB during such opens: so %+.0f MB came from elsewhere (the game itself, other mods, what Windows counts late). The memory "
        "line sums %.0f MB, %.0f MB of it rings and read-backs",
        tally.frames, double(GetTickCount64()-tally.began)/1000.0, now.local-tally.base, now.local, now.other-tally.baseOther, now.other,
        calls, steps, tally.d3d11-tally.stepsDevice, tally.flush, tally.passes-tally.stepsPasses, tally.rest, tally.others, openInCalls,
        static_cast<unsigned long long>(opens.textures-tally.opens0.textures), double(opens.bytes-tally.opens0.bytes)*mb, openMoved,
        tally.between, openBetween, tally.between-openBetween, double(total)*mb, double(transfer)*mb);
    log(text); ++tally.summaries;
    if(GetTickCount64()-tally.began>600000) {
        tally.on=false;
        log("PT memory since the tracer's start: ten minutes on, the count is no longer followed call by call (the memory line goes on)");
    }
}

std::string shader_key(const char* macro, const char* entry, const char* profile, const D3D_SHADER_MACRO* extra) {
    std::string key=std::string(macro)+"/"+entry+"/"+profile;
    for(const D3D_SHADER_MACRO* d=extra; d && d->Name; ++d) key+=std::string("/")+d->Name+"="+(d->Definition ? d->Definition : "");
    return key;
}
ComPtr<ID3DBlob> compile_d3d11_now(const char* macro, const char* entry, const char* profile, const D3D_SHADER_MACRO* extra) {
    const auto began=std::chrono::steady_clock::now();
    const auto key=pass_cache::hash(d3d11Shader, std::strlen(d3d11Shader), shader_key(macro, entry, profile, extra));
    const auto cached=pass_cache::read(key);
    if(!cached.empty()) {
        ComPtr<ID3DBlob> blob;
        if(SUCCEEDED(D3DCreateBlob(cached.size(), &blob))) {
            std::memcpy(blob->GetBufferPointer(), cached.data(), cached.size());
            pass_cache::timing(macro, "D3D11 bytecode load", began, true); return blob;
        }
    }
    std::vector<D3D_SHADER_MACRO> defines{{macro, "1"}};
    for(const D3D_SHADER_MACRO* d=extra; d && d->Name; ++d) defines.push_back(*d);
    defines.push_back({nullptr, nullptr});
    ComPtr<ID3DBlob> bytecode, errors;
    const HRESULT r=D3DCompile(d3d11Shader, std::strlen(d3d11Shader), "TGPathTracing/d3d11.hlsl", defines.data(), nullptr,
        entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
    if(FAILED(r) && errors) log(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
    check(r, "Compile path tracing D3D11 shader");
    pass_cache::write(key, bytecode->GetBufferPointer(), bytecode->GetBufferSize());
    pass_cache::timing(macro, "D3D11 compile", began, false); return bytecode;
}
// A D3D11 pass as start_passes compiled it, or compiled now (one it didn't: a skinning layout of its own).
ComPtr<ID3DBlob> compile_d3d11(const char* macro, const char* entry, const char* profile, const D3D_SHADER_MACRO* extra=nullptr) {
    {
        std::lock_guard<std::mutex> g(warm.mutex);
        if(auto it=warm.d3d11.find(shader_key(macro, entry, profile, extra)); it!=warm.d3d11.end()) return it->second;
    }
    return compile_d3d11_now(macro, entry, profile, extra);
}
ComPtr<ID3D11ComputeShader> make_cs(const char* macro) {
    auto b=compile_d3d11(macro, "main", "cs_5_0"); ComPtr<ID3D11ComputeShader> cs;
    check(st.d11->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &cs), "Create path tracing D3D11 compute shader"); return cs;
}

void release_shared(Shared& s) {
    if(s.t.d12) st.release.add(st.consumer.value, s.t.d12.Get());
    st.heap.release(s.srv); st.heap.release(s.uav);
    s=Shared{};
}
Shared make_shared(UINT w, UINT h, DXGI_FORMAT format, bool d12Writes) {
    Shared s;
    s.t=tgb::SharedTexture::make(st.d11.Get(), st.d12.Get(), w, h, format, D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
    s.srv=texture_srv(st.d12.Get(), st.heap, s.t.d12.Get(), format);
    if(d12Writes) s.uav=texture_uav(st.d12.Get(), st.heap, s.t.d12.Get(), format);
    return s;
}
// Every block of the strand atlas free again: kStrandBlock a side each.
void free_strand_blocks() {
    st.strandFree={}; st.strandBlocks.clear();
    for(uint32_t y=0;y<kStrandAtlas;y+=kStrandBlock) for(uint32_t x=0;x<kStrandAtlas;x+=kStrandBlock) st.strandFree[0].push_back(x|(y<<16));
    std::reverse(st.strandFree[0].begin(), st.strandFree[0].end());
    std::memset(st.strandTiles, 0, sizeof(st.strandTiles));
}
// PT-87 (docs/agents/memory-4.md): the game's side of an atlas, held only around a bake (GameSide; the rest is further down).
void keep_side_handle(uint32_t which);
void close_side(uint32_t which);
// The strand atlas's views on the game's device, one pair for each level (to bake a block and to make its mips): made with
// the atlas, and again whenever the game's device takes its side of it up again (PT-87, memory4: take_up_side). All of
// them or none.
void make_strand_views(ID3D11Texture2D* texture) {
    std::vector<ComPtr<ID3D11UnorderedAccessView>> uavs; std::vector<ComPtr<ID3D11ShaderResourceView>> srvs;
    for(uint32_t level=0;level<kStrandLevels;++level) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format=DXGI_FORMAT_R8_UNORM; u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D; u.Texture2D.MipSlice=level;
        ComPtr<ID3D11UnorderedAccessView> uav; check(st.d11->CreateUnorderedAccessView(texture, &u, &uav), "Create a strand atlas level's UAV");
        D3D11_SHADER_RESOURCE_VIEW_DESC s{}; s.Format=DXGI_FORMAT_R8_UNORM; s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;
        s.Texture2D.MostDetailedMip=level; s.Texture2D.MipLevels=1;
        ComPtr<ID3D11ShaderResourceView> srv; check(st.d11->CreateShaderResourceView(texture, &s, &srv), "Create a strand atlas level's SRV");
        uavs.push_back(uav); srvs.push_back(srv);
    }
    st.strandUavs=std::move(uavs); st.strandSrvs=std::move(srvs);
}
// The strand atlas (bake_strands): made on the game's device with its mips, shared with D3D12, the views of each level on
// D3D11 and one of them all on D3D12.
void make_strand_atlas() {
    auto& a=st.strandAtlas;
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=desc.Height=kStrandAtlas; desc.MipLevels=kStrandLevels; desc.ArraySize=1; desc.Format=DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    check(st.d11->CreateTexture2D(&desc, nullptr, &a.t.d11), "Create the strand atlas");
    ComPtr<IDXGIResource1> dxgi; check(a.t.d11.As(&dxgi), "Query the strand atlas's IDXGIResource1");
    tgb::Handle shared;
    check(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE, nullptr, &shared.value), "Share the strand atlas");
    check(st.d12->OpenSharedHandle(shared.value, IID_PPV_ARGS(&a.t.d12)), "Open the strand atlas on D3D12");
    a.t.width=a.t.height=kStrandAtlas; a.t.format=DXGI_FORMAT_R8_UNORM;
    make_strand_views(a.t.d11.Get());
    D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=DXGI_FORMAT_R8_UNORM; v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Texture2D.MipLevels=kStrandLevels;
    a.srv=st.heap.allocate(); st.d12->CreateShaderResourceView(a.t.d12.Get(), &v, st.heap.cpu(a.srv));
    free_strand_blocks();
    keep_side_handle(kSideStrands); // (PT-87, memory4: the game's device's way back to it once it has let go)
}

void release_d3d11() {
    st.csAlbedo.Reset(); st.csFloat4.Reset(); st.csDepth.Reset(); st.csMotion.Reset(); st.csExposure.Reset();
    st.csCookieCube.Reset(); st.csCookie2D.Reset(); st.csHair.Reset(); st.csHairLobes.Reset(); st.hairConstants.Reset(); st.csEye.Reset(); st.eyeConstants.Reset();
    st.csStrandAlpha.Reset(); st.csStrandMip.Reset();
    st.csSkyCube.Reset(); st.csSkyArray.Reset(); st.csAtlas.Reset(); st.csAlpha.Reset(); st.csComposite.Reset(); st.vsComposite.Reset(); st.psComposite.Reset();
    st.csGuides.Reset(); st.guides={}; st.guideWidth=st.guideHeight=0; st.retiredGuides.clear();
    st.d11Stamps={}; st.d11Open=-1; st.d11StampsFailed=false;
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.guides=TGPT_Guides{sizeof(TGPT_Guides), TGPT_ABI}; }
    st.compositeBlend.Reset(); st.raster.Reset(); st.noDepth.Reset(); st.constants.Reset(); st.compositeConstants.Reset();
    st.linearClamp.Reset(); st.linearWrap.Reset();
    if(st.c11) {
        UINT size=0; void* none=nullptr;
        if(SUCCEEDED(st.c11->GetPrivateData(kContextTag, &size, nullptr))) st.c11->SetPrivateData(kContextTag, 0, none);
    }
    st.scratch.Reset(); st.c11.Reset(); st.d11.Reset();
    st.d3d11Ready=false;
}
bool on_game_device(ID3D11Resource* resource) {
    ComPtr<ID3D11Device> device; resource->GetDevice(&device);
    if(!device) return false;
    if(tgb::same_object(device.Get(), st.d11.Get())) return true;
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    UINT value=0, size=sizeof(value);
    return context && SUCCEEDED(context->GetPrivateData(kContextTag, &size, &value)) && size==sizeof(value) && value==1;
}
// Every resource tied to the D3D12 device or to the game's D3D11 device, after the GPU finished with them.
void release_all() {
    tally.on=false; // (PT-87, memory3: the next start begins its own)
    if(st.queue && st.consumer.ready()) { try { st.consumer.wait_cpu(st.consumer.value); } catch(...) {} }
    st.atlas=Shared{}; st.alphaAtlas=Shared{}; st.sky=Shared{}; st.cookieAtlas=Shared{}; st.fogSky=Shared{};
    st.strandAtlas=Shared{}; st.strandUavs.clear(); st.strandSrvs.clear(); st.strandFree={}; st.strandBlocks.clear();
    std::memset(st.strandTiles, 0, sizeof(st.strandTiles));
    for(uint32_t side=0;side<kSides;++side) close_side(side); // (PT-87, memory4: the atlases' kept handles go with them)
    st.sidesGone=st.sidesBack=0; st.ownSharedBytes=0;
    st.skin={}; for(auto& w:st.skinWork) w.clear(); st.skinNormals={};
    st.skinCs.clear(); st.skinSourceBuffer.Reset(); st.skinSourceSrv.Reset(); st.skinSourceKey=0;
    for(auto& in:st.inputs) in=Inputs{};
    st.outputs=Outputs{};
    for(auto& set:st.caustics) for(auto& c:set) c=State::Caustics{};
    st.scene.clear(nullptr, st.release, st.consumer.value);
    st.release.clear();
    st.surface={}; st.normal={}; st.material={}; st.hair={}; st.specSeen={}; st.giRes={}; st.diRes={}; st.neighborOffsets={}; st.neighborOffsetsSrv=kInvalid;
    st.diffuseRaw={}; st.sunRaw={}; st.specRaw={}; st.tmpA={}; st.tmpB={}; st.tmpC={}; st.tmpD={}; st.directTmp={}; st.directOut={};
    st.diffuseHist={}; st.diffuseMoments={}; st.directHist={}; st.directMoments={}; st.specHist={}; st.specMoments={};
    st.width=st.height=0; st.inputsRetire={};
    share::heap_lost(); // the GPU finished above: its views go with the heap, and textures that died meanwhile are let go of
    st.producer.reset(); st.consumer.reset();
    st.heap.reset(); st.decalSamplers.Reset(); st.sharedBase=kInvalid;
    st.allocators={}; st.lists={}; st.rings={}; st.retired.fill(0); st.fogBoxSrv.fill(kInvalid);
    st.queue.Reset(); st.root.Reset();
    st.meter.Reset(); st.meterReadback=Buffer{}; st.meterBuffer=Buffer{}; st.meterUav=kInvalid;
    st.yardPass.Reset(); st.yardReadback=Buffer{}; st.yardBuffer=Buffer{}; st.yardUav=kInvalid; st.yardSlot={}; st.yardSlotOwn={}; st.ownMotion={}; st.ownEmission={}; st.yardstick={};
    st.crumbs=Buffer{}; st.crumbsOn=false;
    st.prepare.Reset(); st.probe.Reset(); st.probeReadback=Buffer{}; st.probeBuffer=Buffer{}; st.probeUav=kInvalid; st.gi.Reset(); st.giReuse.Reset(); st.giBoil.Reset(); st.giSpatial.Reset(); st.di.Reset(); st.diSpatial.Reset(); st.specular.Reset();
    st.temporal.Reset(); st.atrous.Reset(); st.compose.Reset(); st.walk.Reset(); st.emissive.Reset(); st.queries.Reset(); st.queryReadback=Buffer{};
    st.structQueries.Reset(); st.structReadback=Buffer{}; st.structCount={}; st.structResolved={};
    st.cache.Reset(); st.cacheKeys=Buffer{}; st.cacheSums=Buffer{}; st.cacheLight=Buffer{};
    st.cacheKeysUav=st.cacheSumsUav=st.cacheLightUav=kInvalid; st.cacheRan=false;
    st.causticPass.Reset(); st.causticCorners=Buffer{}; st.causticLight=Buffer{}; st.causticBlas=Buffer{}; st.causticBlasScratch=Buffer{};
    st.causticTlas=Buffer{}; st.causticTlasScratch=Buffer{}; st.causticCounters=Buffer{}; st.causticReadback=Buffer{};
    st.causticCornersUav=st.causticLightUav=st.causticTlasSrv=st.causticCountersUav=kInvalid; st.causticCapacity=st.causticWanted=0; st.causticRead.fill(false);
    st.causticBuilt=st.causticRecent=0; st.causticWaves={};
    st.causticTileSrv.fill(kInvalid); st.causticPicks=Buffer{}; st.causticPicksUav=kInvalid; // (the heap goes below: its views with it)
#ifdef TGPT_SPEED_MEASURE
    st.speedCounters=Buffer{}; st.speedReadback=Buffer{}; st.speedCountersUav=kInvalid; st.speedRead.fill(false); st.speedHaveLast=false;
#endif
    st.causticRoom=st.causticStructure=st.causticRoomIdle=st.causticIdle=0;
    st.wetPass.Reset(); st.wetStore=Buffer{}; st.wetStoreUav=kInvalid; st.wetRan=false;
    st.sunGridPass.Reset(); st.sunGrid=Buffer{}; st.sunGridUav=kInvalid; st.sunGridSlots=0;
    st.fogFroxel.Reset(); st.fogIntegrate.Reset(); st.fogTail.Reset(); for(auto& t:st.fogGrid) t={}; st.tracedFog={}; st.fogGridFrame=0;
    st.skyPass.Reset(); st.skyMap=State::MipTexture{}; st.skyTransmittance={}; st.skyMultiple={}; st.skyGround={}; st.skyClouds={}; st.skyAmbient={};
    st.skyTablesKey=st.skyCloudsKey=0; st.skyTablesPending=st.skyCloudsPending=st.skyOwn=false;
    st.ommBake.Reset(); st.skinUnpack.Reset(); st.sway.Reset(); st.scene.swayPass=nullptr; st.waterMesh.Reset(); st.scene.waterPass=nullptr; st.giSer.Reset(); st.specularSer.Reset(); st.serTable=Buffer{}; st.useDxr12=false; st.scene.ommEnabled=false;
    st.rrg.destroy(); st.rrgGuides.Reset(); st.rrgActive=false; st.rrgHavePrevious=false; st.rrgScale=0;
    st.rrgDepth={}; st.rrgMotion={}; st.rrgNormals={}; st.rrgDiffuse={}; st.rrgSpecular={};
    st.nrd.destroy(); st.nrdActive=false; st.nrdHavePrevious=false; st.nrdMedian=0; std::fill(std::begin(st.nrdSlotScale), std::end(st.nrdSlotScale), 0.0);
    st.nrdViewZ={}; st.nrdMotion={}; st.nrdNormalRoughness={}; st.nrdDiffuseIn={}; st.nrdDiffuseOut={}; st.nrdSpecularIn={}; st.nrdSpecularOut={};
    st.nrdConfidence={}; st.nrdConfidenceTrace.Reset(); st.nrdConfidenceFilter.Reset();
    st.psr={};
    st.skyValid=false; st.haveOrigin=false; st.resetNext=true; st.haveFrameIndex=false; st.reference=0; st.clampReference=0;
    st.d3d12Ready=false;
    release_d3d11();
}

bool ensure_d3d11() {
    auto& h=host();
    const uint32_t generation=h.game_device_generation();
    if(st.d11 && generation==st.generation && st.d3d11Ready) return true;
    ID3D11Device5* device=h.game_device(); ID3D11DeviceContext4* context=h.game_context();
    if(!device || !context) return false;
    // A start that failed part-way is undone and tried again; so is everything on a new device.
    if(st.d11 && generation!=st.generation) log("The game created a new D3D11 device; the path tracer rebuilds its resources");
    if(st.d11) release_all();
    try {
        st.d11=device; st.c11=context; st.generation=generation;
        const UINT one=1; st.c11->SetPrivateData(kContextTag, sizeof(one), &one);
        const auto level=st.d11->GetFeatureLevel(); D3D_FEATURE_LEVEL chosen{};
        check(st.d11->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &chosen, &st.scratch), "Create isolated D3D11 pipeline state");
        st.csAlbedo=make_cs("TGPT_COPY_ALBEDO"); st.csFloat4=make_cs("TGPT_COPY_FLOAT4"); st.csDepth=make_cs("TGPT_COPY_DEPTH");
        st.csMotion=make_cs("TGPT_COPY_MOTION"); st.csExposure=make_cs("TGPT_COPY_EXPOSURE"); st.csSkyCube=make_cs("TGPT_SKY_CUBE");
        st.csSkyArray=make_cs("TGPT_SKY_CUBEARRAY"); st.csAtlas=make_cs("TGPT_ATLAS"); st.csAlpha=make_cs("TGPT_ALPHA"); st.csComposite=make_cs("TGPT_COMPOSITE");
        st.csCookieCube=make_cs("TGPT_COOKIE_CUBE"); st.csCookie2D=make_cs("TGPT_COOKIE_2D"); st.csHair=make_cs("TGPT_HAIR"); st.csHairLobes=make_cs("TGPT_HAIR_LOBES");
        st.csStrandAlpha=make_cs("TGPT_STRAND_ALPHA"); st.csStrandMip=make_cs("TGPT_STRAND_MIP");
        st.csEye=make_cs("TGPT_EYE");
        st.csGuides=make_cs("TGPT_GUIDES");
        auto vs=compile_d3d11("TGPT_COMPOSITE_RASTER", "vsMain", "vs_5_0"), ps=compile_d3d11("TGPT_COMPOSITE_RASTER", "psMain", "ps_5_0");
        check(st.d11->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &st.vsComposite), "Create composite VS");
        check(st.d11->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &st.psComposite), "Create composite PS");
        D3D11_BLEND_DESC blend{}; auto& rt=blend.RenderTarget[0];
        rt.BlendEnable=TRUE; rt.SrcBlend=D3D11_BLEND_ONE; rt.DestBlend=D3D11_BLEND_SRC_ALPHA; rt.BlendOp=D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha=D3D11_BLEND_ZERO; rt.DestBlendAlpha=D3D11_BLEND_ONE; rt.BlendOpAlpha=D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(st.d11->CreateBlendState(&blend, &st.compositeBlend), "Create composite blend state");
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
        check(st.d11->CreateRasterizerState(&raster, &st.raster), "Create composite raster state");
        D3D11_DEPTH_STENCIL_DESC depth{}; check(st.d11->CreateDepthStencilState(&depth, &st.noDepth), "Create composite depth state");
        {   // PT-112 (A): every pixel's depth written, whatever was there; the stencil left as it is.
            auto dvs=compile_d3d11("TGPT_DEPTH_MOTION", "vsMain", "vs_5_0"), dps=compile_d3d11("TGPT_DEPTH_MOTION", "psMain", "ps_5_0");
            check(st.d11->CreateVertexShader(dvs->GetBufferPointer(), dvs->GetBufferSize(), nullptr, &st.vsDepthMotion), "Create depth and motion VS");
            check(st.d11->CreatePixelShader(dps->GetBufferPointer(), dps->GetBufferSize(), nullptr, &st.psDepthMotion), "Create depth and motion PS");
            auto pps=compile_d3d11("TGPT_DEPTH_MOTION", "psPyramid", "ps_5_0");
            check(st.d11->CreatePixelShader(pps->GetBufferPointer(), pps->GetBufferSize(), nullptr, &st.psPyramid), "Create depth pyramid PS");
            D3D11_DEPTH_STENCIL_DESC always{}; always.DepthEnable=TRUE; always.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; always.DepthFunc=D3D11_COMPARISON_ALWAYS;
            check(st.d11->CreateDepthStencilState(&always, &st.depthAlways), "Create depth and motion depth state");
        }
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(D11Constants); cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(st.d11->CreateBuffer(&cb, nullptr, &st.constants), "Create path tracing D3D11 constants");
        cb.ByteWidth=sizeof(CompositeConstants);
        check(st.d11->CreateBuffer(&cb, nullptr, &st.compositeConstants), "Create composite constants");
        cb.ByteWidth=sizeof(HairConstants);
        check(st.d11->CreateBuffer(&cb, nullptr, &st.hairConstants), "Create hair constants");
        cb.ByteWidth=sizeof(EyeConstants);
        check(st.d11->CreateBuffer(&cb, nullptr, &st.eyeConstants), "Create eye constants");
        D3D11_SAMPLER_DESC s{}; s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; s.MaxLOD=D3D11_FLOAT32_MAX;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; check(st.d11->CreateSamplerState(&s, &st.linearClamp), "Create clamp sampler");
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; check(st.d11->CreateSamplerState(&s, &st.linearWrap), "Create wrap sampler");
    } catch(...) { release_all(); throw; }
    st.d3d11Ready=true;
    return true;
}

ComPtr<ID3D12PipelineState> make_pso(ID3D12Device5* device, ID3D12RootSignature* root, const unsigned char* code, size_t size, const char* name) {
    const auto began=std::chrono::steady_clock::now();
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{}; d.pRootSignature=root; d.CS.pShaderBytecode=code; d.CS.BytecodeLength=size;
#if TGPT_MEGA_GEOMETRY
    // (PT-109 on trial: a pipeline made with the driver's support for clusters is kept apart in the cache from one without.)
    const char* kind=mega_cluster_micromaps() ? "compute+clusters+omm" : mega_cluster_pipelines() ? "compute+clusters" : "compute"; // (megageo-5: and micromaps for clusters)
#else
    const char* kind="compute";
#endif
    const auto key=pass_cache::hash(code, size, pass_cache::hash(shaders::prepare, sizeof(shaders::prepare), kind));
    const auto cached=pass_cache::read(key);
    d.CachedPSO={cached.data(), cached.size()};
    ComPtr<ID3D12PipelineState> pso;
    HRESULT result=device->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso));
    const bool hit=SUCCEEDED(result) && !cached.empty();
    if(FAILED(result) && !cached.empty()) {
        log(std::string("Shader cache rejected for ")+name+"; rebuilding the same shader");
        d.CachedPSO={}; result=device->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso));
    }
    check(result, "Create path tracing pipeline");
    if(!hit) { ComPtr<ID3DBlob> blob; if(SUCCEEDED(pso->GetCachedBlob(&blob))) pass_cache::write(key, blob->GetBufferPointer(), blob->GetBufferSize()); }
    pass_cache::timing(name, "D3D12 pipeline (driver compile/link included)", began, hit); return pso;
}
// A ray tracing pipeline of one ray generation shader (a pass that traces with ray queries only, so no
// hit groups or payload), bound to the root signature every pass shares.
ComPtr<ID3D12StateObject> make_raygen(ID3D12Device5* device, ID3D12RootSignature* rootSignature, const unsigned char* code, size_t size, const wchar_t* name) {
    const auto began=std::chrono::steady_clock::now();
    D3D12_EXPORT_DESC exported{name, nullptr, D3D12_EXPORT_FLAG_NONE};
    D3D12_DXIL_LIBRARY_DESC library{}; library.DXILLibrary.pShaderBytecode=code; library.DXILLibrary.BytecodeLength=size;
    library.NumExports=1; library.pExports=&exported;
    D3D12_GLOBAL_ROOT_SIGNATURE root{rootSignature};
    D3D12_RAYTRACING_SHADER_CONFIG shader{4, 8};
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline{1};
    const D3D12_STATE_SUBOBJECT subobjects[]={
        {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library}, {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &root},
        {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader}, {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline}};
    D3D12_STATE_OBJECT_DESC d{}; d.Type=D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE; d.NumSubobjects=UINT(std::size(subobjects)); d.pSubobjects=subobjects;
    ComPtr<ID3D12StateObject> so; check(device->CreateStateObject(&d, IID_PPV_ARGS(&so)), "Create ray generation pipeline");
    pass_cache::timing(name==std::wstring(L"gi") ? "giSer" : "specularSer", "D3D12 state object (driver-managed cache)", began, false); return so;
}
// Runs job(0) to job(count - 1) on up to half the machine's threads, at most `most` (the driver compiles a pipeline on the
// thread that creates it, and the game goes on loading on the others).
void in_parallel(size_t count, const std::function<void(size_t)>& job, unsigned most=8) {
    // TGPT_TEST_PASS_THREADS=<1 to 8> (a test's; docs/agents/perf-1.md): that many pipelines are made at a time. The driver
    // takes gigabytes while it makes one of the two reordering passes anew (two at once passed 8 GB in 94 s in the offline
    // test, 2026-10-09): one after another, a test with a memory ceiling can still make them.
    { char asked[8]{}; if(GetEnvironmentVariableA("TGPT_TEST_PASS_THREADS", asked, sizeof asked)>0 && asked[0]>='1' && asked[0]<='8') most=unsigned(asked[0]-'0'); }
    const size_t threads=std::min<size_t>(count, std::clamp(std::thread::hardware_concurrency()/2, 1u, std::max(most, 1u)));
    std::atomic<size_t> next{0};
    auto work=[&] { for(size_t i=next++; i<count; i=next++) job(i); };
    std::vector<std::thread> pool;
    for(size_t t=1; t<threads; ++t) pool.emplace_back(work);
    work();
    for(auto& t:pool) t.join();
}
// PT-87 (memory3), for start_passes' lines: Windows' count when the first of the two reordering passes was begun (-1: none
// was), the pipelines done by then, the pipelines done, and how many there were to make.
std::atomic<double> reorderBeganAt{-1.0};
std::atomic<uint32_t> reorderBeganDone{0}, pipelinesDone{0}, pipelinesToMake{0};
// The pass pipelines: Shader Model 6.9 where DirectX Raytracing 1.2 is available, 6.6 otherwise, and with Shader Execution
// Reordering where the GPU reorders, all at once (in_parallel). A 6.9 set the driver refuses falls back to 6.6.
std::unique_ptr<Passes> build_passes(ID3D12Device5* device, bool dxr12, bool serReorders) {
    auto p=std::make_unique<Passes>();
    check(device->CreateRootSignature(0, shaders::prepare, sizeof(shaders::prepare), IID_PPV_ARGS(&p->root)), "Create path tracing root signature");
    // Each job in a group: 0 the Shader Model 6.9 set, 1 the passes both sets share, 2 the reordering ones, 3 the 6.6 set.
    struct Job { int group; std::function<void()> make; };
    std::vector<Job> jobs;
    auto pso=[&](int group, ComPtr<ID3D12PipelineState>& into, const unsigned char* code, size_t size, const char* name) {
        jobs.push_back({group, [&into, device, root=p->root.Get(), code, size, name] { into=make_pso(device, root, code, size, name); }}); };
    auto raygen=[&](ComPtr<ID3D12StateObject>& into, const unsigned char* code, size_t size, const wchar_t* name) {
        jobs.push_back({2, [&into, device, root=p->root.Get(), code, size, name] {
            // (PT-87, memory3: the count as the first of the two is begun; they are the last jobs handed out.)
            double none=-1.0;
            if(reorderBeganAt.compare_exchange_strong(none, windows_counts_mb(device))) reorderBeganDone=pipelinesDone.load();
            into=make_raygen(device, root, code, size, name); }}); };
#define TGPT_PASS(group, field, blob) pso(group, p->field, shaders::blob, sizeof(shaders::blob), #blob)
    // PT-99, for finding causes (docs/agents/speed-1.md): with TGPT_TEST_PLAIN_PASSES set when the tracer starts, the bounced
    // light's and the reflections' compute passes are made beside their reordering ones (two more large passes to make), so
    // that the hidden "Speed trial" bit 8 can run a frame with either. Without it they are made only where nothing reorders.
#ifdef TGPT_SPEED_MEASURE
    const bool plainToo=true; // a measuring build makes them always: its "Speed trial" 8 needs no environment variable
#else
    const bool plainToo=GetEnvironmentVariableA("TGPT_TEST_PLAIN_PASSES", nullptr, 0)>0;
#endif
    auto set=[&](bool sm69) {
        const int g=sm69 ? 0 : 3;
        if(sm69) {
            TGPT_PASS(g, nrdConfidenceTrace, nrdConfidenceDxr12);
            TGPT_PASS(g, prepare, prepareDxr12); TGPT_PASS(g, probe, probeDxr12); if(!serReorders || plainToo) TGPT_PASS(g, gi, giDxr12); TGPT_PASS(g, giSpatial, giSpatialDxr12);
            TGPT_PASS(g, di, diDxr12); TGPT_PASS(g, diSpatial, diSpatialDxr12); if(!serReorders || plainToo) TGPT_PASS(g, specular, specularDxr12); TGPT_PASS(g, temporal, temporalDxr12);
            TGPT_PASS(g, atrous, atrousDxr12); TGPT_PASS(g, compose, composeDxr12); TGPT_PASS(g, caustics, causticsDxr12);
            TGPT_PASS(g, walk, walkDxr12); TGPT_PASS(g, wet, wetDxr12);
            TGPT_PASS(g, fogFroxel, fogFroxelDxr12); TGPT_PASS(g, fogIntegrate, fogIntegrateDxr12); TGPT_PASS(g, fogTail, fogTailDxr12);
        } else {
            TGPT_PASS(g, nrdConfidenceTrace, nrdConfidence);
            TGPT_PASS(g, prepare, prepare); TGPT_PASS(g, probe, probe); TGPT_PASS(g, gi, gi); TGPT_PASS(g, giSpatial, giSpatial);
            TGPT_PASS(g, di, di); TGPT_PASS(g, diSpatial, diSpatial); TGPT_PASS(g, specular, specular); TGPT_PASS(g, temporal, temporal);
            TGPT_PASS(g, atrous, atrous); TGPT_PASS(g, compose, compose); TGPT_PASS(g, caustics, caustics);
            TGPT_PASS(g, walk, walk); TGPT_PASS(g, wet, wet);
            TGPT_PASS(g, fogFroxel, fogFroxel); TGPT_PASS(g, fogIntegrate, fogIntegrate); TGPT_PASS(g, fogTail, fogTail);
        }
        // ReSTIR GI's reuse across frames (pt_gi_reuse), a compute pass of this set in every build: it follows the bounced
        // light's pass, the reordering one or the plain one (PT-99, PT-110, docs/agents/rule-2.md).
        if(sm69) TGPT_PASS(g, giReuse, giReuseDxr12); else TGPT_PASS(g, giReuse, giReuse);
    };
    if(dxr12) set(true); else set(false);
    TGPT_PASS(1, ommBake, ommBake); TGPT_PASS(1, rrgGuides, rrgGuides); TGPT_PASS(1, giBoil, giBoil); TGPT_PASS(1, meter, meter); TGPT_PASS(1, yardstick, yardstick);
    TGPT_PASS(1, cache, cache); TGPT_PASS(1, skinUnpack, skinUnpack); TGPT_PASS(1, emissive, emissive);
    TGPT_PASS(1, sway, sway); TGPT_PASS(1, waterMesh, waterMesh); TGPT_PASS(1, sunGrid, sunGrid); TGPT_PASS(1, sky, sky);
    TGPT_PASS(1, nrdConfidenceFilter, nrdConfidenceFilter);
#undef TGPT_PASS
    if(dxr12 && serReorders) {
        raygen(p->giSer, shaders::giSer, sizeof(shaders::giSer), L"gi");
        raygen(p->specularSer, shaders::specularSer, sizeof(shaders::specularSer), L"specular");
    }
    // Every job runs; a group's first failure is kept.
    std::mutex failed; std::array<std::string, 4> errors; std::array<HRESULT, 4> codes{};
    auto run=[&](size_t first) {
        in_parallel(jobs.size()-first, [&](size_t i) {
            const auto& job=jobs[first+i];
            try { job.make(); }
            catch(const DxError& e) { std::lock_guard<std::mutex> g(failed); if(errors[job.group].empty()) { errors[job.group]=e.what(); codes[job.group]=e.hr; } }
            ++pass_cache::completed; ++pipelinesDone;
        });
    };
    pass_cache::total+=uint32_t(jobs.size());
    reorderBeganAt=-1.0; reorderBeganDone=0; pipelinesDone=0; pipelinesToMake=uint32_t(jobs.size());
    run(0);
    if(!errors[1].empty()) throw DxError(codes[1], errors[1]);
    if(!errors[3].empty()) throw DxError(codes[3], errors[3]);
    p->dxr12=dxr12 && errors[0].empty();
    if(dxr12 && !p->dxr12) {
        log("The Shader Model 6.9 passes could not be created ("+errors[0]+" "+tgb::hexhr(codes[0])+"); the 6.6 passes run without opacity micromaps");
        p->giSer.Reset(); p->specularSer.Reset();
        const size_t first=jobs.size(); set(false); pass_cache::total+=uint32_t(jobs.size()-first); run(first);
        if(!errors[3].empty()) throw DxError(codes[3], errors[3]);
    } else if(!errors[2].empty()) {
        log("Shader Execution Reordering could not be set up ("+errors[2]+" "+tgb::hexhr(codes[2])+"); GI and reflections run as compute passes");
        p->giSer.Reset(); p->specularSer.Reset();
        const size_t first=jobs.size();
        pso(1,p->gi,shaders::giDxr12,sizeof(shaders::giDxr12),"giDxr12");
        pso(1,p->specular,shaders::specularDxr12,sizeof(shaders::specularDxr12),"specularDxr12");
        pass_cache::total+=2; run(first);
        if(!errors[1].empty()) throw DxError(codes[1],errors[1]);
    }
    return p;
}
// The passes start_passes made (or, when it failed or they were released since, made now) into the tracer's state, with the
// reordering passes' shader table: each ray generation record on its own 64-byte boundary.
void adopt_passes(Passes& p) {
    st.nrdConfidenceTrace=p.nrdConfidenceTrace; st.nrdConfidenceFilter=p.nrdConfidenceFilter;
    st.root=p.root; st.useDxr12=p.dxr12;
    st.prepare=p.prepare; st.probe=p.probe; st.gi=p.gi; st.giReuse=p.giReuse; st.giSpatial=p.giSpatial; st.di=p.di; st.diSpatial=p.diSpatial;
    st.specular=p.specular; st.temporal=p.temporal; st.atrous=p.atrous; st.compose=p.compose;
    st.fogFroxel=p.fogFroxel; st.fogIntegrate=p.fogIntegrate; st.fogTail=p.fogTail;
    st.causticPass=p.caustics; st.wetPass=p.wet;
    st.ommBake=p.ommBake; st.rrgGuides=p.rrgGuides; st.giBoil=p.giBoil; st.meter=p.meter; st.yardPass=p.yardstick; st.walk=p.walk; st.cache=p.cache;
    st.skinUnpack=p.skinUnpack; st.emissive=p.emissive; st.sway=p.sway; st.waterMesh=p.waterMesh; st.sunGridPass=p.sunGrid; st.skyPass=p.sky;
    st.giSer.Reset(); st.specularSer.Reset(); st.serTable=Buffer{};
    // Every ray query of the 6.9 passes accepts micromaps; a 6.6 query must never meet one.
    st.scene.ommEnabled=st.useDxr12;
    if(p.giSer && p.specularSer) {
        try {
            st.serTable=make_buffer(st.d12.Get(), 2*D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ, "Create ray generation shader table");
            ComPtr<ID3D12StateObjectProperties> gi, specular;
            check(p.giSer.As(&gi), "Ray generation pipeline properties"); check(p.specularSer.As(&specular), "Ray generation pipeline properties");
            const void* ids[2]={gi->GetShaderIdentifier(L"gi"), specular->GetShaderIdentifier(L"specular")};
            if(!ids[0] || !ids[1]) throw DxError(E_FAIL, "a ray generation shader has no identifier");
            for(int i=0;i<2;++i) std::memcpy(static_cast<uint8_t*>(st.serTable.mapped)+i*D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, ids[i], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
            st.giSer=p.giSer; st.specularSer=p.specularSer;
            // PT-99, PT-110 (docs/agents/ser-2.md, rule-2.md): the driver's own count of what a thread of each reordering pass
            // needs of stack: the memory it keeps a thread's values in across the regrouping. The measure of a pass built for
            // reordering, in every build, once a start. The game's run 20261010-130621: the bounced light's pass as it is
            // since 2026-10-10 (regrouped once, after its first ray, outside its rays' loop) 0 bytes, where the pass before
            // it (the call inside that loop, the reuse across frames in the pass) kept 2,592 and took 32 to 35 ms more in a
            // tavern, a town and at a beach; the reflections' pass 128, and its regrouping pays.
            log("Shader Execution Reordering: the driver's stack a thread, the bounced light's pass "+std::to_string(gi->GetShaderStackSize(L"gi"))+
                " bytes, the reflections' pass "+std::to_string(specular->GetShaderStackSize(L"specular"))+" bytes");
        } catch(const DxError& e) {
            log(std::string("Shader Execution Reordering could not be set up (")+e.what()+" "+tgb::hexhr(e.hr)+"); GI and reflections run as compute passes");
            st.giSer.Reset(); st.specularSer.Reset(); st.serTable=Buffer{};
            if(!st.gi || !st.specular) throw; // no render-thread compilation after a resource failure
        }
    }
}
// Every pass ensure_d3d11 makes, compiled on start_passes' thread.
struct D3D11Pass { const char* macro; const char* entry; const char* profile; };
constexpr D3D11Pass kD3D11Passes[]={
    {"TGPT_COPY_ALBEDO", "main", "cs_5_0"}, {"TGPT_COPY_FLOAT4", "main", "cs_5_0"}, {"TGPT_COPY_DEPTH", "main", "cs_5_0"},
    {"TGPT_COPY_MOTION", "main", "cs_5_0"}, {"TGPT_COPY_EXPOSURE", "main", "cs_5_0"}, {"TGPT_SKY_CUBE", "main", "cs_5_0"},
    {"TGPT_SKY_CUBEARRAY", "main", "cs_5_0"}, {"TGPT_ATLAS", "main", "cs_5_0"}, {"TGPT_ALPHA", "main", "cs_5_0"},
    {"TGPT_COMPOSITE", "main", "cs_5_0"}, {"TGPT_COOKIE_CUBE", "main", "cs_5_0"}, {"TGPT_COOKIE_2D", "main", "cs_5_0"},
    {"TGPT_HAIR", "main", "cs_5_0"}, {"TGPT_HAIR_LOBES", "main", "cs_5_0"}, {"TGPT_EYE", "main", "cs_5_0"}, {"TGPT_GUIDES", "main", "cs_5_0"},
    {"TGPT_STRAND_ALPHA", "main", "cs_5_0"}, {"TGPT_STRAND_MIP", "main", "cs_5_0"},
    {"TGPT_COMPOSITE_RASTER", "vsMain", "vs_5_0"}, {"TGPT_COMPOSITE_RASTER", "psMain", "ps_5_0"},
    {"TGPT_DEPTH_MOTION", "vsMain", "vs_5_0"}, {"TGPT_DEPTH_MOTION", "psMain", "ps_5_0"}, {"TGPT_DEPTH_MOTION", "psPyramid", "ps_5_0"}};
// The game's skinning output (Unity's, structured, 20 bytes a vertex: position and one packed value), for its copy pass.
const D3D_SHADER_MACRO kSkinStructured20[]={{"SKIN_STRUCTURED", "1"}, {"SKIN_REST", "2"}, {nullptr, nullptr}};
// Starts making the passes on a thread of their own (see Warmup). The thread is detached: the game quitting meanwhile ends it.
void start_passes(ID3D12Device5* device, bool dxr12, bool serReorders) {
    {
        std::lock_guard<std::mutex> g(warm.mutex);
        if(warm.started) return;
        warm.started=true; warm.done=false;
    }
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.status.starting=1; }
    ComPtr<ID3D12Device5> keep(device);
    std::thread([keep, dxr12, serReorders] {
        const auto began=std::chrono::steady_clock::now();
        // PT-87 (memory3): the passes in Windows' count (count_now's comment). On this thread, beside the game's loading.
        StartSteps counted(keep.Get(), nullptr);
        pass_cache::initialize(keep.Get());
        pass_cache::completed=0; pass_cache::total=uint32_t(std::size(kD3D11Passes)+1); pass_cache::hits=0;
        std::unique_ptr<Passes> passes;
        std::map<std::string, ComPtr<ID3DBlob>> blobs; std::mutex blobsMutex;
        std::string failure;
        double d3d11Seconds=0, d3d12Seconds=0;
        auto since=[](std::chrono::steady_clock::time_point t) { return std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count(); };
        try {
            // The D3D11 passes first (they need no device), alongside the D3D12 ones' compiles.
            const size_t count=std::size(kD3D11Passes)+1;
            std::thread d3d11([&] {
                const auto d3d11Began=std::chrono::steady_clock::now();
                in_parallel(count, [&](size_t i) {
                    try {
                        const bool skin=i==count-1;
                        const char* macro=skin ? "TGPT_SKIN" : kD3D11Passes[i].macro;
                        const char* entry=skin ? "main" : kD3D11Passes[i].entry;
                        const char* profile=skin ? "cs_5_0" : kD3D11Passes[i].profile;
                        const D3D_SHADER_MACRO* extra=skin ? kSkinStructured20 : nullptr;
                        auto blob=compile_d3d11_now(macro, entry, profile, extra);
                        std::lock_guard<std::mutex> g(blobsMutex); blobs[shader_key(macro, entry, profile, extra)]=blob;
                    } catch(...) {} // compiled again where it is needed, which reports the error
                    ++pass_cache::completed;
                }, 3);
                d3d11Seconds=since(d3d11Began);
            });
            const auto d3d12Began=std::chrono::steady_clock::now();
            try { passes=build_passes(keep.Get(), dxr12, serReorders); d3d12Seconds=since(d3d12Began); }
            catch(const DxError& e) { failure=std::string(e.what())+" "+tgb::hexhr(e.hr); }
            catch(const std::exception& e) { failure=e.what(); }
            catch(...) { failure="an unknown error"; }
            d3d11.join();
        } catch(const std::exception& e) { failure=e.what(); }
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        {
            std::lock_guard<std::mutex> g(warm.mutex);
            warm.passes=std::move(passes); warm.d3d11=std::move(blobs); warm.done=true;
        }
        { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.status.starting=0; e.status.startSeconds=float(seconds); if(!failure.empty()) { e.status.disabled=1; e.unsupported="Shader compilation failed: "+failure; } }
        char took[96]; std::snprintf(took, sizeof(took), "%.1f s (the D3D12 ones %.1f s, the D3D11 ones %.1f s)", seconds, d3d12Seconds, d3d11Seconds);
        if(failure.empty()) log(std::string("Path tracing passes made in ")+took+", on threads of their own while the game started");
        else log(std::string("Path tracing passes could not be made while the game started (")+failure+", after "+took+"); path tracing remains off (no compile on the render thread)");
        // PT-87 (memory3): the passes are no figure of "PT memory bytes". The two reordering passes are the last jobs handed out,
        // so the count when the first of them was begun splits the thread's move in two: the compute pipelines (all but the
        // few still being made then), and the reordering passes with those few. In a game that is loading meanwhile the
        // moves are the game's too: the first start of a new build makes them for minutes, a later one in seconds.
        {
            const double before=counted.first().local, reorder=reorderBeganAt.load();
            const CountNow all=counted.lap();
            const uint32_t jobs=pipelinesToMake.load(), early=reorderBeganDone.load();
            char what[320];
            if(reorder>=0.0 && jobs>=2) {
                std::snprintf(what, sizeof what, "the %u compute pipelines, made in %.0f s beside the game's own loading (up to the moment the first reordering pass was begun: "
                              "%u of them were done by then)", jobs-2, seconds, early);
                const CountNow then{reorder, counted.first().other, true}; // (the PC's own memory was not read at that moment)
                counted.say(what, CountNow{reorder-before, 0.0, all.known}, UINT64_MAX, false, &then);
                std::snprintf(what, sizeof what, "the two reordering passes (Shader Execution Reordering), with the %u compute pipelines still being made beside them",
                              jobs-2>early ? jobs-2-early : 0u);
                counted.say(what, CountNow{before+all.local-reorder, all.other, all.known});
            } else {
                std::snprintf(what, sizeof what, "the %u compute pipelines, made in %.0f s beside the game's own loading (no reordering passes on this start)", jobs, seconds);
                counted.say(what, all);
            }
        }
    }).detach();
}
// The caustics' landed triangles as the build of their acceleration structure takes them (caustic_light): one geometry, the
// first `count` of the room's triangles (unused ones inactive: pt_caustics mode 2), non-opaque so a query sees every triangle
// it crosses, each once.
D3D12_RAYTRACING_GEOMETRY_DESC caustic_geometry(uint32_t count) {
    D3D12_RAYTRACING_GEOMETRY_DESC g{}; g.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES; g.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_NO_DUPLICATE_ANYHIT_INVOCATION;
    g.Triangles.VertexBuffer.StartAddress=st.causticCorners.va(); g.Triangles.VertexBuffer.StrideInBytes=16;
    g.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT; g.Triangles.VertexCount=count*3; g.Triangles.IndexFormat=DXGI_FORMAT_UNKNOWN;
    return g;
}
D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS caustic_inputs(const D3D12_RAYTRACING_GEOMETRY_DESC* geometry) {
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{}; in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    in.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD; in.NumDescs=1; in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs=geometry;
    return in;
}
// PT-114, ON TRIAL (TGPT_FRAME_CAUSTICS_GRID; docs/agents/caustgrid.md): the caustics' tiles that can do anything this frame,
// listed for the pass, which then runs a group for each (pt_caustics.hlsl, pass.a.z: the list's view). A tile is listed
// unless the pass itself would end it at its first tests (its slot has no water this frame; it lies wholly in the finer
// level's square: the same whole numbers as the pass's; it lies off the slot's extent: with a centimetre to spare, so the
// pass's own test in floats still decides the tiles at the edge) or NO TRIANGLE OF THE SLOT'S LEVEL WATER LIES UNDER IT
// (GpuScene::water_tiles, within kSlack of its square: far more than the floats' rounding between the CPU's triangles and
// the card's): its corners' rays straight down then meet no water of the slot, so it has no corner, lights nothing and
// writes nothing. That last test is the cut: a river's or a lake's extent is a rectangle round all its meshes, and every
// tile of all eight levels inside it ran its 256 rays down for nothing.
// A listed tile: its x and z in the level's grid (6 bits each) and its level times 4 plus its slot (from bit 12).
// False when the list finds no room in the frame's ring: the pass then runs every tile, as before.
// water: the last test is made (the trial's second step); without it the list holds every tile the pass would run.
bool caustic_tile_list(uint32_t index, const FrameConstants& k, uint32_t levels, bool water, uint32_t& srv, uint32_t& count) {
    constexpr uint32_t tiles=(kCausticGrid+14)/15;
    constexpr double kSlack=0.1, kEdge=0.01;
    LARGE_INTEGER began{}, ended{}, perSecond{}; QueryPerformanceCounter(&began);
    auto& listed=st.causticTileList; listed.clear();
    uint32_t dry=0;
    for(uint32_t level=0;level<levels && level<kCausticLevels;++level) {
        const float* lv=k.causticLevels+level*4;
        const double cell=double(kCausticCell)*std::ldexp(1.0, int(level)), tile=15.0*cell;
        const double lx=double(lv[0]), lz=double(lv[1]);
        const bool inner=lv[2]>-1e8f;
        const int innerX=int(lv[2]), innerZ=int(lv[3]), innerSide=int(kCausticGrid/2);
        for(uint32_t slot=0;slot<TGPT_MAX_WATER;++slot) {
            const float* m=k.water+slot*kWaterRows*4;
            if(!(m[51]>0.5f)) continue; // (pt_common water_slot)
            const double margin=double(m[50])+kEdge;
            uint64_t rows[64];
            if(water) st.scene.water_tiles(slot, st.origin[0]+lx, st.origin[2]+lz, tile, tiles, kSlack, rows);
            for(uint32_t tz=0;tz<tiles;++tz) for(uint32_t tx=0;tx<tiles;++tx) {
                const int fx=int(tx*15u), fz=int(tz*15u);
                if(inner && fx>=innerX && fz>=innerZ && fx+15<=innerX+innerSide && fz+15<=innerZ+innerSide) continue;
                const double lox=lx+double(fx)*cell, loz=lz+double(fz)*cell;
                if(lox+tile<double(m[52])-margin || loz+tile<double(m[53])-margin || lox>double(m[54])+margin || loz>double(m[55])+margin) continue;
                if(water && !((rows[tz]>>tx)&1ull)) { ++dry; continue; }
                listed.push_back(tx|(tz<<6)|((level*TGPT_MAX_WATER+slot)<<12));
            }
        }
    }
    count=uint32_t(listed.size()); srv=kInvalid;
    if(count) {
        void* at=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpuAt=0; uint64_t offset=0;
        const uint64_t bytes=uint64_t(count)*4;
        if(!st.rings[index].allocate(bytes, 256, &at, &gpuAt, &offset)) return false;
        std::memcpy(at, listed.data(), size_t(bytes));
        if(st.causticTileSrv[index]==kInvalid) st.causticTileSrv[index]=st.heap.allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=DXGI_FORMAT_R32_TYPELESS; v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
        v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Buffer.FirstElement=offset/4; v.Buffer.NumElements=UINT(bytes/4);
        v.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
        st.d12->CreateShaderResourceView(st.rings[index].resource(), &v, st.heap.cpu(st.causticTileSrv[index]));
        srv=st.causticTileSrv[index];
    }
    ++st.causticListFrames; st.causticListed+=count; st.causticDry+=dry;
    QueryPerformanceCounter(&ended); QueryPerformanceFrequency(&perSecond);
    if(perSecond.QuadPart>0) st.causticListMs+=1000.0*double(ended.QuadPart-began.QuadPart)/double(perSecond.QuadPart);
    return true;
}
// The most triangles the caustics may write in a frame (pt_caustics.hlsl): at least `wanted`, a power of two from a million
// up, at most 8 million (past it, a frame's triangles beyond it light nothing, and the log says so). A number, not memory
// (PT-87, docs/agents/memory-1.md): until then it was also the room, 158 MB for a million from the first frame with a water
// slot, of which a frame writes and builds over causticBuilt (65,536 where no light lands, 262,144 by the sea). The room
// is ensure_caustic_room's.
void make_caustic_triangles(uint32_t wanted) {
    uint32_t capacity=1u<<20; while(capacity<wanted && capacity<(1u<<23)) capacity<<=1;
    st.causticWanted=0;
    if(capacity==st.causticCapacity) return;
    if(st.causticCapacity) log("The caustics' triangles: up to "+std::to_string(capacity)+" a frame now (a frame wanted "+std::to_string(wanted)+(wanted>capacity ? "; the rest light nothing)" : ")"));
    st.causticCapacity=capacity;
}
// The caustics' room (PT-87): the buffers of their triangles' corners and light and of the acceleration structure built over
// them, for `built` triangles: what this frame may write and builds over (a power of two, never past the limit). Before, the
// room was the limit's. Made when there is none, made larger the frame `built` passes it, and smaller once kCausticShrink
// frames in a row built over half of it or less (and the structure the top level still names fits the new room).
// The picture does not know: a frame's passes read the landed light of the frame before (pt_water_path) until its own
// caustics pass has run (pt_compose reads this frame's), so a new room takes the old one's corners and light along (a copy,
// in this frame's list, before any pass), and the old buffers live through this frame (the top level still names the old
// bottom level until this frame's build). A room made from nothing has no light of a frame before: the counters are
// emptied (the caustics pass's mode 0: caustic_light then asks nothing of a structure that was never built).
// The top level (one instance) is made with the first room. Throws without room for it; nothing is changed then.
constexpr uint32_t kCausticShrink=600;
// PT-87: what a frame no longer runs is let go once this many traced frames in a row went without it (the built-in
// filters' textures, a denoiser's own, the caustics' room): long after the GPU finished the last frame that used it (a
// list is recorded again only when the frame that used it kFrames ago is done), and long enough that going back and
// forth between two rows makes nothing twice.
constexpr uint32_t kIdleFrames=120;
// TGPT_TEST_KEEP_IDLE (a test's): nothing is let go or made smaller, as before PT-87: the pictures of a run with it and of
// the same run without must be the same.
bool keep_idle() { return GetEnvironmentVariableA("TGPT_TEST_KEEP_IDLE", nullptr, 0)>0; }

// ---- PT-87 (docs/agents/memory-4.md): the game's side of the atlases, held only around a bake (GameSide's comment).
const char* const kSideNames[kSides]={"the colour atlas", "the alpha atlas", "the cookie atlas", "hair's strand atlas"};
Shared& side_atlas(uint32_t which) {
    return which==kSideColour ? st.atlas : which==kSideAlpha ? st.alphaAtlas : which==kSideCookies ? st.cookieAtlas : st.strandAtlas;
}
// Whether the game's device can write the atlas now: it holds its side, or the atlas is not made yet (hair's strand atlas,
// which the first hair that has strands makes).
bool side_ready(uint32_t which) { const Shared& a=side_atlas(which); return a.t.d11 || !a.t.d12; }
// When the game's device lets go of an atlas no frame bakes into: kSidesByClock, after kSideIdleMs (the rule); 0, never,
// as before PT-87; another number, after that many traced frames. TGPT_TEST_ATLAS_SIDES=<n> in the environment (a
// test's, whose frames do not go by the clock, and the way to see the cut on and off in the game): 0 never, another
// number that many frames. TGPT_TEST_KEEP_IDLE keeps the sides too: it says "nothing is let go".
constexpr uint32_t kSidesByClock=UINT32_MAX;
uint32_t side_idle_frames() {
    if(keep_idle()) return 0;
    char text[16]{};
    return GetEnvironmentVariableA("TGPT_TEST_ATLAS_SIDES", text, sizeof text)>0 ? uint32_t(std::min<unsigned long>(std::strtoul(text, nullptr, 10), 1000000ul)) : kSidesByClock;
}
// What of the tracer's own shared textures Windows counts ONCE now: the atlases whose side the game's device has let go of.
uint64_t sides_let_go_bytes() {
    uint64_t bytes=0;
    for(uint32_t i=0;i<kSides;++i) { const Shared& a=side_atlas(i); if(a.t.d12 && !a.t.d11) bytes+=allocation_bytes(a.t.d12.Get()); }
    return bytes;
}
// For the cap's trial alone (texture_share.cpp within_budget): the tracer's own shared textures that are in Windows'
// count a second time now, which is all of them (as the memory line last summed them) but those.
void tell_own_shared() { const uint64_t once=sides_let_go_bytes(); share::own_shared(st.ownSharedBytes>once ? st.ownSharedBytes-once : 0); }
// After an atlas is made: the handle the game's device will find its way back by. Without one the side stays held.
void keep_side_handle(uint32_t which) {
    GameSide& s=st.sides[which]; const Shared& a=side_atlas(which);
    if(s.handle) CloseHandle(s.handle);
    s=GameSide{}; s.wroteAt=GetTickCount64(); // (made and cleared just now)
    ComPtr<IDXGIResource1> dxgi;
    HRESULT r=a.t.d11 ? a.t.d11.As(&dxgi) : E_POINTER;
    if(SUCCEEDED(r)) r=dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE, nullptr, &s.handle);
    if(FAILED(r) || !s.handle) {
        s.handle=nullptr; s.stay=true;
        log(std::string("PT memory: no second handle of ")+kSideNames[which]+" could be made ("+tgb::hexhr(r)+"): the game's device keeps holding it, as before");
    }
}
void close_side(uint32_t which) {
    GameSide& s=st.sides[which];
    if(s.handle) CloseHandle(s.handle);
    s=GameSide{};
    st.sideMoves.mask&=~(1u<<which); if(!st.sideMoves.mask) st.sideMoves=SideMoves{}; // (its line, if one waited, goes with it)
}
// The game's device's side of an atlas by its kept handle: the texture, and a view to write it through (the whole
// texture's for the three make_shared made, as tgb::SharedTexture::make made theirs; level 0's for the strand atlas, whose
// own views make_strand_views makes). forBake: the opening a bake waits for; else the second opening, which shows that
// the way back works before the first is let go.
HRESULT open_side(uint32_t which, bool forBake, ComPtr<ID3D11Texture2D>& texture, ComPtr<ID3D11UnorderedAccessView>& uav) {
    const Shared& a=side_atlas(which); const GameSide& s=st.sides[which];
    if(!s.handle || !st.d11) return E_HANDLE;
    // TGPT_TEST_ATLAS_SIDES_BREAK=<bits> (a test's): the opening fails as if the driver had refused it. Bits 0 to 3: the
    // second opening of atlas 0 to 3; bits 4 to 7: the opening for a bake.
    char broken[16]{};
    if(GetEnvironmentVariableA("TGPT_TEST_ATLAS_SIDES_BREAK", broken, sizeof broken)>0 && ((std::strtoul(broken, nullptr, 10)>>(which+(forBake ? 4u : 0u)))&1u)) return E_FAIL;
    tgb::InternalScope internal(&host());
    HRESULT r=st.d11->OpenSharedResource1(s.handle, IID_PPV_ARGS(&texture));
    if(FAILED(r)) return r;
    D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d);
    if(d.Width!=a.t.width || d.Height!=a.t.height || d.Format!=a.t.format || d.MipLevels!=(which==kSideStrands ? kStrandLevels : 1u) ||
       !(d.BindFlags&D3D11_BIND_UNORDERED_ACCESS)) return E_UNEXPECTED; // not the atlas as it was made
    if(which!=kSideStrands) return st.d11->CreateUnorderedAccessView(texture.Get(), nullptr, &uav);
    D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format=d.Format; u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
    return st.d11->CreateUnorderedAccessView(texture.Get(), &u, &uav);
}
// The log line of the sides that moved together, once Windows' count has followed them by half their size or eight calls
// have looked (now: at once, as the count stands): which atlases, their sizes, and the count before and after. The
// first 64 such lines of a start are written, then every thirty-second (the memory line counts every move).
void say_side_moves(bool now) {
    SideMoves& m=st.sideMoves;
    if(!m.direction) return;
    double size=0;
    for(uint32_t i=0;i<kSides;++i) if(m.mask>>i&1u) size+=double(allocation_bytes(side_atlas(i).t.d12.Get()))/1048576.0;
    const CountNow c=count_now(st.d12.Get());
    const double change=c.local-m.before;
    const bool followed=m.direction<0 ? change<=-0.5*size : change>=0.5*size;
    if(!now && c.known && !followed && ++m.waited<8) return;
    if(++st.sideGroups<=64 || st.sideGroups%32==0) {
        std::string list;
        for(uint32_t i=0;i<kSides;++i) {
            if(!(m.mask>>i&1u)) continue;
            const GameSide& s=st.sides[i]; const double mb=double(allocation_bytes(side_atlas(i).t.d12.Get()))/1048576.0;
            char one[160];
            if(m.direction>0) std::snprintf(one, sizeof one, "%s (%.0f MB, opened by its kept handle in %.2f ms)", kSideNames[i], mb, s.openMs);
            else if(s.idleThen) std::snprintf(one, sizeof one, "%s (%.0f MB, %.1f s and %u traced frames after its last bake)", kSideNames[i], mb, s.idleSeconds, s.idleThen);
            else std::snprintf(one, sizeof one, "%s (%.0f MB, no frame having been traced for a second)", kSideNames[i], mb);
            list+=(list.empty() ? "" : ", ")+std::string(one);
        }
        char count[280], sum[200];
        // (A line closed by the next move, before the game's device flushed or wrote, says so: its count has not moved yet.)
        if(c.known) std::snprintf(count, sizeof count, "Windows' count: %.0f MB before, %.0f MB after (%+.0f MB for %.0f MB of atlases, read %u of the tracer's calls later%s)",
                                  m.before, c.local, change, size, m.waited+1, followed ? "" : now ? "; the next move came before the count could follow" : "; the count has not followed");
        else std::snprintf(count, sizeof count, "Windows' count is not known here");
        std::snprintf(sum, sizeof sum, "So far a side was let go %u times and taken up again %u times; of the tracer's own shared textures %.0f MB are counted once now",
                      st.sidesGone, st.sidesBack, double(sides_let_go_bytes())/1048576.0);
        log(std::string("PT memory: the game's device ")+(m.direction>0 ? "took up again for a bake: " : "let go of ")+list+
            (m.direction>0 ? ". " : "; the tracer's device holds and reads them as before. ")+count+". "+sum);
    }
    m=SideMoves{};
}
// A side moved: into the line of those that moved with it, which is begun here. Moves the other way, or after the line
// already looked at the count, close the line that waited first. before: Windows' count before this move, MB.
void note_side_move(uint32_t which, int32_t direction, double before) {
    SideMoves& m=st.sideMoves;
    if(m.direction && (m.direction!=direction || m.waited)) say_side_moves(true);
    if(!m.direction) { m.direction=direction; m.before=before; }
    m.mask|=1u<<which;
}
// A bake waits and the game's device has let go of the atlas: its side again by the kept handle, with the views the bakes
// write through. False: it could not be opened (a log line, one every five seconds at most). The bake then waits in its
// queue, as bakes wait for their turn, and the next frame tries again; once the side is back it stays held for the
// session, as before PT-87.
bool take_up_side(uint32_t which) {
    if(side_ready(which)) return true;
    Shared& a=side_atlas(which); GameSide& s=st.sides[which];
    const CountNow before=count_now(st.d12.Get());
    const auto began=std::chrono::steady_clock::now();
    ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11UnorderedAccessView> uav;
    HRESULT r=open_side(which, true, texture, uav);
    if(SUCCEEDED(r) && which==kSideStrands) {
        try { make_strand_views(texture.Get()); } catch(const DxError& e) { r=FAILED(e.hr) ? e.hr : E_FAIL; } catch(...) { r=E_FAIL; }
    }
    if(FAILED(r)) {
        s.stay=true;
        log_throttled(std::string("PT memory: the game's device could not open ")+kSideNames[which]+" again for a bake ("+tgb::hexhr(r)+
                      "): the bake waits and every frame tries again; once it is back, the game's device keeps holding it, as before");
        return false;
    }
    a.t.d11=texture; if(which!=kSideStrands) a.t.uav=uav;
    s.openMs=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-began).count();
    s.idle=0; s.wroteAt=GetTickCount64(); ++s.back; ++st.sidesBack;
    note_side_move(which, 1, before.local);
    tell_own_shared();
    return true;
}
// The game's device lets go of its side of an atlas: its texture object and its views (freed at its next flush; the
// texture itself lives on with the tracer's device, with all that was baked into it). First the way back is shown to
// work, here and now: the game's device opens the atlas a SECOND time by the kept handle and makes a view on it. If it
// cannot, it does not let go, now or later in this session. idleThen: the frames without a bake it waited (0: no frame
// was traced for a second).
void let_go_side(uint32_t which, uint32_t idleThen) {
    Shared& a=side_atlas(which); GameSide& s=st.sides[which];
    if(!a.t.d11 || !a.t.d12 || !s.handle || s.stay) return;
    {
        ComPtr<ID3D11Texture2D> second; ComPtr<ID3D11UnorderedAccessView> view;
        const HRESULT r=open_side(which, false, second, view);
        if(FAILED(r)) {
            s.stay=true;
            log(std::string("PT memory: the game's device could not open ")+kSideNames[which]+" a second time by its kept handle ("+tgb::hexhr(r)+
                "): it keeps holding it, as before, and nothing is given back for this atlas in this session");
            return;
        }
    }
    const double before=count_now(st.d12.Get()).local;
    a.t.srv.Reset(); a.t.uav.Reset(); a.t.rtv.Reset(); a.t.d11.Reset();
    if(which==kSideStrands) { st.strandUavs.clear(); st.strandSrvs.clear(); }
    s.idleSeconds=double(GetTickCount64()-s.wroteAt)/1000.0;
    s.idle=0; s.written=false; ++s.gone; ++st.sidesGone; s.idleThen=idleThen;
    note_side_move(which, -1, before);
    tell_own_shared();
}
// Every traced frame, after its bakes (render thread, the frame's D3D11 side): a side the frame wrote starts its wait
// again; one that no frame has written for kSideIdleMs is let go (two frames after the write at the earliest: that
// frame's own flush is then behind it at any frame rate).
void tend_sides() {
    const uint32_t after=side_idle_frames(); const uint64_t now=GetTickCount64();
    say_side_moves(false);
    for(uint32_t i=0;i<kSides;++i) {
        GameSide& s=st.sides[i]; const Shared& a=side_atlas(i);
        if(!a.t.d11 || !a.t.d12) { s.written=false; continue; }
        if(s.written) { s.written=false; s.idle=0; s.wroteAt=now; continue; }
        if(s.idle<UINT32_MAX) ++s.idle;
        if(after==kSidesByClock ? (s.idle>=2 && now-s.wroteAt>=kSideIdleMs) : (after && s.idle>=after)) let_go_side(i, s.idle);
    }
}
// The plugin's maintenance event (render thread). While no frame is traced no bake can come (path tracing is off, or held
// back until the game's textures can be opened, which is when the card is fullest): after a second without a frame the
// game's device lets go of every side at once.
constexpr uint64_t kSidesQuietMs=1000;
void tend_sides_quiet() {
    say_side_moves(false);
    if(!side_idle_frames() || GetTickCount64()-st.lastTraceTick<kSidesQuietMs) return;
    for(uint32_t i=0;i<kSides;++i) let_go_side(i, 0);
}

void dispatch(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso, std::initializer_list<uint32_t> constants, Barriers& barriers, uint32_t groupsX, uint32_t groupsY);
void ensure_caustic_room(ID3D12GraphicsCommandList4* list, uint32_t built) {
    bool shrink=false;
    if(st.causticRoom && built<=st.causticRoom/2) shrink=++st.causticRoomIdle>=kCausticShrink && st.causticStructure<=built && !keep_idle();
    else st.causticRoomIdle=0;
    if(st.causticCorners.resource && built<=st.causticRoom && !shrink) return;
    const uint32_t room=keep_idle() ? std::max(built, st.causticCapacity) : built; // (the test's switch: the limit's room, as before)
    const auto uavFlag=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Buffer corners=make_buffer(st.d12.Get(), uint64_t(room)*kCausticCorners, D3D12_HEAP_TYPE_DEFAULT, uavFlag, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the caustics' corners");
    Buffer light=make_buffer(st.d12.Get(), uint64_t(room)*kCausticLight, D3D12_HEAP_TYPE_DEFAULT, uavFlag, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the caustics' light");
    D3D12_RAYTRACING_GEOMETRY_DESC geometry=caustic_geometry(room); geometry.Triangles.VertexBuffer.StartAddress=corners.va();
    const auto in=caustic_inputs(&geometry);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{}; st.d12->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
    Buffer blas=make_buffer(st.d12.Get(), info.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, uavFlag, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create the caustics' structure");
    Buffer scratch=make_buffer(st.d12.Get(), info.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, uavFlag, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the caustics' structure scratch");
    if(!st.causticTlas.resource) {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top{}; top.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL; top.NumDescs=1; top.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topInfo{}; st.d12->GetRaytracingAccelerationStructurePrebuildInfo(&top, &topInfo);
        Buffer tlas=make_buffer(st.d12.Get(), topInfo.ResultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, uavFlag, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create the caustics' top level");
        Buffer tlasScratch=make_buffer(st.d12.Get(), topInfo.ScratchDataSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, uavFlag, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the caustics' top level scratch");
        st.causticTlas=tlas; st.causticTlasScratch=tlasScratch;
        st.causticTlasSrv=st.heap.allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.ViewDimension=D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE; v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.RaytracingAccelerationStructure.Location=st.causticTlas.va();
        st.d12->CreateShaderResourceView(nullptr, &v, st.heap.cpu(st.causticTlasSrv));
    }
    const uint32_t cornersUav=raw_uav(st.d12.Get(), st.heap, corners.resource.Get(), uint64_t(room)*kCausticCorners);
    const uint32_t lightUav=raw_uav(st.d12.Get(), st.heap, light.resource.Get(), uint64_t(room)*kCausticLight);
    const uint32_t before=st.causticRoom;
    if(st.causticCorners.resource) {
        // The frame before's corners and light go along, as far as both rooms reach (the old structure's triangles lie
        // within both: a larger room holds the old one whole, and a smaller one is taken only when they fit). Buffers are
        // in COMMON between lists: the copies promote the old ones to sources and the new ones to destinations, which
        // then become the storage buffers the passes write.
        const uint64_t kept=std::min(before, room);
        list->CopyBufferRegion(corners.resource.Get(), 0, st.causticCorners.resource.Get(), 0, kept*kCausticCorners);
        list->CopyBufferRegion(light.resource.Get(), 0, st.causticLight.resource.Get(), 0, kept*kCausticLight);
        Barriers moved;
        moved.transition_from(corners.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        moved.transition_from(light.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        moved.flush(list);
        // Kept through the frame being recorded (its fence value: the one after the last frame handed over): a path under
        // water reads last frame's landed light before this frame's caustics pass writes the new (pt_water_path), through
        // the top level that still names the old structure.
        for(Buffer* b:{&st.causticCorners, &st.causticLight, &st.causticBlas, &st.causticBlasScratch}) st.release.add(st.consumer.value+1, b->resource.Get());
        st.heap.release(st.causticCornersUav); st.heap.release(st.causticLightUav);
    } else if(st.causticPass && st.causticCountersUav!=kInvalid) {
        Barriers none; dispatch(list, st.causticPass.Get(), {0u}, none, 1, 1);
        st.causticStructure=0;
    }
    st.causticCorners=corners; st.causticLight=light; st.causticBlas=blas; st.causticBlasScratch=scratch;
    st.causticCornersUav=cornersUav; st.causticLightUav=lightUav;
    st.causticRoom=room; st.causticRoomIdle=0;
    log("The caustics' room: "+std::to_string(room)+" triangles"+(before ? " (it was "+std::to_string(before)+")" : std::string())+", "+
        std::to_string((uint64_t(room)*(kCausticCorners+kCausticLight)+info.ResultDataMaxSizeInBytes+info.ScratchDataSizeInBytes)>>20)+
        " MB with their acceleration structure; a frame may write up to "+std::to_string(st.causticCapacity));
}
// The room alone let go (the limit and what the last frames landed stay): with the scene, and once kIdleFrames frames in a
// row ran no caustics pass (the Caustics row off, or no water slot left). The top level goes too: it names the bottom level.
void release_caustic_room(uint64_t fence) {
    for(Buffer* b:{&st.causticCorners,&st.causticLight,&st.causticBlas,&st.causticBlasScratch,&st.causticTlas,&st.causticTlasScratch}) {
        st.release.add(fence,b->resource.Get()); *b=Buffer{};
    }
    for(uint32_t* v:{&st.causticCornersUav,&st.causticLightUav,&st.causticTlasSrv}) { st.heap.release(*v); *v=kInvalid; }
    st.causticRoom=st.causticStructure=st.causticRoomIdle=st.causticIdle=0;
}
void release_scene_extras() {
    const auto fence=st.consumer.value;
    release_caustic_room(fence);
    st.causticCapacity=st.causticWanted=st.causticBuilt=st.causticRecent=0; st.causticRead.fill(false);
    for(auto& a:st.caustics) for(auto& c:a) {
        for(auto* w:{&c.displacement,&c.normals}) { st.release.add(fence,w->d12.Get()); st.release.add(fence,w->d11.Get()); st.heap.release(w->srv); }
        c=State::Caustics{};
    }
    st.release.add(fence,st.strandAtlas.t.d12.Get()); st.release.add(fence,st.strandAtlas.t.d11.Get()); st.heap.release(st.strandAtlas.srv);
    st.strandAtlas=Shared{}; st.strandUavs.clear(); st.strandSrvs.clear(); st.strandBlocks.clear(); st.strandFree={};
    close_side(kSideStrands); // (PT-87, memory4: its kept handle goes with it)
    std::memset(st.strandTiles,0,sizeof(st.strandTiles));
    // The wet ground's store: another scene's ground starts dry, and a scene without water needs none (PT-87).
    st.release.add(fence,st.wetStore.resource.Get()); st.wetStore=Buffer{}; st.heap.release(st.wetStoreUav); st.wetStoreUav=kInvalid;
    st.resetNext=true; st.cacheRan=false; st.wetRan=false; st.sceneResourcesDirty=true;
}

bool ensure_d3d12() {
    if(st.d3d12Ready) return true;
    if(!st.sharedDevice || !st.d11) return false;
    // A start that fails part-way leaves nothing behind: the next frame starts over (or, after enough
    // failures, path tracing turns off), never records with what exists.
    const auto began=std::chrono::steady_clock::now();
    try {
        check(st.sharedDevice.As(&st.d12), "The shared D3D12 device has no ID3D12Device5");
        // PT-87 (memory3): the start's steps in Windows' count from here on, and the tally of what follows (count_now's comment).
        StartSteps steps(st.d12.Get(), &tally.stepsDevice);
        tally=StartTally{};
        if(steps.first().known) {
            tally.on=true; tally.began=GetTickCount64(); tally.base=steps.first().local; tally.baseOther=steps.first().other; tally.opens0=share::open_count();
            char line[256];
            std::snprintf(line, sizeof line, "PT memory at start: before the tracer's first object on its own queue (Windows counts %.0f MB; the PC's own memory beside it %.0f MB)",
                          tally.base, tally.baseOther);
            log(line);
        }
        // A compute queue: its work overlaps HDRP's graphics work instead of taking turns with it.
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
        check(st.d12->CreateCommandQueue(&q, IID_PPV_ARGS(&st.queue)), "Create path tracing queue");
        st.producer.create(st.d12.Get(), st.d11.Get());
        st.consumer.create(st.d12.Get(), st.d11.Get());
        steps.step("the compute queue and the two fences shared with the game's device");
        CountNow listsMoved{0, 0, true}, ringsMoved{0, 0, true}; uint64_t ringBytes=0;
        for(uint32_t i=0;i<kFrames;++i) {
            check(st.d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&st.allocators[i])), "Create path tracing allocator");
            ComPtr<ID3D12GraphicsCommandList> list;
            check(st.d12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, st.allocators[i].Get(), nullptr, IID_PPV_ARGS(&list)), "Create path tracing list");
            check(list.As(&st.lists[i]), "The command list has no ID3D12GraphicsCommandList4");
            check(st.lists[i]->Close(), "Close initial path tracing list");
            listsMoved=listsMoved+steps.lap();
            st.rings[i].create(st.d12.Get(), kRingSize);
            ringsMoved=ringsMoved+steps.lap(); ringBytes+=allocation_bytes(st.rings[i].resource());
        }
        steps.say("the three command allocators and lists", listsMoved);
        steps.say("the three upload rings (D3D12 upload heaps, which the memory line takes for the PC's own memory)", ringsMoved, ringBytes);
        st.retired.fill(0);
        st.heap.create(st.d12.Get(), kHeapSize);
        // Every Unity 2D texture sampler state: three filters, aniso 0..16, four U and V address modes.
        // This is the finite set of API states, not a cap on decals or their textures.
        D3D12_DESCRIPTOR_HEAP_DESC samplerHeap{}; samplerHeap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        samplerHeap.NumDescriptors=3*17*16; samplerHeap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(st.d12->CreateDescriptorHeap(&samplerHeap,IID_PPV_ARGS(&st.decalSamplers)),"Create decal texture samplers");
        auto samplerAt=st.decalSamplers->GetCPUDescriptorHandleForHeapStart();
        const UINT samplerStep=st.d12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        const D3D12_TEXTURE_ADDRESS_MODE address[]={D3D12_TEXTURE_ADDRESS_MODE_WRAP,D3D12_TEXTURE_ADDRESS_MODE_CLAMP,D3D12_TEXTURE_ADDRESS_MODE_MIRROR,D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE};
        const D3D12_FILTER filter[]={D3D12_FILTER_MIN_MAG_MIP_POINT,D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,D3D12_FILTER_MIN_MAG_MIP_LINEAR};
        for(int f=0;f<3;++f) for(int a=0;a<=16;++a) for(int u=0;u<4;++u) for(int v=0;v<4;++v) {
            D3D12_SAMPLER_DESC sampler{}; sampler.Filter=a>1 ? D3D12_FILTER_ANISOTROPIC : filter[f];
            sampler.AddressU=address[u]; sampler.AddressV=address[v]; sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            sampler.MaxAnisotropy=UINT(std::max(a,1)); sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.MinLOD=0; sampler.MaxLOD=D3D12_FLOAT32_MAX;
            st.d12->CreateSampler(&sampler,samplerAt); samplerAt.ptr+=samplerStep;
        }
        st.sharedBase=st.heap.allocate_range(share::kMaxShared);
        steps.step("the descriptor heap of "+std::to_string(kHeapSize)+" views and the heap of 816 samplers");
        // Keep the warm passes: recreating frame resources must never compile shaders on the render thread.
        { std::lock_guard<std::mutex> g(warm.mutex);
          if(!warm.done || !warm.passes) throw DxError(E_FAIL,"Path tracing shaders are not ready");
          adopt_passes(*warm.passes);
        }
        steps.step("the passes taken over from the thread that made them (with the reordering passes' table, where they are)");
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count=kQueries;
        check(st.d12->CreateQueryHeap(&qh, IID_PPV_ARGS(&st.queries)), "Create path tracing timestamps");
        st.queryReadback=make_buffer(st.d12.Get(), kQueries*8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create timestamp read-back");
        // PT-109: the structures' stamps. Without them the "PT structures" line has no times; nothing else depends on them.
        qh.Count=kFrames*kStructMarks;
        if(SUCCEEDED(st.d12->CreateQueryHeap(&qh, IID_PPV_ARGS(&st.structQueries))))
            st.structReadback=make_buffer(st.d12.Get(), kFrames*kStructMarks*8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create the structures' timestamp read-back");
        st.structCount={}; st.structResolved={};
        // How far the GPU got (report_gpu_stop), where the compute queue can write its markers.
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 o3{};
        st.crumbsOn=SUCCEEDED(st.d12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &o3, sizeof(o3))) &&
            (o3.WriteBufferImmediateSupportFlags&D3D12_COMMAND_LIST_SUPPORT_FLAG_COMPUTE);
        if(st.crumbsOn) {
            st.crumbs=make_buffer(st.d12.Get(), 256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create GPU progress markers");
            std::memset(st.crumbs.mapped, 0, 256);
        }
        st.crumbsRecorded=0;
        st.probeBuffer=make_buffer(st.d12.Get(), 48, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create camera light probe");
        st.probeUav=structured_uav(st.d12.Get(), st.heap, st.probeBuffer.resource.Get(), 3, 16);
        st.probeReadback=make_buffer(st.d12.Get(), kFrames*48, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create camera light read-back");
        st.meterBuffer=make_buffer(st.d12.Get(), State::kMeterBins*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create traced light meter");
        st.meterUav=structured_uav(st.d12.Get(), st.heap, st.meterBuffer.resource.Get(), State::kMeterBins, 4);
        st.meterReadback=make_buffer(st.d12.Get(), kFrames*State::kMeterBins*4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create traced light meter read-back");
        st.yardBuffer=make_buffer(st.d12.Get(), kYardCount*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the yardstick's counters");
        st.yardUav=structured_uav(st.d12.Get(), st.heap, st.yardBuffer.resource.Get(), kYardCount, 4);
        st.yardReadback=make_buffer(st.d12.Get(), uint64_t(kFrames)*kYardCount*4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create the yardstick's read-back");
        steps.step("the timestamps, the camera's probe, the light meter and the yardstick's counters, with their read-backs");
        // The radiance cache: 36 MB for its million cells, emptied before its first frame.
        st.cacheKeys=make_buffer(st.d12.Get(), uint64_t(kCacheSlots)*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create radiance cache keys");
        st.cacheSums=make_buffer(st.d12.Get(), uint64_t(kCacheSlots)*16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create radiance cache sums");
        st.cacheLight=make_buffer(st.d12.Get(), uint64_t(kCacheSlots)*16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create radiance cache light");
        st.cacheKeysUav=raw_uav(st.d12.Get(), st.heap, st.cacheKeys.resource.Get(), uint64_t(kCacheSlots)*4);
        st.cacheSumsUav=raw_uav(st.d12.Get(), st.heap, st.cacheSums.resource.Get(), uint64_t(kCacheSlots)*16);
        st.cacheLightUav=raw_uav(st.d12.Get(), st.heap, st.cacheLight.resource.Get(), uint64_t(kCacheSlots)*16);
        st.cacheRan=false;
        steps.step("the radiance cache", allocation_bytes(st.cacheKeys.resource.Get())+allocation_bytes(st.cacheSums.resource.Get())+allocation_bytes(st.cacheLight.resource.Get()));
        // The caustics' landed light (pt_caustics.hlsl): the counters with a read-back slot per frame in flight, and room for a
        // million triangles to start with (with their acceleration structure's), grown when a frame wanted more.
        st.causticCounters=make_buffer(st.d12.Get(), kCausticCounters*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the caustics' counters");
        st.causticCountersUav=raw_uav(st.d12.Get(), st.heap, st.causticCounters.resource.Get(), kCausticCounters*4);
        st.causticReadback=make_buffer(st.d12.Get(), kFrames*kCausticCounters*4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create the caustics' read-back");
#ifdef TGPT_SPEED_MEASURE
        // A measuring build's counters of the two special branches (pt_common speed_count_flush; docs/agents/campspeed.md).
        st.speedCounters=make_buffer(st.d12.Get(), kSpeedCounters*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the measuring build's counters");
        st.speedCountersUav=raw_uav(st.d12.Get(), st.heap, st.speedCounters.resource.Get(), kSpeedCounters*4);
        st.speedReadback=make_buffer(st.d12.Get(), kFrames*kSpeedCounters*4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create the measuring build's counters' read-back");
        st.speedRead.fill(false); st.speedHaveLast=false;
#endif
        st.causticRead.fill(false); st.causticCapacity=0; st.causticCorners=Buffer{}; st.causticCornersUav=st.causticLightUav=kInvalid;
        st.causticTlas=Buffer{}; st.causticTlasSrv=kInvalid; st.causticBuilt=st.causticRecent=0;
        st.causticRoom=st.causticStructure=st.causticRoomIdle=st.causticIdle=0;
        // Water-free scenes need no caustic triangle storage; allocate when water first enters the traced world.
        // Where the water has been (pt_wet.hlsl, PT-52): its store (32 MB) is made by the first frame that has water (trace)
        // and goes with the scene (release_scene_extras), as the caustics' room does (PT-87).
        st.wetStore=Buffer{}; st.wetStoreUav=kInvalid;
        st.wetRan=false;
        UINT64 frequency=0; if(SUCCEEDED(st.queue->GetTimestampFrequency(&frequency)) && frequency) st.ticksPerMs=double(frequency)/1000.0;
        st.queried.fill(false);
        // The atlas starts white: a texture not baked yet leaves its material's own color. Its detail tiles' rows lie below
        // the 64x64 tiles.
        // (PT-87, memory3: the five below are textures both devices hold: made on the game's device, opened on this one. A
        // step's move beside its figure says whether Windows has counted the texture once or twice by then.)
        steps.step("the caustics' counters and their read-back");
        st.atlas=make_shared(kAtlasSize, kAtlasHeight, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        { const float white[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.atlas.t.uav.Get(), white); }
        steps.step("the colour atlas (a texture both devices hold; made, opened, its clearing recorded)", allocation_bytes(st.atlas.t.d12.Get()));
        // The alpha atlas starts opaque: a tile not baked yet keeps its surface solid, as before its texture came.
        st.alphaAtlas=make_shared(kAtlasSize, kAlphaAtlasHeight, DXGI_FORMAT_R8_UNORM, false);
        { const float one[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.alphaAtlas.t.uav.Get(), one); }
        steps.step("the alpha atlas (both devices)", allocation_bytes(st.alphaAtlas.t.d12.Get()));
        // The cookie atlas starts white: a cookie not baked yet leaves its light unmasked.
        st.cookieAtlas=make_shared(kCookieAtlasSize, kCookieAtlasSize, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        { const float white[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.cookieAtlas.t.uav.Get(), white); }
        st.sky=make_shared(kSkyWidth, kSkyHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        { const float blue[4]={0.4f, 0.6f, 1.0f, 1}; st.c11->ClearUnorderedAccessViewFloat(st.sky.t.uav.Get(), blue); }
        st.fogSky=make_shared(kFogSkyWidth, kFogSkyTile*kFogSkyLevels, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        { const float blue[4]={0.4f, 0.6f, 1.0f, 1}; st.c11->ClearUnorderedAccessViewFloat(st.fogSky.t.uav.Get(), blue); }
        steps.step("the cookie atlas, the sky and the fog's sky (both devices)",
                   allocation_bytes(st.cookieAtlas.t.d12.Get())+allocation_bytes(st.sky.t.d12.Get())+allocation_bytes(st.fogSky.t.d12.Get()));
        // PT-87 (docs/agents/memory-4.md): a handle of each atlas is kept, by which the game's device opens its side again
        // for a bake after it has let go of it (GameSide). Not the two skies': the game's device writes those all the time.
        keep_side_handle(kSideColour); keep_side_handle(kSideAlpha); keep_side_handle(kSideCookies);
        // Hair's strands at their textures' own size (85 MB); without it, hair drawn as a layer reads its color tile's alpha.
        // Hair-free scenes need no strand atlas; its first real hair bake creates it.
        char took[48]; std::snprintf(took, sizeof(took), "%.2f s", std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count());
        log("Path tracing ready: private D3D12 compute queue, producer and consumer fences, "+std::to_string(kHeapSize)+" descriptors, "+
            (st.useDxr12 ? std::string("Shader Model 6.9 passes (opacity micromaps")+(st.giSer ? ", Shader Execution Reordering)" : ")") : std::string("Shader Model 6.6 passes"))+
            ", in "+took+(st.crumbsOn ? "; GPU progress markers on" : ""));
        // (PT-87, memory3: the steps above as one. Its own figure: what the memory line will hold of them, the rings included.)
        {
            uint64_t own=ringBytes+allocation_bytes(st.cacheKeys.resource.Get())+allocation_bytes(st.cacheSums.resource.Get())+allocation_bytes(st.cacheLight.resource.Get());
            for(const Shared* t:{&st.atlas, &st.alphaAtlas, &st.cookieAtlas, &st.sky, &st.fogSky}) own+=allocation_bytes(t->t.d12.Get());
            steps.whole("the tracer's own start, all the steps above together,", own);
        }
    } catch(...) { release_all(); throw; }
    st.d3d12Ready=true;
    return true;
}

void set_constants(const D11Constants& c) { st.c11->UpdateSubresource(st.constants.Get(), 0, nullptr, &c, 0, 0); }
// Whether the game's device loads this format through a typed UAV (D3D11's optional formats), and whether it is a floating
// point one, as the D3D11 passes that read HDRP's color buffer back through a UAV declare it (float4; a UNORM target would
// need unorm float4): otherwise those passes draw over it from a copy.
bool typed_uav_load(DXGI_FORMAT format) {
    D3D11_FEATURE_DATA_FORMAT_SUPPORT2 s{format};
    return SUCCEEDED(st.d11->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &s, sizeof(s))) && (s.OutFormatSupport2&D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
}
bool float_format(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT: return true;
    default: return false;
    }
}
void run_cs(ID3D11ComputeShader* cs, std::initializer_list<ID3D11ShaderResourceView*> srvs, ID3D11UnorderedAccessView* uav, UINT x, UINT y,
            ID3D11SamplerState* sampler=nullptr) {
    auto* cb=st.constants.Get();
    st.c11->CSSetShader(cs, nullptr, 0); st.c11->CSSetConstantBuffers(0, 1, &cb);
    std::vector<ID3D11ShaderResourceView*> views(srvs);
    st.c11->CSSetShaderResources(0, UINT(views.size()), views.data());
    st.c11->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    if(sampler) st.c11->CSSetSamplers(0, 1, &sampler);
    st.c11->Dispatch(x, y, 1);
    std::vector<ID3D11ShaderResourceView*> none(views.size(), nullptr); st.c11->CSSetShaderResources(0, UINT(none.size()), none.data());
    ID3D11UnorderedAccessView* noUav=nullptr; st.c11->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
}

// A texture from the game's device read through a view with the given format.
ComPtr<ID3D11ShaderResourceView> view_as(ID3D11Resource* resource, DXGI_FORMAT format) {
    D3D11_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=format; d.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView> v; check(st.d11->CreateShaderResourceView(resource, &d, &v), "Create input view"); return v;
}
D3D11_TEXTURE2D_DESC texture_desc(ID3D11Resource* resource) {
    ComPtr<ID3D11Texture2D> t; check(resource->QueryInterface(IID_PPV_ARGS(&t)), "Input is not a 2D texture");
    D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d); return d;
}
DXGI_FORMAT stencil_view(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
// A material texture's color view: typeless color formats are read as sRGB, as Unity creates color textures.
DXGI_FORMAT color_view(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM_SRGB;
    case DXGI_FORMAT_BC4_TYPELESS: return DXGI_FORMAT_BC4_UNORM;
    case DXGI_FORMAT_BC5_TYPELESS: return DXGI_FORMAT_BC5_UNORM;
    case DXGI_FORMAT_BC6H_TYPELESS: return DXGI_FORMAT_BC6H_UF16;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    default: return f;
    }
}
// A mask map's view: its bytes as they are, typeless formats included (color_view reads those as sRGB). Through an sRGB
// view a metallic or smoothness of 0.5 in a color channel reads 0.21; a texture only an sRGB view can read has that undone
// after filtering (bake_textures).
DXGI_FORMAT linear_view(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
    case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
    case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM;
    default: return color_view(f);
    }
}
bool decodes_srgb(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM_SRGB: case DXGI_FORMAT_BC7_UNORM_SRGB: return true;
    default: return false;
    }
}

// The atlas and the sky are single textures the last frame's D3D12 work may still be reading: D3D11 writes
// them only after that work is done (a GPU wait, and only when it is not done already).
void wait_for_scene_readers() {
    if(st.consumer.value && st.consumer.d12->GetCompletedValue()<st.consumer.value)
        check(st.c11->Wait(st.consumer.d11.Get(), st.consumer.value), "D3D11 wait before updating the atlas or the sky");
}

// PT-98: what a frame may take in goes by the clock, not by the frame (the owner, 2026-10-09, of a guard's textures and a
// town filling in: "it took a LONG time before it kicked in"; the tracer ran at 5 to 10 frames a second, where "24 tiles a
// frame" is 190 a second and at 60 it is 1,440). frame_clock: how many frames of 60 a second the time since the last
// traced frame holds, 1 (60 a second and faster, and the first frame) to kClockMost (7.5 frames a second and slower, and
// the first frame after tracing was off). The tile bakes below and the scene's own budgets (GpuScene::set_clock) are
// multiplied by it: the same work a second whatever the frame rate, in fewer and larger pieces. Nothing is left out; a
// frame that takes more in is longer by that work (a tile is one small D3D11 compute pass).
constexpr double kClockMost=8.0;
double frameClock=1.0;
double frame_clock(std::chrono::steady_clock::time_point now) {
    static std::chrono::steady_clock::time_point last{};
    frameClock=last.time_since_epoch().count() ? std::clamp(std::chrono::duration<double>(now-last).count()*60.0, 1.0, kClockMost) : 1.0;
    last=now;
    return frameClock;
}
size_t by_clock(size_t aFrame) { return size_t(double(aFrame)*frameClock+0.5); }

// PT-87 (docs/agents/memory-4.md), for the bake functions, under the engine's lock, when bakes are waiting in a queue:
// whether the game's device holds its side of every atlas they write (sides). A side it has let go of is opened again
// here, BEFORE anything is taken from the queue, so a bake is written in the very frame that would have written it. The
// engine's lock is let go for that (the plugin's threads register textures under it) and taken again after: the caller
// reads its queue, and the scene's clear count again, only after this. False: a side could not be opened, or (a scene's
// own bakes; the lights' cookies are no scene's) a cleared scene's atlases could not be cleared yet (trace_event). The
// bakes then wait in their queue, as they wait for their turn: none is dropped and none changes places.
bool hold_sides(std::unique_lock<std::mutex>& lock, std::initializer_list<uint32_t> sides, bool sceneBound=true) {
    if(sceneBound && st.sceneResourcesDirty) return false;
    bool ready=true;
    for(const uint32_t side:sides) ready=ready && side_ready(side);
    if(!ready) {
        lock.unlock();
        ready=true;
        for(const uint32_t side:sides) ready=ready && take_up_side(side);
        lock.lock();
        if(!ready) return false;
    }
    for(const uint32_t side:sides) st.sides[side].written=true;
    return true;
}

void bake_textures() {
    std::vector<TextureBake> bakes;
    {
        auto& e=engine(); std::unique_lock<std::mutex> lock(e.mutex);
        if(e.clearGeneration!=st.scene.clear_generation()) return; // after the scene has taken the clear in
        if(!e.newTextures.empty() && (!hold_sides(lock, {kSideColour}) || e.clearGeneration!=st.scene.clear_generation())) return; // (PT-87, memory4)
        const size_t n=std::min<size_t>(e.newTextures.size(), by_clock(24));
        bakes.assign(std::make_move_iterator(e.newTextures.begin()), std::make_move_iterator(e.newTextures.begin()+n));
        e.newTextures.erase(e.newTextures.begin(), e.newTextures.begin()+n);
    }
    if(!bakes.empty()) wait_for_scene_readers();
    for(auto& b:bakes) {
        // Whatever becomes of it, the tile is final from this frame on (a texture that can't be read keeps it white).
        st.scene.tile_settled(b.tile);
        try {
            ComPtr<ID3D11Texture2D> t; check(b.texture.As(&t), "Material texture is not 2D");
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
            // Texture arrays, multisampled and unreadable textures keep their material's own color.
            if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)) continue;
            D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=b.mask ? linear_view(d.Format) : color_view(d.Format);
            v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels;
            ComPtr<ID3D11ShaderResourceView> srv;
            if(FAILED(st.d11->CreateShaderResourceView(t.Get(), &v, &srv))) { log_throttled("A material texture (format "+std::to_string(d.Format)+") has no color view; its material keeps its own color"); continue; }
            // A detail tile (TGPT_RegisterDetailTexture: numbered from kMaxTiles) is 512x512, in the rows below the 64x64 tiles.
            // Either is read at the mip nearest twice its size.
            const bool detail=b.tile>=int32_t(kMaxTiles);
            const uint32_t size=detail ? kDetailTileSize : kTileSize, perRow=detail ? kDetailTilesPerRow : kTilesPerRow;
            const uint32_t index=detail ? uint32_t(b.tile)-kMaxTiles : uint32_t(b.tile);
            const float lod=std::max(0.f, std::log2(float(std::max(d.Width, d.Height))/float(2*size)));
            D11Constants c{}; c.c0[0]=index%perRow*size; c.c0[1]=index/perRow*size+(detail ? kAtlasSize : 0u); c.c1[0]=std::min(lod, float(d.MipLevels-1));
            c.c1[1]=float(size);
            // A mask map is stored as its bytes are (an sRGB view's decoding undone), its channels as c0.w names them.
            if(b.mask) { c.c0[2]=2u|(decodes_srgb(v.Format) ? 0u : 1u); c.c0[3]=b.channels; }
            set_constants(c);
            run_cs(st.csAtlas.Get(), {srv.Get()}, st.atlas.t.uav.Get(), size/8, size/8, st.linearWrap.Get());
        } catch(const std::exception& e) { log_throttled(std::string("Material texture bake failed: ")+e.what()); }
    }
}

// A free block of the strand atlas: of the size wanted (a power of two from kStrandBlock / 8 to kStrandBlock), from a larger
// one split into quarters if none is free, and smaller ones as it fills up. Its corner and size, or false when it is full.
bool strand_block(uint32_t want, uint32_t& corner, uint32_t& size) {
    uint32_t cls=0; while(cls<3 && (kStrandBlock>>cls)>want) ++cls;
    for(uint32_t c=cls;c<4;++c)
        for(int from=int(c);from>=0;--from) {
            auto& list=st.strandFree[size_t(from)];
            if(list.empty()) continue;
            const uint32_t block=list.back(); list.pop_back();
            for(uint32_t k=uint32_t(from);k<c;++k) {
                const uint32_t half=kStrandBlock>>(k+1), x=block&0xFFFF, y=block>>16;
                st.strandFree[k+1].push_back((x+half)|(y<<16));
                st.strandFree[k+1].push_back(x|((y+half)<<16));
                st.strandFree[k+1].push_back((x+half)|((y+half)<<16));
            }
            corner=block; size=kStrandBlock>>c; return true;
        }
    return false;
}
// A hair material's strands (its diffuse map's alpha) at the map's own size (up to kStrandBlock) into a block of the strand
// atlas, texel for texel, with the block's mips (each level the mean of the one above), once per map: another material with
// the same map (the game's clones of it, a character's each) reads the same block. And what its hair reads them with, by its
// detail tile (FrameConstants.strandTiles): the block (none: its color tile's alpha), RealHair's alpha test, the map's tiling.
void bake_strands(const HairBake& b, uint32_t detail, ID3D11ShaderResourceView* diffuse) {
    // (Not made yet: neither device has it. PT-87, memory4: an atlas whose side the game's device has let go of is made
    // already; bake_hair has taken that side up again before it took this bake.)
    if(diffuse && b.maps[0] && !st.strandAtlas.t.d11 && !st.strandAtlas.t.d12) make_strand_atlas();
    if(detail>=kMaxDetailTiles) return;
    uint32_t corner=0, size=0;
    if(diffuse && b.maps[0] && st.strandAtlas.t.d11 && st.csStrandAlpha && st.csStrandMip && st.strandUavs.size()==kStrandLevels) {
        for(const auto& block:st.strandBlocks) if(block.map.Get()==b.maps[0].Get()) { corner=block.corner; size=block.size; break; }
        if(!size) {
            const auto d=texture_desc(b.maps[0].Get());
            uint32_t want=kStrandBlock>>3;
            while(want<kStrandBlock && want*2<=std::max(d.Width, d.Height)) want*=2;
            if(strand_block(want, corner, size)) {
                const float lod=std::max(0.f, std::log2(float(std::max(d.Width, d.Height))/float(size)));
                D11Constants c{}; c.c0[0]=corner&0xFFFF; c.c0[1]=corner>>16; c.c0[2]=size; c.c1[0]=std::min(lod, float(d.MipLevels-1));
                set_constants(c);
                run_cs(st.csStrandAlpha.Get(), {diffuse}, st.strandUavs[0].Get(), (size+7)/8, (size+7)/8, st.linearWrap.Get());
                for(uint32_t level=1, s=size>>1; level<kStrandLevels && s>=1; ++level, s>>=1) {
                    D11Constants m{}; m.c0[0]=(corner&0xFFFF)>>level; m.c0[1]=(corner>>16)>>level; m.c0[2]=s;
                    set_constants(m);
                    run_cs(st.csStrandMip.Get(), {st.strandSrvs[level-1].Get()}, st.strandUavs[level].Get(), (s+7)/8, (s+7)/8);
                }
                st.strandBlocks.push_back({b.maps[0], corner, size});
            }
        }
    }
    const uint32_t bits=size|0x80000000u;
    std::memcpy(&st.strandTiles[detail*2][0], &corner, 4); std::memcpy(&st.strandTiles[detail*2][1], &bits, 4);
    st.strandTiles[detail*2][2]=b.desc.alphaClip; st.strandTiles[detail*2][3]=b.desc.alphaPrepass;
    std::memcpy(st.strandTiles[detail*2+1], b.desc.diffuseTilingOffset, sizeof(float)*4);
}

// Hair materials' colors into their tiles (TGPT_RegisterHairColor), a few a frame, as the game's RealHair computes them:
// its diffuse, root, ID, mask and blend maps (the diffuse map through a color view, the rest as their bytes are) and values,
// into a detail tile or a 64x64 one.
void bake_hair() {
    std::vector<HairBake> bakes;
    std::vector<int32_t> givenBack;
    {
        auto& e=engine(); std::unique_lock<std::mutex> lock(e.mutex);
        if(e.clearGeneration!=st.scene.clear_generation()) return; // after the scene has taken the clear in
        // (PT-87, memory4: hair writes the colour atlas and, where it has strands, the strand atlas: both sides are held
        // before a bake is taken. Where one cannot be, the bakes wait; the tiles given back are taken all the same, in
        // the same breath as the bakes, as ever.)
        const bool held=e.newHairBakes.empty() || hold_sides(lock, {kSideColour, kSideStrands});
        if(e.clearGeneration!=st.scene.clear_generation()) return;
        givenBack.swap(e.detailTilesGivenBack);
        size_t n=std::min<size_t>(e.newHairBakes.size(), by_clock(8));
        if(!held) n=0;
        bakes.assign(std::make_move_iterator(e.newHairBakes.begin()), std::make_move_iterator(e.newHairBakes.begin()+n));
        e.newHairBakes.erase(e.newHairBakes.begin(), e.newHairBakes.begin()+n);
    }
    // Detail tiles hair gave back to the pool (TGPT_ReleaseHairColor, hair colored anew): their strand rows empty, before
    // this frame's bakes write the rows of the hair that took them since. Hair drawn as a layer waits for its row.
    for(const int32_t tile:givenBack)
        if(tile>=int32_t(kMaxTiles) && uint32_t(tile)-kMaxTiles<kMaxDetailTiles) {
            std::memset(st.strandTiles[(uint32_t(tile)-kMaxTiles)*2], 0, sizeof(float)*8);
            uint32_t none[GpuScene::kDetailRecordRows][4]; std::memset(none, 0xFF, sizeof(none));
            st.scene.set_map_record((uint32_t(tile)-kMaxTiles)*GpuScene::kDetailRecordRows, none, GpuScene::kDetailRecordRows);
        }
    if(bakes.empty() || !st.csHair || !st.hairConstants) return;
    wait_for_scene_readers();
    for(auto& b:bakes) {
        st.scene.tile_settled(b.tile);
        try {
            ComPtr<ID3D11ShaderResourceView> srvs[5]; uint32_t present=0;
            for(int i=0;i<5;++i) {
                ComPtr<ID3D11Texture2D> t;
                if(!b.maps[i] || FAILED(b.maps[i].As(&t))) continue;
                D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
                if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)) continue;
                D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=i==0 ? color_view(d.Format) : linear_view(d.Format);
                v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels;
                if(SUCCEEDED(st.d11->CreateShaderResourceView(t.Get(), &v, &srvs[i]))) present|=1u<<i;
            }
            HairConstants h{}; std::memcpy(h.v, reinterpret_cast<const uint8_t*>(&b.desc)+offsetof(TGPT_HairColor, diffuseTilingOffset), sizeof(h.v));
            st.c11->UpdateSubresource(st.hairConstants.Get(), 0, nullptr, &h, 0, 0);
            const bool detail=b.tile>=int32_t(kMaxTiles);
            const uint32_t size=detail ? kDetailTileSize : kTileSize, perRow=detail ? kDetailTilesPerRow : kTilesPerRow;
            const uint32_t index=detail ? uint32_t(b.tile)-kMaxTiles : uint32_t(b.tile);
            D11Constants c{}; c.c0[0]=index%perRow*size; c.c0[1]=index/perRow*size+(detail ? kAtlasSize : 0u); c.c0[2]=present; c.c1[1]=float(size);
            set_constants(c);
            ID3D11Buffer* hairCb=st.hairConstants.Get(); st.c11->CSSetConstantBuffers(1, 1, &hairCb);
            run_cs(st.csHair.Get(), {srvs[0].Get(), srvs[1].Get(), srvs[2].Get(), srvs[3].Get(), srvs[4].Get()}, st.atlas.t.uav.Get(),
                   size/8, size/8, st.linearWrap.Get());
            ID3D11Buffer* noCb=nullptr; st.c11->CSSetConstantBuffers(1, 1, &noCb);
            if(detail) bake_strands(b, index, srvs[0].Get());
            // PT-46: what a ray reads this hair's color with from its own maps (pt_common.hlsli hair_color): their handles,
            // which maps it has and RealHair's values. The tile stays for a map without a number.
            if(detail) {
                uint32_t rows[GpuScene::kDetailRecordRows][4]; std::memset(rows, 0xFF, sizeof(rows));
                uint64_t maps[5]{}; // which texture each handle is read from: mip streaming may make one anew (apply_texture_swaps)
                for(int i=0;i<5;++i) {
                    maps[i]=uint64_t(uintptr_t(b.maps[i].Get()));
                    (i<4 ? rows[0][i] : rows[1][0])=uint32_t(b.maps[i] ? share::handle_of(maps[i]) : -1);
                }
                rows[1][1]=1u; rows[1][2]=present; rows[1][3]=0u;
                std::memcpy(rows[2], h.v, sizeof(h.v));
                st.scene.set_map_record(index*GpuScene::kDetailRecordRows, rows, GpuScene::kDetailRecordRows, maps, 5);
            }
        } catch(const std::exception& e) { log_throttled(std::string("Hair color bake failed: ")+e.what()); }
    }
}

// Hair materials' strands into their lobe tiles (TGPT_RegisterHairLobes), a few a frame, as the game's RealHair reads them
// for its highlights: the flow and mask maps as their bytes are, the specular map through a color view.
void bake_hair_lobes() {
    std::vector<HairLobeBake> bakes;
    {
        auto& e=engine(); std::unique_lock<std::mutex> lock(e.mutex);
        if(e.clearGeneration!=st.scene.clear_generation()) return; // after the scene has taken the clear in
        if(!e.newHairLobeBakes.empty() && (!hold_sides(lock, {kSideColour}) || e.clearGeneration!=st.scene.clear_generation())) return; // (PT-87, memory4)
        const size_t n=std::min<size_t>(e.newHairLobeBakes.size(), by_clock(8));
        bakes.assign(std::make_move_iterator(e.newHairLobeBakes.begin()), std::make_move_iterator(e.newHairLobeBakes.begin()+n));
        e.newHairLobeBakes.erase(e.newHairLobeBakes.begin(), e.newHairLobeBakes.begin()+n);
    }
    if(bakes.empty() || !st.csHairLobes) return;
    wait_for_scene_readers();
    for(auto& b:bakes) {
        st.scene.tile_settled(b.tile);
        try {
            ComPtr<ID3D11ShaderResourceView> srvs[3]; uint32_t present=0;
            for(int i=0;i<3;++i) {
                ComPtr<ID3D11Texture2D> t;
                if(!b.maps[i] || FAILED(b.maps[i].As(&t))) continue;
                D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
                if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)) continue;
                D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=i==2 ? color_view(d.Format) : linear_view(d.Format);
                v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels;
                if(SUCCEEDED(st.d11->CreateShaderResourceView(t.Get(), &v, &srvs[i]))) present|=1u<<i;
            }
            D11Constants c{}; c.c0[0]=uint32_t(b.tile)%kTilesPerRow*kTileSize; c.c0[1]=uint32_t(b.tile)/kTilesPerRow*kTileSize; c.c0[2]=present;
            c.c1[0]=b.desc.smoothnessPower; c.c1[2]=b.desc.flowFlipGreen;
            set_constants(c);
            run_cs(st.csHairLobes.Get(), {srvs[0].Get(), srvs[1].Get(), srvs[2].Get()}, st.atlas.t.uav.Get(), kTileSize/8, kTileSize/8, st.linearWrap.Get());
            // PT-46: the strands read from the maps themselves at a hit (pt_common.hlsli hair_lobe): their handles, which maps
            // it has, the smoothness power and the flow's green flip.
            if(b.tile>=0 && uint32_t(b.tile)<kMaxTiles) {
                uint32_t rows[GpuScene::kLobeRecordRows][4]; std::memset(rows, 0xFF, sizeof(rows));
                uint64_t maps[3]{};
                for(int i=0;i<3;++i) { maps[i]=uint64_t(uintptr_t(b.maps[i].Get())); rows[0][i]=uint32_t(b.maps[i] ? share::handle_of(maps[i]) : -1); }
                rows[0][3]=1u;
                std::memcpy(&rows[1][0], &b.desc.smoothnessPower, 4); std::memcpy(&rows[1][1], &b.desc.flowFlipGreen, 4);
                rows[1][2]=present; rows[1][3]=0u;
                st.scene.set_map_record(GpuScene::kLobeRecordBase+uint32_t(b.tile)*GpuScene::kLobeRecordRows, rows, GpuScene::kLobeRecordRows, maps, 3);
            }
        } catch(const std::exception& e) { log_throttled(std::string("Hair lobe bake failed: ")+e.what()); }
    }
}

// The eyes' colors into their tiles (TGPT_RegisterEyeColor), a few a frame, as the game's eye shader computes them: its
// sclera and iris maps (through color views) and values, into a detail tile or a 64x64 one.
void bake_eyes() {
    std::vector<EyeBake> bakes;
    {
        auto& e=engine(); std::unique_lock<std::mutex> lock(e.mutex);
        if(e.clearGeneration!=st.scene.clear_generation()) return; // after the scene has taken the clear in
        if(!e.newEyeBakes.empty() && (!hold_sides(lock, {kSideColour}) || e.clearGeneration!=st.scene.clear_generation())) return; // (PT-87, memory4)
        const size_t n=std::min<size_t>(e.newEyeBakes.size(), by_clock(4));
        bakes.assign(std::make_move_iterator(e.newEyeBakes.begin()), std::make_move_iterator(e.newEyeBakes.begin()+n));
        e.newEyeBakes.erase(e.newEyeBakes.begin(), e.newEyeBakes.begin()+n);
    }
    if(bakes.empty() || !st.csEye || !st.eyeConstants) return;
    wait_for_scene_readers();
    for(auto& b:bakes) {
        st.scene.tile_settled(b.tile);
        try {
            ComPtr<ID3D11ShaderResourceView> srvs[2]; uint32_t present=0;
            for(int i=0;i<2;++i) {
                ComPtr<ID3D11Texture2D> t;
                if(!b.maps[i] || FAILED(b.maps[i].As(&t))) continue;
                D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
                if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)) continue;
                D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=color_view(d.Format); v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels;
                if(SUCCEEDED(st.d11->CreateShaderResourceView(t.Get(), &v, &srvs[i]))) present|=1u<<i;
            }
            EyeConstants k{}; std::memcpy(k.v, reinterpret_cast<const uint8_t*>(&b.desc)+offsetof(TGPT_EyeColor, scleraTint), sizeof(k.v));
            st.c11->UpdateSubresource(st.eyeConstants.Get(), 0, nullptr, &k, 0, 0);
            const bool detail=b.tile>=int32_t(kMaxTiles);
            const uint32_t size=detail ? kDetailTileSize : kTileSize, perRow=detail ? kDetailTilesPerRow : kTilesPerRow;
            const uint32_t index=detail ? uint32_t(b.tile)-kMaxTiles : uint32_t(b.tile);
            D11Constants c{}; c.c0[0]=index%perRow*size; c.c0[1]=index/perRow*size+(detail ? kAtlasSize : 0u); c.c0[2]=present; c.c1[1]=float(size);
            set_constants(c);
            ID3D11Buffer* eyeCb=st.eyeConstants.Get(); st.c11->CSSetConstantBuffers(1, 1, &eyeCb);
            // Clamped, as the eye shader's one sampler for both maps must be: repeating at the iris's tiling of 3.88, the iris
            // would come back all over the sclera.
            run_cs(st.csEye.Get(), {srvs[0].Get(), srvs[1].Get()}, st.atlas.t.uav.Get(), size/8, size/8, st.linearClamp.Get());
            ID3D11Buffer* noCb=nullptr; st.c11->CSSetConstantBuffers(1, 1, &noCb);
            // PT-46: what a ray reads this eye's color with from its own maps (pt_common.hlsli eye_color).
            if(detail) {
                uint32_t rows[GpuScene::kDetailRecordRows][4]; std::memset(rows, 0xFF, sizeof(rows));
                uint64_t maps[2]{};
                for(int i=0;i<2;++i) { maps[i]=uint64_t(uintptr_t(b.maps[i].Get())); rows[0][i]=uint32_t(b.maps[i] ? share::handle_of(maps[i]) : -1); }
                rows[1][1]=2u; rows[1][2]=present; rows[1][3]=0u;
                std::memcpy(rows[2], k.v, sizeof(k.v));
                st.scene.set_map_record(index*GpuScene::kDetailRecordRows, rows, GpuScene::kDetailRecordRows, maps, 2);
            }
        } catch(const std::exception& e) { log_throttled(std::string("Eye color bake failed: ")+e.what()); }
    }
}

// PT-45 phase 3: hair's and eyes' maps that mip streaming made anew (TGPT_SwapSharedTexture). After this frame's bakes, so
// a record a bake wrote from the old texture is caught too; the records are resolved again before the upload (scene.cpp).
void apply_texture_swaps() {
    std::vector<std::pair<uint64_t, uint64_t>> swaps;
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); swaps.swap(e.textureSwaps); }
    for(const auto& [from, to]:swaps) st.scene.swap_map_texture(from, to, share::handle_of(to));
}

// Lights' cookies into their 64x64 tiles, a few a frame: a point light's cubemap as an octahedral map, a spot's texture
// as it is, from the channel that holds the mask (alpha of an alpha-only texture, red of a single-channel one).
void bake_cookies() {
    std::vector<TextureBake> bakes;
    {
        auto& e=engine(); std::unique_lock<std::mutex> lock(e.mutex);
        if(!e.newCookies.empty() && !hold_sides(lock, {kSideCookies}, false)) return; // (PT-87, memory4)
        const size_t n=std::min<size_t>(e.newCookies.size(), by_clock(8));
        bakes.assign(std::make_move_iterator(e.newCookies.begin()), std::make_move_iterator(e.newCookies.begin()+n));
        e.newCookies.erase(e.newCookies.begin(), e.newCookies.begin()+n);
    }
    if(bakes.empty() || !st.cookieAtlas.t.uav) return;
    wait_for_scene_readers();
    for(auto& b:bakes) {
        try {
            ComPtr<ID3D11Texture2D> t; check(b.texture.As(&t), "Cookie is not a texture");
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
            if(d.SampleDesc.Count!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)) continue;
            const bool cube=(d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) && d.ArraySize>=6;
            D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=color_view(d.Format);
            if(cube) { v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE; v.TextureCube.MipLevels=d.MipLevels; }
            else { v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels; }
            ComPtr<ID3D11ShaderResourceView> srv;
            if(FAILED(st.d11->CreateShaderResourceView(t.Get(), &v, &srv))) { log_throttled("A light's cookie (format "+std::to_string(d.Format)+") has no view; its light stays unmasked"); continue; }
            // An octahedral texel covers about a cube face's texel at half the tile's size per face.
            const float lod=std::max(0.f, std::log2(float(std::max(d.Width, d.Height))/float(cube ? kCookieTileSize/2 : kCookieTileSize)));
            const bool alphaOnly=d.Format==DXGI_FORMAT_A8_UNORM;
            const bool redOnly=d.Format==DXGI_FORMAT_R8_UNORM || d.Format==DXGI_FORMAT_BC4_UNORM || d.Format==DXGI_FORMAT_BC4_TYPELESS ||
                               d.Format==DXGI_FORMAT_R16_UNORM || d.Format==DXGI_FORMAT_R16_FLOAT;
            D11Constants c{}; c.c0[0]=uint32_t(b.tile)%kCookieTilesPerRow*kCookieTileSize; c.c0[1]=uint32_t(b.tile)/kCookieTilesPerRow*kCookieTileSize;
            c.c1[0]=std::min(lod, float(d.MipLevels-1)); c.c1[1]=alphaOnly ? 1.f : redOnly ? 2.f : 0.f;
            set_constants(c);
            run_cs(cube ? st.csCookieCube.Get() : st.csCookie2D.Get(), {srv.Get()}, st.cookieAtlas.t.uav.Get(), kCookieTileSize/8, kCookieTileSize/8, st.linearClamp.Get());
        } catch(const std::exception& e) { log_throttled(std::string("Cookie bake failed: ")+e.what()); }
    }
}

// Alpha-tested materials' textures into their 256x256 alpha tiles, a few a frame, like the color tiles.
void bake_alpha_textures() {
    std::vector<TextureBake> bakes;
    {
        auto& e=engine(); std::unique_lock<std::mutex> lock(e.mutex);
        if(e.clearGeneration!=st.scene.clear_generation()) return; // after the scene has taken the clear in
        if(!e.newAlphaTextures.empty() && (!hold_sides(lock, {kSideAlpha}) || e.clearGeneration!=st.scene.clear_generation())) return; // (PT-87, memory4)
        const size_t n=std::min<size_t>(e.newAlphaTextures.size(), by_clock(8));
        bakes.assign(std::make_move_iterator(e.newAlphaTextures.begin()), std::make_move_iterator(e.newAlphaTextures.begin()+n));
        e.newAlphaTextures.erase(e.newAlphaTextures.begin(), e.newAlphaTextures.begin()+n);
    }
    if(!bakes.empty()) wait_for_scene_readers();
    for(auto& b:bakes) {
        st.scene.alpha_tile_settled(b.tile);
        try {
            ComPtr<ID3D11Texture2D> t; check(b.texture.As(&t), "Material texture is not 2D");
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
            if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)) continue;
            D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=color_view(d.Format);
            v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels;
            ComPtr<ID3D11ShaderResourceView> srv;
            if(FAILED(st.d11->CreateShaderResourceView(t.Get(), &v, &srv))) { log_throttled("An alpha-tested texture (format "+std::to_string(d.Format)+") has no view; its alpha tile stays opaque"); continue; }
            // Sampled 4x4 a texel at the mip nearest four times the tile's size, each sample tested against the cutoff.
            const float lod=std::max(0.f, std::log2(float(std::max(d.Width, d.Height))/float(4*kAlphaTileSize)));
            D11Constants c{}; c.c0[0]=uint32_t(b.tile)%kAlphaTilesPerRow*kAlphaTileSize; c.c0[1]=uint32_t(b.tile)/kAlphaTilesPerRow*kAlphaTileSize; c.c1[0]=std::min(lod, float(d.MipLevels-1));
            c.c1[1]=b.cutoff;
            set_constants(c);
            run_cs(st.csAlpha.Get(), {srv.Get()}, st.alphaAtlas.t.uav.Get(), kAlphaTileSize/8, kAlphaTileSize/8, st.linearWrap.Get());
        } catch(const std::exception& e) { log_throttled(std::string("Alpha texture bake failed: ")+e.what()); }
    }
}

// Characters' skinned positions from the game's skinning output into this frame's shared texture, a dispatch
// each, in the order given until the texture is full. What was copied is this parity's skin work for D3D12.
void copy_skinned(const ComPtr<ID3D11Buffer>& source, uint32_t stride, const std::vector<TGPT_SkinnedInstance>& skinned, uint32_t parity) {
    auto& work=st.skinWork[parity];
    work.clear();
    st.skinNormals[parity]=false;
    if(!source || skinned.empty()) return;
    // ON TRIAL (PT-92): the byte at which a vertex holds its skinned normal (TGPT_SetSkinned: the stride's bits 16-23), 0 none.
    uint32_t normalAt=(stride>>TGPT_SKINNED_NORMAL_SHIFT)&0xFFu;
    stride&=TGPT_SKINNED_STRIDE_MASK;
    D3D11_BUFFER_DESC bd{}; source->GetDesc(&bd);
    const bool structured=(bd.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)!=0, raw=(bd.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)!=0;
    if(structured) stride=bd.StructureByteStride;
    if((!structured && !raw) || !(bd.BindFlags&D3D11_BIND_SHADER_RESOURCE) || stride<12 || stride%4) {
        if(!st.skinLogged) { st.skinLogged=true; log("Characters are not traced: the skinning output is neither a readable structured nor a raw buffer (misc 0x"+
            tgb::hexhr(HRESULT(bd.MiscFlags))+", stride "+std::to_string(stride)+")"); }
        return;
    }
    const uint32_t vertices=bd.ByteWidth/stride;
    if(normalAt<12 || normalAt%4 || normalAt+4>stride) normalAt=0; // not inside this buffer's vertex: none
    // Views of the game's buffer, remade when it is replaced (Unity grows it by re-creating it).
    const uint32_t key=(structured ? 0x10000u : 0u)|stride|(normalAt<<20);
    if(source.Get()!=st.skinSourceBuffer.Get() || !st.skinSourceSrv) {
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};
        if(structured) { v.Format=DXGI_FORMAT_UNKNOWN; v.ViewDimension=D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement=0; v.Buffer.NumElements=vertices; }
        else { v.Format=DXGI_FORMAT_R32_TYPELESS; v.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX; v.BufferEx.FirstElement=0; v.BufferEx.NumElements=bd.ByteWidth/4; v.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW; }
        st.skinSourceSrv.Reset();
        check(st.d11->CreateShaderResourceView(source.Get(), &v, &st.skinSourceSrv), "Create the skinning output's view");
        st.skinSourceBuffer=source;
    }
    auto& cs=st.skinCs[key];
    if(!cs) {
        // (SKIN_NORMAL last: without a normal its place is the list's end.)
        const std::string rest=std::to_string((stride-12)/4), normal=std::to_string(normalAt ? (normalAt-12)/4 : 0u);
        const D3D_SHADER_MACRO defines[]={{structured ? "SKIN_STRUCTURED" : "SKIN_RAW", "1"}, {"SKIN_REST", rest.c_str()},
            {normalAt ? "SKIN_NORMAL" : nullptr, normalAt ? normal.c_str() : nullptr}, {nullptr, nullptr}};
        auto b=compile_d3d11("TGPT_SKIN", "main", "cs_5_0", defines);
        check(st.d11->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &cs), "Create the character copy shader");
        log("Characters: skinning output "+std::string(structured ? "structured" : "raw")+", "+std::to_string(stride)+" bytes a vertex, "+std::to_string(vertices)+" vertices"+
            (normalAt ? ", its skinned normals at byte "+std::to_string(normalAt)+" (characters are shaded smooth)" : std::string(", positions alone (characters are shaded by their flat triangles)")));
    }
    if(!st.skin[parity].t.d12) st.skin[parity]=make_shared(kSkinWidth, kSkinRows, DXGI_FORMAT_R32G32B32A32_FLOAT, false);
    uint32_t texel=0;
    for(const auto& s:skinned) {
        if(!s.vertexCount || uint64_t(s.firstVertex)+s.vertexCount>vertices || texel+s.vertexCount>kSkinTexels) continue;
        // A mesh once a frame: two unpacks into one position buffer and two builds of one BLAS would race on the GPU.
        if(std::any_of(work.begin(), work.end(), [&](const State::SkinWork& w) { return w.instance.mesh==s.mesh; })) continue;
        D11Constants c{}; c.c0[0]=s.firstVertex; c.c0[1]=s.vertexCount; c.c0[2]=texel; c.c0[3]=stride;
        set_constants(c);
        run_cs(cs.Get(), {st.skinSourceSrv.Get()}, st.skin[parity].t.uav.Get(), (s.vertexCount+63)/64, 1);
        State::SkinWork w; w.firstTexel=texel; w.count=s.vertexCount; w.instance=s;
        work.push_back(w);
        texel+=s.vertexCount;
    }
    st.skinNormals[parity]=normalAt!=0;
}

void update_sky() {
    ComPtr<ID3D11Resource> sky; uint32_t version=0;
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); sky=e.sky; version=e.skyVersion; }
    // HDRP re-renders its sky as the time of day moves: resampled every half second or so.
    if(!sky || (version==st.skyVersion && st.skyValid && st.frame-st.skyFrame<30)) return;
    st.skyVersion=version; st.skyFrame=st.frame;
    ComPtr<ID3D11Texture2D> t; if(FAILED(sky.As(&t))) return;
    wait_for_scene_readers();
    D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
    if(!(d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) || d.ArraySize<6) { log_throttled("HDRP's sky is not a cubemap; the tracer keeps a plain sky"); return; }
    D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=color_view(d.Format);
    const bool array=d.ArraySize>6;
    if(array) { v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBEARRAY; v.TextureCubeArray.MipLevels=d.MipLevels; v.TextureCubeArray.NumCubes=1; }
    else { v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE; v.TextureCube.MipLevels=d.MipLevels; }
    ComPtr<ID3D11ShaderResourceView> srv; check(st.d11->CreateShaderResourceView(t.Get(), &v, &srv), "Create sky view");
    D11Constants c{}; c.c0[0]=kSkyWidth; c.c0[1]=kSkyHeight; c.c1[0]=std::min(1.f, float(d.MipLevels-1));
    set_constants(c);
    run_cs(array ? st.csSkyArray.Get() : st.csSkyCube.Get(), {srv.Get()}, st.sky.t.uav.Get(), kSkyWidth/8, kSkyHeight/8, st.linearClamp.Get());
    // And at the mips HDRP's fog reads its color at (GetFogColor's SampleSkyTexture: mip 6 up close, blurring less with the
    // distance), a tile each, for the fog along reflected and refracted rays (PT-37, fog_sky). A cubemap with fewer mips
    // gives its last, as the hardware's sampling of it does.
    if(st.fogSky.t.uav) {
        for(uint32_t i=0;i<kFogSkyLevels;++i) {
            D11Constants fc{}; fc.c0[0]=kFogSkyWidth; fc.c0[1]=kFogSkyTile; fc.c0[2]=i*kFogSkyTile;
            fc.c1[0]=std::min(float(kFogSkyFirstMip+i), float(d.MipLevels-1));
            set_constants(fc);
            run_cs(array ? st.csSkyArray.Get() : st.csSkyCube.Get(), {srv.Get()}, st.fogSky.t.uav.Get(), kFogSkyWidth/8, kFogSkyTile/8, st.linearClamp.Get());
        }
    }
    if(!st.skyValid) log("Sky: HDRP's "+std::string(array ? "cubemap array" : "cubemap")+" "+std::to_string(d.Width)+"px, resampled every 30 frames");
    st.skyValid=true;
}

// Everything made for one render size let go: HDRP's frame copies, the outputs, the surfaces, the signals, the reservoirs,
// the filters' textures and the denoisers' own. The GPU must be done with whatever used them (the caller waits).
void drop_frame_textures() {
    st.psr.release(st.heap, st.release, st.consumer.value);
    st.nrdConfidence.release(st.heap, st.release, st.consumer.value);
    for(auto& in:st.inputs) for(Shared* s:{&in.gb0, &in.nrm, &in.gb2, &in.gb3, &in.depth, &in.motion, &in.exposure}) release_shared(*s);
    for(Shared* s:{&st.outputs.a, &st.outputs.b, &st.outputs.c, &st.outputs.g0, &st.outputs.g1, &st.outputs.dm, &st.outputs.mv}) release_shared(*s);
    auto drop=[](Texture& t) { if(t.resource) st.release.add(st.consumer.value, t.resource.Get()); st.heap.release(t.srv); st.heap.release(t.uav); t=Texture{}; };
    for(auto& t:st.surface) drop(t);
    for(auto& t:st.normal) drop(t);
    for(auto& t:st.material) drop(t);
    for(Texture* t:{&st.diffuseRaw, &st.sunRaw, &st.specRaw, &st.tmpA, &st.tmpB, &st.tmpC, &st.tmpD, &st.directTmp, &st.directOut,
                    &st.rrgDepth, &st.rrgMotion, &st.rrgNormals, &st.rrgDiffuse, &st.rrgSpecular,
                    &st.nrdViewZ, &st.nrdMotion, &st.nrdNormalRoughness, &st.nrdDiffuseIn, &st.nrdDiffuseOut, &st.nrdSpecularIn, &st.nrdSpecularOut}) drop(*t);
    st.nrd.destroy(); st.nrdHavePrevious=false;
    for(auto* a:{&st.diffuseHist, &st.diffuseMoments, &st.directHist, &st.directMoments, &st.specHist, &st.specMoments}) for(auto& t:*a) drop(t);
    drop(st.hair); drop(st.specSeen); drop(st.ownMotion); drop(st.ownEmission); drop(st.yardstick); drop(st.tracedFog);
    for(auto& t:st.fogGrid) drop(t); st.fogGridFrame=0;
    // PT-112, the sky (on trial): its tables, clouds and map, made again by the next frame that asks (ensure_sky_textures).
    for(Texture* t:{&st.skyTransmittance, &st.skyMultiple, &st.skyGround, &st.skyClouds, &st.skyAmbient}) drop(*t);
    if(st.skyMap.resource) st.release.add(st.consumer.value, st.skyMap.resource.Get());
    st.heap.release(st.skyMap.srv); for(uint32_t u:st.skyMap.uav) st.heap.release(u);
    st.skyMap=State::MipTexture{}; st.skyTablesKey=st.skyCloudsKey=0; st.skyTablesPending=st.skyCloudsPending=false;
    for(Buffer* r:{&st.diRes, &st.giRes}) { if(*r) st.release.add(st.consumer.value, r->resource.Get()); *r=Buffer{}; }
    st.heap.release(st.diUav); st.heap.release(st.giUav); st.diUav=st.giUav=kInvalid;
}

void ensure_frame_textures(uint32_t w, uint32_t h) {
    if(st.width==w && st.height==h && st.outputs.a.t.d11) return;
    // Whatever used the old textures has to be finished on the GPU first.
    st.consumer.wait_cpu(st.consumer.value);
    drop_frame_textures();
    auto* d=st.d12.Get(); auto& heap=st.heap;
    // (PT-87: for the log line at this function's end, and memory3's steps: the render size's textures by kind.)
    const double countedBefore=windows_counts_mb(d);
    StartSteps steps(d, &tally.stepsDevice);
    const std::string size=" at "+std::to_string(w)+" x "+std::to_string(h);
    auto bytes_of=[](std::initializer_list<ID3D12Resource*> resources) { uint64_t n=0; for(ID3D12Resource* r:resources) n+=allocation_bytes(r); return n; };
    st.outputs.a=make_shared(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    st.outputs.b=make_shared(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    st.outputs.c=make_shared(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, true); // RGB specular albedo + A hit distance
    st.outputs.g0=make_shared(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    st.outputs.g1=make_shared(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    st.outputs.dm=make_shared(w, h, DXGI_FORMAT_R32_FLOAT, true);
    st.outputs.mv=make_shared(w, h, DXGI_FORMAT_R16G16_FLOAT, true);
    steps.step("the seven outputs"+size+" (textures both devices hold)",
               bytes_of({st.outputs.a.t.d12.Get(), st.outputs.b.t.d12.Get(), st.outputs.c.t.d12.Get(), st.outputs.g0.t.d12.Get(), st.outputs.g1.t.d12.Get(),
                         st.outputs.dm.t.d12.Get(), st.outputs.mv.t.d12.Get()}));
    CountNow copiesMoved{0, 0, true}, ownMoved{0, 0, true}; uint64_t copiesBytes=0, ownBytes=0;
    for(int i=0;i<2;++i) {
        auto& in=st.inputs[i];
        in.gb0=make_shared(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        in.nrm=make_shared(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        in.gb2=make_shared(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        in.gb3=make_shared(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        in.depth=make_shared(w, h, DXGI_FORMAT_R32_FLOAT, false);
        in.motion=make_shared(w, h, DXGI_FORMAT_R16G16_FLOAT, false);
        in.exposure=make_shared(1, 1, DXGI_FORMAT_R32_FLOAT, false);
        copiesMoved=copiesMoved+steps.lap();
        copiesBytes+=bytes_of({in.gb0.t.d12.Get(), in.nrm.t.d12.Get(), in.gb2.t.d12.Get(), in.gb3.t.d12.Get(), in.depth.t.d12.Get(), in.motion.t.d12.Get(), in.exposure.t.d12.Get()});
        st.surface[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create surface texture");
        st.normal[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32_UINT, "Create normal texture");
        // (The built-in filters' histories, moments and scratch: ensure_filter_textures, when a frame runs them. PT-87.)
        // This frame's material and the previous frame's: RTXDI's temporal reuse weighs a light at the previous surface.
        st.material[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32_UINT, "Create material texture");
        ownMoved=ownMoved+steps.lap();
        ownBytes+=bytes_of({st.surface[i].resource.Get(), st.normal[i].resource.Get(), st.material[i].resource.Get()});
    }
    steps.say("the copies of HDRP's frame, this frame's and the last's (14 textures both devices hold)", copiesMoved, copiesBytes);
    steps.say("the surfaces, normals and materials of this frame and the last (6 textures of the tracer's own)", ownMoved, ownBytes);
    // RTXDI's reservoir layout (RTXDI_ReservoirPositionToPointer): 16x16 pixel blocks, block rows, then arrays.
    st.rtxdiRowPitch=((w+RTXDI_RESERVOIR_BLOCK_SIZE-1)/RTXDI_RESERVOIR_BLOCK_SIZE)*RTXDI_RESERVOIR_BLOCK_SIZE*RTXDI_RESERVOIR_BLOCK_SIZE;
    st.rtxdiArrayPitch=st.rtxdiRowPitch*((h+RTXDI_RESERVOIR_BLOCK_SIZE-1)/RTXDI_RESERVOIR_BLOCK_SIZE);
    st.diRes=make_buffer(d, uint64_t(st.rtxdiArrayPitch)*2*sizeof(RTXDI_PackedDIReservoir), D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create DI reservoirs");
    st.diUav=structured_uav(d, heap, st.diRes.resource.Get(), st.rtxdiArrayPitch*2, sizeof(RTXDI_PackedDIReservoir));
    st.giRes=make_buffer(d, uint64_t(st.rtxdiArrayPitch)*2*sizeof(RTXDI_PackedGIReservoir), D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create GI reservoirs");
    st.giUav=structured_uav(d, heap, st.giRes.resource.Get(), st.rtxdiArrayPitch*2, sizeof(RTXDI_PackedGIReservoir));
    if(!st.neighborOffsets) {
        std::vector<uint8_t> bytes(size_t(kNeighborOffsets)*2);
        rtxdi::FillNeighborOffsetBuffer(bytes.data(), kNeighborOffsets);
        st.neighborOffsets=make_buffer(d, uint64_t(kNeighborOffsets)*8, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create RTXDI neighbour offsets");
        auto* offsets=static_cast<float*>(st.neighborOffsets.mapped);
        for(size_t i=0;i<bytes.size();++i) offsets[i]=float(int8_t(bytes[i]))/127.f; // signed bytes, as RTXDI's R8G8_SNORM view reads them
        st.neighborOffsetsSrv=structured_srv(d, heap, st.neighborOffsets.resource.Get(), kNeighborOffsets, 8);
    }
    steps.step("RTXDI's own memory: the reservoirs of the direct and the bounced light (two arrays each) and its neighbour offsets",
               bytes_of({st.diRes.resource.Get(), st.giRes.resource.Get()}));
    st.diffuseRaw=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create diffuse signal");
    st.sunRaw=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light signal");
    st.specRaw=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular signal");
    st.hair=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32_UINT, "Create the hair texture");
    st.specSeen=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32_UINT, "Create the reflections' guide");
    // The emission in HDRP's own units (a flame's is far past half precision's largest at a dark exposure).
    // (z: the view depth's change, for 2.5D motion: pt_primary point_motion; w unused.)
    st.ownMotion=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create the camera rays' own motion");
    st.ownEmission=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create the camera rays' emission");
    st.yardstick=make_texture(d, heap, w, h, DXGI_FORMAT_R32_UINT, "Create the yardstick");
    steps.step("the three raw signals, hair's texture, the reflections' guide, the camera rays' own motion and emission and the yardstick (8 textures of the tracer's own)",
               bytes_of({st.diffuseRaw.resource.Get(), st.sunRaw.resource.Get(), st.specRaw.resource.Get(), st.hair.resource.Get(), st.specSeen.resource.Get(),
                         st.ownMotion.resource.Get(), st.ownEmission.resource.Get(), st.yardstick.resource.Get()}));
    st.width=w; st.height=h; st.resetNext=true; st.inputsRetire={};
    st.diffuseFilterIdle=st.ownFilterIdle=st.nrdIdle=st.rrgIdle=0;
    // What was just made, as the device counts it: HDRP's frame twice (2 x 28 bytes a pixel), the outputs (26), the surfaces,
    // normals and materials of this frame and the last (2 x 32), the reservoirs (two arrays of each), the three signals
    // (3 x 8), hair's, the reflections' guide and the camera rays' own (40).
    uint64_t made=allocation_bytes(st.diRes.resource.Get())+allocation_bytes(st.giRes.resource.Get()), both=0;
    for(const auto& in:st.inputs) for(const Shared* s:{&in.gb0, &in.nrm, &in.gb2, &in.gb3, &in.depth, &in.motion, &in.exposure}) both+=allocation_bytes(s->t.d12.Get());
    for(const Shared* s:{&st.outputs.a, &st.outputs.b, &st.outputs.c, &st.outputs.g0, &st.outputs.g1, &st.outputs.dm, &st.outputs.mv}) both+=allocation_bytes(s->t.d12.Get());
    made+=both;
    for(const auto* a:{&st.surface, &st.normal, &st.material}) for(const auto& t:*a) made+=allocation_bytes(t.resource.Get());
    for(const Texture* t:{&st.diffuseRaw, &st.sunRaw, &st.specRaw, &st.hair, &st.specSeen, &st.ownMotion, &st.ownEmission, &st.yardstick}) made+=allocation_bytes(t->resource.Get());
    // (PT-87: and what Windows' count for the process did meanwhile; `both` of the bytes are textures both devices hold.
    // A move of made + both: Windows counts those twice. Of made: once.)
    char counted[200]; std::snprintf(counted, sizeof(counted), "; %llu MB of them are textures both devices hold; Windows' count for the process moved by %+.0f MB meanwhile",
                                     (unsigned long long)(both>>20), windows_counts_mb(d)-countedBefore);
    log("Render size "+std::to_string(w)+"x"+std::to_string(h)+": tracer textures and reservoirs recreated ("+std::to_string(made>>20)+
        " MB; the built-in filters' textures and a denoiser's own are made when a frame runs them"+counted+")");
}

// PT-87 (docs/agents/memory-1.md). The built-in filters' textures at the render size, made when a frame runs the filter they
// serve (ensure_filter_textures) and let go once kIdleFrames traced frames in a row went without it (release_idle_filters;
// the GPU is long done with them then: a list is recorded again only when the frame that used it kFrames ago is finished).
// diffuse: the bounced light's temporal accumulation and its a-trous ping and pong (the built-in denoiser alone). own: the
// direct light's and the reflections' (the built-in denoiser, and NRD, which takes the bounced light only). No shader
// names them but pt_temporal and pt_atrous by their dispatch constants and pt_compose by the three results
// (FrameConstants.denoise1 x and z, denoise2 z), which the frame points at the raw signals when the filters do not run.
// A group just made holds nothing: the frame says so (FLAG_DENOISER_RESET, as when the filters resume: pt_temporal then
// reads no history, and every texel a later pass reads was written in the same frame). Returns true when it made one;
// throws without room for one (nothing of that group is left behind).
// Moments in 32-bit floats: a hot spot's second moment passed half precision's largest value (65,504 at a luminance of 256
// in storage units), and a dim wall's fell below its normal range.
// A texture no frame in flight uses any more, gone once the GPU is past the last frame handed over.
void drop_idle(Texture& t) { if(t.resource) st.release.add(st.consumer.value, t.resource.Get()); st.heap.release(t.srv); st.heap.release(t.uav); t=Texture{}; }
void drop_diffuse_filter() { for(auto* a:{&st.diffuseHist, &st.diffuseMoments}) for(auto& t:*a) drop_idle(t); drop_idle(st.tmpA); drop_idle(st.tmpB); st.diffuseFilterIdle=0; }
void drop_own_filters() {
    for(auto* a:{&st.directHist, &st.directMoments, &st.specHist, &st.specMoments}) for(auto& t:*a) drop_idle(t);
    drop_idle(st.tmpC); drop_idle(st.tmpD); drop_idle(st.directTmp); drop_idle(st.directOut); st.ownFilterIdle=0;
}
bool ensure_filter_textures(bool diffuse, bool own) {
    auto* d=st.d12.Get(); auto& heap=st.heap; const uint32_t w=st.width, h=st.height;
    bool made=false;
    if(!(diffuse && !st.tmpB) && !(own && !st.directOut)) return false;
    StartSteps steps(d, &tally.stepsPasses); // PT-87 (memory3): what making a group does to Windows' count
    auto bytes_of=[](std::initializer_list<const Texture*> textures) { uint64_t n=0; for(const Texture* t:textures) n+=allocation_bytes(t->resource.Get()); return n; };
    if(diffuse && !st.tmpB) { // the last one made: a group is whole or not there
        try {
            for(int i=0;i<2;++i) {
                st.diffuseHist[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create diffuse history");
                st.diffuseMoments[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create diffuse moments");
            }
            st.tmpA=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create denoiser ping");
            st.tmpB=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create denoiser pong");
        } catch(...) { drop_diffuse_filter(); throw; }
        made=true;
        steps.step("the built-in denoiser's textures for the bounced light (6 of the tracer's own)",
                   bytes_of({&st.diffuseHist[0], &st.diffuseHist[1], &st.diffuseMoments[0], &st.diffuseMoments[1], &st.tmpA, &st.tmpB}));
    }
    if(own && !st.directOut) {
        try {
            for(int i=0;i<2;++i) {
                st.directHist[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light history");
                st.directMoments[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create direct light moments");
                st.specHist[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular history");
                st.specMoments[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create specular moments");
            }
            st.tmpC=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular denoiser ping");
            st.tmpD=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular denoiser pong");
            st.directTmp=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light denoiser ping");
            st.directOut=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light denoiser output");
        } catch(...) { drop_own_filters(); throw; }
        made=true;
        steps.step("the direct light's and the reflections' filter textures (12 of the tracer's own)",
                   bytes_of({&st.directHist[0], &st.directHist[1], &st.directMoments[0], &st.directMoments[1], &st.specHist[0], &st.specHist[1], &st.specMoments[0],
                             &st.specMoments[1], &st.tmpC, &st.tmpD, &st.directTmp, &st.directOut}));
    }
    return made;
}
// Once a frame, with the filters it ran: a group no frame has run for kIdleFrames in a row is let go (not with keep_idle).
void release_idle_filters(bool diffuseRan, bool ownRan) {
    if(diffuseRan) st.diffuseFilterIdle=0;
    else if((st.diffuseHist[0] || st.tmpB) && ++st.diffuseFilterIdle>=kIdleFrames && !keep_idle()) {
        drop_diffuse_filter();
        log("The built-in denoiser's textures for the bounced light are let go: no frame has run it for "+std::to_string(kIdleFrames)+" frames");
    }
    if(ownRan) st.ownFilterIdle=0;
    else if((st.directHist[0] || st.directOut) && ++st.ownFilterIdle>=kIdleFrames && !keep_idle()) {
        drop_own_filters();
        log("The direct light's and the reflections' filter textures are let go: no frame has run them for "+std::to_string(kIdleFrames)+" frames");
    }
}

// HDRP's frame into one input set, over the render rectangle.
void copy_inputs(const TGPT_Frame& f, Inputs& in) {
    auto* gb0=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.gbuffer0));
    auto* nb=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.normal));
    auto* gb2=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.gbuffer2));
    auto* gb3=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.gbuffer3));
    auto* depth=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.depth));
    auto* motion=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.motion));
    auto* exposure=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.exposure));
    const UINT w=f.renderWidth, h=f.renderHeight, gx=(w+7)/8, gy=(h+7)/8;
    const auto gb0Desc=texture_desc(gb0), depthDesc=texture_desc(depth);
    if(gb0Desc.Width<w || gb0Desc.Height<h || depthDesc.Width<w || depthDesc.Height<h) throw DxError(E_INVALIDARG, "HDRP's GBuffer is smaller than the render size");
    // GBuffer0: bytes as they are (sRGB-encoded) when a UNORM view is possible, re-encoded otherwise.
    D11Constants c{}; c.c0[0]=w; c.c0[1]=h;
    DXGI_FORMAT albedoView=DXGI_FORMAT_R8G8B8A8_UNORM;
    switch(gb0Desc.Format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: break;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: albedoView=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; c.c0[2]|=1; break;
    default: albedoView=tgb::typed_read_format(gb0Desc.Format, tgb::ReadAs::Color); c.c0[2]|=1; break;
    }
    if(albedoView==DXGI_FORMAT_UNKNOWN) throw DxError(DXGI_ERROR_UNSUPPORTED, "Unsupported GBuffer0 format "+std::to_string(gb0Desc.Format));
    auto albedoSrv=view_as(gb0, albedoView);
    ComPtr<ID3D11ShaderResourceView> stencilSrv;
    const DXGI_FORMAT sv=stencil_view(depthDesc.Format);
    if(sv!=DXGI_FORMAT_UNKNOWN) {
        D3D11_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=sv; d.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels=1;
        if(FAILED(st.d11->CreateShaderResourceView(depth, &d, &stencilSrv))) stencilSrv.Reset();
    }
    if(stencilSrv) c.c0[2]|=2;
    else log_throttled("HDRP's stencil can't be read (depth format "+std::to_string(depthDesc.Format)+"); every surface counts as deferred");
    set_constants(c);
    run_cs(st.csAlbedo.Get(), {albedoSrv.Get(), stencilSrv.Get()}, in.gb0.t.uav.Get(), gx, gy);
    c.c0[2]=0; set_constants(c);
    run_cs(st.csFloat4.Get(), {tgb::typed_srv(st.d11.Get(), nb, tgb::ReadAs::Color).Get()}, in.nrm.t.uav.Get(), gx, gy);
    if(gb2) run_cs(st.csFloat4.Get(), {tgb::typed_srv(st.d11.Get(), gb2, tgb::ReadAs::Color).Get()}, in.gb2.t.uav.Get(), gx, gy);
    if(gb3) run_cs(st.csFloat4.Get(), {tgb::typed_srv(st.d11.Get(), gb3, tgb::ReadAs::Color).Get()}, in.gb3.t.uav.Get(), gx, gy);
    run_cs(st.csDepth.Get(), {tgb::typed_srv(st.d11.Get(), depth, tgb::ReadAs::Depth).Get()}, in.depth.t.uav.Get(), gx, gy);
    ComPtr<ID3D11ShaderResourceView> motionSrv;
    if(motion) motionSrv=tgb::typed_srv(st.d11.Get(), motion, tgb::ReadAs::Motion);
    c.c0[2]=motionSrv ? 1u : 0u; set_constants(c);
    run_cs(st.csMotion.Get(), {motionSrv.Get()}, in.motion.t.uav.Get(), gx, gy);
    ComPtr<ID3D11ShaderResourceView> exposureSrv;
    if(exposure) { try { exposureSrv=tgb::typed_srv(st.d11.Get(), exposure, tgb::ReadAs::Motion); } catch(...) { exposureSrv.Reset(); } }
    c.c0[2]=exposureSrv ? 1u : 0u; c.c1[0]=1.f; set_constants(c);
    run_cs(st.csExposure.Get(), {exposureSrv.Get()}, in.exposure.t.uav.Get(), 1, 1);
}

#include "sky_model.inl" // PT-112, the sky, on trial: fill_sky, ensure_sky_textures, record_sky
// One 8x8 group per 8x8 pixels of the render size, or the groups given.
void dispatch(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso, std::initializer_list<uint32_t> constants, Barriers& barriers,
              uint32_t groupsX=0, uint32_t groupsY=0) {
    uint32_t values[8]; std::fill(std::begin(values), std::end(values), kInvalid);
    size_t i=0; for(uint32_t v:constants) if(i<8) values[i++]=v;
    list->SetPipelineState(pso);
    list->SetComputeRoot32BitConstants(1, 8, values, 0);
    list->Dispatch(groupsX ? groupsX : (st.width+7)/8, groupsY ? groupsY : (st.height+7)/8, 1);
    barriers.uav(nullptr); barriers.flush(list);
}
// AMD Ray Regeneration's guides at the render size, made when it first denoises.
#include "fog_froxel.inl" // PT-118: allocation/recording only, no exported API or frame-constant rows
#include "nrd_confidence.inl" // PT-127: isolated lighting-gradient trial
void ensure_rrg_textures() {
    if(st.rrgDepth) return;
    auto* d=st.d12.Get(); const uint32_t w=st.width, h=st.height;
    st.rrgDepth=make_texture(d, st.heap, w, h, DXGI_FORMAT_R32_FLOAT, "Create Ray Regeneration depth");
    st.rrgMotion=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create Ray Regeneration motion");
    st.rrgNormals=make_texture(d, st.heap, w, h, DXGI_FORMAT_R10G10B10A2_UNORM, "Create Ray Regeneration normals");
    st.rrgDiffuse=make_texture(d, st.heap, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, "Create Ray Regeneration diffuse albedo");
    st.rrgSpecular=make_texture(d, st.heap, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, "Create Ray Regeneration specular albedo");
}

// NVIDIA NRD's inputs and output at the render size, made when it first denoises: view depth, 2.5D motion, normal and
// roughness in its encoding, the bounced light with its hit distance, and its result.
void ensure_nrd_textures() {
    if(st.nrdViewZ) return;
    auto* d=st.d12.Get(); const uint32_t w=st.width, h=st.height;
    st.nrdViewZ=make_texture(d, st.heap, w, h, DXGI_FORMAT_R32_FLOAT, "Create NRD view depth");
    st.nrdMotion=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create NRD motion");
    st.nrdNormalRoughness=make_texture(d, st.heap, w, h, DXGI_FORMAT_R10G10B10A2_UNORM, "Create NRD normals");
    st.nrdDiffuseIn=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create NRD diffuse input");
    st.nrdDiffuseOut=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create NRD diffuse output");
    st.nrdSpecularIn=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create NRD specular input");
    st.nrdSpecularOut=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create NRD specular output");
}
// PT-87: a denoiser's own memory is let go once kIdleFrames traced frames in a row ran without it (another row of Denoising,
// a debug view): NRD's inputs, results and texture pools (262 MB at 1924 x 1084), Ray Regeneration's guides and context.
// They are made again when it next runs; its history starts over then, as it does whenever it resumes (nrdHavePrevious,
// Nrd's restart, a new context). The GPU is long done with them: no frame in flight is that old. (TGPT_TEST_KEEP_IDLE, a
// test's: nothing is let go, keep_idle.)
void release_idle_denoisers(bool nrdRuns, bool rrgRuns) {
    if(nrdRuns) st.nrdIdle=0;
    else if((st.nrdViewZ || st.nrdSpecularOut || st.nrd.has_pools()) && ++st.nrdIdle>=kIdleFrames && !keep_idle()) {
        for(Texture* t:{&st.nrdViewZ, &st.nrdMotion, &st.nrdNormalRoughness, &st.nrdDiffuseIn, &st.nrdDiffuseOut, &st.nrdSpecularIn, &st.nrdSpecularOut}) drop_idle(*t);
        st.nrdConfidence.release(st.heap, st.release, st.consumer.value);
        st.nrd.destroy(); st.nrdHavePrevious=false; st.nrdIdle=0;
        log("NVIDIA NRD's textures are let go: no frame has run it for "+std::to_string(kIdleFrames)+" frames");
    }
    if(rrgRuns) st.rrgIdle=0;
    else if((st.rrgDepth || st.rrgSpecular || st.rrg.has_context() || st.rrg.is_starting()) && ++st.rrgIdle>=kIdleFrames && !keep_idle() && !st.rrg.is_creating()) {
        for(Texture* t:{&st.rrgDepth, &st.rrgMotion, &st.rrgNormals, &st.rrgDiffuse, &st.rrgSpecular}) drop_idle(*t);
        st.rrg.destroy(); st.rrgActive=false; st.rrgHavePrevious=false; st.rrgScale=0; st.rrgIdle=0;
        log("AMD Ray Regeneration's guides and context are let go: no frame has run it for "+std::to_string(kIdleFrames)+" frames");
    }
}
// PT-87: path tracing is off (the plugin says so with its maintenance event, render event 7 with data 1: the Mode row is
// Off, or tracing cannot run this session; NOT a pause for textures, which must come back by itself and would find the
// card fuller again) and no frame has been handed over for kOffMs: what only a traced frame needs is let go, 1.1 to 1.6 GB
// at 1924 x 1084 (the render size's textures and reservoirs, the filters', the denoiser's own, the caustics' room). The
// next traced frame makes them again (ensure_frame_textures finds no size: milliseconds, no world to build), and its
// history starts over, as after any gap in the frames. The traced world itself stays: letting that go is the plugin's
// to decide (docs/agents/memory-1.md, the choices).
constexpr uint64_t kOffMs=5000;
void release_frame_memory() {
    if(!st.width || !st.d3d12Ready || st.recording) return;
    uint64_t bytes=allocation_bytes(st.diRes.resource.Get())+allocation_bytes(st.giRes.resource.Get())+st.nrd.memory_bytes()+st.nrdConfidence.memory_bytes()+st.psr.memory_bytes();
    for(const auto& in:st.inputs) for(const Shared* s:{&in.gb0, &in.nrm, &in.gb2, &in.gb3, &in.depth, &in.motion, &in.exposure}) bytes+=allocation_bytes(s->t.d12.Get());
    for(const Shared* s:{&st.outputs.a, &st.outputs.b, &st.outputs.c, &st.outputs.g0, &st.outputs.g1, &st.outputs.dm, &st.outputs.mv}) bytes+=allocation_bytes(s->t.d12.Get());
    for(const auto* a:{&st.surface, &st.normal, &st.material, &st.diffuseHist, &st.diffuseMoments, &st.directHist, &st.directMoments, &st.specHist, &st.specMoments})
        for(const auto& t:*a) bytes+=allocation_bytes(t.resource.Get());
    for(const Texture* t:{&st.diffuseRaw, &st.sunRaw, &st.specRaw, &st.tmpA, &st.tmpB, &st.tmpC, &st.tmpD, &st.directTmp, &st.directOut, &st.hair, &st.specSeen,
                          &st.ownMotion, &st.ownEmission, &st.yardstick, &st.rrgDepth, &st.rrgMotion, &st.rrgNormals, &st.rrgDiffuse, &st.rrgSpecular,
                          &st.nrdViewZ, &st.nrdMotion, &st.nrdNormalRoughness, &st.nrdDiffuseIn, &st.nrdDiffuseOut, &st.nrdSpecularIn, &st.nrdSpecularOut}) bytes+=allocation_bytes(t->resource.Get());
    for(const Buffer* b:{&st.causticCorners, &st.causticLight, &st.causticBlas, &st.causticBlasScratch, &st.causticTlas, &st.causticTlasScratch, &st.causticPicks}) bytes+=allocation_bytes(b->resource.Get());
    st.consumer.wait_cpu(st.consumer.value); // the last frame handed over is long done; nothing waits here in practice
    drop_frame_textures();
    st.rrg.destroy(); st.rrgActive=false; st.rrgHavePrevious=false; st.rrgScale=0; st.nrdActive=false;
    release_caustic_room(st.consumer.value);
    st.width=st.height=0; st.resetNext=true; st.inputsRetire={};
    st.denoisedLast=st.ownDenoisedLast=false; st.diffuseFilterIdle=st.ownFilterIdle=st.nrdIdle=st.rrgIdle=0;
    log("Path tracing is off: what only a traced frame needs is let go ("+std::to_string(bytes>>20)+" MB); the next traced frame makes it again");
}
// World-to-view for NRD in the scene's origin-relative space: HDRP's camera rotation (Unity's memory order, column
// vectors) and the camera's position, so the previous frame's differs from this one's by the camera's move.
void nrd_view(const float* view, const double* camera, float* out) {
    std::memcpy(out, view, 16*sizeof(float));
    const float p[3]={float(camera[0]-st.origin[0]), float(camera[1]-st.origin[1]), float(camera[2]-st.origin[2])};
    for(int r=0;r<3;++r) out[12+r]=-(view[r]*p[0]+view[4+r]*p[1]+view[8+r]*p[2]);
    out[3]=out[7]=out[11]=0.f; out[15]=1.f;
}

// A pass as a ray generation shader over the render size (Shader Execution Reordering), or as its compute
// pipeline where there is none.
void dispatch_pass(ID3D12GraphicsCommandList4* list, ID3D12StateObject* rays, uint32_t record, ID3D12PipelineState* pso, Barriers& barriers) {
#ifdef TGPT_SPEED_MEASURE
    // A measuring build: the two passes take its counters' buffer in their constants' last word (pt_common speed_count_flush).
    if(!rays || st.plainPasses) { dispatch(list, pso, {kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, kInvalid, st.speedCountersUav}, barriers); return; }
    uint32_t values[8]; std::fill(std::begin(values), std::end(values), kInvalid);
    values[7]=st.speedCountersUav;
#else
    if(!rays || st.plainPasses) { dispatch(list, pso, {}, barriers); return; } // (plainPasses: PT-99's bit 8, for finding causes)
    uint32_t values[8]; std::fill(std::begin(values), std::end(values), kInvalid);
#endif
    list->SetPipelineState1(rays);
    list->SetComputeRoot32BitConstants(1, 8, values, 0);
    D3D12_DISPATCH_RAYS_DESC d{};
    d.RayGenerationShaderRecord.StartAddress=st.serTable.va()+uint64_t(record)*D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    d.RayGenerationShaderRecord.SizeInBytes=D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    d.Width=st.width; d.Height=st.height; d.Depth=1;
    list->DispatchRays(&d);
    barriers.uav(nullptr); barriers.flush(list);
}

// The stages' mean GPU time over the frames summed, as text: "scene 1.20 ms, prepare 0.31 ms, ..., water 0.80 ms".
std::string stage_text(const std::array<double, kStages>& sums, uint32_t frames) {
    if(!frames) return "none yet";
    std::string text;
    for(uint32_t s=0;s<kStages;++s) {
        char b[64]; std::snprintf(b, sizeof(b), "%s%s %.2f ms", s ? ", " : "", kStageNames[s], sums[s]/frames);
        text+=b;
    }
    return text;
}
// About once a second, the stages' means since for TGPT_GetStageTimes (the F8 report).
void publish_stage_times() {
    const uint64_t now=GetTickCount64();
    if(!st.recentStart) { st.recentStart=now; return; }
    if(now-st.recentStart<1000 || !st.recentFrames) return;
    std::string text=stage_text(st.recentStage, st.recentFrames);
    // (PT-109, megageo-5: and the scene stage's structures by kind over the same second, those that took time or were built:
    // "structures: plants 1.20 ms (2.0 built, 600.0 refitted a frame), ...". The harness's walk reads it a second at a time.)
    if(st.recentStructRecorded) {
        std::string parts;
        for(uint32_t kind=0;kind<GpuScene::kStructureKinds;++kind) {
            const double t=st.recentStructFrames ? st.recentStruct[kind]/st.recentStructFrames : 0.0;
            const auto& w=st.recentStructWork[kind];
            if(t<0.005 && !w.builds && !w.refits) continue;
            char b[160];
            std::snprintf(b, sizeof(b), "%s%s %.2f ms (%.1f built, %.1f refitted a frame)", parts.empty() ? "" : ", ", kStructureNames[kind], t,
                          double(w.builds)/st.recentStructRecorded, double(w.refits)/st.recentStructRecorded);
            parts+=b;
        }
        text+="; structures: "+(parts.empty() ? std::string("none") : parts);
    }
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.stageTimes=text; }
    st.recentStage={}; st.recentFrames=0; st.recentStart=now;
    st.recentStruct={}; st.recentStructWork={}; st.recentStructFrames=0; st.recentStructRecorded=0;
}

// The yardstick's numbers of one traced frame (pt_yardstick.hlsl's counters): for the plugin (TGPT_GetYardstick, which names
// the materials), and as a line in the log every ten seconds. own: the picture itself was made of the tracer's own camera
// rays in that frame (not only the yardstick's view).
void publish_yardstick(const uint32_t* c, uint32_t frameIndex, bool own) {
    TGPT_Yardstick y{sizeof(TGPT_Yardstick), TGPT_ABI};
    y.frameIndex=frameIndex; y.pixels=c[6];
    for(int i=0;i<6;++i) y.where[i]=c[i];
    y.normal=c[8]; y.colour=c[9]; y.roughness=c[10]; y.motion=c[11]; y.forward=c[12];
    for(int k=0;k<5;++k) for(int i=0;i<6;++i) y.kinds[k][i]=c[16+8*k+i];
    // The materials with the most differing pixels.
    std::vector<uint32_t> order;
    auto row=[&](uint32_t m) { return c+kYardCounters+size_t(kYardPerMaterial)*m; };
    for(uint32_t m=0;m<kYardMaterials;++m) if(row(m)[1]) order.push_back(m);
    const size_t top=std::min<size_t>(order.size(), TGPT_YARDSTICK_MATERIALS);
    std::partial_sort(order.begin(), order.begin()+top, order.end(), [&](uint32_t a, uint32_t b) { return row(a)[1]>row(b)[1]; });
    y.materialCount=uint32_t(top);
    for(size_t i=0;i<top;++i) {
        const uint32_t* r=row(order[i]);
        y.materials[i].material=order[i]; y.materials[i].pixels=r[0]; y.materials[i].differing=r[1];
        y.materials[i].place=r[2]; y.materials[i].colour=r[3]; y.materials[i].normal=r[4]; y.materials[i].roughness=r[5];
    }
    // PT-40, who takes the scattering walk (pt_yardstick's counters 13 and 14): the pixels whose ray took it in that frame,
    // and those on a material light walks inside.
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.yardstick=y; e.walkPixels[0]=frameIndex; e.walkPixels[1]=c[14]; e.walkPixels[2]=c[13]; }
    const uint64_t now=GetTickCount64();
    if(st.yardLogged && now-st.yardLogged<10000) return;
    st.yardLogged=now;
    if(!y.pixels) return;
    const double all=double(y.pixels), same=double(std::max(y.where[1], 1u));
    auto share=[&](uint32_t n) { return 100.0*double(n)/all; };
    char b[1024];
    std::snprintf(b, sizeof(b), "Yardstick (the tracer's own first hit against HDRP's G-buffer, frame %u, %s): the same surface %.1f %% of the screen "
        "(of those pixels another colour %.1f %%, another normal %.1f %%, another roughness %.1f %%, drawn forward by HDRP %.1f %%), the tracer's hit nearer %.2f %%, "
        "farther %.2f %%, nothing traced where HDRP drew %.2f %%, something traced where HDRP has sky %.2f %%, sky for both %.1f %%; its own motion more than a pixel from HDRP's %.2f %%",
        frameIndex, own ? "the picture from its own camera rays" : "the picture from HDRP's G-buffer", share(y.where[1]), 100.0*y.colour/same, 100.0*y.normal/same, 100.0*y.roughness/same, 100.0*y.forward/same,
        share(y.where[2]), share(y.where[3]), share(y.where[4]), share(y.where[5]), share(y.where[0]), share(y.motion));
    std::string text=b;
    static const char* kinds[5]={"still", "characters", "swaying", "moved", "hair cards"};
    text+="; by instance (same / nearer / farther / over sky, % of the screen):";
    for(int k=0;k<5;++k) {
        const uint32_t n=y.kinds[k][1]+y.kinds[k][2]+y.kinds[k][3]+y.kinds[k][5];
        if(!n) continue;
        std::snprintf(b, sizeof(b), " %s %.1f / %.2f / %.2f / %.2f,", kinds[k], share(y.kinds[k][1]), share(y.kinds[k][2]), share(y.kinds[k][3]), share(y.kinds[k][5]));
        text+=b;
    }
    if(text.back()==',') text.pop_back();
    if(top) {
        text+="; materials with the most differing pixels (index: % of the screen, % of its own pixels):";
        for(size_t i=0;i<top && i<8;++i) {
            std::snprintf(b, sizeof(b), " %u: %.2f, %.0f;", y.materials[i].material, share(y.materials[i].differing), 100.0*y.materials[i].differing/std::max(y.materials[i].pixels, 1u));
            text+=b;
        }
        text.pop_back();
    }
    std::snprintf(b, sizeof(b), "; the scattering walk (PT-40): %.2f %% of the screen on materials light walks inside (%u of the scene's materials), %.2f %% took the walk in this frame",
        share(c[14]), st.scene.stats().subsurfaceMaterials, share(c[13]));
    text+=b;
    log(text);
    if(top) {
        text="Yardstick material programs (material: program, missing texture slots in hex; 0 means ready, ffffffff invalid record):";
        for(size_t i=0;i<top;++i) {
            uint32_t kind=0; const uint32_t missing=st.scene.program_missing(y.materials[i].material, kind);
            std::snprintf(b, sizeof(b), " %u: %u, %08x;", y.materials[i].material, kind, missing); text+=b;
        }
        text.pop_back(); log(text);
    }
}

// Records and submits the D3D12 part of a frame, with the input set of this parity. Returns
// the consumer fence value it signals.
// PT-99 (R19): a frame's reflections' stage time into the sort's two windows (State::ReflectionsSort): the choice's frames
// and a probe's (the other sort, while it is still the other one). Once a probe's frames are all in: the median of each, and
// the other sort kept from now on where it was faster by 5 %.
void sort_measured(uint32_t index, double ms, double sceneMs) {
    auto& s=st.sort;
    const uint8_t used=s.used[index]; s.used[index]=0;
    if(!used || !(ms>0.0) || !std::isfinite(ms)) return;
    const bool noHint=used==2u || used==4u, probe=used>=3u;
    auto keep=[](std::vector<double>& w, double v) { w.push_back(v); if(w.size()>kSortProbe) w.erase(w.begin()); };
    if(!probe && noHint==s.noHint) { keep(s.mine, ms); keep(s.mineScene, sceneMs); }
    else if(probe && noHint!=s.noHint) { keep(s.other, ms); keep(s.otherScene, sceneMs); }
    else return;
    if(!probe || s.other.size()<kSortProbe || s.mine.size()<kSortProbe/2) return;
    auto median=[](std::vector<double> w) { std::sort(w.begin(), w.end()); return w[w.size()/2]; };
    // The two windows must show the same scene: a load between them (an empty world, then one arriving) made a probe of
    // 0.2 ms beat a choice of 11 ms on 2026-10-11 (PT-129). The bounced light's stage, which the hint does not touch, tells:
    // more than a fifth apart, no verdict; both windows start over.
    const double sceneMine=median(s.mineScene), sceneOther=median(s.otherScene);
    if(!(sceneMine>0.0) || !(sceneOther>0.0) || std::abs(sceneOther/sceneMine-1.0)>0.2) {
        s.mine.clear(); s.other.clear(); s.mineScene.clear(); s.otherScene.clear();
        return;
    }
    s.lastMine=median(s.mine); s.lastOther=median(s.other);
    s.other.clear(); s.otherScene.clear();
    if(!(s.lastOther<0.95*s.lastMine)) { ++s.kept; return; }
    s.noHint=!s.noHint; s.mine.clear(); s.mineScene.clear(); ++s.switches; s.kept=0;
    if(s.switches<=20 || s.switches%20==0) {
        char text[200];
        std::snprintf(text, sizeof text, "Reflections' regrouping: now %s (its stage %.1f ms against %.1f %s, the medians of %u frames; switch %u)",
                      s.noHint ? "with no hint" : "by the material", s.lastOther, s.lastMine, s.noHint ? "by the material" : "with no hint", kSortProbe, s.switches);
        log(text);
    }
}

void cpu_clock_trace(uint32_t sequence, uint64_t begin, uint64_t end, uint64_t calGpu, uint64_t calCpu); // (PT-107, below)
namespace waits { std::string line(uint32_t frames); extern thread_local uint32_t lastMark; } // (PT-107, below)

uint64_t trace(const TGPT_Frame& f, uint32_t parity) {
    const uint32_t index=st.listIndex++%kFrames;
    const auto waitStart=std::chrono::steady_clock::now();
    st.consumer.wait_cpu(st.retired[index]);
    const auto recordStart=std::chrono::steady_clock::now();
    st.phase[2]=std::chrono::duration<double, std::milli>(recordStart-waitStart).count();
    const uint64_t retire=st.consumer.value+1;
    st.release.collect(st.consumer.d12->GetCompletedValue());
    st.scene.collect(st.consumer.d12->GetCompletedValue());
    if(st.yardSlot[index] && st.yardReadback.mapped) publish_yardstick(static_cast<const uint32_t*>(st.yardReadback.mapped)+size_t(index)*kYardCount, st.yardSlot[index], st.yardSlotOwn[index]);
    st.yardSlot[index]=0;
    // The timestamps this slot resolved when it last ran.
    if(st.queried[index] && st.ticksPerMs>0) {
        const auto* t=static_cast<const uint64_t*>(st.queryReadback.mapped)+index*kMarks;
        if(t[kStages]>t[0]) {
            const double ms=double(t[kStages]-t[0])/st.ticksPerMs; { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.status.gpuMs=float(ms); } st.summaryGpu+=ms;
            bool ordered=true; for(uint32_t s=0;s<kStages;++s) ordered=ordered && t[s+1]>=t[s];
            if(ordered) {
                for(uint32_t s=0;s<kStages;++s) { const double stage=double(t[s+1]-t[s])/st.ticksPerMs; st.summaryStage[s]+=stage; st.recentStage[s]+=stage; }
                ++st.summaryStageFrames; ++st.recentFrames;
                sort_measured(index, double(t[kReflectionsStage+1]-t[kReflectionsStage])/st.ticksPerMs,
                              double(t[kSceneCostStage+1]-t[kSceneCostStage])/st.ticksPerMs); // (PT-99, PT-128, PT-129)
            }
            cpu_clock_trace(st.listSequence[index], t[0], t[kStages], st.calGpu[index], st.calCpu[index]); // (PT-107, R19)
            publish_stage_times();
        }
        // PT-109: the structures' stamps of that frame: the time between two stamps is the later one's kind's.
        if(st.structReadback.mapped && st.structResolved[index]>1) {
            const auto* s=static_cast<const uint64_t*>(st.structReadback.mapped)+index*kStructMarks;
            const uint32_t stamps=st.structResolved[index];
            bool ordered=true; for(uint32_t i=1;i<stamps;++i) ordered=ordered && s[i]>=s[i-1];
            if(ordered) {
                for(uint32_t i=1;i<stamps;++i) {
                    const double ms=double(s[i]-s[i-1])/st.ticksPerMs;
                    st.summaryStruct[st.structKinds[index][i]]+=ms; st.recentStruct[st.structKinds[index][i]]+=ms;
                }
                ++st.summaryStructFrames; ++st.recentStructFrames;
            }
        }
        // And the sky the camera sees, against the open sky's.
        const auto* l=static_cast<const float*>(st.probeReadback.mapped)+index*12;
        const float traced=0.2126f*l[0]+0.7152f*l[1]+0.0722f*l[2], open=0.2126f*l[4]+0.7152f*l[5]+0.0722f*l[6];
        if(std::isfinite(traced) && std::isfinite(open) && open>0.f && l[3]>0.f) {
            auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
            e.status.cameraLight=traced/open; for(int c=0;c<3;++c) e.status.cameraRadiance[c]=l[c];
        }
        {
            // What the traced scene had under the screen's centre.
            uint32_t pick[4]; std::memcpy(pick, l+8, sizeof(pick));
            auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
            e.status.pickFound=(pick[3]&1u) ? 1u : 0u; e.status.pickInstance=pick[0]; e.status.pickMaterial=pick[1]; e.status.pickDistance=l[10];
            e.status.pickSurface=pick[3];
        }
        // And the traced light's histogram: its median and 90th percentile, log2 (bins of 24/64 EV from -16).
        const auto* bins=static_cast<const uint32_t*>(st.meterReadback.mapped)+index*State::kMeterBins;
        // The bounced light handed to NRD in that frame (pt_nrd_prep, bins 64 to 127: log2 from -24, half an EV a bin): its
        // median, in HDRP's units through the scale it was handed over with, for the next restart's scale.
        if(st.nrdSlotScale[index]>0.0) {
            const uint32_t* nb=bins+64; uint64_t n=0; for(int i=0;i<64;++i) n+=nb[i];
            if(n>64) {
                double below=0; int i=0;
                for(;i<64;++i) { if(below+nb[i]>=0.5*double(n)) break; below+=nb[i]; }
                const double log2Median=-24.0+(double(i)+(i<64 && nb[i] ? (0.5*double(n)-below)/double(nb[i]) : 0.5))*0.5;
                st.nrdMedian=std::exp2(log2Median)*st.nrdSlotScale[index];
            }
            st.nrdSlotScale[index]=0.0;
        }
        uint64_t total=0; for(int i=0;i<64;++i) total+=bins[i];
        if(total>0) {
            auto percentile=[&](double q) {
                const double target=q*double(total); double below=0;
                for(int i=0;i<64;++i) {
                    if(bins[i] && below+bins[i]>=target) return float(-16.0+(double(i)+(target-below)/double(bins[i]))*24.0/64.0);
                    below+=bins[i];
                }
                return 8.f;
            };
            auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
            e.status.tracedLog2Mid=percentile(0.5); e.status.tracedLog2High=percentile(0.98);
            e.status.tracedSamples=uint32_t(std::min<uint64_t>(total, 0xFFFFFFFFull));
        }
    }
    // The caustics' counters this slot read back when it last ran: a frame that wanted more triangles than there was room for
    // grows the room for the next (its triangles past the room lit nothing); and what the waves are where the tracer reads them
    // (per slot: the highest a wave moved the surface, the steepest slope, one grid point's surface), for the log.
    if(st.causticRead[index]) {
        st.causticRead[index]=false;
        const auto* n=static_cast<const uint32_t*>(st.causticReadback.mapped)+index*kCausticCounters;
        st.causticTriangleSum+=n[0]; ++st.causticFrames; st.causticMost=std::max(st.causticMost, n[0]);
        if(n[0]>st.causticCapacity) st.causticWanted=std::max(st.causticWanted, n[0]);
        st.causticRecent=std::max(n[0], st.causticRecent-st.causticRecent/16);
        for(uint32_t i=0;i<st.causticSums.size();++i) for(uint32_t l=0;l<kCausticLevels;++l) st.causticSums[i][l]+=n[48+8*i+l];
        {
            uint64_t tiles[kCausticTileKinds]={};
            for(uint32_t i=0;i<kCausticTileKinds;++i) for(uint32_t c=0;c<kCausticLevels*TGPT_MAX_WATER;++c) { st.causticTiles[i][c]+=n[kCausticTileCounts+32*i+c]; tiles[i]+=n[kCausticTileCounts+32*i+c]; }
            if(caustic_counters_logged()) {
                char line[256];
                std::snprintf(line, sizeof(line), "Caustics frame %u: %u triangles, tolerance %08X, tiles ran %llu, on water %llu, a light reached %llu, wrote %llu, checksum %08X %08X",
                              st.causticFrame[index], n[0], n[1], (unsigned long long)tiles[0], (unsigned long long)tiles[1], (unsigned long long)tiles[2], (unsigned long long)tiles[3],
                              n[kCausticChecksum], n[kCausticChecksum+1]);
                log(line);
            }
        }
        // TGPT_LOG_WATER_SAMPLES (the offline check of the waves against HDRP's formula): every frame's sample in the log.
        const bool everySample=GetEnvironmentVariableA("TGPT_LOG_WATER_SAMPLES", nullptr, 0)>0;
        for(uint32_t i=0;i<TGPT_MAX_WATER;++i) {
            auto& w=st.causticWaves[i];
            w.high=std::max(w.high, float(n[8+i])*1e-4f); w.steep=std::max(w.steep, float(n[12+i])*1e-4f);
            if(!n[4+i]) continue;
            std::memcpy(w.sample, n+16+i*8, sizeof(w.sample)); w.sampled=true; ++w.frames;
            for(int a=0;a<3;++a) w.origin[a]=st.causticOrigin[index][size_t(a)];
            if(everySample) {
                char line[320];
                std::snprintf(line, sizeof(line), "Water slot %u sample: at x %.6f z %.6f the surface is at %.6f %.6f %.6f, its normal %.6f %.6f %.6f", i,
                              w.origin[0]+double(w.sample[0]), w.origin[2]+double(w.sample[1]), w.origin[0]+double(w.sample[2]), w.origin[1]+double(w.sample[3]),
                              w.origin[2]+double(w.sample[4]), double(w.sample[5]), double(w.sample[6]), double(w.sample[7]));
                log(line);
            }
        }
    }
#ifdef TGPT_SPEED_MEASURE
    // A measuring build's counters this slot read back when it last ran (PT-131, docs/agents/campspeed.md; pt_common
    // speed_count_flush): the buffer holds running sums, so the frame's own counts are these less the frame before's
    // (the slots are read in the frames' order). Summed, and said once a second as means a frame.
    if(st.speedRead[index] && st.speedReadback.mapped) {
        st.speedRead[index]=false;
        const auto* n=static_cast<const uint32_t*>(st.speedReadback.mapped)+size_t(index)*kSpeedCounters;
        if(st.speedHaveLast) { for(uint32_t i=0;i<kSpeedCounters;++i) st.speedSums[i]+=uint32_t(n[i]-st.speedLast[i]); ++st.speedFrames; }
        std::memcpy(st.speedLast.data(), n, size_t(kSpeedCounters)*4); st.speedHaveLast=true;
        const uint64_t now=GetTickCount64();
        if(st.speedFrames && now-st.speedLogged>=1000) {
            bool any=false;
            for(const uint64_t v:st.speedSums) any=any || v!=0;
            if(any || !st.speedSaidNone) {
                const double framesSummed=double(st.speedFrames);
                std::string text="Special branches 1s: "+std::to_string(st.speedFrames)+" frames; a frame";
                static const char* const kPassNames[2]={"bounced light", "reflections"};
                for(uint32_t p=0;p<2;++p) {
                    const uint64_t* c=st.speedSums.data()+size_t(p)*kSpeedBlock;
                    auto m=[&](uint32_t i) { return double(c[i])/framesSummed; };
                    const double paths=double(std::max<uint64_t>(c[1], 1)), under=double(std::max<uint64_t>(c[1]-std::min(c[1], c[32]), 1));
                    char line[2400];
                    std::snprintf(line, sizeof line,
                        " | %s: pixels %.0f; water paths %.0f (from below %.0f, start past 60 m %.0f, from a point %.0f; their starts %.0f m from the camera on average, "
                        "%.0f over its height by %.1f m on average, the rest under it by %.1f m), turns %.0f (in the water %.0f; "
                        "paths of 8+ turns %.0f, 32+ %.0f, 128+ %.0f), scatter points %.0f, surface met %.0f, solid shaded %.0f, landed light asked %.0f; "
                        "sun asked %.0f (straight, the row off: %.0f), surface found %.0f (mean way %.1f m; the path's ray there %.2f m wide on average, under 10 cm at %.0f), "
                        "from tiles %.0f (tiles asked %.0f, corners %.0f), "
                        "patches made %.0f (turns %.0f, corners %.0f, of 256+ corners %.0f; why: no kept corners %.0f, not their level plane %.0f, their cell too coarse %.0f, "
                        "tilt unbounded %.0f, block unsolved %.0f, off the grid %.0f, too many tiles %.0f; made coarser by the ray's width %.0f); the sun's light reached the point %.0f, was stopped on its way %.0f, "
                        "none landed %.0f; "
                        "glass walks %.0f, interfaces %.0f, medium triangles %.0f, ended on water %.0f",
                        kPassNames[p], m(0), m(1), m(2), m(3), m(31), double(c[35])/paths, m(32), c[32] ? double(c[33])/double(c[32]) : 0.0, double(c[34])/under,
                        m(4), m(5), m(9), m(10), m(11), m(6), m(7), m(8), m(25),
                        m(12), m(30), m(13), c[13] ? double(c[24])/double(c[13]) : 0.0, c[13] ? 0.1*double(c[44])/double(c[13]) : 0.0, m(46), m(14), m(15), m(16),
                        m(17), m(18), m(19), m(20), m(21), m(22), m(36), m(37), m(38), m(23), m(39), m(45), m(40), m(41), m(42), m(26), m(27), m(28), m(29));
                    text+=line;
                }
                log(any ? text : text+" (nothing special met: not said again until something is)");
            }
            st.speedSaidNone=!any; st.speedSums.fill(0); st.speedFrames=0; st.speedLogged=now;
        }
    }
#endif
    if(st.causticWanted>st.causticCapacity) make_caustic_triangles(st.causticWanted);
    // AMD Ray Regeneration denoises instead of the built-in denoiser where it runs. Its context follows the
    // render size; the GPU finishes the old one's frames before it is replaced.
    bool rrg=(f.options&TGPT_OPTION_RAY_REGENERATION) && !(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION)) &&
        !st.rrgFailed && rrg_available() && f.debugView<TGPT_VIEW_COUNT;
    if(rrg && st.rrg.needs_context(st.width, st.height)) {
        if(st.rrg.has_context()) st.consumer.wait_cpu(st.consumer.value);
        std::string error;
        if(!st.rrg.ensure(st.d12.Get(), st.width, st.height, error)) {
            rrg=false; // pending context uses the existing fallback for this frame
            if(!error.empty()) {
                st.rrgFailed=true;
                log("AMD Ray Regeneration could not start ("+error+"); the fallback denoiser serves for the rest of the session");
            }
        }
    }
    if(rrg && !st.rrgDepth) { // (with them made, ensure_rrg_textures has nothing to do)
        StartSteps steps(st.d12.Get(), &tally.stepsPasses);
        try { ensure_rrg_textures(); }
        catch(const DxError& e) {
            st.rrgFailed=true; rrg=false;
            log(std::string("AMD Ray Regeneration's guides could not be created (")+e.what()+"); the built-in denoiser serves for the rest of the session");
        }
        if(st.rrgDepth) steps.step("Ray Regeneration's five guides (the tracer's own textures)", allocation_bytes(st.rrgDepth.resource.Get())+
            allocation_bytes(st.rrgMotion.resource.Get())+allocation_bytes(st.rrgNormals.resource.Get())+allocation_bytes(st.rrgDiffuse.resource.Get())+
            allocation_bytes(st.rrgSpecular.resource.Get()));
    }
    // PT-87: the built-in filters' textures this frame will run with, by the rule the frame applies below (rrg, useNrd,
    // denoise, denoiseOwn), made here, before the list is opened: without room for them the frame fails cleanly and the next
    // one asks again (a failure while a list is being recorded turns path tracing off for the session).
    bool freshFilters=false;
    {
        const bool chosen=!(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION));
        const bool rrgRuns=rrg;
        const bool nrdRuns=chosen && (f.options&TGPT_OPTION_NRD) && !rrgRuns && !st.nrdFailed && nrd_available() && f.debugView==TGPT_VIEW_NONE;
        freshFilters=ensure_filter_textures(chosen && !rrgRuns && !nrdRuns, chosen && !rrgRuns);
    }
    // PT-107 (R19): this slot's frame, and the queue's clock against the CPU's, for State::CpuClock when its stamps are read.
    st.listSequence[index]=st.traceSequence;
    if(FAILED(st.queue->GetClockCalibration(&st.calGpu[index], &st.calCpu[index]))) st.calCpu[index]=0;
    auto* list=st.lists[index].Get();
    check(st.allocators[index]->Reset(), "Reset path tracing allocator");
    check(list->Reset(st.allocators[index].Get(), nullptr), "Reset path tracing list");
    // From here a failure leaves the scene's bookkeeping ahead of a list that never runs (see trace_event).
    st.recording=true;
    st.rings[index].reset();
    ID3D12DescriptorHeap* heaps[]={st.heap.get(),st.decalSamplers.Get()};
    list->SetDescriptorHeaps(2, heaps);
    list->SetComputeRootSignature(st.root.Get());
    // The frame's start, and the end of each stage (kStageNames), whether or not it had work this frame; and how far the
    // GPU got, once the work before each mark is done (report_gpu_stop).
    // PT-87 (memory3): in the first two traced frames of a start, Windows' count as each stage's commands are recorded and
    // as the list is handed to the GPU (start_summary's "recording and running the passes", stage by stage: the driver
    // takes memory of its own when a pass first runs, in no figure of the memory line).
    const bool countStages=tally.on && tally.frames<2;
    std::array<double, kMarks> stageCount{};
    auto mark=[&](uint32_t stage) {
        list->EndQuery(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index*kMarks+stage);
        if(st.crumbsOn) { st.crumbsRecorded=(uint32_t(st.frame)<<8)|stage; crumb(list, 0, st.crumbsRecorded); }
        if(countStages && stage<kMarks) stageCount[stage]=windows_counts_mb(st.d12.Get());
    };
    mark(0);
    // PT-109: the structures' stamps (GpuScene::structureStamp): the first is the frame's start, each later one ends its kind's
    // commands. Past kStructMarks a frame's later stamps are dropped (their time is then counted with no kind: the sum falls
    // short of the scene stage's and says so).
    st.structCount[index]=0; st.structResolved[index]=0; st.structFrame[index]=uint32_t(st.frame);
    st.scene.structureStamp=[list, index](uint32_t kind) {
        uint32_t& n=st.structCount[index];
        if(!st.structQueries || n>=kStructMarks || kind>=GpuScene::kStructureKinds) return;
        list->EndQuery(st.structQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index*kStructMarks+n);
        st.structKinds[index][n]=uint8_t(kind); ++n;
        if(st.crumbsOn) { crumb(list, 2, (uint32_t(st.frame)<<8)|(kind+1u)); crumb(list, 3, (uint32_t(st.frame)<<8)|(n-1u)); }
    };
    // PT-125: the plants' steps, numbered (GpuScene::structureNote): slot 5 the frame, slot 4 the last step done.
    st.scene.noteFrame=uint32_t(st.frame);
    st.scene.structureNote=[list](uint32_t step) { if(st.crumbsOn) { crumb(list, 5, uint32_t(st.frame)); crumb(list, 4, step); } };
    st.scene.structure_stamp(GpuScene::kStructureOther);

    // The scene origin follows the camera in 256 m steps, keeping positions small next to it.
    const double cam[3]={f.cameraPosition[0], f.cameraPosition[1], f.cameraPosition[2]};
    std::memcpy(st.memoryCamera, cam, sizeof(cam)); st.haveMemoryCamera=true; // (publish_memory's count by distance, PT-87)
    std::memcpy(st.prevOrigin, st.origin, sizeof(st.origin));
    if(!st.haveOrigin || std::fabs(cam[0]-st.origin[0])>512 || std::fabs(cam[1]-st.origin[1])>512 || std::fabs(cam[2]-st.origin[2])>512) {
        for(int i=0;i<3;++i) st.origin[i]=std::round(cam[i]/256.0)*256.0;
        if(!st.haveOrigin) std::memcpy(st.prevOrigin, st.origin, sizeof(st.origin));
        st.haveOrigin=true;
    }
    st.scene.ommAllowed=!(f.options&TGPT_OPTION_NO_MICROMAPS);
    st.scene.sharedTextures=st.sharedBase!=kInvalid && !(f.options&TGPT_OPTION_ATLAS_TEXTURES); // pt_common.hlsli shared_on
    st.scene.emissiveLights=(f.options&TGPT_OPTION_EMISSIVE_LIGHTS)!=0;
    // The frame constants' slot is taken and bound now and filled once the scene is recorded, so every root argument
    // is set before the first dispatch (the character unpack reads none of them).
    void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu=0;
    if(!st.rings[index].allocate(sizeof(FrameConstants), 256, &cpu, &gpu)) {
        // This frame's ring is full: the constants get a buffer of their own.
        Buffer upload=make_buffer(st.d12.Get(), sizeof(FrameConstants), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create frame constants");
        cpu=upload.mapped; gpu=upload.va();
        st.release.add(retire, upload.resource.Get());
    }
    list->SetComputeRootConstantBufferView(0, gpu);
    list->SetComputeRootShaderResourceView(2, st.scene.tlas ? st.scene.tlas : gpu);
    // Characters: this frame's skinned positions from the shared texture into each one's position buffer, for the
    // scene to rebuild their BLAS from.
    std::vector<TGPT_SkinnedInstance> characters;
    {
        auto& work=st.skinWork[parity];
        Shared& skin=st.skin[parity];
        if(!work.empty() && skin.t.d12 && st.skinUnpack) {
            Barriers sb;
            sb.transition_from(skin.t.d12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            std::vector<std::pair<Buffer*, uint32_t>> targets(work.size(), {nullptr, kInvalid});
            // And each one's positions of the frame before, which the unpack moves over first (a hit's own motion: pt_primary).
            struct Before { Buffer* buffer=nullptr; uint32_t uav=kInvalid; bool fresh=true; };
            std::vector<Before> before(work.size());
            // ON TRIAL (PT-92): and each one's smooth normals, where this frame's texture brings them (its alpha: copy_skinned).
            struct Smooth { Buffer* buffer=nullptr; uint32_t uav=kInvalid; };
            std::vector<Smooth> smooth(work.size());
            for(size_t i=0;i<work.size();++i) {
                targets[i].first=st.scene.skinned_positions(work[i].instance.mesh, work[i].count, targets[i].second, st.frame+1, before[i].uav, before[i].fresh, before[i].buffer,
                                                            st.skinNormals[parity], smooth[i].uav, smooth[i].buffer);
                if(targets[i].first) sb.transition(*targets[i].first, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                if(targets[i].first && before[i].buffer) sb.transition(*before[i].buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                if(targets[i].first && smooth[i].buffer) sb.transition(*smooth[i].buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            sb.flush(list);
            list->SetPipelineState(st.skinUnpack.Get());
            for(size_t i=0;i<work.size();++i) {
                if(!targets[i].first) continue;
                const uint32_t values[8]={skin.srv, targets[i].second, work[i].firstTexel, work[i].count, before[i].uav, before[i].fresh ? 1u : 0u, smooth[i].uav, kInvalid};
                list->SetComputeRoot32BitConstants(1, 8, values, 0);
                list->Dispatch((work[i].count+63)/64, 1, 1);
                characters.push_back(work[i].instance);
            }
            sb.uav(nullptr);
            for(auto& t:targets) if(t.first) sb.transition(*t.first, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            for(size_t i=0;i<work.size();++i) if(targets[i].first && before[i].buffer) sb.transition(*before[i].buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            for(size_t i=0;i<work.size();++i) if(targets[i].first && smooth[i].buffer) sb.transition(*smooth[i].buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            sb.transition_from(skin.t.d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
            sb.flush(list);
        }
    }
    st.scene.structure_stamp(GpuScene::kStructureSkinning);
    // The water's surface (scene_water.cpp): each slot's water this frame, and the size its waves want of a triangle near the
    // camera, half a texel of its finest band (HDRP samples a band at its position times the band's scale: a texel is 1 over
    // the scale and the buffer's size; an edge is fully on the waves from twice the wanted size on, so the waves are followed
    // texel by texel there; still water, without wave data, wants none).
    {
        WaterSlot slots[TGPT_MAX_WATER];
        for(uint32_t i=0;i<TGPT_MAX_WATER;++i) {
            const auto& c=st.caustics[parity][i];
            slots[i].live=c.live;
            for(int a=0;a<3;++a) slots[i].up[a]=c.desc.worldToWater[4+a];
            float finest=0.f;
            const uint32_t bands=c.live && c.waves ? std::min(c.desc.bandCount, c.displacement.slices) : 0u;
            for(uint32_t b=0;b<bands;++b) if(c.desc.bands[b][3]!=0.f) finest=std::max(finest, std::fabs(c.desc.bands[b][0]));
            slots[i].cell=finest>0.f && c.displacement.size ? 0.5f/(finest*float(c.displacement.size)) : 0.f;
        }
        st.scene.set_water(slots); st.scene.waterPass=st.waterMesh.Get();
    }
    // ON TRIAL (PT-109, docs/agents/megageo-1.md; TGPT_Frame.flags' bits 2 to 5, the plugin's hidden "Mega Geometry
    // trial", 0 from every other caller): NVIDIA RTX Mega Geometry. 1, 2, 3: the scene makes the frame's top level a
    // partitioned one (mega_geometry.hpp; cells of 64, 128, 256 m), in a build with CMake's TGPT_MEGA_GEOMETRY and where
    // the driver reports it. 4, 5, 6 (docs/agents/megageo-4.md): each cell's small plants in a group of their own; 5 builds
    // those groups from clusters stamped from templates where the driver reports clusters, with their micromaps as the
    // clusters' own micromap arrays where it reports those (docs/agents/megageo-5.md); 6 leaves their micromaps out. 7 (megageo-5): NVIDIA's
    // layout, every plant its own bottom level of its clusters (with micromaps) and its own instance, in the partitioned top
    // level (cells of 128 m); 8: 7 without micromaps. The same hits either way: no pass knows of it.
    {
        const uint32_t trial=(f.flags>>TGPT_FRAME_MEGA_SHIFT)&TGPT_FRAME_MEGA_MASK;
        static uint32_t said=~0u;
        if(said!=trial) {
            said=trial;
#if TGPT_MEGA_GEOMETRY
            const auto& caps=st.scene.megaCaps;
            const bool clusters=caps.clusters && mega_cluster_pipelines(), maps=clusters && caps.omm && mega_cluster_micromaps();
            const std::string mapsText=maps ? "with their micromaps as the clusters' own micromap arrays" : "without micromaps (the driver reports no micromaps for clusters)";
            log("Mega Geometry trial: "+std::to_string(trial)+(trial==0 ? " (off: the plain acceleration structures)" :
                trial==4 ? " (each cell's small plants in a group of their own, plain, with their micromaps: the yardstick for 5)" :
                trial==6 ? " (each cell's small plants in a group of their own, plain, without micromaps)" :
                trial==5 ? (clusters ? " (each cell's small plants in a group of their own, built from clusters stamped from templates, "+mapsText+"; the driver reports clusters)"
                                     : std::string(" (the small plants as clusters were asked for, but the driver does not report them: their groups stay plain)")) :
                trial==7 ? (clusters ? " (NVIDIA's layout: every plant its own bottom level built from its clusters, "+mapsText+", its own instance, in the partitioned top level of 128 m cells"+
                                       (caps.partitionedTop ? std::string(")") : std::string("; the driver does not report that: the plain top level)"))
                                     : std::string(" (every plant as clusters was asked for, but the driver does not report them: the groups stay plain)")) :
                trial==8 ? (clusters ? std::string(" (NVIDIA's layout as 7, without micromaps: 7's yardstick for them)") : std::string(" (as 7, but the driver does not report clusters: the groups stay plain)")) :
                trial>8 ? " (not a trial this build knows: the plain acceleration structures serve)" :
                caps.partitionedTop ? std::string(" (the top level partitioned, cells of ")+(trial==1 ? "64" : trial==2 ? "128" : "256")+" m; the driver reports it)" :
                " (the top level partitioned was asked for, but the driver does not report it: the plain top level serves)"));
#else
            if(trial!=0) log("Mega Geometry trial: this build has it compiled out (configure with -DTGPT_MEGA_GEOMETRY=ON to try it): nothing changes");
#endif
        }
#if TGPT_MEGA_GEOMETRY
        st.scene.megaTrial=trial;
#endif
    }
    // FOR MEASURING (PT-110, docs/agents/omm-1.md; TGPT_Frame.flags' bits 6 to 13, the plugin's hidden "Micromaps trial",
    // 0 from every other caller; tgpt_api.h TGPT_OMM_TRIAL_*): what each kind of cut-out costs. A number each, added up:
    // 1, 2 and 4 make the plants', the characters' and every other cut-out solid to every ray but the camera's own (the
    // scene gives their instances the flag); 8 leaves every micromap unused; 16 hands the plain submeshes of characters
    // and plants to the shader's test again (M3 undone). The picture (1, 2, 4) or its speed (8, 16) changes ON PURPOSE
    // for the step that sets them; the log says so when it changes, and in every "PT cut-outs" line.
    // And ON TRIAL (M1), off until the game has answered: 32, the plants in the wind's pose trace with their meshes'
    // micromaps (the scene links them into each group's structure and passes the links again at every refit); 64 with
    // it, for finding a cause: such a group is built anew at every pose in place of refitted. The same picture.
    {
        const uint32_t trial=(f.flags>>TGPT_FRAME_OMM_SHIFT)&TGPT_FRAME_OMM_MASK;
        static uint32_t said=0;
        if(said!=trial) {
            said=trial;
            log("Micromaps trial: "+std::to_string(trial)+(trial==0 ? " (off: every cut-out as it ships)" :
                // (64: for finding a cause, the plants' groups with micromaps built anew at every pose. The same picture.)
                trial==TGPT_OMM_TRIAL_PLANT_REBUILD ? std::string(" (for finding a cause, the plants' groups with their micromaps are built anew at every pose in place of refitted (slow))") :
                std::string(" (A MEASURING STEP, the picture or its speed changed on purpose:")+
                ((trial&TGPT_OMM_TRIAL_NO_PLANT_MAPS) ? " the plants in the wind's pose trace without their meshes' micromaps, as before round 20;" : "")+
                ((trial&TGPT_OMM_TRIAL_PLANT_REBUILD) ? " (64: their groups built anew at every pose, for finding a cause);" : "")+
                ((trial&TGPT_OMM_TRIAL_PLANTS_SOLID) ? " the cut-outs of the plants in the wind's pose are solid to every ray but the camera's;" : "")+
                ((trial&TGPT_OMM_TRIAL_CHARACTERS_SOLID) ? " the characters' cut-outs are solid to every ray but the camera's;" : "")+
                ((trial&TGPT_OMM_TRIAL_STILL_SOLID) ? " every other cut-out is solid to every ray but the camera's (a micromap's bare pieces stay bare);" : "")+
                ((trial&TGPT_OMM_TRIAL_NO_MAPS) ? " no instance traces with its micromaps (the shader's alpha test decides alone);" : "")+
                ((trial&TGPT_OMM_TRIAL_PLAIN_TESTED) ? " the submeshes of characters and of plants in the wind's pose that are no cut-out go to the shader's test with their cut-outs, as before 2026-10-10;" : "")+
                ((trial&~127u) ? " the numbers "+std::to_string(trial&~127u)+" do nothing in this build;" : std::string())+")"));
        }
        // FOR MEASURING (round 20, PT-110's M7): the micromaps of leaves and grass at level 6 are the rule; the hidden "Speed
        // trial" bakes them at level 4 as before (128) or at 5 (256) (the Micromaps trial's eight bits are all taken), handed to
        // the scene in its own word as TGPT_OMM_TRIAL_LEAVES_4 and _5. A mesh baked at another level is baked again
        // (GpuScene::omm_stale). The same picture.
        uint32_t leaves=0;
        {
            const float asked=std::isfinite(f.prevCameraPosition[3]) && f.prevCameraPosition[3]>0.f ? std::floor(std::min(f.prevCameraPosition[3], 524287.f)/8.f) : 0.f;
            const uint32_t speed=uint32_t(asked+0.5f);
            leaves=(speed&128u) ? TGPT_OMM_TRIAL_LEAVES_4 : (speed&256u) ? TGPT_OMM_TRIAL_LEAVES_5 : 0u;
            static uint32_t saidLeaves=~0u;
            if(saidLeaves!=leaves) {
                saidLeaves=leaves;
                log(std::string("Micromaps of leaves and grass: level ")+(leaves==TGPT_OMM_TRIAL_LEAVES_4 ? "4, FOR MEASURING (the Speed trial's 128)" :
                    leaves==TGPT_OMM_TRIAL_LEAVES_5 ? "5, FOR MEASURING (the Speed trial's 256)" : "6, as the rule")+" (the rest's 4); every leaf mesh's maps are baked again at it");
            }
        }
        st.scene.ommTrial=trial|leaves;
        if(trial&TGPT_OMM_TRIAL_NO_MAPS) st.scene.ommAllowed=false;
    }
    st.scene.record(st.d12.Get(), list, st.heap, st.rings[index], st.release, retire, st.origin, cam, characters);

    auto& in=st.inputs[parity];
    auto& out=st.outputs;
    // Frames that went untraced (path tracing turned off a while, another camera, a failed frame) leave history from
    // another view: it starts over.
    const bool gap=st.haveFrameIndex && f.frameIndex!=st.lastFrameIndex+1;
    const bool reset=st.resetNext || (f.flags&TGPT_FRAME_RESET) || gap;
    st.resetNext=false; st.haveFrameIndex=true; st.lastFrameIndex=f.frameIndex;
    const uint32_t cur=st.parity, prev=1-st.parity;
    // NVIDIA NRD denoises all the light a surface gives in place of the built-in denoiser where it is the choice and started
    // on the device, for the image itself (debug views show the built-in denoiser's). Glass and water keep the reflections'
    // own filter (pt_nrd_prep says why). Its texture pools follow the render size; the GPU finishes the old ones' frames
    // before they are replaced.
    bool useNrd=(f.options&TGPT_OPTION_NRD) && !(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION)) && !rrg &&
        !st.nrdFailed && nrd_available() && f.debugView==TGPT_VIEW_NONE;
    // NRD's denoiser this frame: RELAX (1), or ON TRIAL (PT-127, TGPT_FRAME_NRD_* 2) REBLUR, both as NRD's README sets them up
    // (pt_nrd_prep). Another than the last frame's makes NRD's instance again (nrd_use), once the GPU is done with the old
    // one; its history starts over.
    const uint32_t nrdKind=((f.flags>>TGPT_FRAME_NRD_SHIFT)&TGPT_FRAME_NRD_MASK)==2u ? 2u : 1u;
    ID3D12PipelineState* nrdPrep=nullptr;
    if(useNrd) {
        try {
            std::string error;
            if(nrd_kind()!=nrdKind) {
                st.consumer.wait_cpu(st.consumer.value);
                st.nrd.destroy(); st.nrdHavePrevious=false;
                if(!nrd_use(nrdKind)) throw DxError(E_FAIL, "NRD could not be made for the denoiser of its trial");
            }
            if(st.nrd.needs_pools(st.width, st.height)) {
                st.consumer.wait_cpu(st.consumer.value);
                StartSteps steps(st.d12.Get(), &tally.stepsPasses); // PT-87 (memory3)
                if(!st.nrd.ensure(st.d12.Get(), st.width, st.height, error)) throw DxError(E_FAIL, error);
                steps.step("NRD's own memory (its pools of textures at the render size)", st.nrd.memory_bytes());
            }
            if(!st.nrdViewZ) { // (with them made, ensure_nrd_textures has nothing to do)
                StartSteps steps(st.d12.Get(), &tally.stepsPasses);
                ensure_nrd_textures();
                steps.step("NRD's seven inputs and results (the tracer's own textures)", allocation_bytes(st.nrdViewZ.resource.Get())+
                    allocation_bytes(st.nrdMotion.resource.Get())+allocation_bytes(st.nrdNormalRoughness.resource.Get())+allocation_bytes(st.nrdDiffuseIn.resource.Get())+
                    allocation_bytes(st.nrdDiffuseOut.resource.Get())+allocation_bytes(st.nrdSpecularIn.resource.Get())+allocation_bytes(st.nrdSpecularOut.resource.Get()));
            }
            nrdPrep=nrd_prep_for(st.root.Get());
            if(!nrdPrep) throw DxError(E_FAIL, "no prep pass");
        } catch(const DxError& e) {
            st.nrdFailed=true; useNrd=false;
            log(std::string("NVIDIA NRD could not start (")+e.what()+"); the built-in denoiser serves for the rest of the session");
        }
    }
    const bool denoise=!(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION)) && !rrg && !useNrd;
    // The reflections' own filter, which serves glass and water with NRD too (NRD leaves them alone), and the direct light's,
    // which serves the built-in denoiser alone (NRD takes the direct light since 2026-10-11: PT-127).
    const bool denoiseOwn=denoise || useNrd;
    const bool denoiseOwnDirect=denoise;
    // PT-87: those filters' textures were made before the list was opened (ensure_filter_textures, above), unless the
    // denoiser that was to run could not start in this very frame and the built-in one serves in its place: then now. And
    // what the denoisers of other frames left is let go.
    if((denoise && !st.tmpB) || (denoiseOwn && !st.directOut)) freshFilters=ensure_filter_textures(denoise, denoiseOwn) || freshFilters;
    release_idle_filters(denoise, denoiseOwn);
    release_idle_denoisers(useNrd, rrg || (st.rrg.is_starting() && (f.options&TGPT_OPTION_RAY_REGENERATION)));
    const uint32_t atrousIterations=denoise ? 5 : 0; // steps 1 to 16: about 60 pixels each way
    // PT-127, primary surface replacement (pt_psr_record.hlsli; NRD README's section on it): the mirrors' records,
    // wherever NRD denoises and the tracer fires its own camera rays. No setting: the rule since the game's answer of
    // 2026-10-11 (run 20261011-213531). Let go with any other denoiser, a debug view, or the G-buffer's surfaces.
    const bool psrRecords=useNrd && !(f.options&TGPT_OPTION_GBUFFER_SURFACES);
    if(psrRecords) {
        try {
            const bool had=bool(st.psr.records.resource);
            st.psr.ensure(st.d12.Get(), st.heap, st.width, st.height);
            if(!had) log("NRD is told of what a still pure mirror shows (primary surface replacement): its records hold "+std::to_string(st.psr.memory_bytes()>>20)+" MB");
        } catch(const std::exception& e) {
            st.psr.release(st.heap, st.release, st.consumer.value);
            log_throttled(std::string("The mirrors' records for NRD could not be made (a mirror then keeps its own surface): ")+e.what());
        }
    } else if(st.psr.records.resource) st.psr.release(st.heap, st.release, st.consumer.value);
    FrameConstants k{};
    // (A record counts only in the frame that stamped it with this serial, so none is ever cleared; 0 is no frame's.
    // TGPT_TEST_PSR_VIEW=<1 to 9>, for tgpt_gpu_test alone: the picture shows what NRD was told of replaced pixels (1 to 5,
    // pt_compose) or one part of their light (6 to 9, pt_nrd_prep's back end).)
    if(++st.psrSerial==0) st.psrSerial=1;
    k.psr[0]=st.psr.uav; k.psr[3]=st.psrSerial;
    { char view[8]{}; if(GetEnvironmentVariableA("TGPT_TEST_PSR_VIEW", view, sizeof(view))>0 && view[0]>='1' && view[0]<='9') k.psr[2]=uint32_t(view[0]-'0'); }
    std::memcpy(k.invViewProj, f.invViewProj, sizeof(k.invViewProj)); std::memcpy(k.viewProj, f.viewProj, sizeof(k.viewProj));
    std::memcpy(k.prevViewProj, f.prevViewProj, sizeof(k.prevViewProj));
    // This frame's view-projection without jitter (column vectors, as the others): HDRP's motion vectors' current one.
    for(int col=0;col<4;++col) for(int row=0;row<4;++row) {
        float v=0.f;
        for(int i=0;i<4;++i) v+=f.nonJitteredProj[i*4+row]*f.view[col*4+i];
        k.motionViewProj[col*4+row]=v;
    }
    k.primary[0]=st.ownMotion.uav; k.primary[1]=st.ownEmission.uav; k.primary[2]=st.yardstick.uav; k.primary[3]=st.scene.movingPreviousSrv;
    for(int i=0;i<3;++i) {
        k.cameraPos[i]=float(cam[i]-st.origin[i]);
        k.prevCameraPos[i]=float(double(f.prevCameraPosition[i])-st.origin[i]);
        k.prevOriginDelta[i]=float(st.prevOrigin[i]-st.origin[i]);
        k.sunDir[i]=f.sunDirection[i];
        k.sunColor[i]=f.sunColor[i];
    }
    k.sunDir[3]=std::cos(std::clamp(f.sunDirection[3], 0.0001f, 0.2f)); k.sunColor[3]=f.sunColor[3];
    // PT-42/81/94: cameraPosition.w was unused in the API. prevCameraPos.w held retired HDRP ambient; no shader read it.
    // (The water's three bits are that word's bits 4 to 6; its low 4 are glass's, below.)
    k.prevCameraPos[3]=std::isfinite(f.cameraPosition[3]) ? float((uint32_t(std::clamp(f.cameraPosition[3], 0.f, 127.f))>>4)&7u) : 0.f;
    // Its history froze while Ray Reconstruction, Ray Regeneration or no denoiser served: it starts over.
    // With NRD only the direct light's and the reflections' filters run, whose histories carry on from the built-in
    // denoiser's (its bounced light's history starts over when it takes over again).
    // (And when their textures were made this frame, PT-87: they hold nothing. They are only ever made in a frame that
    // resumes them anyway; said here too so that it does not rest on that.)
    const bool resumeDenoiser=(denoise && !st.denoisedLast) || (denoiseOwn && !st.ownDenoisedLast) || freshFilters;
    st.denoisedLast=denoise; st.ownDenoisedLast=denoiseOwn;
    k.size[0]=st.width; k.size[1]=st.height; k.size[2]=f.frameIndex; k.size[3]=(reset ? 1u : 0u)|(resumeDenoiser ? 2u : 0u);
    // The game's fog is the tracer's this frame (pt_common FLAG_TRACED_FOG, 8: mirrors and refractions take the traced medium,
    // pt_medium.hlsli), with the medium's albedo and anisotropy for every pass (FrameConstants.fogMedium).
    if((f.flags&TGPT_FRAME_TRACED_FOG) && st.fogFroxel && st.fogIntegrate && st.fogTail && f.debugView==0) k.size[3]|=8u;
    for(int i=0;i<4;++i) k.fogMedium[i]=std::isfinite(f.fogMedium[i]) ? std::clamp(f.fogMedium[i], i==3 ? -0.99f : 0.f, i==3 ? 0.99f : 1.f) : 0.f;
    // The game's fog boxes (PT-112), while the fog is traced: relative to the scene origin, each mask's handle to its number
    // in the heap this frame (a mask not open: the box is left out this frame, and the log says how many), into this slot's
    // upload ring, read by the shaders as raw records (pt_medium.hlsli fog_box).
    k.fogBoxes[0]=kInvalid; k.fogBoxes[1]=k.fogBoxes[2]=k.fogBoxes[3]=0;
    if((k.size[3]&8u) && !st.fogBoxes.empty()) {
        std::vector<TGPT_FogBox> boxes; boxes.reserve(st.fogBoxes.size());
        uint32_t unread=0;
        for(TGPT_FogBox b:st.fogBoxes) {
            if(b.mask>=0) {
                int32_t number=b.mask; share::resolve(&number, 1);
                if(number<0 || st.sharedBase==kInvalid) { ++unread; continue; }
                b.mask=number;
            }
            for(int a=0;a<3;++a) b.center[a]=float(double(b.center[a])-st.origin[a]);
            boxes.push_back(b);
        }
        void* at=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpuAt=0; uint64_t offset=0;
        const uint64_t bytes=uint64_t(boxes.size())*sizeof(TGPT_FogBox);
        ID3D12Resource* holder=nullptr;
        if(!boxes.empty()) {
            if(st.rings[index].allocate(bytes, 256, &at, &gpuAt, &offset)) holder=st.rings[index].resource();
            else {
                Buffer own=make_buffer(st.d12.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create the fog boxes' records");
                at=own.mapped; offset=0; holder=own.resource.Get();
                st.release.add(retire, own.resource.Get());
            }
            std::memcpy(at, boxes.data(), size_t(bytes));
            if(st.fogBoxSrv[index]==kInvalid) st.fogBoxSrv[index]=st.heap.allocate();
            D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=DXGI_FORMAT_R32_TYPELESS; v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Buffer.FirstElement=offset/4; v.Buffer.NumElements=UINT(bytes/4);
            v.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
            st.d12->CreateShaderResourceView(holder, &v, st.heap.cpu(st.fogBoxSrv[index]));
            k.fogBoxes[0]=st.fogBoxSrv[index]; k.fogBoxes[1]=uint32_t(boxes.size());
        }
        const uint32_t said=uint32_t(boxes.size())|(unread<<16);
        if(said!=st.fogBoxesSaid) {
            st.fogBoxesSaid=said;
            log("Traced fog: "+std::to_string(boxes.size())+" of the game's fog boxes in the medium"+
                (unread ? "; "+std::to_string(unread)+" left out this frame: their mask texture is not open" : ""));
        }
    } else if(st.fogBoxesSaid!=UINT32_MAX && st.fogBoxesSaid!=0) { st.fogBoxesSaid=0; log("Traced fog: no fog boxes in the medium"); }
    // ON TRIAL (PT-13, PT-35, docs/agents/glass-3.md; TGPT_Frame.cameraPosition's w, the plugin's hidden "Glass trial", 0 from
    // every other caller): why the jars are cloudy. Handed to the shaders in the flags' bits 8-15 (pt_common glass_trial: 1
    // the view through see-through glass crosses the mesh's smooth face, 2 its paint only on the faces the game draws, 4
    // glass has an inside where its piece of the mesh closes a volume, 8 clear transmission crosses the actual wall planes;
    // 0: everything as it is).
    {
        const float asked=std::isfinite(f.cameraPosition[3]) && f.cameraPosition[3]>0.f ? float(uint32_t(std::min(f.cameraPosition[3], 127.f))&15u) : 0.f;
        const uint32_t trial=uint32_t(asked+0.5f);
        k.size[3]|=trial<<8;
        static uint32_t said=~0u;
        if(said!=trial) {
            said=trial;
            log(trial==0 ? std::string("Glass on trial: off (glass as before)") :
                "Glass on trial ("+std::to_string(trial)+"): "+((trial&1u) ? "the view through see-through glass crosses the mesh's smooth face; " : "")+
                ((trial&2u) ? "its paint only on the faces the game draws; " : "")+
                ((trial&4u) ? "glass has an inside where its piece of the mesh closes a volume; " : "")+
                ((trial&8u) ? "clear transmission crosses the actual wall planes (overrides the smooth face)" : ""));
        }
    }
    k.settings[0]=f.thickProfiles; k.settings[1]=f.debugView<TGPT_VIEW_COUNT ? f.debugView : 0; k.settings[2]=std::clamp(f.bounces, 1u, 4u); k.settings[3]=std::min(f.spatialSamples, 16u);
    k.settings2[0]=st.scene.previousLightsSrv!=kInvalid ? st.scene.previousLightCount : 0u; k.settings2[1]=(rrg ? f.options : (f.options&~TGPT_OPTION_RAY_REGENERATION))&~TGPT_OPTION_NRD; k.settings2[2]=st.scene.lightCount; k.settings2[3]=atrousIterations;
    k.prevOriginDelta[3]=std::clamp(f.indirectColor, 0.f, 4.f); // Indirect Color, applied where the bounced light is shaded
    k.cameraPos[3]=std::isfinite(f.reflectionBoost) ? std::clamp(f.reflectionBoost, 0.f, 1.f) : 0.f; // Reflection Boost, in the prepare pass
    k.params[0]=std::isfinite(f.tracedGain) && f.tracedGain>0.f ? std::clamp(f.tracedGain, 1.f/64.f, 4096.f) : 1.f; k.params[1]=f.maxRayDistance>1 ? f.maxRayDistance : 1000.f; k.params[2]=f.skyIntensity>0 ? f.skyIntensity : 1.f;
    k.params[3]=std::max(8.f, 30.f*float(st.width)/1920.f);
#ifdef TGPT_SPEED_MEASURE
    // A measuring build's alone (PT-99, docs/agents/perf-1.md; the hidden "Speed trial" number 34816: the second page's
    // 2048): every ray ends after 60 m, to time what the rays' unlimited length costs (the owner, 2026-10-09: "a ray can
    // travel forever ... isn't that the primary reason for our perf woes?"). It changes the picture: for measuring only,
    // never in a build for players.
    if(std::isfinite(f.prevCameraPosition[3]) && f.prevCameraPosition[3]>0.f && (uint32_t(std::floor(std::min(f.prevCameraPosition[3], 524287.f)/8.f)+0.5f)&0x8800u)==0x8800u) k.params[1]=60.f;
#endif
    k.look[0]=std::isfinite(f.highlightKept) && f.highlightKept>0.f ? f.highlightKept : 0.f;
    // Where the previous frame's pixel grid lies against this one's (PT-85). Each frame's camera rays carry another
    // sub-pixel shift (PT-84), and a pixel's motion is HDRP's kind, without either shift: the point a pixel shows now
    // sat, in the previous frame's own grid, at its place less its motion PLUS this frame's shift less the previous
    // one's (up to a render pixel each way). Every pass that fetches the previous frame's data by a pixel's place adds it
    // (pt_common previous_place): ReSTIR's reuse across frames, the tracer's own histories, NRD's kept direction. Read
    // from the matrices the rays are made of, so it holds however the shift came (told, or put in by the tracer).
    // ON TRIAL (TGPT_Frame.jitter's z, the plugin's hidden setting "Reuse follows camera shift"): 0, the default, leaves the
    // difference at 0, every fetch by the motion alone as before. 1: the tracer's own histories (direct light, reflections,
    // bounced light, NRD's kept direction) follow the shifts; ReSTIR's reuse keeps its fetch by the motion alone. That is the
    // fix as measured: ReSTIR finds its surface again by its own search (its raw samples are the same with and without
    // the shift), and with its fetch moved too the denoised direct light changed 19 % more from frame to frame inside flat
    // surfaces (docs/agents/reuse-shift.md, section 6). 2, for finding causes: ReSTIR's fetch as well.
    // (look's w is what the shaders read: 2 for the histories alone, 1 for everything.)
    {
        const float asked=std::isfinite(f.jitter[2]) ? f.jitter[2] : 0.f;
        const int trial=asked>1.5f ? 2 : asked>0.5f ? 1 : 0;
        float held[2]={0.f, 0.f};
        const bool have=held_camera_shift(f, held);
        if(trial && have && st.haveCameraShift && !reset) { k.look[1]=held[0]-st.cameraShift[0]; k.look[2]=held[1]-st.cameraShift[1]; }
        k.look[3]=trial==1 ? 2.f : trial==2 ? 1.f : 0.f;
        st.cameraShift[0]=held[0]; st.cameraShift[1]=held[1]; st.haveCameraShift=have;
        static int said=-1;
        if(said!=trial) {
            said=trial;
            log(trial==0 ? "Reuse across frames: fetched by the motion alone (the trial of PT-85 is off)" :
                trial==1 ? "Reuse across frames: the tracer's own histories are fetched in the previous frame's own pixel grid (PT-85 on trial: the two frames' sub-pixel shifts' difference added to the motion; ReSTIR's reuse by the motion alone)" :
                           "Reuse across frames: the tracer's own histories AND ReSTIR's reuse are fetched in the previous frame's own pixel grid (PT-85 on trial, 2: for finding causes)");
        }
    }
    k.inputs0[0]=in.gb0.srv; k.inputs0[1]=in.nrm.srv; k.inputs0[2]=in.gb2.srv; k.inputs0[3]=in.gb3.srv;
    k.inputs1[0]=in.depth.srv; k.inputs1[1]=in.motion.srv; k.inputs1[2]=in.exposure.srv; k.inputs1[3]=st.sky.srv;
    k.decals[0]=st.scene.decalsSrv; k.decals[1]=st.scene.decalNodesSrv; k.decals[2]=st.scene.decalNodeCount;
    k.scene0[0]=st.scene.instancesSrv; k.scene0[1]=st.scene.meshesSrv; k.scene0[2]=st.scene.geometriesSrv; k.scene0[3]=st.scene.materialsSrv;
    k.scene1[0]=st.scene.materialIndicesSrv; k.scene1[1]=st.scene.lightsSrv; k.scene1[2]=st.atlas.srv; k.scene1[3]=out.a.uav;
    k.extra[0]=st.alphaAtlas.srv; k.extra[1]=st.material[prev].uav; k.extra[2]=st.neighborOffsetsSrv; k.extra[3]=st.scene.previousLightsSrv;
    // The reference radiance the clamps measure against and traced light is stored relative to: a white surface in full
    // sun, or lit by the brightest light 1 m away, whichever is brighter. It depends on the scene's lights, never on
    // where the camera looks or on HDRP's exposure (clamps measured in pre-exposed units made a hall's bounced light
    // follow the auto exposure). Eased in log space over about ten frames: a bright light coming into range at night
    // would otherwise jump it, and the built-in denoiser rescales its history by the change (pt_temporal).
    // The clamps' own reference is the sun's, or the brightest light's 25 cm away (16 times its 1 m value): the table under
    // a candle and the walls of a fireplace are the bounced light's main sources indoors.
    {
        const double sun=f.sunColor[3]>0.f ? (0.2126*f.sunColor[0]+0.7152*f.sunColor[1]+0.0722*f.sunColor[2])/3.14159265358979 : 0.0;
        const double lamp=double(st.scene.brightest_light())/3.14159265358979;
        double ref=std::max(sun, lamp), clampRef=std::max(sun, 16.0*lamp);
        if(!(ref>1e-6) || !std::isfinite(ref)) ref=1.0;
        if(!(clampRef>1e-6) || !std::isfinite(clampRef)) clampRef=ref;
        auto ease=[reset](double previous, double target) {
            return (reset || !(previous>0.0)) ? target : std::exp(std::log(previous)+(std::log(target)-std::log(previous))*0.1);
        };
        const double previous=st.reference;
        st.reference=ease(previous, ref); st.clampReference=ease(st.clampReference, clampRef);
        // PT-136: what the reference is made of, in the log when it has moved by a tenth or every 30 s: one large glowing
        // mesh once set it 20,000 times over the sun's white and nothing said so.
        {
            static double told=0.0; static uint64_t toldAt=0;
            const uint64_t now=GetTickCount64();
            if(!(told>0.0) || std::fabs(std::log(ref/told))>0.1 || now-toldAt>=30000) {
                told=ref; toldAt=now;
                float lampCd=0.f, emitterCd=0.f, emitterRawCd=0.f; st.scene.brightest_parts(lampCd, emitterCd, emitterRawCd);
                float medianCd=0.f; const float countedCd=st.scene.lamps_reference(nullptr, &medianCd);
                char b[520];
                std::snprintf(b, sizeof b, "The reference radiance: %.4g (a white surface in the sun %.4g; 1 m from the brightest lamp %.4g, counted as %.4g: "
                              "no lamp counts for more than 64 times the median lamp's %.4g; by the brightest glowing mesh %.4g, which its clusters' intensity "
                              "alone would have put at %.4g); the clamps' %.4g; stored light is relative to the first",
                              ref, sun, double(lampCd)/3.14159265358979, double(countedCd)/3.14159265358979, double(medianCd)/3.14159265358979,
                              double(emitterCd)/3.14159265358979, double(emitterRawCd)/3.14159265358979, clampRef);
                log(b);
                log("The lamps the reference is taken from: "+st.scene.brightest_lamps_text());
            }
        }
        k.radiance[0]=float(st.reference); k.radiance[1]=float(previous>0.0 ? previous : st.reference); k.radiance[2]=float(st.clampReference);
        { const auto s=st.scene.stats(); k.radiance[3]=float((s.swayingInstances>0 ? 1u : 0u)|(s.clothInstances>0 ? 2u : 0u)|(s.seeThroughInstances>0 ? 4u : 0u)); } // what the wind moves; thin cloth; see-through glass
        for(int i=0;i<4;++i) k.fog[i]=std::isfinite(f.fog[i]) ? f.fog[i] : 0.f;
        // HDRP's fog along reflected and refracted rays (PT-37, pt_common.hlsli fogged): what it scatters of its color, its
        // maximum fog distance, its mip fog, and HDRP's sky at those mips in the Sky colour mode.
        // In the Sky colour mode without HDRP's sky (not a cubemap: the tracer's plain sky), no fog color: fogged leaves the rays.
        const bool fogSkyMode=f.fogColor[3]>0.5f;
        for(int i=0;i<3;++i) k.fogColor[i]=std::isfinite(f.fogColor[i]) && (!fogSkyMode || st.skyValid) ? std::clamp(f.fogColor[i], 0.f, 64.f) : 0.f;
        k.fogColor[3]=std::isfinite(f.forward[2]) && f.forward[2]>0.f ? std::min(f.forward[2], 1e7f) : 0.f;
        for(int i=0;i<3;++i) k.fogMip[i]=std::isfinite(f.fogMip[i]) ? f.fogMip[i] : 0.f;
        { const uint32_t sky=fogSkyMode && st.skyValid ? st.fogSky.srv : kInvalid; std::memcpy(&k.fogMip[3], &sky, 4); }
        // Caustics (TGPT_SetWaterCaustics): each slot's water and its waves as HDRP's water shader samples them (its
        // SampleWaterSurface): per band, the undisplaced point's texture coordinates are its position in the surface's space (x
        // and z) times the band's scale less its offset, here one affine map of the position relative to the scene origin worked
        // out in double (the world's coordinates run to kilometres, a band repeats every few metres), its whole repeats dropped.
        k.waterGeometry[0]=st.scene.waterRecordsSrv; k.waterGeometry[1]=st.scene.waterRecordCount;
        bool causticWater=false; double causticReach=0.0;
        const double camX=double(k.cameraPos[0]), camZ=double(k.cameraPos[2]);
        for(uint32_t i=0;i<TGPT_MAX_WATER;++i) {
            const auto& c=st.caustics[parity][i];
            k.waterTextures[i]=k.waterNormals[i]=kInvalid; k.waterTop[i]=-1e30f; k.waterBottom[i]=1e30f;
            float* m=k.water+i*kWaterRows*4;
            std::fill(m, m+kWaterRows*4, 0.f);
            if(!c.live) continue;
            const auto& d=c.desc;
            std::memcpy(k.waterScatter[i], d.scattering, sizeof(d.scattering));
            std::memcpy(k.waterFoam[i][0], d.foam, sizeof(d.foam));
            std::memcpy(k.waterFoam[i][1], d.foamMask, sizeof(d.foamMask));
            std::memcpy(k.waterFoam[i][2], d.foamFlow, sizeof(d.foamFlow));
            for(int axis=0;axis<2;++axis) {
                const int row=axis==0 ? 0 : 2;
                double shift=d.worldToWater[row*4+3];
                for(int col=0;col<3;++col) { k.waterFoam[i][3+axis][col]=d.worldToWater[row*4+col]; shift+=double(d.worldToWater[row*4+col])*st.origin[col]; }
                k.waterFoam[i][3+axis][3]=float(shift);
            }
            std::memcpy(k.waterFoamMaps[i], d.foamMaps, sizeof(d.foamMaps));
            share::resolve(k.waterFoamMaps[i], 2);
            std::memcpy(k.waterFoam[i][5], d.foamColor, sizeof(d.foamColor));
            // The surface's own smoothness (pt_water_surface.hlsli water_smoothness): near in waterScatter's w, far in the
            // foam colour's w, the range of distances as two floats' bits in waterFoamMaps' z and w. A surface that sends
            // none (TGPT_WATER_SMOOTHNESS) is a clear mirror facet: 1 near and far.
            {
                const bool smooth=(d.flags&TGPT_WATER_SMOOTHNESS)!=0;
                const float range[2]={smooth ? d.smoothnessFade[0] : 0.f, smooth ? std::max(d.smoothnessFade[1], 1e-3f) : 1.f};
                k.waterScatter[i][3]=smooth ? std::clamp(d.scattering[3], 0.f, 1.f) : 1.f;
                k.waterFoam[i][5][3]=smooth ? std::clamp(d.foamColor[3], 0.f, 1.f) : 1.f;
                std::memcpy(&k.waterFoamMaps[i][2], range, sizeof(range));
            }
            const uint32_t bands=c.waves ? std::min(d.bandCount, c.displacement.slices) : 0u;
            for(uint32_t b=0;b<bands;++b) {
                for(int axis=0;axis<2;++axis) {
                    const int r=axis==0 ? 0 : 2;
                    const double scale=double(d.bands[b][0]);
                    double offset=scale*double(d.worldToWater[r*4+3])-double(d.bands[b][1+axis]);
                    float* row=m+(b*3+axis)*4;
                    for(int col=0;col<3;++col) { row[col]=float(scale*d.worldToWater[r*4+col]); offset+=scale*double(d.worldToWater[r*4+col])*st.origin[col]; }
                    row[3]=float(offset-std::floor(offset));
                }
                float* extra=m+(b*3+2)*4; extra[0]=d.bands[b][3]; extra[1]=d.fades[b][0]; extra[2]=d.fades[b][1];
            }
            for(int r=0;r<3;++r) for(int col=0;col<3;++col) m[(9+r)*4+col]=d.worldToWater[r*4+col];
            m[48]=float(bands); m[49]=float(c.displacement.size); m[50]=std::max(d.maxDisplacement, 0.f); m[51]=1.f;
            m[52]=float(double(d.bounds[0])-st.origin[0]); m[53]=float(double(d.bounds[1])-st.origin[2]);
            m[54]=float(double(d.bounds[2])-st.origin[0]); m[55]=float(double(d.bounds[3])-st.origin[2]);
            for(int ch=0;ch<3;++ch) m[56+ch]=d.extinction[ch];
            if(bands) { k.waterTextures[i]=c.displacement.srv; k.waterNormals[i]=c.normals.srv; }
            // Previous simulation, mapped into this frame's relative coordinates. Both input sets stay alive until
            // this frame finishes: primary water motion reads the previous set as well as the current one.
            const auto& priorWater=st.caustics[1-parity][i];
            const bool previousValid=!reset && priorWater.live && priorWater.waves && priorWater.desc.displacement==d.displacement;
            k.waterPreviousTextures[i]=previousValid ? priorWater.displacement.srv : kInvalid;
            if(previousValid) {
                const auto& pd=priorWater.desc;
                for(uint32_t band=0;band<std::min(pd.bandCount, priorWater.displacement.slices);++band) {
                    for(int axis=0;axis<2;++axis) {
                        const int row=axis==0 ? 0 : 2;
                        double shift=double(pd.bands[band][0])*pd.worldToWater[row*4+3]-pd.bands[band][1+axis];
                        float* dst=k.waterPrevious[i][band*3+axis];
                        for(int col=0;col<3;++col) {
                            dst[col]=pd.bands[band][0]*pd.worldToWater[row*4+col];
                            shift+=double(pd.bands[band][0])*pd.worldToWater[row*4+col]*st.origin[col];
                        }
                        dst[3]=float(shift-std::floor(shift));
                    }
                    float* extra=k.waterPrevious[i][band*3+2];
                    extra[0]=pd.bands[band][3]; extra[1]=pd.fades[band][0]; extra[2]=pd.fades[band][1];
                }
                for(int row=0;row<3;++row) for(int col=0;col<3;++col) k.waterPrevious[i][9+row][col]=pd.worldToWater[row*4+col];
                k.waterPrevious[i][12][0]=float(std::min(pd.bandCount, priorWater.displacement.slices));
            }
            // The slot's top: the plugin's, or the highest corner of the slot's own traced water where that stands higher (round
            // 20: some of the scene's water stood over the plugin's top, so the rays straight down from it (water_over,
            // water_base) began under that water, and Speed trial 1's check refused every frame, PT-99).
            const double top=std::max(double(d.top), st.scene.waterHighestSlot[i]);
            k.waterTop[i]=float(top-st.origin[1]); k.waterBottom[i]=float(double(std::min(d.bottom, d.top))-st.origin[1]);
            causticWater=true;
            const double margin=double(m[50]);
            causticReach=std::max({causticReach, std::fabs(double(m[52])-margin-camX), std::fabs(double(m[54])+margin-camX),
                                   std::fabs(double(m[53])-margin-camZ), std::fabs(double(m[55])+margin-camZ)});
        }
        // The water's kept wave corners (pt_sun_grid.hlsl, pt_water_sun.hlsli; docs/agents/water-8.md): a grid for the first
        // kSunGrids slots that have waves and whose level surface is one flat, level plane (HDRP's sea, a lake, a pool; a bent
        // river mesh's patches are made at their points, as before), while the sun is up and the Caustics row is on (with the
        // row off the sun's ray goes straight: no patch). The grid's two steps are the ones a patch over still water takes:
        // along the sun's bearing (the cell over the bent ray's cosine) and across it (the cell); the cell is the one a patch
        // wants, half a texel of the slot's finest band, times kSunCellShare. It lies round the camera, most of it ahead
        // (pushed along the view by six tenths of its half width, less as the camera looks down), and is shifted by a random
        // part of a cell every frame, as the caustics' grid is: the frames' mean is the mean over every way of cutting the
        // surface into triangles. Where it does not reach, a patch is made at its point: the same light, the old cost.
        // TGPT_TEST_SUN_GRID (a test's): 0, no grid: every patch is made at its point. TGPT_TEST_SUN_PICK (a test's): how
        // many of the tiles left a point sums (1 to 8; the shaders' SUN_PICK without it): the same light, other noise and time.
        k.waterGeometry[2]=kInvalid; k.waterGeometry[3]=0u; st.sunGridSlots=0;
        {
            char mode[8]{}; GetEnvironmentVariableA("TGPT_TEST_SUN_GRID", mode, sizeof(mode));
            auto mix=[](uint32_t v) { v=v*747796405u+2891336453u; v=((v>>((v>>28u)+4u))^v)*277803737u; return (v>>22u)^v; };
            const double sunX=double(k.sunDir[0]), sunY=double(k.sunDir[1]), sunZ=double(k.sunDir[2]);
            const double sunFlat=std::hypot(sunX, sunZ), sunLength=std::sqrt(sunX*sunX+sunY*sunY+sunZ*sunZ);
            // The view's direction on the level plane: the middle of the screen, as the shaders unproject a pixel.
            double viewX=0.0, viewZ=0.0;
            {
                double p[4]{};
                const double clip[4]={0.0, 0.0, 0.5, 1.0};
                for(int row=0;row<4;++row) for(int col=0;col<4;++col) p[row]+=double(f.invViewProj[col*4+row])*clip[col];
                const double length=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
                if(std::isfinite(length) && length>1e-12 && std::isfinite(p[3]) && std::fabs(p[3])>1e-12) { const double side=p[3]>0.0 ? 1.0 : -1.0; viewX=side*p[0]/length; viewZ=side*p[2]/length; }
            }
            uint32_t grids=0;
            if(mode[0]!='0' && st.sunGridPass && k.sunColor[3]>0.f && sunLength>1e-6 && sunY/sunLength>1e-3 && !(f.options&TGPT_OPTION_NO_CAUSTICS)) for(uint32_t i=0;i<TGPT_MAX_WATER && grids<kSunGrids;++i) {
                const auto& c=st.caustics[parity][i];
                const float* m=k.water+i*kWaterRows*4;
                if(!c.live || m[48]<1.f || m[49]<1.f) continue;
                const auto& d=c.desc;
                if(!(std::fabs(double(d.top)-double(std::min(d.bottom, d.top)))<1e-3) || !(d.worldToWater[5]>0.99999f)) continue;
                double finest=1e30;
                for(uint32_t band=0;band<uint32_t(m[48]);++band) {
                    const float* row=m+band*12;
                    const double scale=std::sqrt(double(row[0])*row[0]+double(row[1])*row[1]+double(row[2])*row[2]);
                    if(scale>0.0) finest=std::min(finest, 0.5/(scale*double(m[49])));
                }
                if(!(finest<1e29) || !(finest>1e-4)) continue;
                const double cell=finest*kSunCellShare;
                const double bearingX=sunFlat>1e-6*sunLength ? sunX/sunFlat : 1.0, bearingZ=sunFlat>1e-6*sunLength ? sunZ/sunFlat : 0.0;
                const double sine=sunFlat/sunLength/kWaterIndex, cosine=std::sqrt(std::max(1.0-sine*sine, 0.05));
                const double ux=bearingX*cell/cosine, uz=bearingZ*cell/cosine, vx=-bearingZ*cell, vz=bearingX*cell;
                const double half=0.5*double(kSunCorners-1), ahead=0.6*half*cell;
                const uint32_t seed=mix(f.frameIndex*2654435761u+0x51ed270bu+i*0x9E3779B9u);
                const double shiftU=double(mix(seed^0x68e31da4u)>>8)/16777216.0, shiftV=double(mix(seed^0xb5297a4du)>>8)/16777216.0;
                const double firstX=camX+ahead*viewX-(half-shiftU)*ux-(half-shiftV)*vx, firstZ=camZ+ahead*viewZ-(half-shiftU)*uz-(half-shiftV)*vz;
                float* steps=k.sunGrid[i*2]; float* first=k.sunGrid[i*2+1];
                steps[0]=float(ux); steps[1]=float(uz); steps[2]=float(vx); steps[3]=float(vz);
                first[0]=float(firstX); first[1]=float(firstZ); first[2]=k.waterTop[i];
                const uint32_t firstByte=grids*kSunSlotBytes; std::memcpy(&first[3], &firstByte, 4);
                st.sunGridSlots|=1u<<i; ++grids;
            }
            if(grids && !st.sunGrid.resource) {
                const uint64_t bytes=uint64_t(kSunGrids)*kSunSlotBytes;
                try {
                    st.sunGrid=make_buffer(st.d12.Get(), bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the water's kept wave corners");
                    st.sunGridUav=raw_uav(st.d12.Get(), st.heap, st.sunGrid.resource.Get(), bytes);
                } catch(const DxError& e) {
                    // Without the room every patch is made at its point: the same light, slower.
                    log(std::string("The water's kept wave corners could not be made (")+e.what()+"); the sun's patches inside the water are made at their points");
                    st.sunGrid=Buffer{}; st.sunGridUav=kInvalid; st.sunGridPass.Reset();
                }
            }
            if(grids && st.sunGridUav!=kInvalid) {
                k.waterGeometry[2]=st.sunGridUav;
                char pick[8]{};
                if(GetEnvironmentVariableA("TGPT_TEST_SUN_PICK", pick, sizeof(pick))>0 && pick[0]>='1' && pick[0]<='8') k.waterGeometry[3]=uint32_t(pick[0]-'0');
            }
            else { st.sunGridSlots=0; for(auto& row:k.sunGrid) std::fill(std::begin(row), std::end(row), 0.f); }
            // TGPT_TEST_SUN_WIDTH (a test's; PT-131, docs/agents/campfix.md): where the sun is asked for inside the water,
            // every path's ray is taken as this many centimetres wide (pt_water_sun.hlsli, the cell by the ray's width), in
            // the bits over the count of tiles: what a wide ray's coarse cell does, shown by a camera's own narrow rays.
            { char wide[16]{}; if(GetEnvironmentVariableA("TGPT_TEST_SUN_WIDTH", wide, sizeof(wide))>0) k.waterGeometry[3]|=uint32_t(std::min(std::max(std::atoi(wide), 0), 0x7FFFFF))<<8; }
        }
        // The caustics' grid over the water (pt_caustics.hlsl): levels of kCausticGrid cells a side round the camera, the finest
        // kCausticCell, each next one twice as coarse and holding the finer one's square in its middle on its own cell lines, as
        // many as reach the farthest water; all of it moved by a random fraction of the coarsest cell every frame (the light it
        // gives converges over frames to the light a finer grid gives).
        std::fill(std::begin(k.caustics), std::end(k.caustics), kInvalid);
        std::fill(std::begin(k.causticGrid), std::end(k.causticGrid), 0.f);
        std::fill(std::begin(k.causticLevels), std::end(k.causticLevels), -1e9f);
        const bool causticsRun=causticWater && !(f.options&TGPT_OPTION_NO_CAUSTICS) && st.causticPass;
        if(causticsRun && !st.causticCapacity) make_caustic_triangles(1u<<20);
        // The triangles this frame may write and builds over: half as many again as the last frames wrote, 65,536 at least,
        // never past the limit; and room for them (PT-87, ensure_caustic_room: the room follows this count, where it was the
        // limit's). Without room on the card there are no caustics this frame, and the next one asks again. With no caustics
        // pass for kIdleFrames frames (the row off, no water slot left) the room is let go.
        uint32_t causticBuild=0;
        if(causticsRun) {
            uint32_t built=1u<<16; const uint64_t want=uint64_t(st.causticRecent)*3/2; while(built<want && built<st.causticCapacity) built<<=1;
            causticBuild=std::min(built, st.causticCapacity);
            st.causticIdle=0;
            try { ensure_caustic_room(list, causticBuild); }
            catch(const DxError& e) { log_throttled(std::string("The caustics' room could not be made (")+e.what()+"); no caustics this frame"); }
        } else if(st.causticRoom && ++st.causticIdle>=kIdleFrames && !keep_idle()) {
            release_caustic_room(st.consumer.value);
            log("The caustics' room is let go: no frame has run them for "+std::to_string(kIdleFrames)+" frames");
        }
        if(causticsRun && causticBuild && st.causticRoom>=causticBuild && st.causticCornersUav!=kInvalid && st.causticTlasSrv!=kInvalid) {
            uint32_t levels=1;
            while(levels<kCausticLevels && 0.5*double(kCausticGrid)*kCausticCell*std::ldexp(1.0, int(levels)-1)<causticReach) ++levels;
            auto mix=[](uint32_t v) { v=v*747796405u+2891336453u; v=((v>>((v>>28u)+4u))^v)*277803737u; return (v>>22u)^v; };
            const uint32_t seed=mix(f.frameIndex*2654435761u+0x5bd1e995u);
            const double coarse=kCausticCell*std::ldexp(1.0, int(levels)-1), n=double(kCausticGrid);
            const double jx=double(mix(seed^0x68e31da4u)>>8)/16777216.0*coarse, jz=double(mix(seed^0xb5297a4du)>>8)/16777216.0*coarse;
            double ox=jx+std::floor((camX-0.5*n*coarse-jx)/coarse)*coarse, oz=jz+std::floor((camZ-0.5*n*coarse-jz)/coarse)*coarse;
            k.causticLevels[(levels-1)*4]=float(ox); k.causticLevels[(levels-1)*4+1]=float(oz);
            for(int l=int(levels)-2;l>=0;--l) {
                const double outer=kCausticCell*std::ldexp(1.0, l+1);
                const double kx=std::round((camX-ox)/outer-0.25*n), kz=std::round((camZ-oz)/outer-0.25*n);
                k.causticLevels[(l+1)*4+2]=float(kx); k.causticLevels[(l+1)*4+3]=float(kz);
                ox+=kx*outer; oz+=kz*outer;
                k.causticLevels[l*4]=float(ox); k.causticLevels[l*4+1]=float(oz);
            }
            k.caustics[0]=st.causticLightUav; k.caustics[1]=st.causticTlasSrv; k.caustics[2]=st.causticCountersUav;
            st.causticBuilt=causticBuild; // (worked out above, before its room was looked at)
            k.caustics[3]=st.causticBuilt;
            k.causticGrid[0]=kCausticCell; std::memcpy(&k.causticGrid[1], &st.causticCornersUav, 4); k.causticGrid[2]=float(levels); std::memcpy(&k.causticGrid[3], &seed, 4);
        }
        // Where the water has been (pt_wet.hlsl, PT-52): levels of kWetGrid cells round the camera, the finest kWetCell, each next
        // twice as coarse, as many as reach the farthest water; each level's square on the world's own cells of its size (its
        // corner cell's world index, worked out in double), so a cell's record stays the same piece of ground as the camera moves.
        // The clock: the game's shader time (Time.time, which stands still when the world does). The ceiling: no ground above
        // every slot's highest water and its waves' largest move is ever wet.
        std::fill(std::begin(k.wet), std::end(k.wet), 0u); k.wet[0]=kInvalid;
        std::fill(std::begin(k.wetGrid), std::end(k.wetGrid), 0.f); k.wetGrid[2]=-1e30f;
        std::fill(std::begin(k.wetLevels), std::end(k.wetLevels), -1e9f);
        // The store itself: every level's cells, made when water first enters the traced world and emptied by its first pass
        // (wetRan); it holds where the water has been, so it stays until the scene goes (release_scene_extras). PT-87: a scene
        // without water holds none. Without room for it the ground by the water stays dry, and the next frame asks again.
        if(causticWater && !(f.options&TGPT_OPTION_DRY_GROUND) && st.wetPass && !st.wetStore.resource) {
            const uint64_t bytes=uint64_t(kWetLevels)*kWetGrid*kWetGrid*kWetCellBytes;
            try {
                st.wetStore=make_buffer(st.d12.Get(), bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the wet ground's store");
                st.wetStoreUav=raw_uav(st.d12.Get(), st.heap, st.wetStore.resource.Get(), bytes);
                st.wetRan=false;
            } catch(const DxError& e) {
                log_throttled(std::string("The wet ground's store could not be made (")+e.what()+"); the ground by the water is drawn dry");
                st.wetStore=Buffer{}; st.wetStoreUav=kInvalid;
            }
        }
        if(causticWater && !(f.options&TGPT_OPTION_DRY_GROUND) && st.wetPass && st.wetStoreUav!=kInvalid) {
            uint32_t levels=1;
            while(levels<kWetLevels && 0.5*double(kWetGrid)*kWetCell*std::ldexp(1.0, int(levels)-1)<causticReach) ++levels;
            const double worldX=st.origin[0]+camX, worldZ=st.origin[2]+camZ;
            for(uint32_t l=0;l<levels;++l) {
                const double c=double(kWetCell)*std::ldexp(1.0, int(l));
                const double lx=std::floor(worldX/c)-0.5*kWetGrid, lz=std::floor(worldZ/c)-0.5*kWetGrid;
                k.wetLevels[l*4]=float(lx*c-st.origin[0]); k.wetLevels[l*4+1]=float(lz*c-st.origin[2]);
                const int32_t ix=int32_t(lx), iz=int32_t(lz);
                std::memcpy(&k.wetLevels[l*4+2], &ix, 4); std::memcpy(&k.wetLevels[l*4+3], &iz, 4);
            }
            if(!st.wetRan || st.wetScene!=st.scene.clear_generation()) st.wetCeiling=-1e30f;
            for(uint32_t i=0;i<TGPT_MAX_WATER;++i) if(k.water[i*kWaterRows*4+51]>0.5f) st.wetCeiling=std::max(st.wetCeiling, k.waterTop[i]+k.water[i*kWaterRows*4+50]+0.05f);
            k.wet[0]=st.wetStoreUav; k.wet[1]=levels;
            std::memcpy(&k.wet[2], &st.wetClock, 4); std::memcpy(&k.wet[3], &st.wetClock, 4);
            k.wetGrid[0]=kWetCell; k.wetGrid[1]=float(st.origin[1]); k.wetGrid[2]=st.wetCeiling;
        }
        { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); std::memcpy(k.profileF0, e.profileF0.data(), sizeof(k.profileF0)); }
    }
    // ON TRIAL (PT-85 and PT-40, docs/agents/flame-specks.md; TGPT_Frame.jitter's w, the plugin's four hidden "Lamp trial"
    // settings packed, 0 from every other caller): which use of a small lamp's size makes the specks. Handed to the shaders
    // as it is in wetGrid's w (pt_common lamp_trial reads its bits: A the near field's distance in cm, B the reflected
    // ball, C the highlight, D the shadow's disc; 0: everything as before). After the wet ground's block, which clears wetGrid.
    {
        // (Bits 14-21, the job after's, docs/agents/flame-near.md: what stands within that many centimetres of a lamp is
        // found on purpose, pt_common lamp_around; 0 off. 22 bits in all: a float holds them exactly.)
        const float asked=std::isfinite(f.jitter[3]) && f.jitter[3]>0.f ? std::min(f.jitter[3], 4194303.f) : 0.f;
        const uint32_t trial=uint32_t(asked+0.5f);
        k.wetGrid[3]=float(trial);
        // ON TRIAL (PT-93, docs/agents/grass-boil.md; TGPT_Frame.fogMip's w, the plugin's hidden "Grass shadow trial", 0 from
        // every other caller): 1, a light's shadow ray takes the product of what the blades of grass and the leaves in its
        // way let through, in place of a coin at each (pt_common leaf_product_on). Bit 22 of the same number, above the
        // lamps' 22 bits (23 in all: a float still holds them exactly).
        {
            const uint32_t grass=std::isfinite(f.fogMip[3]) && f.fogMip[3]>0.5f ? 1u : 0u;
            if(grass) k.wetGrid[3]=float(trial|(1u<<22));
            static uint32_t saidGrass=~0u;
            if(saidGrass!=grass) {
                saidGrass=grass;
                log(grass ? "Grass shadows on trial: a light's ray takes the product of what each blade in its way lets through (no coin)"
                          : "Grass shadows on trial: off (a light's ray passes each blade by chance, as before)");
            }
        }
        static uint32_t said=~0u;
        if(said!=trial) {
            said=trial;
            static const char* const kSize[4]={"its own size", "HDRP's radius", "5 times its own size", "5 times its own size"};
            log(trial==0 ? std::string("Small lamps on trial: all four switches off (lamps as before)") :
                "Small lamps on trial (for finding causes): A near light held to "+std::to_string(trial&0xFFu)+" cm (0: off); B the reflected ball at "+
                kSize[(trial>>8)&3u]+"; C the highlight at "+kSize[(trial>>10)&3u]+"; D the shadow's disc at "+kSize[(trial>>12)&3u]+
                "; lit surroundings found on purpose within "+std::to_string((trial>>14)&0xFFu)+" cm (0: off)");
#ifdef TGPT_NO_LAMP_AROUND
            // PT-99 (docs/agents/speed-1.md): this build's passes hold no code of that last trial (CMake's TGPT_LAMP_AROUND is
            // OFF: with the setting at 0 its code cost the bounced light's pass about 30 ms a frame in the game).
            if((trial>>14)&0xFFu) log("Small lamps on trial: 'lit surroundings' is asked for, but this build has it compiled out (configure with -DTGPT_LAMP_AROUND=ON to try it): nothing changes");
#endif
        }
    }
    // ON TRIAL (PT-108 and PT-104, docs/agents/rain-1.md; TGPT_SetRain, which the plugin's hidden "Rain wetness" alone
    // calls: no field of TGPT_Frame and no option bit): the rain wets what a ray meets as the game's own pass wets what
    // the camera sees (pt_common rain_roughness). The noise texture is the game's own, opened with no copy: its handle to
    // its number in the heap this frame (none: dry, and the log says so).
    {
        const TGPT_Rain& r=st.rain;
        int32_t noise=r.noise;
        const bool asked=r.size==sizeof(TGPT_Rain) && r.moisture>0.f;
        if(asked) share::resolve(&noise, 1); else noise=-1;
        const bool on=asked && noise>=0 && st.sharedBase!=kInvalid;
        const uint32_t number=on ? uint32_t(noise) : kInvalid;
        k.rain[0]=on ? r.moisture : 0.f; k.rain[1]=r.soaked; k.rain[2]=r.screenNoise; std::memcpy(&k.rain[3], &number, 4);
        static int said=-1;
        const int state=!asked ? 0 : !on ? 1 : 2;
        if(said!=state) {
            said=state;
            char text[256];
            std::snprintf(text, sizeof text, "Rain wetness on trial: on, moisture %.2f, a soaked surface's smoothness %.2f at the camera, the screen noise's stand-in %.3f (no shelter yet: everything is under the open sky)",
                double(r.moisture), double(r.soaked), double(r.screenNoise));
            log(state==0 ? std::string("Rain wetness on trial: off or dry (surfaces as their materials make them)") :
                state==1 ? std::string("Rain wetness on trial: asked for, but the game's noise texture is not open to the tracer yet: dry until it is") : std::string(text));
#ifndef TGPT_RAIN_WETNESS
            if(state!=0) log("Rain wetness on trial: this build has it compiled out (configure with -DTGPT_RAIN_WETNESS=ON to try it): nothing changes");
#endif
        }
    }
    // ON TRIAL (PT-40, docs/agents/flame-near.md; TGPT_Frame.frameReserved, the plugin's two hidden "Highlight shoulder"
    // settings packed, 0 from every other caller): what is far past white rolls off above a knee (pt_common
    // highlight_shoulder: the knee in 64ths in bits 0-11, 0 the hold's W; the most in 64ths in bits 12-23, 0 off). Handed
    // to the shaders in radiance's z, which no shader read (above: the old clamps' reference, overwritten here).
    {
        const float asked=std::isfinite(f.frameReserved) && f.frameReserved>0.f ? std::min(f.frameReserved, 16777215.f) : 0.f;
        const uint32_t shoulder=uint32_t(asked+0.5f);
        k.radiance[2]=float(shoulder);
        static uint32_t said=~0u;
        if(said!=shoulder) {
            said=shoulder;
            char text[160];
            std::snprintf(text, sizeof text, "Highlights' shoulder on trial: knee %.2f (0: the hold's), most %.2f (0: off)", double(shoulder&0xFFFu)/64.0, double(shoulder>>12)/64.0);
            log(std::string(text));
        }
    }
    // ON TRIAL (PT-99, docs/agents/speed-1.md; TGPT_Frame.prevCameraPosition's w, the plugin's hidden "Speed trial", 0 from
    // every other caller): speed that leaves the picture as it is. A whole number of 16 bits, handed to the shaders in the
    // frame flags' bits 16-31 (pt_common speed_trial). Bit 1, THE RULE since round 20 (set here in every frame): a shadow
    // ray's "which water am I in" ray is traced only under the height no water stands over (pt_water_surface water_ceiling:
    // every slot's highest water, waterCeiling, and the waves' reach).
    // That height is the plugin's (TGPT_WaterCaustics.top); the bit goes to the shaders only in a frame in which the
    // scene's own water triangles bear it out (GpuScene::waterHighest, the highest corner of every water surface traced
    // this frame, before its waves): elsewhere every point asks, as before. After the water's block, which fills waterTop.
    // (Bits 4 to 15 do something only in a measuring build, CMake's TGPT_SPEED_MEASURE: they leave work out, and light.)
    // Bit 8 (the value 8), for finding causes: the bounced light's and the reflections' passes run as the plain compute
    // passes they also are (the same source, DirectX Raytracing 1.2, no Shader Execution Reordering), where they otherwise
    // run as ray generation shaders that regroup their threads: what the regrouping gains or costs, in one run
    // (dispatch_pass reads st.plainPasses).
    {
        // The word's bits 0 to 2 are the reflections' relief trial (below, at the reflections' filter): this is the rest.
        const float asked=std::isfinite(f.prevCameraPosition[3]) && f.prevCameraPosition[3]>0.f ? std::floor(std::min(f.prevCameraPosition[3], 524287.f)/8.f) : 0.f;
        uint32_t trial=uint32_t(asked+0.5f);
#ifdef TGPT_SPEED_MEASURE
        // A measuring build's third page (PT-131, docs/agents/campspeed.md; pt_common speed_measure3): the number 32769
        // (the second page's 32768 with the 1 that is the rule in every frame, so that it asks nothing by itself) and
        // its switches, handed to the passes in waterCeiling's y (below); none of the first two pages' switches is on
        // with them (8, the plain passes, stays the tracer's; 2048 is the 60 m of the second page: above).
        const bool thirdPage=(trial&0x8001u)==0x8001u;
        const uint32_t third=thirdPage ? trial&0x7FF6u : 0u;
        if(thirdPage) trial&=9u;
#endif
        // Bit 1 is the rule since round 20 (runs 20261011-064558: the tavern, the town and the forest, the same picture and
        // fewer rays, no change of speed beyond a reading's wander; exact since waterCeiling holds every slot's water).
        trial|=1u;
        bool ceilingHolds=true;
        std::string ceilingNumbers; // for the log, when the height does not hold: the two heights it compares, in the world
        // The height over which no water stands takes the scene's own water of EVERY slot (round 20), the ones the plugin
        // describes and the ones it does not, in a constant of its own (pt_water_surface water_ceiling): a river 65 m up
        // beside the beach's sea at 35, in a slot the plugin gave no top, held this trial off in every frame of rounds 19
        // and 20. With it the check below holds wherever the scene's water is known.
        const double highestWater=st.scene.waterHighest;
        k.waterCeiling[0]=highestWater>-1e299 ? float(highestWater-st.origin[1]) : -1e30f;
        k.waterCeiling[1]=k.waterCeiling[2]=k.waterCeiling[3]=0.f;
#ifdef TGPT_SPEED_MEASURE
        k.waterCeiling[1]=float(third); // (a measuring build's third page: pt_common speed_measure3)
#endif
        if(trial&1u) {
            const float top=std::max(std::max(std::max(k.waterTop[0], k.waterTop[1]), std::max(k.waterTop[2], k.waterTop[3])), k.waterCeiling[0]);
            const double highest=st.scene.waterHighest;
            ceilingHolds=!(highest>-1e299) || (top>-1e29f && float(highest-st.origin[1])<=top+1e-3f);
            if(!ceilingHolds) trial&=~1u;
            if(!ceilingHolds) {
                char text[256];
                std::snprintf(text, sizeof text, " (the scene's water reaches y %.3f; the plugin's highest points a slot: %.3f, %.3f, %.3f, %.3f; -1e30: the slot holds no water)", highest,
                              k.waterTop[0]>-1e29f ? double(k.waterTop[0])+st.origin[1] : -1e30, k.waterTop[1]>-1e29f ? double(k.waterTop[1])+st.origin[1] : -1e30,
                              k.waterTop[2]>-1e29f ? double(k.waterTop[2])+st.origin[1] : -1e30, k.waterTop[3]>-1e29f ? double(k.waterTop[3])+st.origin[1] : -1e30);
                ceilingNumbers=text;
            }
        }
        k.size[3]|=trial<<16;
        st.plainPasses=(trial&8u)!=0 && st.gi && st.specular; // (made beside the reordering ones only with TGPT_TEST_PLAIN_PASSES: build_passes)
        // PT-99 (R19): the reflections' sort this frame (State::ReflectionsSort): held by the Speed trial (16 the material, 32
        // no hint; in a measuring build those numbers are its own), else the choice, or the other one while a probe runs.
        {
            auto& s=st.sort;
#ifdef TGPT_SPEED_MEASURE
            const uint32_t held=0;
#else
            const uint32_t held=trial&48u;
#endif
            bool noHint=s.noHint;
            uint8_t counted=0;
            if(held==16u) noHint=false;
            else if(held==32u) noHint=true;
            else if(!st.plainPasses && st.specularSer) {
                if(reset) { s.kept=0; s.frames=std::max(s.frames, kSortEvery-kSortProbe*4); } // (another view: a probe soon)
                if(!s.probeLeft && ++s.frames>=(kSortEvery<<std::min(s.kept, 3u))) { s.probeLeft=kSortProbe; s.frames=0; s.other.clear(); ++s.probes; }
                const bool probing=s.probeLeft>0;
                if(probing) { noHint=!s.noHint; --s.probeLeft; }
                counted=uint8_t((noHint ? 2u : 1u)+(probing ? 2u : 0u)); // 1, 2 the choice's frames; 3, 4 a probe's
            }
            s.used[index]=counted;
            if(noHint) k.size[3]|=4u;
        }
        static uint32_t said=~0u;
#ifdef TGPT_SPEED_MEASURE
        const uint32_t state=((trial&~1u)|(ceilingHolds ? 0u : 0x10000u))^(third<<17)^(thirdPage ? 0x20000u : 0u); // (and a measuring build's third page)
#else
        const uint32_t state=(trial&~1u)|(ceilingHolds ? 0u : 0x10000u); // (bit 1: the rule, said only when it cannot hold)
#endif
        if(said!=state) {
            said=state;
            log(state==0 ? std::string("Speed on trial: off (everything as before)") :
                "Speed on trial ("+std::to_string(trial&~1u)+"): "+
                (!ceilingHolds ? "NOT skipping the water question: some water of the scene stands over the highest point the plugin gave"+ceilingNumbers+" (every point asks, as before); " : "")+
                // 2 and 4 (PT-99, docs/agents/perf-1.md; pt_water_path.hlsli water_regroup): the water's path's threads
                // regrouped by what they do next, in the two reordering passes of a build configured with
                // -DTGPT_REGROUP_WATER=ON.
#ifdef TGPT_REGROUP_WATER
                ((trial&2u) ? (st.giSer && st.specularSer && !(trial&8u) ? "the water's path regroups its threads at every turn; " : "the water's path's regrouping is asked for, but the reordering passes do not run: nothing changes; ") : "")+
                ((trial&4u) ? (st.giSer && st.specularSer && !(trial&8u) ? "the points that make their own patch of waves are regrouped first; " : "the patch's regrouping is asked for, but the reordering passes do not run: nothing changes; ") : "")+
#elif !defined(TGPT_SPEED_MEASURE)
                ((trial&6u) ? "the water's path's regrouping is asked for (2 or 4), but this build has it compiled out (configure with -DTGPT_REGROUP_WATER=ON to try it): nothing changes; " : "")+
#endif
                ((trial&8u) ? (st.plainPasses ? "bounced light and reflections as plain compute passes (no Shader Execution Reordering); " :
                                                "plain compute passes asked for, but they were not made (start with TGPT_TEST_PLAIN_PASSES set); ") : "")+
#ifdef TGPT_SPEED_MEASURE
                // A measuring build: 2 (every triangle solid to the rays) and 4 (one query a ray from a surface) are measuring
                // switches too, and 32768 turns to the second page (the water's path by its parts; 2048 there: every ray
                // ends after 60 m): pt_common SPEED_M_SOLID, SPEED_M_ONE_QUERY, SPEED_M2_*; above.
                ((trial&0xFFF6u) ? std::string("measuring switches ")+((trial&0x8000u) ? "of the second page (the water's path, the caustics' stage, the rays' length) " : "")+std::to_string(trial&0x7FF6u)+" (this is a measuring build: they leave work out and change the picture)" : "")+
                // The third page (PT-131, docs/agents/campspeed.md): glass followed and the water's own path by their parts.
                (thirdPage ? std::string("measuring switches of the third page (glass followed and the water's own path by their parts) ")+std::to_string(third)+
                             ((third&2u) ? ": glass not followed" : "")+((third&4u) ? ": the water's path not run" : "")+((third&16u) ? ": no sun through the waves" : "")+
                             ((third&32u) ? ": no patch of waves made at a point" : "")+((third&64u) ? ": the water's body scatters nothing" : "")+
                             ((third&512u) ? ": the water's path ends after its first turn" : "")+((third&1024u) ? ": 1024 does nothing (the shadow rays first came out on 2026-10-12)" : "")+
                             ((third&2048u) ? ": every ray ends after 60 m" : "")+((third&4096u) ? ": no landed light asked" : "")+
                             ((third&8192u) ? ": the sun's patch at a point as before 2026-10-12 (its rule, the cell by the ray's width, is off)" : "")+((third&16384u) ? ": the glass walk ends at its first interface" : "")+
                             " (this is a measuring build: they leave work out and change the picture)" : ""));
#else
                // (16 and 32768 were trials of their own until 2026-10-10, docs/agents/rule-1.md: the lamps' search tree
                // and the feeders' key are in every build now, with no switch; 32, the water's short rays, came out. 16 to
                // 80 were the reordering study's and the split trial's arrangements of the bounced light's pass the same
                // day, docs/agents/rule-2.md: one of them, 48, is that pass in every build now; the rest came out.)
                // 16 and 32 (R19): the reflections' sort held (State::ReflectionsSort), the same picture.
                ((trial&48u)==16u ? "the reflections regroup by the material in every frame (the tracer's own choice held); " :
                 (trial&48u)==32u ? "the reflections regroup with no hint in every frame (the tracer's own choice held); " :
                 (trial&48u)==48u ? "16 and 32 together hold nothing: the reflections' sort is the tracer's own choice; " : "")+
                // (64 turned the water's rule off for a run's comparison on 2026-10-12: pt_water_sun.hlsli sun_cell_by_width.)
                ((trial&0xFFC0u) ? "the numbers "+std::to_string(trial&0xFFC0u)+" do nothing in this build (they are a measuring build's switches, and change the picture there)" : ""));
#endif
        }
    }
    // NVIDIA RTXDI's ReSTIR DI reuse, with the defaults of its ReSTIRDI.cpp except where noted: temporal history in
    // frames (the plugin's, 20 by default), permutation sampling with a new random number every frame, the boiling
    // filter at 0.2 (pt_boil.hlsli, for any wave width), its Basic bias correction for spatial reuse over the Spatial
    // Samples setting's neighbours within the spatial radius (its pairwise MIS let empty neighbours, out of every
    // lamp's range, take the pixel's own light's weight away), and no ray-traced bias correction (the shading pass
    // traces the final visibility, and pt_di drops a light its pixel doesn't see).
    k.rtxdiRuntime.neighborOffsetMask=kNeighborOffsets-1; k.rtxdiRuntime.activeCheckerboardField=0; k.rtxdiRuntime.frameIndex=f.frameIndex;
    k.rtxdiReservoirs.reservoirBlockRowPitch=st.rtxdiRowPitch; k.rtxdiReservoirs.reservoirArrayPitch=st.rtxdiArrayPitch;
    k.diTemporal.maxHistoryLength=f.diHistory ? std::clamp(f.diHistory, 1u, 64u) : 20u;
    // Basic bias correction; ray-traced (a neighbour counts only if it sees the chosen light) with the option that keeps
    // visibility in the reservoirs (pt_di.hlsl), which Basic's normalization would darken shadows' surroundings with.
    const bool diVisibility=(f.options&TGPT_OPTION_DI_VISIBILITY)!=0;
    k.diTemporal.biasCorrectionMode=diVisibility ? ReSTIRDI_TemporalBiasCorrectionMode::Raytraced : ReSTIRDI_TemporalBiasCorrectionMode::Basic;
    k.diTemporal.depthThreshold=0.1f; k.diTemporal.normalThreshold=0.5f;
    k.diTemporal.enableVisibilityShortcut=0; k.diTemporal.enablePermutationSampling=1;
    k.diTemporal.uniformRandomNumber=rtxdi::JenkinsHash(f.frameIndex);
    k.diBoiling.enableBoilingFilter=(f.options&TGPT_OPTION_NO_BOILING) ? 0u : 1u; k.diBoiling.boilingFilterStrength=0.2f;
    k.diSpatial.numSamples=std::min(f.spatialSamples, 16u); k.diSpatial.numDisocclusionBoostSamples=8;
    k.diSpatial.samplingRadius=k.params[3]; k.diSpatial.biasCorrectionMode=diVisibility ? ReSTIRDI_SpatialBiasCorrectionMode::Raytraced : ReSTIRDI_SpatialBiasCorrectionMode::Basic;
    k.diSpatial.depthThreshold=0.1f; k.diSpatial.normalThreshold=0.5f; k.diSpatial.targetHistoryLength=0;
    k.diSpatial.enableMaterialSimilarityTest=1;
    // Neighbours with a sample count of 2 or less are skipped as unconverged, which only means something with a longer
    // history: with 1 frame (or none) every neighbour has that count, and spatial reuse would do nothing.
    // Not discounted: RTXDI 3.1's Basic spatial reuse counts a neighbour it skips as naive in the MIS denominator all the
    // same, which darkened pixels for a moment after disocclusions.
    k.diSpatial.discountNaiveSamples=0u;
    // Its ReSTIR GI reuse, with the defaults of its ReSTIRGI.cpp except where noted: temporal history in frames (the
    // plugin's, 8 by default) over at most 30 frames of a sample's age, without the fallback to whatever surface was at
    // the pixel before in disocclusions (on in RTXDI's defaults, which call it biased, usually darker: surfaces a turn
    // of the camera reveals took their light from unrelated ones), permutation sampling (off in RTXDI's defaults; on
    // here, as for DI, for the denoisers), the boiling filter at 0.2 (its own pass: pt_gi_boil), and spatial reuse over
    // the Spatial Samples setting's neighbours within the spatial radius. The shading pass traces the final visibility.
    // TGPT_OPTION_GI_VISIBILITY (the Crevice Light row), measured against the plain path tracer on sand under a canvas
    // close over it (the harness's crevices): both reuses count a pixel for the chosen sample only if it sees it (RTXDI's
    // ray-traced bias correction, a shadow ray for each pixel counted), where Basic's counting them all left the canvas's
    // front edge 14 % short (the pixels further in don't see the sky through the slit under it); a history is kept
    // whatever its sample's age, where RTXDI drops the whole history at 30 frames, and in a dark place lit by rare paths
    // only the dark histories last that long: restarted, they counted the next lit path up to three times, and the sand
    // deep under the canvas came out 9 % too bright (17 % with 20 frames of history), while the history's own limit
    // already fades a stale sample by 8/9 a frame; and the boiling filter brings an outlier down to its group's bound
    // (pt_gi_boil). Together: the edge within 1 % and deeper in 3 to 9 % short, for more noise (in the harness's room the
    // denoised error is 15 to 30 % higher and a single frame's 11 %), so it stays a row, off, until it is seen in game.
    const bool giVisibility=(f.options&TGPT_OPTION_GI_VISIBILITY)!=0;
    const auto giBias=giVisibility ? RTXDI_GIBiasCorrectionMode::Raytraced : RTXDI_GIBiasCorrectionMode::Basic;
    k.giTemporal.depthThreshold=0.1f; k.giTemporal.normalThreshold=0.6f;
    k.giTemporal.maxHistoryLength=f.giHistory ? std::clamp(f.giHistory, 1u, 64u) : 8u;
    k.giTemporal.enableFallbackSampling=0; k.giTemporal.biasCorrectionMode=giBias;
    // A stored age stops at 255: the temporal pass never sees one above 256.
    k.giTemporal.maxReservoirAge=giVisibility ? 256u : 30u; k.giTemporal.enablePermutationSampling=1;
    k.giTemporal.uniformRandomNumber=rtxdi::JenkinsHash(f.frameIndex^0x9E3779B9u);
    k.giBoiling.enableBoilingFilter=(f.options&(TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_GI_BOILING)) ? 0u : 1u; k.giBoiling.boilingFilterStrength=0.2f;
    k.giSpatial.depthThreshold=0.1f; k.giSpatial.normalThreshold=0.6f;
    k.giSpatial.numSamples=std::min(f.spatialSamples, 16u); k.giSpatial.samplingRadius=k.params[3];
    k.giSpatial.biasCorrectionMode=giBias;
    // Reservoir arrays: this frame's and the previous one's, alternating. With spatial feedback (an option) they stay
    // put instead: temporal reuse writes array 1 from array 0, and spatial reuse writes its result into array 0, which
    // the next frame's temporal reuse reads; no pass reads and writes the same array.
    const bool feedback=(f.options&TGPT_OPTION_SPATIAL_FEEDBACK)!=0;
    const uint32_t rcur=feedback ? 1u : cur, rprev=feedback ? 0u : prev;
    k.rtxdi[0]=rcur; k.rtxdi[1]=rprev; k.rtxdi[2]=rcur; k.rtxdi[3]=rprev;
    k.surface[0]=st.surface[cur].uav; k.surface[1]=st.surface[prev].uav; k.surface[2]=st.normal[cur].uav; k.surface[3]=st.normal[prev].uav;
    k.material[0]=st.material[cur].uav; k.material[1]=st.giUav; k.material[2]=st.specSeen.uav; k.material[3]=st.diUav;
    // Hair the tracer's own camera rays find (pt_primary), where the scene has it: its strands' alpha from the strand atlas, read
    // with an upscaler's mip bias.
    k.hair[0]=st.hair.uav; k.hair[1]=st.scene.stats().hairLayerInstances>0 ? 1u : kInvalid; k.hair[2]=st.strandAtlas.srv;
    { const float bias=std::isfinite(f.forward[1]) ? std::clamp(f.forward[1], -4.f, 0.f) : 0.f; std::memcpy(&k.hair[3], &bias, 4); }
    k.textures[0]=st.sharedBase; k.textures[1]=st.scene.map_records_srv(); k.textures[2]=kInvalid; k.textures[3]=st.scene.programs_srv(); // PT-64
    std::memcpy(k.strandTiles, st.strandTiles, sizeof(k.strandTiles));
    k.signals[0]=st.cookieAtlas.srv; k.signals[1]=st.diffuseRaw.uav; k.signals[2]=st.sunRaw.uav; k.signals[3]=st.specRaw.uav;
    for(int i=0;i<3;++i) k.gridMin[i]=st.scene.gridMin[i];
    k.gridMin[3]=float(1.0/kGridCell);
    k.grid[0]=st.scene.gridCellsSrv; k.grid[1]=st.scene.gridIndicesSrv;
    k.grid[2]=kGridDims[0]|(kGridDims[1]<<8)|(kGridDims[2]<<16); k.grid[3]=std::clamp(f.raysPerPixel, 1u, 16u);
    k.denoise0[0]=st.diffuseHist[cur].uav; k.denoise0[1]=st.diffuseHist[prev].uav; k.denoise0[2]=st.diffuseMoments[cur].uav; k.denoise0[3]=st.diffuseMoments[prev].uav;
    const uint32_t finalDiffuse=!denoise ? st.diffuseRaw.uav : (atrousIterations%2 ? st.tmpA.uav : (atrousIterations ? st.tmpB.uav : st.diffuseHist[cur].uav));
    // The direct light (the sun and the lamps): its own temporal accumulation and two a-trous iterations, ending in directOut.
    k.denoise1[0]=denoiseOwnDirect ? st.directOut.uav : st.sunRaw.uav;
    k.denoise1[1]=kInvalid; // (not read since 2026-10-11: RELAX's directional form, which kept its direction here, is gone)
    // Reflections: temporal accumulation and four a-trous iterations (steps 1, 2, 4, 8), ending in D.
    k.denoise1[2]=denoiseOwn ? st.tmpD.uav : st.specRaw.uav; k.denoise1[3]=st.specHist[prev].uav;
    k.denoise2[0]=st.tmpA.uav; k.denoise2[1]=st.tmpB.uav; k.denoise2[2]=finalDiffuse; k.denoise2[3]=out.b.uav;
    // NRD's result for the bounced light.
    if(useNrd) k.denoise2[2]=st.nrdDiffuseOut.uav;
    // The radiance cache, unless the Radiance Cache setting is off.
    const bool cache=!(f.options&TGPT_OPTION_NO_RADIANCE_CACHE) && st.cache && st.cacheKeysUav!=kInvalid;
    k.cache[0]=st.cacheKeysUav; k.cache[1]=st.cacheSumsUav; k.cache[2]=st.cacheLightUav; k.cache[3]=cache ? 1u : 0u;
    for(int i=0;i<3;++i) k.cacheOrigin[i]=float(st.origin[i]);
    // Emissive triangles: their records this frame and the previous frame's (the same until a build writes the other buffer),
    // and what paths meeting one need to know whether its light was sampled at the vertex they left (the instance table).
    k.emissive[0]=st.scene.emittersSrv; k.emissive[1]=st.scene.previousEmittersSrv;
    k.emissive[2]=st.scene.emittersSrv!=kInvalid ? st.scene.emitterCount : 0u; k.emissive[3]=st.scene.previousEmittersSrv!=kInvalid ? st.scene.previousEmitterCount : 0u;
    k.emissive2[0]=st.scene.emitterTableSrv; k.emissive2[1]=st.scene.emitterTableSrv!=kInvalid ? st.scene.emitterTableInstances : 0u;
    k.emissive2[2]=st.scene.emittersSrv!=kInvalid ? st.scene.sampledClusters : 0u; k.emissive2[3]=st.scene.clusterBase;
    fill_sky(k, f); // PT-112, the sky, on trial
    std::memcpy(cpu, &k, sizeof(k));
    list->SetComputeRootConstantBufferView(0, gpu);
    list->SetComputeRootShaderResourceView(2, st.scene.tlas);

    // The shared textures are in COMMON between frames (D3D11 uses them in between) and return there.
    Barriers b;
    if(st.psr.records.resource) b.transition(st.psr.records, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    std::vector<Shared*> reads={&in.gb0, &in.nrm, &in.gb2, &in.gb3, &in.depth, &in.motion, &in.exposure, &st.sky, &st.atlas, &st.alphaAtlas, &st.cookieAtlas};
    if(st.strandAtlas.t.d12) reads.push_back(&st.strandAtlas);
    if(st.fogSky.t.d12) reads.push_back(&st.fogSky);
    const std::initializer_list<Shared*> writes={&out.a, &out.b, &out.c, &out.g0, &out.g1};
    for(Shared* s:reads) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(Shared* s:writes) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    b.flush(list);
    // Opacity micromaps for meshes whose cut-outs' textures are there to read now (the game's own, or the atlas's tiles once
    // they are in); their instances use them next frame.
    st.scene.structure_stamp(GpuScene::kStructureOther);
    st.scene.record_omm(st.d12.Get(), list, st.heap, st.release, retire, st.ommBake.Get());
    st.scene.structure_stamp(GpuScene::kStructureMicromaps);
    // The emissive triangles' records, when their emitters, the lights near them or the origin changed.
    st.scene.record_emitters(list, st.emissive.Get());
    b.flush(list);
    st.scene.structure_stamp(GpuScene::kStructureOther);
    // PT-109: what the scene counted of this frame's structures, for the log's "PT structures" line.
    st.scene.structureStamp=nullptr; st.scene.structureNote=nullptr;
    for(uint32_t kind=0;kind<GpuScene::kStructureKinds;++kind) {
        auto& from=st.scene.structureWork[kind]; auto& to=st.summaryStructWork[kind]; auto& recent=st.recentStructWork[kind];
        to.builds+=from.builds; to.refits+=from.refits; to.triangles+=from.triangles; to.bytes+=from.bytes;
        recent.builds+=from.builds; recent.refits+=from.refits; recent.triangles+=from.triangles; recent.bytes+=from.bytes;
        from=GpuScene::StructureWork{};
    }
    ++st.summaryStructRecorded; ++st.recentStructRecorded;
    mark(1); // the scene: its meshes, characters, micromaps, emitters and acceleration structures

    // PT-112, the sky, on trial (pt_sky.hlsl): the tracer's own sky before any pass reads it: its tables when the atmosphere
    // changed, the clouds' bake when HDRP would bake them, their ambient light and the sky map with its mips.
    record_sky(list, b);
    b.flush(list); mark(2);

    // Where the water has been (pt_wet.hlsl, PT-52), before the camera's surfaces read it: emptied with another scene and
    // before its first frame, then each cell's record of the water over it this frame.
    const uint32_t causticParts=caustic_parts();
    if(k.wet[0]!=kInvalid && !(causticParts&0x100u)) {
        if(!st.wetRan || st.wetScene!=st.scene.clear_generation()) dispatch(list, st.wetPass.Get(), {1u}, b, kWetGrid/8, kWetLevels*kWetGrid/8);
        dispatch(list, st.wetPass.Get(), {0u}, b, kWetGrid/8, k.wet[1]*kWetGrid/8);
        st.wetRan=true; st.wetScene=st.scene.clear_generation();
    }
    // The water's kept wave corners (pt_sun_grid.hlsl), before the first pass whose paths ask for the sun inside the water:
    // a slot's corners, then a record a tile of them, a record a row of tiles, and the whole grid's record.
    // Each stage for every slot, then one barrier: the slots' grids lie in ranges of their own (a slot's first byte), so only
    // a stage waits for the one before it (until 2026-10-11 every slot's every stage waited for the last: four barriers a
    // slot; docs/agents/review-perf-astra.md 9).
    if(st.sunGridSlots && !(causticParts&0x200u)) {
        const uint32_t groups[4][2]={{kSunCorners/8, kSunCorners/8}, {kSunTiles/8, kSunTiles/8}, {kSunTiles/8, 1}, {1, 1}};
        list->SetPipelineState(st.sunGridPass.Get());
        for(uint32_t stage=0;stage<4;++stage) {
            for(uint32_t i=0;i<TGPT_MAX_WATER;++i) if(st.sunGridSlots&(1u<<i)) {
                uint32_t values[8]; std::fill(std::begin(values), std::end(values), kInvalid); values[0]=i; values[1]=stage;
                list->SetComputeRoot32BitConstants(1, 8, values, 0);
                list->Dispatch(groups[stage][0], groups[stage][1], 1);
            }
            b.uav(nullptr); b.flush(list);
        }
    }
    // BEFORE EVERY PASS THAT READS THEM (since 2026-10-11, PT-130): the bounced light's and the reflections' paths read the
    // caustics' landed light at solid points under water (pt_water_path), and ran before this block until then: a ray saw
    // the frame before's caustics. The pass reads the scene, the lights and the water's waves alone, nothing of the
    // camera's surfaces. (NRD as its documentation sets it up takes the caustics with all the diffuse light, pt_nrd_prep.)
    // The caustics (pt_caustics.hlsl): the counters emptied, then the light traced off and through every slot's water from its
    // grid (a group a tile of 15 x 15 cells, per level and slot), the room past this frame's triangles made inactive, and their
    // acceleration structure built (its one instance's top level too), for caustic_light to ask where the light landed.
    if(k.caustics[0]!=kInvalid) {
        dispatch(list, st.causticPass.Get(), {0u}, b, 1, 1);
        const uint32_t tiles=(kCausticGrid+14)/15, levels=uint32_t(k.causticGrid[2]);
        const uint32_t parts=causticParts;
        // PT-114, on trial (TGPT_FRAME_CAUSTICS_GRID): a group for each tile that can do anything (caustic_tile_list), where
        // a group runs for every tile of every level and slot.
        uint32_t listSrv=kInvalid, listed=0;
        // (The trial's steps as the pass's own switches: what a step leaves out is as it was before.)
        uint32_t cutsOff=caustic_cuts_off();
        if(!(f.flags&TGPT_FRAME_CAUSTICS_GRID_WATER)) cutsOff|=2u|32u;
        if(!(f.flags&TGPT_FRAME_CAUSTICS_GRID_VIEW) || (cutsOff&32u)) cutsOff|=1u;
        const bool cut=(f.flags&TGPT_FRAME_CAUSTICS_GRID) && caustic_tile_list(index, k, levels, !(cutsOff&32u), listSrv, listed);
        const int step=!cut ? 0 : (cutsOff&32u) ? 1 : (cutsOff&1u) ? 2 : 3;
        if(step!=st.causticListSaid) {
            st.causticListSaid=step;
            log(step==0 ? std::string("The caustics' grid: every tile of every level and slot is run and picks its own lights (the trial of PT-114 is off)")
                        : std::string("The caustics' grid, on trial (PT-114), step ")+std::to_string(step)+" of 3: the tiles' lights are picked in a pass of their own, a thread a tile"+
                          (step>=2 ? "; only the tiles with their slot's level water under them are run, and no corner that no cell needs" : "")+
                          (step>=3 ? "; a tile's light whose rays cannot land in view is not traced" : ""));
        }
        uint32_t values[8]; std::fill(std::begin(values), std::end(values), kInvalid); values[0]=1u; values[1]=parts&15u;
        if(cut && listed) {
            values[2]=listSrv; values[3]=cutsOff;
            // The lights' pick of every listed tile, in a pass of its own (a thread a tile), before the tiles' pass reads it.
            if(!(cutsOff&16u) && !(parts&0x1000u)) {
                constexpr uint64_t kPickBytes=uint64_t((kCausticGrid+14)/15)*((kCausticGrid+14)/15)*kCausticLevels*TGPT_MAX_WATER*16;
                if(st.causticPicksUav==kInvalid) {
                    try {
                        st.causticPicks=make_buffer(st.d12.Get(), kPickBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the caustics' picks");
                        st.causticPicksUav=raw_uav(st.d12.Get(), st.heap, st.causticPicks.resource.Get(), kPickBytes);
                    } catch(const DxError& e) {
                        // Without the room each tile's group picks its lights itself: the same lights, slower.
                        log_throttled(std::string("The caustics' picks could not be made (")+e.what()+"); each tile picks its lights itself");
                        st.causticPicks=Buffer{}; st.causticPicksUav=kInvalid;
                    }
                }
                if(st.causticPicksUav!=kInvalid) {
                    uint32_t pick[8]; std::fill(std::begin(pick), std::end(pick), kInvalid);
                    pick[0]=3u; pick[2]=listSrv; pick[4]=listed; pick[5]=st.causticPicksUav;
                    list->SetPipelineState(st.causticPass.Get());
                    list->SetComputeRoot32BitConstants(1, 8, pick, 0);
                    list->Dispatch((listed+255)/256, 1, 1);
                    b.uav(nullptr); b.flush(list);
                    values[4]=listed; values[5]=st.causticPicksUav;
                }
            }
        }
        list->SetPipelineState(st.causticPass.Get());
        list->SetComputeRoot32BitConstants(1, 8, values, 0);
        if(parts&0x1000u) { }
        else if(!cut) list->Dispatch(tiles, tiles, levels*TGPT_MAX_WATER);
        else if(listed) list->Dispatch(listed, 1, 1);
        b.uav(nullptr); b.flush(list);
        if(!(parts&0x400u)) dispatch(list, st.causticPass.Get(), {2u}, b, (st.causticBuilt+255)/256, 1);
        b.transition(st.causticCorners, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); b.flush(list);
        if(!(parts&0x800u)) {
            const D3D12_RAYTRACING_GEOMETRY_DESC geometry=caustic_geometry(st.causticBuilt);
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{}; build.Inputs=caustic_inputs(&geometry);
            build.DestAccelerationStructureData=st.causticBlas.va(); build.ScratchAccelerationStructureData=st.causticBlasScratch.va();
            list->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
            b.uav(st.causticBlas.resource.Get()); b.flush(list);
            // The top level's one instance (the bottom level as it is, both faces seen: caustic_light judges a triangle's side
            // itself), in this frame's ring.
            D3D12_RAYTRACING_INSTANCE_DESC instance{}; instance.Transform[0][0]=instance.Transform[1][1]=instance.Transform[2][2]=1.f;
            instance.InstanceMask=0xFF; instance.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE; instance.AccelerationStructure=st.causticBlas.va();
            void* mapped=nullptr; D3D12_GPU_VIRTUAL_ADDRESS at=0;
            if(!st.rings[index].allocate(sizeof(instance), 16, &mapped, &at)) throw DxError(E_OUTOFMEMORY, "The caustics' instance found no room in the frame's ring");
            std::memcpy(mapped, &instance, sizeof(instance));
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top{}; top.Inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
            top.Inputs.NumDescs=1; top.Inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; top.Inputs.InstanceDescs=at;
            top.DestAccelerationStructureData=st.causticTlas.va(); top.ScratchAccelerationStructureData=st.causticTlasScratch.va();
            list->BuildRaytracingAccelerationStructure(&top, 0, nullptr);
            b.uav(st.causticTlas.resource.Get());
            st.causticStructure=st.causticBuilt; // what the top level names from here on (ensure_caustic_room)
        }
        b.transition(st.causticCorners, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
        for(int a=0;a<3;++a) st.causticOrigin[index][size_t(a)]=st.origin[a];
        st.causticFrame[index]=f.frameIndex;
        b.transition(st.causticCounters, D3D12_RESOURCE_STATE_COPY_SOURCE); b.flush(list);
        list->CopyBufferRegion(st.causticReadback.resource.Get(), uint64_t(index)*kCausticCounters*4, st.causticCounters.resource.Get(), 0, kCausticCounters*4);
        b.transition(st.causticCounters, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
        st.causticRead[index]=true;
    }
    b.flush(list); mark(3);
    // The pixels' surfaces: HDRP's G-buffer, and the tracer's own camera rays where hair is (pt_primary), which also write
    // the guides of the hair each ray crossed into the tracer's guide outputs (pt_compose lays the rest under them).
    dispatch(list, st.prepare.Get(), {out.g0.uav, out.g1.uav}, b);
    dispatch(list, st.probe.Get(), {st.probeUav}, b, 1, 1);
    b.flush(list); mark(4);
    // The radiance cache starts empty with another scene (TGPT_ClearScene) and when it is turned back on (its light
    // stopped following the scene's while it was off). Its cells are the world's: a camera cut or a quick turn keeps
    // them, which would otherwise darken the bounced light for the half second they take to fill again.
    if(cache && (!st.cacheRan || st.cacheScene!=st.scene.clear_generation())) dispatch(list, st.cache.Get(), {1u}, b, kCacheSlots/256, 1);
    st.cacheRan=cache; st.cacheScene=st.scene.clear_generation();
    // The bounced light: the rays and their shading (pt_gi; with Shader Execution Reordering a ray generation pass that
    // regroups its threads once), then ReSTIR GI's reuse across frames in the screen's order (pt_gi_reuse), which also
    // turns pt_gi's stored samples into RTXDI's own records: it always follows pt_gi (PT-99, PT-110, docs/agents/rule-2.md).
    // Both inside the bounced light's own stage, so that its time is the two passes'.
    dispatch_pass(list, st.giSer.Get(), 0, st.gi.Get(), b);
    dispatch(list, st.giReuse.Get(), {}, b);
    b.flush(list); mark(5);
    if(k.giBoiling.enableBoilingFilter) dispatch(list, st.giBoil.Get(), {}, b);
    dispatch(list, st.giSpatial.Get(), {}, b);
    b.flush(list); mark(6);
    dispatch(list, st.di.Get(), {}, b);
    dispatch(list, st.diSpatial.Get(), {}, b);
    // Light inside subsurface materials (pt_walk.hlsl: wax, skin): a random walk from each such pixel into its traced mesh,
    // lit where it comes out, in place of the pixel's direct and bounced diffuse light (both signals, before any denoiser).
    // A pixel walks by the material its own camera ray met (TGPT_MATERIAL_SUBSURFACE, PT-59): the pass runs while the scene
    // holds such a material. (On the route before the tracer's own rays nothing walks: HDRP's G-buffer is no longer asked.)
    const bool walkOwn=(!(f.options&TGPT_OPTION_GBUFFER_SURFACES) || f.debugView==TGPT_VIEW_YARDSTICK) && st.scene.stats().subsurfaceMaterials>0;
    if(walkOwn && st.walk && !(f.options&TGPT_OPTION_NO_SUBSURFACE)) dispatch(list, st.walk.Get(), {}, b);
    b.flush(list); mark(7);
    dispatch_pass(list, st.specularSer.Get(), 1, st.specular.Get(), b); // dielectric transmission is always part of a surface
    b.flush(list); mark(8);
    // Every pass that reads or feeds the radiance cache has run: its cells take this frame's samples in.
    if(cache) dispatch(list, st.cache.Get(), {0u}, b, kCacheSlots/256, 1);
    b.flush(list); mark(9);
    if(denoise) {
        dispatch(list, st.temporal.Get(), {st.diffuseRaw.uav, st.diffuseHist[cur].uav, st.diffuseHist[prev].uav, st.diffuseMoments[cur].uav,
                                           st.diffuseMoments[prev].uav, 32u, 1u, 0u}, b);
        // The second iteration feeds the first one's result back into this frame's history (SVGF).
        uint32_t input=st.diffuseHist[cur].uav;
        for(uint32_t i=0;i<atrousIterations;++i) {
            const uint32_t output=(i%2==0) ? st.tmpA.uav : st.tmpB.uav;
            dispatch(list, st.atrous.Get(), {input, output, 1u<<i, 0u, i==1 ? st.diffuseHist[cur].uav : kInvalid}, b);
            input=output;
        }
    }
    if(denoiseOwn) {
        // The direct light keeps its shadows: its own variance steers its filter, two short iterations (steps 1, 2).
        if(denoiseOwnDirect) {
            dispatch(list, st.temporal.Get(), {st.sunRaw.uav, st.directHist[cur].uav, st.directHist[prev].uav, st.directMoments[cur].uav,
                                               st.directMoments[prev].uav, 16u, 1u, 0u}, b);
            dispatch(list, st.atrous.Get(), {st.directHist[cur].uav, st.directTmp.uav, 1u, 0u, kInvalid}, b);
            dispatch(list, st.atrous.Get(), {st.directTmp.uav, st.directOut.uav, 2u, 0u, st.directHist[cur].uav}, b);
        }
        // The reflections (the last 1): a mirror's history is fetched where its picture was (pt_temporal).
        dispatch(list, st.temporal.Get(), {st.specRaw.uav, st.specHist[cur].uav, st.specHist[prev].uav, st.specMoments[cur].uav,
                                           st.specMoments[prev].uav, 16u, 1u, 1u}, b);
        // ON TRIAL (PT-95, PT-19, docs/agents/nrd-relief.md; TGPT_Frame.prevCameraPosition's w, the plugin's hidden
        // "Reflection relief trial", 0 from every other caller; its bits 0 to 2, nothing else of that word is read): a
        // hammered iron helmet came out smooth with NRD, because a metal's light is all in this signal and its filter
        // spread every bump's reflection over its neighbours. Handed to the four passes as their sixth constant
        // (pt_atrous pass.b.y: 1 the normals' term is the lobe's own, 2 a neighbour counts as far as it reflects alike,
        // 4 the first pass's result is not fed back into the history; 0: everything as before). No frame constant and
        // no option bit.
        const uint32_t relief=std::isfinite(f.prevCameraPosition[3]) && f.prevCameraPosition[3]>0.f ? uint32_t(std::min(f.prevCameraPosition[3], 524287.f)+0.5f)&7u : 0u; // the rest of the word: PT-99's speed trial, times 8
        {
            static uint32_t said=~0u;
            if(said!=relief) {
                said=relief;
                log(relief==0 ? std::string("Reflections' filter on trial: off (its weights and its feedback as before)") :
                    "Reflections' filter on trial ("+std::to_string(relief)+"): "+
                    ((relief&4u) ? "a surface's history keeps its own samples (the first pass is not fed back into it); " : "")+
                    ((relief&1u) ? "two pixels exchange a reflection only as far as their normals gather the same light (by the lobe); " : "")+
                    ((relief&2u) ? "and only as far as they reflect alike (their specular colour)" : ""));
            }
        }
        // (With NRD, which takes every other surface's reflections, the passes filter glass and water alone: pass.b.z 1,
        // pt_atrous; review-vendor-astra 10, 2026-10-11.)
        const uint32_t onlyThrough=useNrd ? 1u : 0u;
        dispatch(list, st.atrous.Get(), {st.specHist[cur].uav, st.tmpC.uav, 1u, 1u, kInvalid, relief, onlyThrough}, b);
        dispatch(list, st.atrous.Get(), {st.tmpC.uav, st.tmpD.uav, 2u, 1u, st.specHist[cur].uav, relief, onlyThrough}, b);
        dispatch(list, st.atrous.Get(), {st.tmpD.uav, st.tmpC.uav, 4u, 1u, kInvalid, relief, onlyThrough}, b);
        dispatch(list, st.atrous.Get(), {st.tmpC.uav, st.tmpD.uav, 8u, 1u, kInvalid, relief, onlyThrough}, b);
    }
    st.rrgActive=false;
    if(rrg) {
        // AMD has no exposure/history-rescale input. Keep one physical radiance unit throughout each history.
        const bool previous=st.rrgHavePrevious && f.frameIndex==st.rrgLastFrame+1u && !reset;
        const double ratio=st.rrgScale>0.0 ? st.reference/st.rrgScale : 0.0;
        const bool restart=!previous || !(ratio>=0.25 && ratio<=4.0);
        if(restart) st.rrgScale=std::max(st.reference, 1e-6);
        const float toStable=float(st.reference/st.rrgScale);
        uint32_t stableBits=0; std::memcpy(&stableBits, &toStable, sizeof stableBits);
        dispatch(list, st.rrgGuides.Get(), {st.rrgDepth.uav, st.rrgMotion.uav, st.rrgNormals.uav, st.rrgDiffuse.uav, st.rrgSpecular.uav, stableBits, 0u}, b);
        RrgFrame r;
        r.width=st.width; r.height=st.height; r.frameIndex=f.frameIndex; r.reset=restart;
        r.linearDepth=st.rrgDepth.resource.Get(); r.motion=st.rrgMotion.resource.Get(); r.normals=st.rrgNormals.resource.Get();
        r.diffuseAlbedo=st.rrgDiffuse.resource.Get(); r.specularAlbedo=st.rrgSpecular.resource.Get();
        r.indirectDiffuse=st.diffuseRaw.resource.Get(); r.directDiffuse=st.sunRaw.resource.Get(); r.indirectSpecular=st.specRaw.resource.Get();
        // HDRP's matrices in Unity's memory order are the SDK's row-major, row-vector ones as they stand;
        // its jitter is negated as for AMD's upscaler.
        std::memcpy(r.view, f.view, sizeof(r.view)); std::memcpy(r.projection, f.nonJitteredProj, sizeof(r.projection));
        r.jitter[0]=-f.jitter[0]; r.jitter[1]=-f.jitter[1];
        for(int i=0;i<3;++i) r.cameraDelta[i]=f.prevCameraPosition[i]-f.cameraPosition[i];
        std::string error;
        st.rrgActive=st.rrg.dispatch(list, r, error);
        // AMD's backend binds its own descriptor heap and root signature: the tracer's go back.
        list->SetDescriptorHeaps(2, heaps);
        list->SetComputeRootSignature(st.root.Get());
        list->SetComputeRootConstantBufferView(0, gpu);
        list->SetComputeRootShaderResourceView(2, st.scene.tlas);
        b.uav(nullptr); b.flush(list);
        // Restore the same radiance and specular factors even on dispatch failure; distance/alpha never scale.
        dispatch(list, st.rrgGuides.Get(), {st.rrgDepth.uav, st.rrgMotion.uav, st.rrgNormals.uav, st.rrgDiffuse.uav, st.rrgSpecular.uav, stableBits, 1u}, b);
        st.rrgHavePrevious=st.rrgActive; st.rrgLastFrame=f.frameIndex;
        if(!st.rrgActive) {
            st.rrgFailed=true;
            log("AMD Ray Regeneration failed ("+error+"); the built-in denoiser serves from the next frame on");
        }
    } else st.rrgHavePrevious=false;
    st.nrdActive=false;
    uint32_t nrdToStorage=kInvalid; // compose: NRD's result into the tracer's storage units (a float's bits)
    if(!useNrd && st.nrdConfidence.samples) {
        st.nrdConfidence.release(st.heap, st.release, st.consumer.value);
        log("NRD's history confidence: NRD is off, its resources retired");
    }
    if(useNrd) {
        // NRD's radiance in units of its own: the tracer stores light divided by the scene's reference, which eases as
        // lights come and go, and NRD's history can't be rescaled (the built-in denoiser's is, pt_temporal). NRD's 1 stands
        // for twice the bounced light's median as NRD was handed it a few frames back (pt_nrd_prep's histogram), which puts
        // that median at a half, where RELAX's half floats hold the light's square for nine EV above and six below (pt_nrd_prep
        // says why). Measured against the reference instead (the brightest light 1 m away, or the sun), a room's bounced
        // light came out hundreds of times below 1, its square below what a half float holds, and RELAX's filter stopped at
        // its noise. The scale holds until NRD's history starts over: on a cut, and when the median has left 1/16 to 2 of it
        // (a stand-in until the first median: a hundredth of the reference).
        const bool previous=st.nrdHavePrevious && !reset;
        const double median=st.nrdMedian>0.0 && st.nrdScale>0.0 ? st.nrdMedian/st.nrdScale : 0.5;
        const bool restart=!previous || !(st.nrdScale>0.0) || !(median>=1.0/16.0 && median<=2.0);
        if(restart) st.nrdScale=st.nrdMedian>0.0 ? 2.0*st.nrdMedian : 0.01*st.reference;
        const double ratio=st.reference/st.nrdScale;
        const float toNrd=float(ratio), fromNrd=float(1.0/ratio);
        uint32_t toNrdBits; std::memcpy(&toNrdBits, &toNrd, 4);
        dispatch(list, nrdPrep, {kInvalid, kInvalid, kInvalid, kInvalid, 0u, st.meterUav, 0u}, b, 1, 1);
        // The front end (pt_nrd_prep mode 1 RELAX, 2 REBLUR): the diffuse light into nrdDiffuseIn, the specular into nrdSpecularIn.
        dispatch(list, nrdPrep, {st.nrdViewZ.uav, st.nrdMotion.uav, st.nrdNormalRoughness.uav, st.nrdDiffuseIn.uav, toNrdBits, st.meterUav,
                                 nrdKind==2 ? 2u : 1u, st.nrdSpecularIn.uav}, b);
        st.nrdSlotScale[index]=st.nrdScale;
        NrdFrame n;
        n.width=st.width; n.height=st.height; n.frameIndex=f.frameIndex;
        n.reset=restart;
        n.kind=nrdKind;
        n.primarySurfaceReplacement=psrRecords && st.psr.uav!=kInvalid;
        {
            const auto now=std::chrono::steady_clock::now();
            const double since=previous ? std::chrono::duration<double>(now-st.nrdLastAt).count() : 0.0;
            if(since>0.0 && since<2.0) st.nrdFrameSeconds=st.nrdFrameSeconds>0.0 ? 0.9*st.nrdFrameSeconds+0.1*since : since;
            st.nrdLastAt=now;
            n.frameRate=st.nrdFrameSeconds>0.0 ? float(1.0/st.nrdFrameSeconds) : 0.f;
        }
        // This frame's camera and the previous one's: HDRP's rotation and projection without jitter, and its jitter.
        nrd_view(f.view, cam, n.worldToView);
        if(previous) nrd_view(st.nrdView, st.nrdCamera, n.worldToViewPrev); else std::memcpy(n.worldToViewPrev, n.worldToView, sizeof(n.worldToView));
        std::memcpy(n.viewToClip, f.nonJitteredProj, sizeof(n.viewToClip));
        std::memcpy(n.viewToClipPrev, previous ? st.nrdProjection : f.nonJitteredProj, sizeof(n.viewToClipPrev));
        for(int i=0;i<2;++i) { n.jitter[i]=f.jitter[i]; n.jitterPrev[i]=previous ? st.nrdJitter[i] : f.jitter[i]; }
        auto tex=[](Texture& t) { return NrdTexture{t.resource.Get(), &t.state, t.format}; };
        if(record_nrd_confidence(list, b, f, restart, nrdKind)) {
            n.diffuseConfidence=tex(st.nrdConfidence.confidence);
            n.specularConfidence=n.diffuseConfidence;
        }
        n.viewZ=tex(st.nrdViewZ); n.motion=tex(st.nrdMotion); n.normalRoughness=tex(st.nrdNormalRoughness);
        n.diffuse=tex(st.nrdDiffuseIn); n.diffuseOut=tex(st.nrdDiffuseOut); n.specular=tex(st.nrdSpecularIn); n.specularOut=tex(st.nrdSpecularOut);
        std::string error;
        st.nrdActive=st.nrd.dispatch(list, index, st.rings[index], n, error);
        // NRD binds its own descriptor heap and root signature: the tracer's go back.
        list->SetDescriptorHeaps(2, heaps);
        list->SetComputeRootSignature(st.root.Get());
        list->SetComputeRootConstantBufferView(0, gpu);
        list->SetComputeRootShaderResourceView(2, st.scene.tlas);
        b.uav(nullptr); b.flush(list);
        if(st.nrdActive) {
            std::memcpy(&nrdToStorage, &fromNrd, 4);
            // The back end (pt_nrd_prep mode 3 RELAX, 4 REBLUR): the diffuse result as linear light, and the specular one into the
            // reflections' own result wherever NRD took the pixel, which the composite takes as it is.
            dispatch(list, nrdPrep, {st.nrdDiffuseOut.uav, st.nrdSpecularOut.uav, st.tmpD.uav, kInvalid, nrdToStorage, kInvalid, nrdKind==2 ? 4u : 3u}, b);
        }
        else {
            // Compose takes this frame's bounced light as traced (NRD recorded nothing: its checks run before any command).
            b.transition(st.diffuseRaw, D3D12_RESOURCE_STATE_COPY_SOURCE); b.transition(st.nrdDiffuseOut, D3D12_RESOURCE_STATE_COPY_DEST); b.flush(list);
            list->CopyResource(st.nrdDiffuseOut.resource.Get(), st.diffuseRaw.resource.Get());
            b.transition(st.diffuseRaw, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.transition(st.nrdDiffuseOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
            st.nrdFailed=true;
            log("NVIDIA NRD failed ("+error+"); the built-in denoiser serves from the next frame on");
        }
        std::memcpy(st.nrdView, f.view, sizeof(st.nrdView)); std::memcpy(st.nrdProjection, f.nonJitteredProj, sizeof(st.nrdProjection));
        for(int i=0;i<2;++i) st.nrdJitter[i]=f.jitter[i];
        for(int i=0;i<3;++i) st.nrdCamera[i]=cam[i];
        st.nrdHavePrevious=true;
    } else { st.nrdHavePrevious=false; }
    b.flush(list); mark(10);
    // The reflections Ray Regeneration's guides divided by their albedo are multiplied back in. Ray Reconstruction's guides where
    // the tracer knows the pixel better than the GBuffer (pt_compose traced_guides), over the hair layer's when it ran.
    // The game's fog, traced (TGPT_FRAME_TRACED_FOG; fog_froxel.inl: the cells, each pixel's walk through them, the lamps'
    // light right by them), which the composite lays over the picture.
    uint32_t fogUav=kInvalid;
    if((k.size[3]&8u)!=0u) {
        if(!st.tracedFog.resource) {
            try { st.tracedFog=make_texture(st.d12.Get(), st.heap, st.width, st.height, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create the traced fog"); }
            catch(const std::exception& e) { log_throttled(std::string("The traced fog's target could not be made: ")+e.what()); st.tracedFog={}; }
        }
        if(st.tracedFog.resource && record_fog_froxels(list, b, f, reset, k, st.tracedFog.uav, mark)) fogUav=st.tracedFog.uav;
    }
    if(fogUav==kInvalid) { mark(11); mark(12); mark(13); }
    {
        static int said=-1;
        const int now=fogUav!=kInvalid ? 1 : 0;
        if(now!=said) { said=now; log(now==1 ? "The game's fog traced in cells along the camera's rays (HDRP's opaque fog pass left out)" :
            (k.size[3]&8u)!=0u ? "The game's fog: NOT traced this frame (its cells or its target could not be made)" : "The game's fog: none this frame (a debug view)"); }
    }
    dispatch(list, st.compose.Get(), {(st.nrdActive ? 2u : 0u), out.c.uav, nrdToStorage, out.g0.uav, out.g1.uav, out.dm.uav, out.mv.uav, fogUav}, b);
    // The traced light as shown, for its gain: every other pixel into the histogram (cleared first).
    dispatch(list, st.meter.Get(), {st.meterUav, 0u}, b, 1, 1);
    dispatch(list, st.meter.Get(), {st.meterUav, 1u}, b, (st.width/4+8)/8, (st.height/4+8)/8); // a thread a 4 x 4 block (pt_meter)
    // The yardstick's numbers (pt_yardstick.hlsl): every 30th frame while the tracer's own camera rays run, every frame of its
    // view. pt_prepare left each pixel's word in those frames.
    const bool ownRays=!(f.options&TGPT_OPTION_GBUFFER_SURFACES) || f.debugView==TGPT_VIEW_YARDSTICK;
    // (Not while HDRP draws nothing under the traced picture, PT-112 A: its G-buffer is empty, the numbers would say nothing.)
    const bool yardCount=ownRays && st.yardPass && st.yardstick.uav!=kInvalid &&
                         (f.debugView==TGPT_VIEW_YARDSTICK || (f.frameIndex%30==0 && !(f.flags&TGPT_FRAME_OWN_DEPTH)));
    if(yardCount) {
        dispatch(list, st.yardPass.Get(), {st.yardUav, 0u}, b, 129, 3); // 1032 by 24 threads: every counter
        dispatch(list, st.yardPass.Get(), {st.yardUav, 1u}, b);
    }

    for(Shared* s:reads) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    for(Shared* s:writes) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    b.flush(list);
    b.transition(st.probeBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE); b.flush(list);
    list->CopyBufferRegion(st.probeReadback.resource.Get(), uint64_t(index)*48, st.probeBuffer.resource.Get(), 0, 48);
    b.transition(st.probeBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
    b.transition(st.meterBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE); b.flush(list);
    list->CopyBufferRegion(st.meterReadback.resource.Get(), uint64_t(index)*State::kMeterBins*4, st.meterBuffer.resource.Get(), 0, State::kMeterBins*4);
    b.transition(st.meterBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
#ifdef TGPT_SPEED_MEASURE
    if(st.speedCounters.resource && st.speedReadback.resource) { // a measuring build's counters (trace's start reads this slot back)
        b.transition(st.speedCounters, D3D12_RESOURCE_STATE_COPY_SOURCE); b.flush(list);
        list->CopyBufferRegion(st.speedReadback.resource.Get(), uint64_t(index)*kSpeedCounters*4, st.speedCounters.resource.Get(), 0, kSpeedCounters*4);
        b.transition(st.speedCounters, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
        st.speedRead[index]=true;
    }
#endif
    if(yardCount) {
        b.transition(st.yardBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE); b.flush(list);
        list->CopyBufferRegion(st.yardReadback.resource.Get(), uint64_t(index)*kYardCount*4, st.yardBuffer.resource.Get(), 0, kYardCount*4);
        b.transition(st.yardBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
        st.yardSlot[index]=std::max(f.frameIndex, 1u); st.yardSlotOwn[index]=!(f.options&TGPT_OPTION_GBUFFER_SURFACES);
    }
    mark(kStages);
    list->ResolveQueryData(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index*kMarks, kMarks, st.queryReadback.resource.Get(), uint64_t(index)*kMarks*8);
    if(st.structQueries && st.structReadback && st.structCount[index]) {
        list->ResolveQueryData(st.structQueries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index*kStructMarks, st.structCount[index], st.structReadback.resource.Get(), uint64_t(index)*kStructMarks*8);
        st.structResolved[index]=st.structCount[index];
    }
    check(list->Close(), "Close path tracing list");
    ID3D12CommandList* lists[]={list};
    const double countClosed=countStages ? windows_counts_mb(st.d12.Get()) : 0.0;
    st.queue->ExecuteCommandLists(1, lists);
    if(countStages) {
        const double handed=windows_counts_mb(st.d12.Get());
        std::string text="PT memory at start: traced frame "+std::to_string(tally.frames+1)+"'s list in Windows' count, as each stage's commands were recorded (MB):";
        char part[160];
        for(uint32_t s=0;s<kStages;++s) { std::snprintf(part, sizeof part, "%s %s %+.0f", s ? "," : "", kStageNames[s], stageCount[s+1]-stageCount[s]); text+=part; }
        std::snprintf(part, sizeof part, "; closing it %+.0f; handing it to the GPU %+.0f (Windows counts %.0f MB)", countClosed-stageCount[kStages], handed-countClosed, handed);
        log(text+part);
    }
    st.recording=false;
    st.queried[index]=true;
    st.phase[3]=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-recordStart).count();
    st.parity=1-st.parity;
    st.retired[index]=retire;
    return retire;
}

// What the waves were where the tracer read them since the last summary (pt_caustics's counters), per slot that had water under
// its grid: the proof that HDRP's waves arrive (a slot traced as still water, as every slot was until 2026-10-07, shows 0.0 cm
// and flat), with one grid point's surface for a look at the numbers.
std::string wave_text() {
    std::string text;
    for(uint32_t i=0;i<TGPT_MAX_WATER;++i) {
        const auto& w=st.causticWaves[i];
        if(!w.frames) continue;
        char b[320];
        std::snprintf(b, sizeof(b), "%s slot %u waves up to %.1f cm high, slopes to %.3f (at x %.2f z %.2f the surface is at %.3f %.3f %.3f, normal %.3f %.3f %.3f)",
                      text.empty() ? "; water as the tracer reads it:" : ",", i, double(w.high)*100.0, double(w.steep),
                      w.origin[0]+double(w.sample[0]), w.origin[2]+double(w.sample[1]), w.origin[0]+double(w.sample[2]), w.origin[1]+double(w.sample[3]),
                      w.origin[2]+double(w.sample[4]), double(w.sample[5]), double(w.sample[6]), double(w.sample[7]));
        text+=b;
    }
    return text;
}
// The caustics' tiles a frame since the last status line (PT-114; pt_caustics.hlsl's counters): how many ran, how many of
// them had a corner on their slot's water, in how many a light reached a corner, how many wrote a triangle; then the same
// four per level (from the finest) and per slot. A tile that ran and wrote nothing did its corners' work for nothing in view.
std::string tiles_text() {
    if(!st.causticFrames) return std::string();
    const double frames=double(st.causticFrames);
    double all[kCausticTileKinds]={}; std::string levels, slots;
    for(uint32_t l=0;l<kCausticLevels;++l) {
        double v[kCausticTileKinds]={};
        for(uint32_t i=0;i<kCausticTileKinds;++i) for(uint32_t s=0;s<TGPT_MAX_WATER;++s) v[i]+=double(st.causticTiles[i][l*TGPT_MAX_WATER+s]);
        for(uint32_t i=0;i<kCausticTileKinds;++i) all[i]+=v[i];
        if(!(v[0]>0)) continue;
        char b[112]; std::snprintf(b, sizeof(b), "%s%u: %.0f / %.0f / %.0f / %.0f", levels.empty() ? "" : ", ", l, v[0]/frames, v[1]/frames, v[2]/frames, v[3]/frames);
        levels+=b;
    }
    for(uint32_t s=0;s<TGPT_MAX_WATER;++s) {
        double v[kCausticTileKinds]={};
        for(uint32_t i=0;i<kCausticTileKinds;++i) for(uint32_t l=0;l<kCausticLevels;++l) v[i]+=double(st.causticTiles[i][l*TGPT_MAX_WATER+s]);
        if(!(v[0]>0)) continue;
        char b[112]; std::snprintf(b, sizeof(b), "%s%u: %.0f / %.0f / %.0f / %.0f", slots.empty() ? "" : ", ", s, v[0]/frames, v[1]/frames, v[2]/frames, v[3]/frames);
        slots+=b;
    }
    char b[240];
    std::snprintf(b, sizeof(b), "; caustics' tiles a frame: %.0f ran, %.0f on water, a light reached %.0f, %.0f wrote a triangle (ran / on water / lit / wrote per level ",
                  all[0]/frames, all[1]/frames, all[2]/frames, all[3]/frames);
    std::string cut;
    if(st.causticListFrames) {
        char c[320];
        std::snprintf(c, sizeof(c), "; on trial (PT-114), of %llu frames: %.0f tiles a frame listed, %.0f left out with no water of their slot under them (the list took %.3f ms a frame on the CPU)",
                      (unsigned long long)st.causticListFrames, double(st.causticListed)/double(st.causticListFrames), double(st.causticDry)/double(st.causticListFrames),
                      st.causticListMs/double(st.causticListFrames));
        cut=c;
    }
    return b+levels+"; per slot "+slots+")"+cut;
}
// The sun's light under the water as the caustics landed it since the last status line (pt_caustics.hlsl's counters): of
// what the grid's triangles in view carried to what their corners met, the share on triangles whose corners met different
// surfaces (left out, unless the trial lands it: TGPT_OPTION_CAUSTICS_SPLIT) and the share of the whole that landed; then
// the same two per level, from the finest. A level's sums are in units of its own cell, so the whole weighs level l by 4^l.
std::string split_text() {
    const auto& sums=st.causticSums;
    double carried=0, split=0, landed=0;
    std::string levels, ripple;
    for(uint32_t l=0;l<kCausticLevels;++l) {
        const double weight=std::ldexp(1.0, int(2*l)), c=double(sums[0][l]), s=double(sums[1][l]), t=double(sums[2][l]);
        carried+=weight*c; split+=weight*s; landed+=weight*t;
        char b[96];
        if(c>0) {
            std::snprintf(b, sizeof(b), "%s%u: %.1f / %.1f", levels.empty() ? "" : ", ", l, 100.0*s/c, 100.0*(c-s+t)/c);
            levels+=b;
        }
        // The rippling light of the level: the waves' slopes (root mean square), how far under the surface its light landed
        // (the mean ray), and the spread of the landed light's brightness over what it landed on (its standard deviation
        // over its mean, across the beam: the waves' focusing alone, no shadow or absorption in it).
        const double across=double(sums[3][l]), through=double(sums[4][l]), square=double(sums[5][l]), count=double(sums[7][l]), corners=double(sums[9][l]);
        if(across>0 && through>0 && count>0) {
            const double mean=through/across, spread=std::sqrt(std::max(square/across/(mean*mean)-1.0, 0.0));
            std::snprintf(b, sizeof(b), "%s%u: slopes %.3f, %.0f cm down, ups and downs %.1f %%", ripple.empty() ? "" : ", ", l,
                          corners>0 ? std::sqrt(double(sums[8][l])/4096.0/corners) : 0.0, double(sums[6][l])/count, 100.0*spread);
            ripple+=b;
        }
    }
    if(!(carried>0)) return std::string();
    char b[320];
    std::snprintf(b, sizeof(b), "; the sun's light under the water in view: %.1f %% on triangles whose corners met different surfaces, %.1f %% landed (per level, split / landed %%: ",
                  100.0*split/carried, 100.0*(carried-split+landed)/carried);
    return b+levels+")"+(ripple.empty() ? std::string() : "; its rippling where it landed (per level: "+ripple+")");
}
void summarise(const TGPT_Frame& f, double cpuMs) {
    const uint64_t now=GetTickCount64();
    ++st.summaryFrames; st.summaryCpu+=cpuMs;
    for(size_t i=0;i<st.phase.size();++i) st.summaryPhase[i]+=st.phase[i];
    if(!st.lastSummary) { st.lastSummary=now; return; }
    if(now-st.lastSummary<10000) return;
    const auto s=st.scene.stats();
    TGPT_Status status{}; { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); status=e.status; }
    const double n=double(st.summaryFrames);
    auto ms=[](double v) { char b[32]; std::snprintf(b, sizeof(b), "%.2f", v); return std::string(b); };
    log("Path tracing "+std::to_string((now-st.lastSummary)/1000)+"s: frames="+std::to_string(st.summaryFrames)+
        " render="+std::to_string(st.width)+"x"+std::to_string(st.height)+" gpu="+ms(st.summaryGpu/n)+"ms cpu="+ms(st.summaryCpu/n)+"ms (d3d11 "+ms(st.summaryPhase[0]/n)+
        " flush "+ms(st.summaryPhase[1]/n)+" wait "+ms(st.summaryPhase[2]/n)+" record "+ms(st.summaryPhase[3]/n)+") composited="+std::to_string(st.composited)+
        " gpuWaits="+std::to_string(st.compositeWaits)+" skipped="+std::to_string(st.compositeSkipped)+
        // PT-107 (R19): the game's device's own GPU clock (State::D11Stamps), a frame's mean over the frames read.
        (st.summaryD11Frames ? " game device a frame (ms, "+std::to_string(st.summaryD11Frames)+" frames read): the tracer's inputs "+ms(st.summaryD11[0]/st.summaryD11Frames)+
            ", the game to the composite "+ms(st.summaryD11[1]/st.summaryD11Frames)+", the wait for the trace "+ms(st.summaryD11[2]/st.summaryD11Frames)+
            ", the composite "+ms(st.summaryD11[3]/st.summaryD11Frames)+", Fog Adaptation "+ms(st.summaryD11[4]/st.summaryD11Frames)+
            ", the game from the composite to the next frame's trace "+ms(st.summaryD11[5]/st.summaryD11Frames)+" (to its fog "+
            (st.summaryD11FogFrames ? ms(st.summaryD11ToFog/st.summaryD11FogFrames) : std::string("-"))+"; from its Fog Adaptation to the frame's end "+
            (st.summaryD11NextFrames ? ms(st.summaryD11Next[0]/st.summaryD11NextFrames)+", from the frame's end to the next frame's rendering "+
             ms(st.summaryD11Next[1]/st.summaryD11NextFrames)+", from that to the next frame's trace "+ms(st.summaryD11Next[2]/st.summaryD11NextFrames) : std::string("-"))+
            ");" : std::string(" game device a frame: not read;"))+
        // PT-112 (measure-waits only): HDRP's pass groups on the game's device, a frame's mean.
        (st.summaryHdrpFrames ? " HDRP's groups on the game's device (ms, "+std::to_string(st.summaryHdrpFrames)+" frames): the fog's end to BeforeTransparent "+
            ms(st.summaryHdrp[0]/st.summaryHdrpFrames)+", the transparents "+ms(st.summaryHdrp[1]/st.summaryHdrpFrames)+", post-processing "+
            ms(st.summaryHdrp[2]/st.summaryHdrpFrames)+", after it to the frame's end "+ms(st.summaryHdrp[3]/st.summaryHdrpFrames)+", the gap "+
            ms(st.summaryHdrp[4]/st.summaryHdrpFrames)+", the next frame's start to BeforeRendering "+ms(st.summaryHdrp[5]/st.summaryHdrpFrames)+
            ", the depth prepass "+ms(st.summaryHdrp[6]/st.summaryHdrpFrames)+", after it to our trace "+ms(st.summaryHdrp[7]/st.summaryHdrpFrames)+";" : std::string())+
        // PT-107 (R19): how often the CPU came late (State::summaryCpuLate).
        " the frame before's trace already done at a frame's start: "+std::to_string(st.summaryCpuLate)+" of "+std::to_string(st.summaryCpuLateOf)+" frames;"+
        // PT-107 (R19): the CPU's clock against the trace's (State::CpuClock), a frame's mean.
        (st.summaryCpuClockFrames ? " the CPU's clock against the trace (ms, "+std::to_string(st.summaryCpuClockFrames)+" frames): the trace starts "+
            ms(st.summaryCpuClock[0]/st.summaryCpuClockFrames)+" after its inputs are flushed; after the trace's end (below 0: before it) the CPU is at the composite "+
            ms(st.summaryCpuClock[1]/st.summaryCpuClockFrames)+", at Fog Adaptation "+ms(st.summaryCpuClock[2]/st.summaryCpuClockFrames)+", at the next frame's trace "+
            ms(st.summaryCpuClock[3]/st.summaryCpuClockFrames)+"; the CPU from the composite's end to Fog Adaptation "+ms(st.summaryCpuClock[4]/st.summaryCpuClockFrames)+
            ", from Fog Adaptation to the next frame's trace "+ms(st.summaryCpuClock[5]/st.summaryCpuClockFrames)+";" : std::string(" the CPU's clock against the trace: not read;"))+
        (st.summaryNextFrames ? " the next frame after the trace's end (ms, "+std::to_string(st.summaryNextFrames)+" frames): the main thread's Update "+
            ms(st.summaryNextFrame[0]/st.summaryNextFrames)+", the render thread at that Update's mark "+ms(st.summaryNextFrame[1]/st.summaryNextFrames)+
            ", HDRP's rendering begun "+ms(st.summaryNextFrame[2]/st.summaryNextFrames)+", the render thread at its first command "+ms(st.summaryNextFrame[3]/st.summaryNextFrames)+
            ", the main thread's capture begun "+ms(st.summaryNextFrame[4]/st.summaryNextFrames)+", done "+ms(st.summaryNextFrame[5]/st.summaryNextFrames)+
            ", HDRP's rendering done "+ms(st.summaryNextFrame[6]/st.summaryNextFrames)+"; the main thread's LateUpdate "+ms(st.summaryNextFrame[7]/st.summaryNextFrames)+
            ", the render thread at its mark "+ms(st.summaryNextFrame[8]/st.summaryNextFrames)+", at the mark queued when HDRP's rendering begun "+
            ms(st.summaryNextFrame[9]/st.summaryNextFrames)+";" :
            std::string(" the next frame after the trace's end: not read;"))+
        waits::line(uint32_t(st.summaryFrames))+ // (PT-107: only with the file measure-waits)
        // PT-112 (A), on trial: the frames that asked for their own depth in HDRP's buffers, and those written.
        (st.summaryOwnAsked ? " own depth into HDRP's buffers: "+std::to_string(st.summaryOwnDepth)+" of "+std::to_string(st.summaryOwnAsked)+
            " frames that asked, the depth pyramid too in "+std::to_string(st.summaryOwnPyramid)+";" : std::string())+
        // PT-99 (R19): the reflections' sort (State::ReflectionsSort) and its last probe's medians.
        " reflections' sort: "+(st.sort.noHint ? "no hint" : "by the material")+" ("+std::to_string(st.sort.probes)+" probes, "+std::to_string(st.sort.switches)+
            " switches; the last probe: "+ms(st.sort.lastMine)+" ms with the choice of then, "+ms(st.sort.lastOther)+" with the other);"+
        " meshes ready/queued/failed="+std::to_string(s.meshesReady)+"/"+std::to_string(s.meshesQueued)+"/"+std::to_string(s.meshesFailed)+
        " instances traced/registered="+std::to_string(s.instancesTraced)+"/"+std::to_string(status.instances)+" triangles="+std::to_string(s.triangles)+
        " lights="+std::to_string(st.scene.lightCount)+" (grid: "+std::to_string(s.gridCellsLit)+" cells lit, "+
        (s.gridCellsLit ? std::to_string(s.gridEntries/s.gridCellsLit) : std::string("0"))+" lights a lit cell on average, at most "+
        std::to_string(s.gridMostInCell)+", "+std::to_string(s.gridBuilds)+" builds, "+std::to_string(s.gridFarBuilds)+" of the far lists; the lamps' search tree "+
        std::to_string(st.scene.lampTreeNodes)+" nodes) rays/pixel="+std::to_string(std::clamp(f.raysPerPixel, 1u, 16u))+
        " textures="+std::to_string(status.textures)+" materials="+std::to_string(status.materials)+
        (st.useDxr12 ? " micromaps: meshes="+std::to_string(s.ommMeshes)+" waiting="+std::to_string(s.ommWaiting)+" triangles="+std::to_string(s.ommTriangles)+
            " instances="+std::to_string(s.ommInstances)+" "+std::to_string(s.ommBytes>>20)+"MB ser="+(st.giSer ? "on" : "off") : std::string(" sm=6.6"))+
        " characters="+std::to_string(s.charactersTraced)+" ("+std::to_string(s.characterTriangles/1000)+"k triangles)"+
        // PT-71 item 4: the things the game moves, placed where HDRP draws them in the frame (TGPT_SetMoving).
        " moving: "+std::to_string(s.movingTraced)+" placed this frame, "+std::to_string(s.movingHidden)+" hidden, "+std::to_string(s.movingReplaced)+
            " of the scene's instances stood in for;"+
        " plants: "+std::to_string(s.plantInstances)+" in the wind's pose in "+std::to_string(s.plantGroups)+" groups ("+std::to_string(s.plantVertices)+" vertices, "+
        std::to_string(s.plantTriangles)+" triangles, "+std::to_string(s.plantBuilt)+" groups made this frame, "+std::to_string(s.plantWaiting)+" instances waiting their turn);"+
        // PT-71 item 5: the water's surface, its waves as triangles (scene_water.cpp).
        // (PT-99, scene.hpp WaterGroup: and the groups they are traced in, a bottom level, a refit and an instance each.)
        " water: "+std::to_string(s.waterUnits)+" pieces of its surface ("+std::to_string(s.waterUnitTriangles)+" triangles, "+std::to_string(s.waterUnitVertices)+" vertices, "+
            std::to_string(s.waterUnitsMade)+" made this frame, "+std::to_string(s.waterUnitsKept)+" kept) in "+std::to_string(s.waterGroups)+" groups ("+
            std::to_string(s.waterGroupsBuilt)+" built this frame, "+std::to_string(s.waterGroupsMerged)+" of them put together of several, "+
            std::to_string(s.waterBuiltTriangles)+" triangles built, "+std::to_string(s.waterCollapsed)+" pieces collapsed, "+std::to_string(s.waterDead)+" dropped pieces' geometries);"+
        " cloth: "+std::to_string(s.clothInstances)+" thin, "+std::to_string(s.clothSwayed)+" in the wind's pose ("+std::to_string(s.clothSwayedVertices)+" vertices, "+
            std::to_string(s.clothSwayCopies)+" copies)"+
        // PT-99 (docs/agents/speed-1.md): instances whose every triangle the shader's alpha test is asked about (one of their
        // materials is alpha-tested, and they have no micromaps), and how many of those triangles are of plain submeshes.
        " tested by the shader: "+std::to_string(s.shaderTestedInstances)+" instances, every triangle ("+std::to_string(s.shaderTestedTriangles)+" triangles, "+
            std::to_string(s.shaderTestedOpaque)+" of them of submeshes that are no cut-out)"+
        " emissive: "+std::to_string(s.emitters)+" submeshes, "+std::to_string(s.emitterTriangles)+" triangles, "+std::to_string(s.emitterTrianglesSampled)+
            " sampled as lights in "+std::to_string(s.emitterClustersSampled)+" clusters"+(s.emittersOverBudget ? ", "+std::to_string(s.emittersOverBudget)+" over the budget" : std::string())+
            " ("+std::to_string(s.emitterBuilds)+" builds)"+
        " memory="+std::to_string(st.accountedBytes>>20)+"MB sky="+(st.skyValid ? "hdrp" : "plain")+
        " stages: "+stage_text(st.summaryStage, st.summaryStageFrames)+
        // The caustics' landed triangles a frame (pt_caustics.hlsl), on average and at most, what the last build took and the room
        // for them; and what the waves were where the tracer read them (0 high and flat: the slot was traced as still water).
        (st.causticFrames ? "; caustics: "+std::to_string(st.causticTriangleSum/st.causticFrames)+" triangles a frame, at most "+
            std::to_string(st.causticMost)+" (built over "+std::to_string(st.causticBuilt)+", room for "+std::to_string(st.causticRoom)+", up to "+std::to_string(st.causticCapacity)+" a frame)"+tiles_text()+split_text()+wave_text() : std::string()));
    // PT-109 (docs/agents/megageo-1.md): the scene stage's acceleration structure work by kind. The GPU time between the
    // stamps (a frame's mean over the frames whose stamps were read) and what was built, refitted or copied (a frame's mean
    // over the frames recorded); then what the structures hold. The harness freezes the world: the plants are posed and
    // refitted only in frames in which the wind's clock or the camera moved (pose_plants), so a frozen world shows them at 0.
    if(st.summaryStructRecorded) {
        const double timed=double(st.summaryStructFrames), recorded=double(st.summaryStructRecorded);
        std::string text; double sum=0;
        for(uint32_t at=1;at<=GpuScene::kStructureKinds;++at) {
            const uint32_t kind=at%GpuScene::kStructureKinds; // the rest of the stage last
            const auto& w=st.summaryStructWork[kind];
            const double t=timed>0 ? st.summaryStruct[kind]/timed : 0.0; sum+=t;
            char b[256];
            // The top level: built anew each frame, or (PT-109's trial, GpuScene::megaTrial 1: no build counted) partitioned,
            // with what the driver was told of a frame's instances: written whole or told to take new bounds.
            if(kind==GpuScene::kStructureTopLevel && w.builds<st.summaryStructRecorded) {
                const double parted=double(st.summaryStructRecorded-w.builds);
                std::snprintf(b, sizeof(b), "%s %.3f ms (partitioned in %.0f of %.0f frames: %.1f of %.0f instances told to the driver a frame, %.2f MB)", kStructureNames[kind], t,
                              parted, recorded, double(w.refits)/parted, double(w.triangles)/recorded, double(w.bytes)/recorded/1048576.0);
            } else if(kind==GpuScene::kStructureTopLevel)
                std::snprintf(b, sizeof(b), "%s %.3f ms (built anew over %.0f instances, %.2f MB)", kStructureNames[kind], t, double(w.triangles)/recorded, double(w.bytes)/recorded/1048576.0);
            else if(kind==GpuScene::kStructureOther || kind==GpuScene::kStructureSkinning)
                std::snprintf(b, sizeof(b), "%s %.3f ms", kStructureNames[kind], t);
            else
                std::snprintf(b, sizeof(b), "%s %.3f ms (%.2f built, %.2f refitted, %.0f triangles a frame)", kStructureNames[kind], t, double(w.builds)/recorded,
                              double(w.refits)/recorded, double(w.triangles)/recorded);
            text+=(text.empty() ? "" : "; ")+std::string(b);
        }
        TGPT_Diagnostics held{sizeof(TGPT_Diagnostics), TGPT_ABI}; st.scene.memory(held);
        char tail[320];
        std::snprintf(tail, sizeof(tail), "; sum %.3f ms of the scene stage's %.3f (%u frames timed, %u recorded); held, MB: acceleration structures %.1f, plants' posed copies and their structures %.1f, "
                      "cloth %.1f, of the first the water's %.1f", sum, st.summaryStageFrames ? st.summaryStage[0]/st.summaryStageFrames : 0.0, st.summaryStructFrames, st.summaryStructRecorded,
                      double(held.acceleration)/1048576.0, double(held.plants)/1048576.0, double(held.cloth)/1048576.0, double(st.scene.water_memory())/1048576.0);
        log("PT structures "+std::to_string((now-st.lastSummary)/1000)+"s: "+text+tail);
    }
    // PT-110 (docs/agents/omm-1.md): the traced world's cut-outs in the last recorded frame, by kind, and how much of them
    // the opacity micromaps settle before the shader's alpha test is asked (GpuScene::cutout_text): a count on the CPU.
    log(st.scene.cutout_text());
    st.summaryStruct={}; st.summaryStructFrames=0; st.summaryStructRecorded=0; st.summaryStructWork={};
    st.causticFrames=0; st.causticTriangleSum=0; st.causticMost=0;
    for(auto& sum:st.causticSums) sum.fill(0);
    for(auto& tiles:st.causticTiles) tiles.fill(0);
    st.causticListFrames=0; st.causticListed=0; st.causticDry=0; st.causticListMs=0;
    for(auto& w:st.causticWaves) { w.high=0; w.steep=0; w.sampled=false; w.frames=0; }
    st.lastSummary=now; st.summaryFrames=0; st.summaryGpu=0; st.summaryCpu=0; st.summaryPhase={};
    st.summaryStage={}; st.summaryStageFrames=0;
    st.composited=st.compositeWaits=st.compositeSkipped=0;
    st.summaryD11={}; st.summaryD11Frames=0; st.summaryD11ToFog=0; st.summaryD11FogFrames=0; st.summaryCpuLate=st.summaryCpuLateOf=0;
    st.summaryD11Next={}; st.summaryD11NextFrames=0; st.summaryHdrp={}; st.summaryHdrpFrames=0;
    st.summaryOwnDepth=st.summaryOwnPyramid=st.summaryOwnAsked=0;
    st.summaryCpuClock={}; st.summaryCpuClockFrames=0; st.summaryNextFrame={}; st.summaryNextFrames=0;
}

// force: now, whenever the last one was (PT-87, memory3: the lines after the first frames of a start).
void publish_memory(bool force=false) {
    static uint64_t last=0; static uint32_t scene=~0u;
    const auto now=GetTickCount64();
    if(!force && now-last<1000 && scene==st.scene.clear_generation()) return;
    last=now; scene=st.scene.clear_generation();
    TGPT_Diagnostics d{sizeof(TGPT_Diagnostics), TGPT_ABI}; st.scene.memory(d);
    auto buffer=[](const Buffer& b) { return allocation_bytes(b.resource.Get()); };
    auto texture=[](const Texture& t) { return allocation_bytes(t.resource.Get()); };
    auto shared=[](const Shared& t) { return allocation_bytes(t.t.d12.Get()); };
    auto d11=[&](ID3D11Texture2D* t)->uint64_t {
        if(!t) return 0; D3D11_TEXTURE2D_DESC a{}; t->GetDesc(&a);
        D3D12_RESOURCE_DESC b{}; b.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; b.Width=a.Width; b.Height=a.Height;
        b.DepthOrArraySize=UINT16(a.ArraySize); b.MipLevels=UINT16(a.MipLevels); b.Format=a.Format; b.SampleDesc=a.SampleDesc;
        if(a.BindFlags&D3D11_BIND_UNORDERED_ACCESS) b.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return st.d12->GetResourceAllocationInfo(0,1,&b).SizeInBytes;
    };
    d.sharedTextures=share::memory_bytes();
    for(const Shared* t:{&st.atlas,&st.alphaAtlas,&st.strandAtlas,&st.cookieAtlas,&st.sky,&st.fogSky}) d.atlases+=shared(*t);
    for(const auto& t:st.skin) d.skinned+=shared(t);
    for(const auto& i:st.inputs) for(const Shared* t:{&i.gb0,&i.nrm,&i.gb2,&i.gb3,&i.depth,&i.motion,&i.exposure}) d.targets+=shared(*t);
    for(const Shared* t:{&st.outputs.a,&st.outputs.b,&st.outputs.c,&st.outputs.g0,&st.outputs.g1,&st.outputs.dm,&st.outputs.mv}) d.targets+=shared(*t);
    for(const auto* a:{&st.surface,&st.normal,&st.material}) for(const auto& t:*a) d.targets+=texture(t);
    d.targets+=texture(st.tracedFog);
    for(const Texture* t:{&st.diffuseRaw,&st.sunRaw,&st.specRaw,&st.tmpA,&st.tmpB,&st.tmpC,&st.tmpD,&st.directTmp,&st.directOut,&st.hair,&st.specSeen,&st.ownMotion,&st.ownEmission}) d.targets+=texture(*t);
    for(const auto& t:st.guides) d.targets+=d11(t.texture.Get());
    for(const auto& t:st.retiredGuides) d.retired+=d11(t.second.Get());
    d.histories=buffer(st.diRes)+buffer(st.giRes);
    for(const auto* a:{&st.diffuseHist,&st.diffuseMoments,&st.directHist,&st.directMoments,&st.specHist,&st.specMoments}) for(const auto& t:*a) d.histories+=texture(t);
    for(const auto& a:st.caustics) for(const auto& t:a) d.caustics+=allocation_bytes(t.displacement.d12.Get())+allocation_bytes(t.normals.d12.Get());
    for(const Buffer* b:{&st.causticCorners,&st.causticLight,&st.causticBlas,&st.causticBlasScratch,&st.causticTlas,&st.causticTlasScratch,&st.causticCounters,&st.causticReadback}) d.caustics+=buffer(*b);
    d.caustics+=buffer(st.sunGrid); d.wetStore=buffer(st.wetStore); d.yardstick=texture(st.yardstick)+buffer(st.yardBuffer)+buffer(st.yardReadback);
    d.radianceCache=buffer(st.cacheKeys)+buffer(st.cacheSums)+buffer(st.cacheLight);
    for(const auto& t:st.fogGrid) d.radianceCache+=texture(t);
    for(const auto& ring:st.rings) d.transfer+=allocation_bytes(ring.resource());
    for(const Buffer* b:{&st.serTable,&st.queryReadback,&st.probeBuffer,&st.probeReadback,&st.meterBuffer,&st.meterReadback,&st.crumbs,&st.neighborOffsets}) d.transfer+=buffer(*b);
    for(const Texture* t:{&st.rrgDepth,&st.rrgMotion,&st.rrgNormals,&st.rrgDiffuse,&st.rrgSpecular,&st.nrdViewZ,&st.nrdMotion,&st.nrdNormalRoughness,&st.nrdDiffuseIn,&st.nrdDiffuseOut,&st.nrdSpecularIn,&st.nrdSpecularOut}) d.denoiser+=texture(*t);
    d.denoiser+=st.nrd.memory_bytes()+st.nrdConfidence.memory_bytes()+st.psr.memory_bytes(); d.retired+=st.release.bytes();
    d.total=d.meshGeometry+d.acceleration+d.scratch+d.plants+d.cloth+d.skinned+d.records+d.sharedTextures+d.atlases+d.targets+d.histories+d.caustics+d.wetStore+d.yardstick+d.radianceCache+d.transfer+d.denoiser+d.retired;
    st.accountedBytes=d.total; st.accountedTransfer=d.transfer;
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.diagnostics=d; }
    static uint64_t logged=0; static uint32_t loggedScene=~0u;
    if(now-logged>=10000 || loggedScene!=scene) {
        logged=now; loggedScene=scene;
        std::ostringstream line; line<<"PT memory bytes: scene="<<scene;
        line<<" meshGeometry="<<d.meshGeometry;
        line<<" acceleration="<<d.acceleration;
        line<<" scratch="<<d.scratch;
        line<<" plants="<<d.plants;
        line<<" cloth="<<d.cloth;
        line<<" skinned="<<d.skinned;
        line<<" records="<<d.records;
        line<<" sharedTextures="<<d.sharedTextures;
        line<<" atlases="<<d.atlases;
        line<<" targets="<<d.targets;
        line<<" histories="<<d.histories;
        line<<" caustics="<<d.caustics;
        line<<" wetStore="<<d.wetStore;
        line<<" yardstick="<<d.yardstick;
        line<<" radianceCache="<<d.radianceCache;
        line<<" transfer="<<d.transfer;
        line<<" denoiser="<<d.denoiser;
        line<<" retired="<<d.retired;
        line<<" total="<<d.total;
        // PT-87: what Windows gives the game of the card's memory and what it counts for the game now (every device of the
        // process), and that count less the tracer's whole sum. That rest is NOT "the game" (it was printed as notTracer
        // and read so; docs/agents/memory-2.md): it is the game and the other mods, AND what the tracer brings that no
        // figure here has, measured offline by memory3 (its section M3-2): the memory the driver takes when the passes
        // first run (about 330 MB at 1280 x 720 once settled, 700 in the first frames), and a second count of the
        // tracer's own shared textures (ownShared: the atlases, HDRP's frame, the outputs, the characters' and the
        // waves' copies), which comes when the second device first uses them. The sum is taken off whole: the upload
        // rings and read-backs (transfer) are in Windows' count for the card too on this card and driver (their step:
        // +144 MB for 144 MB), where this line took them for the PC's memory until memory3. Windows counts a texture two
        // devices hold open once for each (measured: tests/pathtracing_gpu_test.cpp "the second count",
        // docs/agents/texture-share.md): the game's textures the tracer opened are in sharedTextures, which is that
        // second count and no memory of the card's.
        {
            uint64_t budget=0, counted=0;
            if(share::video_memory(budget, counted)) {
                uint64_t ownShared=d.atlases+d.skinned;
                for(const auto& i:st.inputs) for(const Shared* t:{&i.gb0,&i.nrm,&i.gb2,&i.gb3,&i.depth,&i.motion,&i.exposure}) ownShared+=shared(*t);
                for(const Shared* t:{&st.outputs.a,&st.outputs.b,&st.outputs.c,&st.outputs.g0,&st.outputs.g1,&st.outputs.dm,&st.outputs.mv}) ownShared+=shared(*t);
                for(const auto& a:st.caustics) for(const auto& t:a) ownShared+=allocation_bytes(t.displacement.d12.Get())+allocation_bytes(t.normals.d12.Get());
                line<<" windowsBudget="<<budget<<" windowsCounts="<<counted<<" notInThisLine="<<(counted>d.total ? counted-d.total : 0)<<" ownShared="<<ownShared;
                // PT-87 (docs/agents/memory-4.md): of ownShared, the atlases whose side the game's device has let go of
                // until their next bake: Windows counts those once, the rest twice. And how often a side went and came.
                line<<" ownSharedOnce="<<sides_let_go_bytes()<<" atlasSidesLetGo="<<st.sidesGone<<" atlasSidesTakenUp="<<st.sidesBack;
                st.ownSharedBytes=ownShared; tell_own_shared(); // (for the cap's trial alone: texture_share.cpp within_budget)
            } else line<<" windowsBudget=unknown";
        }
        // The upload rings (PT-87, memory3: three of kRingSize, in the card's count): the most a frame has taken of one since
        // the tracer started, and how often a frame found one full. What sizing them by need would give back.
        {
            uint64_t peak=0, full=0;
            for(const auto& ring:st.rings) { peak=std::max(peak, ring.peak()); full+=ring.found_full(); }
            line<<" ringPeak="<<peak<<"/"<<kRingSize<<" ringFull="<<full;
        }
        // How much of the atlases that are made whole is in use (PT-87: what growing them by need would save): the colour
        // atlas's 64 x 64 tiles (4,096: 64 MB) and its detail tiles (128: 128 MB), the alpha atlas's tiles (1,024: 64 MB).
        {
            auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
            line<<" colorTiles="<<e.textureTiles.size()<<"/"<<kMaxTiles<<" detailTiles="<<(e.detailTilesMade-uint32_t(e.detailTilesFree.size()))<<"/"<<kMaxDetailTiles
                <<" alphaTiles="<<e.alphaTiles.size()<<"/"<<kMaxAlphaTiles;
        }
        line<<" (shared resources counted once; transfer is upload rings and read-backs, which Windows counts against the card's budget too; driver-private and opaque "
              "SDK heaps excluded; sharedTextures is the game's own textures held open: Windows' second count of them, no memory of the card's; notInThisLine is the game, "
              "the other mods, the memory the driver takes when the tracer's passes first run and Windows' second count of ownShared, less ownSharedOnce: the atlases "
              "the game's device has let go of until their next bake, which Windows counts once";
        // The water's surface has no figure of its own in TGPT_Diagnostics: its pieces are in the figures of what they are.
        if(const uint64_t water=st.scene.water_memory()) line<<"; of meshGeometry, acceleration, scratch, records and transfer the water's surface holds "<<water;
        line<<")"; log(line.str());
        // PT-87, the owner's question (2026-10-09: "We removed limits so that ... we trace infinitely out into the distance.
        // isn't that the primary reason for our perf woes?"): the traced world's memory by its distance from the camera
        // (GpuScene::distance_bands says what is counted where). Counted only: nothing is cut by distance.
        if(st.haveMemoryCamera && d.meshGeometry+d.acceleration+d.plants+d.sharedTextures>0) {
            std::vector<uint64_t> textureBytes; share::open_bytes(textureBytes);
            GpuScene::DistanceBands bands; st.scene.distance_bands(st.memoryCamera, textureBytes, bands);
            const auto stats=st.scene.stats();
            std::ostringstream text; text<<"PT memory by distance from the camera, bytes (a mesh and a texture at the nearest instance that uses it, plants at their group's cell, "
                "water at the piece's nearest point):";
            static const char* const names[GpuScene::DistanceBands::kBands+1]={"under 50 m", "50 to 150 m", "150 to 500 m", "500 to 1500 m", "over 1500 m", "named by no instance"};
            for(int b=0;b<=GpuScene::DistanceBands::kBands;++b) {
                text<<(b ? " | " : " ")<<names[b]<<": instances="<<bands.instances[b]<<" instanceTriangles="<<bands.instanceTriangles[b]<<" meshes="<<bands.meshes[b]
                   <<" geometry="<<bands.geometry[b]<<" acceleration="<<bands.acceleration[b]<<" plantGroups="<<bands.plantGroups[b]<<" plantTriangles="<<bands.plantTriangles[b]
                   <<" plants="<<bands.plants[b]<<" waterPieces="<<bands.waterUnits[b]<<" waterTriangles="<<bands.waterTriangles[b]<<" water="<<bands.water[b]
                   <<" textures="<<bands.textureCount[b]<<" textureBytes="<<bands.textures[b];
            }
            // And how much of the arenas' chunks is in use (the rest is free room inside chunks that are held whole). The
            // arenas' own figures (GpuScene::arena_use, PT-87 memory-2): meshGeometry and acceleration above also hold the
            // water's pages and the top level, which the sentence this replaces left for the reader to take out.
            const auto arenas=st.scene.arena_use();
            text<<" ; the arenas' chunks: mesh geometry "<<arenas.geometryChunks<<" with "<<arenas.geometryUsed<<" in use; acceleration structures that stay "<<arenas.keptChunks
                <<" with "<<arenas.keptUsed<<" in use; full-size builds waiting to be compacted "<<arenas.buildChunks<<" with "<<arenas.buildUsed<<" in use (the meshes' own count: "
                <<stats.geometryBytes<<" of geometry, "<<stats.accelerationBytes<<" of structures)";
            log(text.str());
        }
        // PT-87 (memory3): and, for ten minutes from the tracer's start, where the count's moves since then came from.
        start_summary(st.d12.Get(), d.total, d.transfer);
    }
}

void publish_status(const TGPT_Frame& f, double cpuMs, bool ok) {
    publish_memory();
    const auto s=st.scene.stats();
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    if(ok) ++e.status.framesTraced; else ++e.status.framesFailed;
    e.status.frameMs=float(cpuMs);
    e.status.d3d11Ms=float(st.phase[0]); e.status.flushMs=float(st.phase[1]); e.status.waitMs=float(st.phase[2]); e.status.recordMs=float(st.phase[3]);
    e.status.meshesReady=s.meshesReady; e.status.meshesFailed=s.meshesFailed; e.status.meshesQueued=s.meshesQueued;
    e.status.instancesTraced=s.instancesTraced; e.status.triangles=s.triangles; e.status.geometryBytes=s.geometryBytes;
    e.status.accelerationBytes=s.accelerationBytes; e.status.renderWidth=f.renderWidth; e.status.renderHeight=f.renderHeight;
    e.status.features=(st.useDxr12 ? TGPT_FEATURE_DXR12 : 0u)|(st.giSer ? TGPT_FEATURE_SER : 0u)|
        (rrg_available() && !st.rrgFailed ? TGPT_FEATURE_RAY_REGENERATION : 0u)|(st.rrgActive ? TGPT_FEATURE_RAY_REGENERATION_ACTIVE : 0u)|
        (nrd_available() && !st.nrdFailed ? TGPT_FEATURE_NRD : 0u)|(st.nrdActive ? TGPT_FEATURE_NRD_ACTIVE : 0u);
    e.status.micromapMeshes=s.ommMeshes; e.status.micromapInstances=s.ommInstances; e.status.micromapsWaiting=s.ommWaiting;
    // PT-98: what the native side still owes of the traced world.
    e.status.tilesToBake=uint32_t(e.newTextures.size()+e.newAlphaTextures.size()+e.newHairBakes.size()+e.newHairLobeBakes.size()+
                                  e.newEyeBakes.size()+e.newCookies.size());
    e.status.instancesWaiting=s.instancesWaiting; e.status.plantsWaiting=s.plantWaiting;
}

// A GPU progress marker: value into the markers' slot once the work recorded before it is done.
void crumb(ID3D12GraphicsCommandList4* list, uint32_t slot, uint32_t value) {
    const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER at{st.crumbs.va()+slot*4u, value};
    const D3D12_WRITEBUFFERIMMEDIATE_MODE mode=D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT;
    list->WriteBufferImmediate(1, &at, &mode);
}
// When the GPU stops (a device lost: the driver reset it after it hung or faulted), once: why each device says it was lost,
// and how far the tracer's own GPU work got by its progress markers, so a stage that hung names itself.
void report_gpu_stop(HRESULT hr) {
    static bool reported=false;
    if(reported) return;
    const HRESULT d12=st.d12 ? st.d12->GetDeviceRemovedReason() : S_OK, d11=st.d11 ? st.d11->GetDeviceRemovedReason() : S_OK;
    const bool lost=hr==DXGI_ERROR_DEVICE_REMOVED || hr==DXGI_ERROR_DEVICE_HUNG || hr==DXGI_ERROR_DEVICE_RESET || hr==DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    if(!lost && d12==S_OK && d11==S_OK) return;
    reported=true;
    auto why=[](HRESULT r) {
        switch(r) {
        case S_OK: return std::string("not lost");
        case DXGI_ERROR_DEVICE_HUNG: return std::string("hung (its own work took too long) ")+tgb::hexhr(r);
        case DXGI_ERROR_DEVICE_RESET: return std::string("reset (a bad command) ")+tgb::hexhr(r);
        case DXGI_ERROR_DEVICE_REMOVED: return std::string("removed (the driver reset the GPU) ")+tgb::hexhr(r);
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return std::string("a driver error ")+tgb::hexhr(r);
        default: return tgb::hexhr(r);
        }
    };
    std::string where="no progress markers on this GPU";
    if(st.crumbsOn && st.crumbs.mapped) {
        const auto* at=static_cast<const volatile uint32_t*>(st.crumbs.mapped);
        const uint32_t done=at[0];
        const uint32_t mark=done&0xFFu, frame=done>>8, recordedFrame=st.crumbsRecorded>>8;
        where="the last frame recorded "+std::to_string(recordedFrame)+"; the GPU finished ";
        if(!done) where+="none of the tracer's work";
        else {
            where+=mark==0 ? "the start of frame "+std::to_string(frame) : "stage "+std::string(kStageNames[std::min(mark, kStages)-1])+" of frame "+std::to_string(frame);
            if(done==st.crumbsRecorded) where+=", all the tracer recorded";
            else if(mark<kStages) {
                where+=", and stopped in stage "+std::string(kStageNames[mark]);
                // In the scene stage: the last of its structures' kinds whose commands the GPU finished (slot 2).
                const uint32_t structure=at[2], number=at[3];
                if(mark==0 && (structure>>8)==frame && (structure&0xFFu)>=1u && (structure&0xFFu)<=GpuScene::kStructureKinds) {
                    where+=" (after its structures' "+std::string(kStructureNames[(structure&0xFFu)-1u])+"; the next kind's work did not end";
                    // Which of the frame's stamps that was, and from what the CPU recorded for the frame, the kind after it.
                    if((number>>8)==frame) {
                        const uint32_t n=number&0xFFu;
                        where+=": that was stamp "+std::to_string(n)+" of the frame";
                        for(uint32_t i=0;i<kFrames;++i) {
                            if((st.structFrame[i]&0xFFFFFFu)!=frame || n>=st.structCount[i] || st.structKinds[i][n]!=(structure&0xFFu)-1u) continue;
                            where+=n+1u<st.structCount[i] ? ", and the work that did not end is "+std::string(kStructureNames[st.structKinds[i][n+1u]])+"'s"
                                                          : ", the frame's last: what did not end is the scene stage's tail";
                            // The plants' own steps (GpuScene::plant_step): slot 5 the frame of the last one done, slot 4 its number.
                            if(n+1u<st.structCount[i] && st.structKinds[i][n+1u]==GpuScene::kStructurePlants)
                                where+=" ["+st.scene.describe_plant_steps(frame, at[4], (at[5]&0xFFFFFFu)==frame)+"]";
                            where+=" (the frame's stamps:";
                            for(uint32_t k=0;k<st.structCount[i];++k) where+=std::string(k ? ", " : " ")+kStructureNames[st.structKinds[i][k]];
                            where+=")";
                            break;
                        }
                    }
                    where+=")";
                }
                else if(mark==0) where+=" (before any of its structures' kinds ended)";
            }
            else where+=", and never started frame "+std::to_string(frame+1)+"'s work";
        }
    }
    log("The GPU stopped (the driver reset it): the path tracer's D3D12 device "+why(d12)+", the game's D3D11 device "+why(d11)+"; "+where);
}

void fail(const std::string& message, HRESULT hr) {
    log_throttled("Path tracing frame failed: "+message+" "+tgb::hexhr(hr));
    try { report_gpu_stop(hr); } catch(...) {}
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    e.status.lastError=hr;
    if(++st.failures>=30 && !e.status.disabled) {
        e.status.disabled=1;
        log("Path tracing failed 30 frames in a row; it is off for the rest of this session");
    }
}

// The four-channel float formats HDRP keeps its wave buffers in, as a view reads them (a typeless one as its float form).
DXGI_FORMAT wave_format(DXGI_FORMAT f) {
    switch(f) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}
// One of HDRP's wave buffers (a square 2D array, a slice a band) into a copy shared with D3D12: each slice's top mip, the
// copy made again when the size, the slices or the format change. False when the source is no such texture on the game's device.
bool copy_waves(const ComPtr<ID3D11Resource>& from, State::WaveCopy& c) {
    ComPtr<ID3D11Texture2D> source;
    if(!from || FAILED(from.As(&source)) || !on_game_device(source.Get())) return false;
    D3D11_TEXTURE2D_DESC d{}; source->GetDesc(&d);
    const DXGI_FORMAT format=wave_format(d.Format);
    if(format==DXGI_FORMAT_UNKNOWN || d.Width!=d.Height || d.Width<8 || d.Width>4096 || d.ArraySize<1 || d.ArraySize>3 || d.SampleDesc.Count!=1 || d.MipLevels<1) return false;
    if(!c.d11 || c.size!=d.Width || c.slices!=d.ArraySize || c.format!=format) {
        if(c.d12) st.release.add(st.consumer.value, c.d12.Get());
        st.heap.release(c.srv);
        c=State::WaveCopy{};
        D3D11_TEXTURE2D_DESC t{}; t.Width=t.Height=d.Width; t.MipLevels=1; t.ArraySize=d.ArraySize; t.Format=format;
        t.SampleDesc.Count=1; t.Usage=D3D11_USAGE_DEFAULT; t.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        t.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        check(st.d11->CreateTexture2D(&t, nullptr, &c.d11), "Create a copy of the water's waves");
        ComPtr<IDXGIResource1> dxgi; check(c.d11.As(&dxgi), "Query the waves' copy's IDXGIResource1");
        tgb::Handle shared;
        check(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE, nullptr, &shared.value), "Share the waves' copy");
        check(st.d12->OpenSharedHandle(shared.value, IID_PPV_ARGS(&c.d12)), "Open the waves' copy on D3D12");
        D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=format; v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Texture2DArray.MipLevels=1; v.Texture2DArray.ArraySize=d.ArraySize;
        c.srv=st.heap.allocate(); st.d12->CreateShaderResourceView(c.d12.Get(), &v, st.heap.cpu(c.srv));
        c.size=d.Width; c.slices=d.ArraySize; c.format=format;
    }
    for(UINT s=0;s<d.ArraySize;++s)
        st.c11->CopySubresourceRegion(c.d11.Get(), D3D11CalcSubresource(0, s, 1), 0, 0, 0, source.Get(), D3D11CalcSubresource(0, s, d.MipLevels), nullptr);
    return true;
}
// HDRP's waves for this frame (TGPT_SetWaterCaustics), copied on D3D11 into this parity's textures shared with D3D12. A slot
// whose buffers can't be copied keeps its water, still (its light reflected and refracted by the level surface).
void copy_caustics(const std::array<Engine::Caustics, TGPT_MAX_WATER>& from, uint32_t parity) {
    for(uint32_t i=0;i<TGPT_MAX_WATER;++i) {
        auto& c=st.caustics[parity][i];
        c.live=from[i].live; c.waves=false; c.desc=from[i].desc;
        if(!c.live || !from[i].displacement) continue;
        c.waves=copy_waves(from[i].displacement, c.displacement) && copy_waves(from[i].normals, c.normals) &&
            c.normals.size==c.displacement.size && c.normals.slices==c.displacement.slices;
        // Said once a session: HDRP's buffers in a form this copy doesn't take (the slot's water stays still).
        static bool told=false;
        if(!c.waves && !told) {
            told=true;
            auto describe=[](const ComPtr<ID3D11Resource>& r) {
                ComPtr<ID3D11Texture2D> t; D3D11_TEXTURE2D_DESC d{};
                if(!r || FAILED(r.As(&t))) return std::string("no 2D texture");
                t->GetDesc(&d);
                return std::to_string(d.Width)+"x"+std::to_string(d.Height)+" x"+std::to_string(d.ArraySize)+" format "+std::to_string(int(d.Format))+" mips "+std::to_string(d.MipLevels);
            };
            log("The water's waves could not be copied (displacement "+describe(from[i].displacement)+", gradient "+describe(from[i].normals)+
                "): that water is traced still");
        }
    }
}

// What a frame's composite needs: the frame's own traced light, and the input set the frame copied.
struct CompositeWork { Result traced; uint32_t parity=0; bool inputs=false; };
std::array<CompositeWork, 8> compositeWork;

// PT-107 (R19), a MEASUREMENT, only while a file named "measure-waits" lies in BepInEx\TGPathTracing\Native when the first
// frame is traced (never for players): the game's own calls on its D3D11 immediate context that can wait for the card,
// timed where they are made (the render thread): Map (a read back waits until the card is done with the resource),
// GetData asked again and again of one query until it answers (a spin on an event query waits for the card), Flush. Ours
// are left out (the bridge's internal mark). The 10-second line's "the game's waits on its device".
namespace waits {
using MapFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using GetDataFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11DeviceContext*, ID3D11Asynchronous*, void*, UINT, UINT);
using FlushFn=void(STDMETHODCALLTYPE*)(ID3D11DeviceContext*);
constexpr size_t kMap=14, kGetData=29, kFlush=111; // ID3D11DeviceContext's vtable
std::atomic<MapFn> map{nullptr}; std::atomic<GetDataFn> getData{nullptr}; std::atomic<FlushFn> flush{nullptr};
bool checked=false, on=false;
std::mutex mutex; // the sums (the render thread writes, the 10-second line reads and clears)
struct Sums { uint64_t maps=0, mapSlow=0; double mapMs=0, mapMax=0; std::string mapWhat, mapCaller; uint64_t spins=0; double spinMs=0, spinMax=0;
              std::string spinWhat; uint64_t flushSlow=0; double flushMs=0; std::array<uint32_t, 64> slowAfter{}; std::array<double, 64> slowAfterMs{}; } sums;
thread_local uint32_t lastMark=0; // the last render event 8's mark this thread ran (render_event)
// Who called: the modules (and offsets) on the stack of a slow call, the hook's own frames left out.
std::string caller() {
    void* frames[14]{}; const USHORT n=RtlCaptureStackBackTrace(2, 14, frames, nullptr);
    std::string s;
    for(USHORT i=0;i<n;++i) {
        HMODULE module=nullptr; char name[MAX_PATH]{};
        std::string here;
        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(frames[i]), &module) && module &&
           GetModuleFileNameA(module, name, MAX_PATH)) {
            const char* base=std::strrchr(name, '\\'); base=base ? base+1 : name;
            char offset[32]; std::snprintf(offset, sizeof(offset), "+0x%llx", static_cast<unsigned long long>(static_cast<const char*>(frames[i])-reinterpret_cast<const char*>(module)));
            here=std::string(base)+offset;
        } else here="(no module: managed code)";
        s+=(s.empty() ? "" : " < ")+here;
    }
    return s;
}
thread_local ID3D11Asynchronous* spinning=nullptr; thread_local int64_t spinSince=0;
double ms_since(int64_t from) {
    static const double perMs=[] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return double(f.QuadPart)/1000.0; }();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now); return double(now.QuadPart-from)/perMs;
}
int64_t now_qpc() { LARGE_INTEGER now{}; QueryPerformanceCounter(&now); return now.QuadPart; }
HRESULT STDMETHODCALLTYPE map_detour(ID3D11DeviceContext* c, ID3D11Resource* r, UINT sub, D3D11_MAP type, UINT flags, D3D11_MAPPED_SUBRESOURCE* out) {
    MapFn original=map.load();
    if(!original) { original=reinterpret_cast<MapFn>(host().vtable_original(c, kMap)); map=original; }
    if(host().internal()) return original(c, r, sub, type, flags, out);
    const int64_t from=now_qpc();
    const HRESULT hr=original(c, r, sub, type, flags, out);
    const double took=ms_since(from);
    std::lock_guard<std::mutex> lock(mutex);
    ++sums.maps;
    if(took>=0.5) {
        ++sums.mapSlow; sums.mapMs+=took;
        ++sums.slowAfter[lastMark%64u]; sums.slowAfterMs[lastMark%64u]+=took;
        if(took>sums.mapMax) {
            sums.mapMax=took; sums.mapCaller=caller()+" (after mark "+std::to_string(lastMark)+")";
            D3D11_RESOURCE_DIMENSION dimension{}; if(r) r->GetType(&dimension);
            UINT64 bytes=0; D3D11_USAGE usage=D3D11_USAGE_DEFAULT;
            ComPtr<ID3D11Buffer> buffer; ComPtr<ID3D11Texture2D> texture;
            if(r && SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&buffer)))) { D3D11_BUFFER_DESC d{}; buffer->GetDesc(&d); bytes=d.ByteWidth; usage=d.Usage; }
            else if(r && SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&texture)))) { D3D11_TEXTURE2D_DESC d{}; texture->GetDesc(&d); bytes=UINT64(d.Width)*d.Height; usage=d.Usage; }
            sums.mapWhat=std::string(dimension==D3D11_RESOURCE_DIMENSION_BUFFER ? "a buffer" : "a texture")+" of "+std::to_string(bytes)+
                (dimension==D3D11_RESOURCE_DIMENSION_BUFFER ? " bytes" : " texels")+(usage==D3D11_USAGE_STAGING ? ", staging" : usage==D3D11_USAGE_DYNAMIC ? ", dynamic" : "")+
                ", map type "+std::to_string(int(type))+", flags "+std::to_string(flags);
        }
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE get_data_detour(ID3D11DeviceContext* c, ID3D11Asynchronous* q, void* data, UINT size, UINT flags) {
    GetDataFn original=getData.load();
    if(!original) { original=reinterpret_cast<GetDataFn>(host().vtable_original(c, kGetData)); getData=original; }
    if(host().internal()) return original(c, q, data, size, flags);
    const HRESULT hr=original(c, q, data, size, flags);
    if(hr==S_FALSE) { if(spinning!=q) { spinning=q; spinSince=now_qpc(); } return hr; }
    if(spinning==q && spinSince) {
        const double took=ms_since(spinSince); spinning=nullptr; spinSince=0;
        if(took>=0.5) {
            std::lock_guard<std::mutex> lock(mutex);
            ++sums.spins; sums.spinMs+=took;
            if(took>sums.spinMax) {
                sums.spinMax=took;
                ComPtr<ID3D11Query> query; D3D11_QUERY_DESC d{};
                if(q && SUCCEEDED(q->QueryInterface(IID_PPV_ARGS(&query)))) query->GetDesc(&d);
                sums.spinWhat="query type "+std::to_string(int(d.Query))+", flags "+std::to_string(flags);
            }
        }
    }
    return hr;
}
void STDMETHODCALLTYPE flush_detour(ID3D11DeviceContext* c) {
    FlushFn original=flush.load();
    if(!original) { original=reinterpret_cast<FlushFn>(host().vtable_original(c, kFlush)); flush=original; }
    if(host().internal()) { original(c); return; }
    const int64_t from=now_qpc();
    original(c);
    const double took=ms_since(from);
    if(took>=0.5) { std::lock_guard<std::mutex> lock(mutex); ++sums.flushSlow; sums.flushMs+=took; }
}
// Once, at the first traced frame: the file decides.
void install(ID3D11DeviceContext* c) {
    if(checked || !c) return;
    checked=true;
    const wchar_t* root=host().bepinex_root();
    if(!root) return;
    const std::wstring file=std::wstring(root)+L"\\TGPathTracing\\Native\\measure-waits";
    if(GetFileAttributesW(file.c_str())==INVALID_FILE_ATTRIBUTES) return;
    const bool a=host().add_vtable_hook(c, kMap, reinterpret_cast<void*>(&map_detour));
    const bool b=host().add_vtable_hook(c, kGetData, reinterpret_cast<void*>(&get_data_detour));
    const bool d=host().add_vtable_hook(c, kFlush, reinterpret_cast<void*>(&flush_detour));
    on=a || b || d;
    log(std::string("PT-107 measurement (measure-waits): the game's Map, GetData and Flush on its device are timed: ")+(a ? "Map " : "")+(b ? "GetData " : "")+(d ? "Flush" : "")+
        (on ? "" : "none could be hooked"));
}
// The 10-second line's part, a frame's mean; the sums start over.
std::string line(uint32_t frames) {
    if(!on || !frames) return std::string();
    std::lock_guard<std::mutex> lock(mutex);
    char b[64]; auto ms=[&](double v) { std::snprintf(b, sizeof(b), "%.2f", v); return std::string(b); };
    std::string s=" the game's waits on its device (measure-waits; a frame's mean over "+std::to_string(frames)+" frames): Map "+ms(double(sums.maps)/frames)+
        " calls, of them "+ms(double(sums.mapSlow)/frames)+" of 0.5 ms or more, "+ms(sums.mapMs/frames)+" ms (the slowest "+ms(sums.mapMax)+" ms: "+
        (sums.mapWhat.empty() ? std::string("-") : sums.mapWhat)+"); a query asked until it answered, 0.5 ms or more: "+ms(double(sums.spins)/frames)+", "+
        ms(sums.spinMs/frames)+" ms (the longest "+ms(sums.spinMax)+" ms: "+(sums.spinWhat.empty() ? std::string("-") : sums.spinWhat)+"); Flush of 0.5 ms or more "+
        ms(double(sums.flushSlow)/frames)+", "+ms(sums.flushMs/frames)+" ms; the slowest Map's callers: "+(sums.mapCaller.empty() ? std::string("-") : sums.mapCaller)+
        "; slow Maps by the frame's last mark before them:";
    for(size_t i=0;i<sums.slowAfter.size();++i) if(sums.slowAfter[i]) s+=" "+std::to_string(i)+": "+ms(double(sums.slowAfter[i])/frames)+" a frame, "+ms(sums.slowAfterMs[i]/frames)+" ms;";
    sums=Sums{};
    return s;
}
} // namespace waits

// PT-107 (R19): State::CpuClock. The CPU's clock now, into the frame's record (when the record is the frame's).
void cpu_clock(uint32_t sequence, uint32_t which) {
    auto& c=st.cpuClocks[sequence%uint32_t(st.cpuClocks.size())];
    if(c.sequence!=sequence) return;
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now); c.at[which]=now.QuadPart; c.reached|=1u<<which;
}
// A frame's D3D12 trace (its first and last stamp, read some frames later) on the CPU's clock, against that frame's record.
void cpu_clock_trace(uint32_t sequence, uint64_t begin, uint64_t end, uint64_t calGpu, uint64_t calCpu) {
    auto& c=st.cpuClocks[sequence%uint32_t(st.cpuClocks.size())];
    constexpr uint32_t needed=(1u<<0)|(1u<<2)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<8);
    if(c.sequence!=sequence || (c.reached&needed)!=needed || !calCpu || st.ticksPerMs<=0) return;
    static const double qpcPerMs=[] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return double(f.QuadPart)/1000.0; }();
    const double calMs=double(calCpu)/qpcPerMs;
    auto gpu=[&](uint64_t t) { return calMs+double(int64_t(t-calGpu))/st.ticksPerMs; };
    auto cpu=[&](int i) { return double(c.at[i])/qpcPerMs; };
    const double traceEnd=gpu(end);
    st.summaryCpuClock[0]+=gpu(begin)-cpu(8); st.summaryCpuClock[1]+=cpu(2)-traceEnd; st.summaryCpuClock[2]+=cpu(6)-traceEnd;
    st.summaryCpuClock[3]+=cpu(4)-traceEnd; st.summaryCpuClock[4]+=cpu(6)-cpu(5); st.summaryCpuClock[5]+=cpu(4)-cpu(7);
    ++st.summaryCpuClockFrames;
    c.reached=0;
    // The next frame's start on the main thread and the render thread (written before its render event 1, which came before
    // this read).
    Engine::MainClock m{};
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); m=e.mainClocks[(sequence+1u)%e.mainClocks.size()]; }
    const auto& mark=st.renderMarks[m.frameCount%uint32_t(st.renderMarks.size())];
    bool whole=m.sequence==sequence+1u && mark.frameCount==m.frameCount;
    for(auto q:m.at) whole=whole && q;
    for(auto q:mark.at) whole=whole && q;
    if(whole) {
        auto at=[&](int64_t q) { return double(q)/qpcPerMs-traceEnd; };
        const double next[10]={at(m.at[0]), at(mark.at[0]), at(m.at[1]), at(mark.at[1]), at(m.at[2]), at(m.at[3]), at(m.at[4]),
                               at(m.at[5]), at(mark.at[2]), at(mark.at[3])};
        for(int i=0;i<10;++i) st.summaryNextFrame[i]+=next[i];
        ++st.summaryNextFrames;
    }
}
// PT-107 (R19): State::D11Stamps. One timestamp of the game's device into the frame's slot (when the slot is the frame's).
void d11_stamp(uint32_t sequence, uint32_t which) {
    cpu_clock(sequence, which);
    auto& s=st.d11Stamps[sequence%uint32_t(st.d11Stamps.size())];
    if(!s.open || s.sequence!=sequence || !s.at[which] || !st.c11) return;
    tgb::InternalScope internal(&host());
    st.c11->End(s.at[which].Get()); s.reached|=1u<<which;
}
// [8] and [9] (render event 8): into the slot still open, the frame before the next frame's trace.
void d11_stamp_open(uint32_t which) {
    if(st.d11Open<0 || !st.c11) return;
    auto& s=st.d11Stamps[uint32_t(st.d11Open)];
    if(!s.open || !s.at[which] || (s.reached&(1u<<which))) return;
    tgb::InternalScope internal(&host());
    st.c11->End(s.at[which].Get()); s.reached|=1u<<which;
}
// The slots whose results are in: their four stretches into the 10-second line's sums. Never waits.
void d11_stamps_collect() {
    for(auto& s:st.d11Stamps) {
        if(!s.pending) continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{};
        if(st.c11->GetData(s.disjoint.Get(), &clock, sizeof clock, D3D11_ASYNC_GETDATA_DONOTFLUSH)!=S_OK) continue;
        s.pending=false;
        if(clock.Disjoint || !clock.Frequency || (s.reached&0x3Fu)!=0x3Fu) continue;
        UINT64 t[15]{}; bool ok=true;
        for(int i=0;i<15 && ok;++i) ok=!(s.reached&(1u<<i)) || st.c11->GetData(s.at[i].Get(), &t[i], sizeof t[i], D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK;
        const bool fog=(s.reached&0xC0u)==0xC0u;
        ok=ok && t[0]<=t[1] && t[1]<=t[2] && t[2]<=t[3] && t[3]<=t[5] && t[5]<=t[4] && (!fog || (t[5]<=t[6] && t[6]<=t[7] && t[7]<=t[4]));
        if(!ok) continue;
        const double ms=1000.0/double(clock.Frequency), fogMs=fog ? double(t[7]-t[6])*ms : 0.0;
        st.summaryD11[0]+=double(t[1]-t[0])*ms; st.summaryD11[1]+=double(t[2]-t[1])*ms; st.summaryD11[2]+=double(t[3]-t[2])*ms;
        st.summaryD11[3]+=double(t[5]-t[3])*ms; st.summaryD11[4]+=fogMs; st.summaryD11[5]+=double(t[4]-t[5])*ms-fogMs;
        if(fog) { st.summaryD11ToFog+=double(t[6]-t[5])*ms; ++st.summaryD11FogFrames; }
        if(fog && (s.reached&0x300u)==0x300u && t[7]<=t[8] && t[8]<=t[9] && t[9]<=t[4]) {
            st.summaryD11Next[0]+=double(t[8]-t[7])*ms; st.summaryD11Next[1]+=double(t[9]-t[8])*ms; st.summaryD11Next[2]+=double(t[4]-t[9])*ms;
            ++st.summaryD11NextFrames;
            if((s.reached&0x7C00u)==0x7C00u && t[7]<=t[12] && t[12]<=t[13] && t[13]<=t[14] && t[14]<=t[8] && t[9]<=t[10] && t[10]<=t[11] && t[11]<=t[4]) {
                const UINT64 at[9]={t[7], t[12], t[13], t[14], t[8], t[9], t[10], t[11], t[4]};
                for(int i=0;i<8;++i) st.summaryHdrp[i]+=double(at[i+1]-at[i])*ms;
                ++st.summaryHdrpFrames;
            }
        }
        ++st.summaryD11Frames;
    }
}
// At render event 1's start: the frame before is closed (its [4]) and this frame's slot opened ([0]).
void d11_stamps_begin(uint32_t sequence) {
    {   // the CPU's clock (State::CpuClock): the frame before's [4], this frame's record opened
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        auto& before=st.cpuClocks[(sequence-1u)%uint32_t(st.cpuClocks.size())];
        if(before.sequence==sequence-1u && before.reached) { before.at[4]=now.QuadPart; before.reached|=1u<<4; }
        auto& c=st.cpuClocks[sequence%uint32_t(st.cpuClocks.size())];
        c=State::CpuClock{}; c.sequence=sequence; c.at[0]=now.QuadPart; c.reached=1u;
    }
    if(st.d11StampsFailed || !st.d11 || !st.c11) return;
    if(!st.d11Stamps[0].disjoint) {
        const D3D11_QUERY_DESC clock{D3D11_QUERY_TIMESTAMP_DISJOINT, 0}, stamp{D3D11_QUERY_TIMESTAMP, 0};
        for(auto& s:st.d11Stamps) {
            bool made=SUCCEEDED(st.d11->CreateQuery(&clock, &s.disjoint));
            for(auto& q:s.at) made=made && SUCCEEDED(st.d11->CreateQuery(&stamp, &q));
            if(!made) { st.d11Stamps={}; st.d11StampsFailed=true; log("The game's device's timestamps could not be made: no split of its frame in the 10-second line"); return; }
        }
    }
    if(st.d11Open>=0) {
        auto& previous=st.d11Stamps[uint32_t(st.d11Open)];
        if(previous.open) {
            st.c11->End(previous.at[4].Get()); previous.reached|=1u<<4;
            st.c11->End(previous.disjoint.Get()); previous.open=false; previous.pending=true;
        }
        st.d11Open=-1;
    }
    d11_stamps_collect();
    const uint32_t index=sequence%uint32_t(st.d11Stamps.size());
    auto& s=st.d11Stamps[index];
    s.pending=false; s.sequence=sequence; s.reached=0; s.open=true; // (a slot not read in four frames is given up)
    st.c11->Begin(s.disjoint.Get());
    st.d11Open=int(index);
    d11_stamp(sequence, 0);
}

void trace_event(uint32_t sequence) {
    TGPT_Frame frame{};
    ComPtr<ID3D11Buffer> skinSource; uint32_t skinStride=0; std::vector<TGPT_SkinnedInstance> skinned;
    float windTime=0; std::vector<TGPT_ClothWind> winds;
    TGPT_PlantWindFrame plantFrame{}; std::vector<TGPT_PlantWind> plantWinds; std::shared_ptr<const WindNoise> windNoise;
    std::shared_ptr<const SceneList> sceneList; std::shared_ptr<const MovingList> moving;
    std::array<Engine::Caustics, TGPT_MAX_WATER> caustics;
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& slot=e.frames[sequence%e.frames.size()];
        if(!slot.occupied || slot.sequence!=sequence) { log_throttled("Path tracing event for an unknown frame"); return; }
        frame=slot.frame;
        skinSource=slot.skinSource; skinStride=slot.skinStride; skinned=slot.skinned;
        windTime=slot.windTime; winds=slot.winds;
        plantFrame=slot.plantFrame; plantWinds=slot.plantWinds; windNoise=slot.windNoise;
        st.rain=slot.rain; // PT-108, on trial: this frame's rain (trace reads it)
        st.fogBoxes=slot.fogBoxes; // PT-112: this frame's fog boxes
        st.skyModel=slot.skyModel; // PT-112, the sky (on trial): this frame's
        sceneList=slot.scene; moving=slot.moving;
        caustics=e.caustics;
    }
    const auto started=std::chrono::steady_clock::now();
    st.lastTraceTick=GetTickCount64(); // (PT-87: frames are being handed over; release_frame_memory waits for them to stop)
    auto& work=compositeWork[sequence%compositeWork.size()];
    work=CompositeWork{};
    // Its passes are still being made (start_passes): a frame handed over before the plugin saw TGPT_Status.starting goes
    // untraced, which is no failure.
    if(passes_starting()) return;
    const uint32_t parity=uint32_t(st.frame+1)&1u;
    TallyCall counting; counting.enter(st.d12.Get()); // PT-87 (memory3): this call in Windows' count, for ten minutes from the start
    st.scene.set_clock(frame_clock(started)); // PT-98: this frame's budgets, by the time since the last one
    bool produced=false, traced=false; uint64_t retire=0;
    tgb::InternalScope internal(&host());
    try {
        if(st.broken) throw DxError(E_FAIL, "the tracer is off after a D3D12 error");
        if(!ensure_d3d11()) throw DxError(E_FAIL, "the game's D3D11 device has not been observed");
        if(!ensure_d3d12()) throw DxError(E_FAIL, "TG Graphics Bridge created no D3D12 device");
        waits::install(st.c11.Get()); // (PT-107: a measurement, only with the file measure-waits)
        d11_stamps_begin(sequence); // (PT-107: the game's device's clock, [0])
        if(st.consumer.value) { ++st.summaryCpuLateOf; if(st.consumer.d12->GetCompletedValue()>=st.consumer.value) ++st.summaryCpuLate; } // (PT-107, R19)
        // Descriptors released from here on may be read by every frame up to the one about to be traced.
        st.heap.set_retire(st.consumer.value+1);
        st.heap.collect(st.consumer.d12->GetCompletedValue());
        // The game's textures opened since the last frame: their views into the heap (PT-45; no pass reads them yet).
        share::publish(st.d12.Get(), st.heap, st.sharedBase, st.consumer.d12.Get(), st.consumer.value+1);
        const bool cleared=st.scene.intake(&st.heap, st.release, st.consumer.value+1);
        if(cleared) release_scene_extras();
        // The scene's list this frame was submitted with, and the things the game moved in it then (TGPT_SetMoving).
        st.scene.set_lists(sceneList, moving);
        auto lap=[last=std::chrono::steady_clock::now()](double& into) mutable {
            const auto now=std::chrono::steady_clock::now(); into=std::chrono::duration<double, std::milli>(now-last).count(); last=now; };
        {
            tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
            // A cleared scene hands its alpha tiles out again: opaque until the new textures are baked, not the old ones' alpha.
            // Its color tiles too: white, as the atlas starts (a material's own color, a mask map's dielectric), not an old
            // texture's colors read as a mask map or the other way round.
            // (PT-87, memory4: the game's device takes its side of both up again for it. Should one not open, its clear is
            // left for a later frame, and the scene's bakes wait for it: hold_sides. Clearing the other once more then
            // changes nothing, nothing having been baked meanwhile.)
            if(st.sceneResourcesDirty) { take_up_side(kSideAlpha); take_up_side(kSideColour); }
            if(st.sceneResourcesDirty && st.alphaAtlas.t.uav) {
                wait_for_scene_readers();
                const float one[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.alphaAtlas.t.uav.Get(), one);
                if(st.atlas.t.uav) st.c11->ClearUnorderedAccessViewFloat(st.atlas.t.uav.Get(), one);
                st.sides[kSideAlpha].written=true; st.sides[kSideColour].written=st.atlas.t.uav!=nullptr;
                st.sceneResourcesDirty=!st.atlas.t.uav;
            }
            st.scene.pump_d3d11(st.d11.Get(), st.c11.Get(), st.frame);
            bake_textures();
            bake_hair();
            bake_hair_lobes();
            bake_eyes();
            apply_texture_swaps();
            bake_alpha_textures();
            bake_cookies();
            tend_sides(); // (PT-87, memory4: an atlas no frame has baked into for a while: the game's device lets go of its side)
            // (PT-112, the sky, on trial: with the tracer's own sky the rays read its map, and HDRP's is no longer resampled
            // here once there is one for Ray Reconstruction's guides, nor waited for.)
            if(!(st.skyModel.mode==TGPT_SKY_OWN && st.skyValid && !st.skyFailed)) update_sky();
            ensure_frame_textures(frame.renderWidth, frame.renderHeight);
            // D3D12 must be done with the frame that last read this input set (two frames ago) before it is overwritten.
            const uint64_t busy=st.inputsRetire[parity];
            if(busy && st.consumer.d12->GetCompletedValue()<busy) check(st.c11->Wait(st.consumer.d11.Get(), busy), "D3D11 wait before reusing an input set");
            copy_inputs(frame, st.inputs[parity]);
            try { copy_skinned(skinSource, skinStride, skinned, parity); }
            catch(const std::exception& e) { st.skinWork[parity].clear(); log_throttled(std::string("Characters could not be copied: ")+e.what()); }
            try { copy_caustics(caustics, parity); }
            catch(const std::exception& e) { for(auto& c:st.caustics[parity]) c.live=false; log_throttled(std::string("The water's caustics could not be copied: ")+e.what()); }
            work.inputs=true; work.parity=parity;
        }
        lap(st.phase[0]);
        counting.adopt(); counting.mark(st.d12.Get(), tally.d3d11);
        const uint64_t producedValue=st.producer.next();
        check(st.c11->Signal(st.producer.d11.Get(), producedValue), "Signal path tracing inputs");
        d11_stamp(sequence, 1); // (PT-107: [1], the inputs handed to the D3D12 queue)
        st.c11->Flush();
        cpu_clock(sequence, 8); // (PT-107: the CPU's clock once they are flushed)
        lap(st.phase[1]);
        check(st.queue->Wait(st.producer.d12.Get(), producedValue), "D3D12 wait for path tracing inputs");
        produced=true;
        counting.mark(st.d12.Get(), tally.flush);
        // The wind on swaying cloth, in the frame HDRP drew: its pose is made where the scene is recorded (pt_sway).
        st.scene.set_wind(windTime, winds); st.scene.swayPass=st.sway.Get();
        // And on grass and leaves (PT-63): every such instance posed as the game's vertex shaders pose it this frame.
        st.scene.set_plant_wind(plantFrame, plantWinds, windNoise);
        st.wetClock=windTime;
        // The game's global saturation, which the material programs apply as its shaders do (PT-64, TGPT_Frame.forward.w).
        st.scene.set_program_saturation(std::isfinite(frame.forward[3]) ? std::clamp(frame.forward[3], -1.f, 4.f) : 0.f);
        st.traceSequence=sequence; // (PT-107: State::CpuClock)
        retire=trace(frame, parity);
        const HRESULT signalled=st.queue->Signal(st.consumer.d12.Get(), retire);
        if(FAILED(signalled)) { st.broken=true; throw DxError(signalled, "D3D12 signal after path tracing"); }
        st.consumer.value=retire;
        st.inputsRetire[parity]=retire;
        if(st.scene.waterRecordCount) st.inputsRetire[1-parity]=retire; // water motion also read the previous simulation
        traced=true;
        st.failures=0;
        counting.mark(st.d12.Get(), tally.passes);
    } catch(const DxError& e) {
        fail(e.what(), e.hr);
    } catch(const std::exception& e) {
        fail(e.what(), E_FAIL);
    }
    if(st.recording) {
        // The scene marked meshes built and resources transitioned for a list that never runs: nothing it
        // says can be trusted any more, so path tracing stays off for the session.
        st.recording=false; st.broken=true;
        log("A path tracing frame failed while it was being recorded; path tracing stays off for the rest of this session");
    }
    if(produced && !traced && st.queue && st.consumer.ready()) {
        // The D3D12 side never ran this frame's work; its fence still moves on so nothing waits on it.
        const uint64_t v=st.consumer.value+1;
        if(SUCCEEDED(st.queue->Signal(st.consumer.d12.Get(), v))) { st.consumer.value=v; st.inputsRetire[parity]=v; }
    }
    ++st.frame;
    // The frame's own composite (render event 2, later in this frame) takes this light, once the GPU is at `retire`.
    if(traced) { work.traced.valid=true; work.traced.retire=retire; work.traced.width=frame.renderWidth; work.traced.height=frame.renderHeight; }
    const double ms=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-started).count();
    counting.adopt(); counting.leave(st.d12.Get(), tally.rest); // (a failed frame's moves are in "the end of a frame's call")
    publish_status(frame, ms, traced);
    if(traced) summarise(frame, ms);
    // PT-87 (memory3): after the first, the second and the tenth traced frame of a start, what the count has done since it
    // began (then with every memory line, start_summary).
    if(traced && tally.on) {
        ++tally.frames;
        if(tally.frames==1 || tally.frames==2 || tally.frames==10) {
            if(tally.frames==1) {
                char line[320];
                std::snprintf(line, sizeof line, "PT memory at start: the first traced frame, apart from the steps above, %+.0f MB (Windows counts %.0f MB): its D3D11 side "
                              "%+.0f, the flush and the wait for it %+.0f, recording and running the passes %+.0f",
                              tally.d3d11+tally.flush+tally.passes+tally.rest-tally.stepsDevice-tally.stepsPasses, tally.lastExit,
                              tally.d3d11-tally.stepsDevice, tally.flush, tally.passes+tally.rest-tally.stepsPasses);
                log(line);
            }
            const uint32_t said=tally.summaries;
            publish_memory(true);
            if(tally.summaries==said) start_summary(st.d12.Get(), st.accountedBytes, st.accountedTransfer);
        }
    }
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    auto& slot=e.frames[sequence%e.frames.size()];
    if(slot.occupied && slot.sequence==sequence) { slot.traced=work.inputs; slot.retire=retire; }
}

// DLSS Ray Reconstruction's guides from this frame's GBuffer copies, into textures of exactly the render
// size that TGPT_GetGuides hands out. A size change replaces them; the old ones live on for eight frames,
// since the plugin may already have handed their pointers to TG Better Upscaling.
void write_guides(const TGPT_Frame& frame, const Inputs& in) {
    const uint32_t w=frame.renderWidth, h=frame.renderHeight;
    while(!st.retiredGuides.empty() && st.retiredGuides.front().first+8<=st.frame) st.retiredGuides.erase(st.retiredGuides.begin());
    if(!st.guides[0].texture || st.guideWidth!=w || st.guideHeight!=h) {
        for(auto& g:st.guides) if(g.texture) st.retiredGuides.emplace_back(st.frame, g.texture);
        const DXGI_FORMAT formats[5]={DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
        for(int i=0;i<5;++i) {
            D3D11_TEXTURE2D_DESC d{}; d.Width=w; d.Height=h; d.MipLevels=1; d.ArraySize=1; d.Format=formats[i]; d.SampleDesc.Count=1;
            d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
            st.guides[i]=State::Guide{};
            try {
                check(st.d11->CreateTexture2D(&d, nullptr, &st.guides[i].texture), "Create Ray Reconstruction guide");
                check(st.d11->CreateUnorderedAccessView(st.guides[i].texture.Get(), nullptr, &st.guides[i].uav), "Create Ray Reconstruction guide UAV");
            } catch(const DxError& error) {
                if(i!=4) throw; // material guides and reflection distance are required
                st.guides[i]=State::Guide{};
                log_throttled(std::string("RR's optional transparency snapshot could not be allocated: ")+error.what());
            }
        }
        st.guideWidth=w; st.guideHeight=h;
    }
    CompositeConstants c{};
    std::memcpy(c.invViewProj, frame.invViewProj, sizeof(c.invViewProj));
    c.frameSize[0]=w; c.frameSize[1]=h;
    // Reflection Boost: the guides describe the surfaces as the tracer shades them (not skin: the thick profiles).
    c.cameraDelta[3]=std::isfinite(frame.reflectionBoost) ? std::clamp(frame.reflectionBoost, 0.f, 1.f) : 0.f;
    c.extras[1]=frame.thickProfiles;
    // And a leaf's reflectance, its diffusion profile's (pt_prepare), so that Ray Reconstruction's specular guide agrees.
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); std::memcpy(c.profileF0, e.profileF0.data(), sizeof(c.profileF0)); }
    st.c11->UpdateSubresource(st.compositeConstants.Get(), 0, nullptr, &c, 0, 0);
    {
        tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
        ID3D11ShaderResourceView* srvs[4]={in.gb0.t.srv.Get(), in.nrm.t.srv.Get(), in.gb2.t.srv.Get(), in.depth.t.srv.Get()};
        ID3D11UnorderedAccessView* uavs[3]={st.guides[0].uav.Get(), st.guides[1].uav.Get(), st.guides[2].uav.Get()};
        ID3D11Buffer* cbs[2]={st.constants.Get(), st.compositeConstants.Get()};
        st.c11->CSSetShader(st.csGuides.Get(), nullptr, 0); st.c11->CSSetConstantBuffers(0, 2, cbs);
        st.c11->CSSetShaderResources(0, 4, srvs); st.c11->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
        st.c11->Dispatch((w+7)/8, (h+7)/8, 1);
        ID3D11ShaderResourceView* noSrvs[4]{}; st.c11->CSSetShaderResources(0, 4, noSrvs);
        ID3D11UnorderedAccessView* noUavs[3]{}; st.c11->CSSetUnorderedAccessViews(0, 3, noUavs, nullptr);
    }
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    e.guides.width=w; e.guides.height=h; e.guides.frameIndex=frame.frameIndex;
    e.guides.diffuseAlbedo=uint64_t(uintptr_t(st.guides[0].texture.Get()));
    e.guides.specularAlbedo=uint64_t(uintptr_t(st.guides[1].texture.Get()));
    e.guides.normalRoughness=uint64_t(uintptr_t(st.guides[2].texture.Get()));
}

// PT-112 (A), ON TRIAL (TGPT_FRAME_OWN_DEPTH): the traced frame's own depth and motion (outputs.dm, pt_compose) written into
// HDRP's depth buffer and motion vectors (outputs.dm and .mv) with a full-screen draw (a depth buffer takes no compute writes on D3D11), so that
// what reads them after the composite sees the traced picture's geometry: with HDRP's opaque drawing left out, the only
// depth there is. The stencil is left as HDRP has it. Its own views each frame (HDRP's buffers come and go with the size).
bool write_depth_motion(const TGPT_Frame& frame, uint32_t w, uint32_t h, uint64_t pyramidHandle) {
    ++st.summaryOwnAsked;
    if(!st.vsDepthMotion || !st.psDepthMotion || !st.depthAlways || !st.outputs.dm.t.srv || !st.outputs.mv.t.srv) {
        log_throttled(std::string("PT-112: the depth and motion pass is not there (")+(st.vsDepthMotion ? "" : "no VS ")+(st.psDepthMotion ? "" : "no PS ")+
                      (st.depthAlways ? "" : "no state ")+(st.outputs.dm.t.srv ? "" : "no depth output ")+(st.outputs.mv.t.srv ? "" : "no motion output")+")");
        return false;
    }
    auto* depth=reinterpret_cast<ID3D11Resource*>(uintptr_t(frame.depth));
    auto* motion=reinterpret_cast<ID3D11Resource*>(uintptr_t(frame.motion));
    if(!depth || !motion || !on_game_device(depth) || !on_game_device(motion)) { log_throttled("PT-112: HDRP's depth or motion vectors are missing; the traced frame's own are not written"); return false; }
    const auto dd=texture_desc(depth), md=texture_desc(motion);
    if(dd.Width<w || dd.Height<h || md.Width<w || md.Height<h) { log_throttled("PT-112: HDRP's depth or motion vectors are smaller than the render size"); return false; }
    DXGI_FORMAT dsvFormat=DXGI_FORMAT_UNKNOWN;
    switch(dd.Format) {
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: dsvFormat=DXGI_FORMAT_D32_FLOAT_S8X24_UINT; break;
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: dsvFormat=DXGI_FORMAT_D32_FLOAT; break;
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: dsvFormat=DXGI_FORMAT_D24_UNORM_S8_UINT; break;
    default: break;
    }
    // (Motion's own reading: HDRP's are R16G16 typeless, 16-bit floats. The colour reading had no type for them: run
    // 20261011-003003 wrote nothing.)
    const auto rtvFormat=tgb::typed_read_format(md.Format, tgb::ReadAs::Motion);
    if(dsvFormat==DXGI_FORMAT_UNKNOWN || !(dd.BindFlags&D3D11_BIND_DEPTH_STENCIL) || !(md.BindFlags&D3D11_BIND_RENDER_TARGET) || rtvFormat==DXGI_FORMAT_UNKNOWN) {
        log_throttled("PT-112: HDRP's depth (format "+std::to_string(dd.Format)+(dsvFormat==DXGI_FORMAT_UNKNOWN ? ", no depth view" : "")+
                      ((dd.BindFlags&D3D11_BIND_DEPTH_STENCIL) ? "" : ", not a depth buffer")+") or motion vectors (format "+std::to_string(md.Format)+
                      (rtvFormat==DXGI_FORMAT_UNKNOWN ? ", no type" : "")+((md.BindFlags&D3D11_BIND_RENDER_TARGET) ? "" : ", not a render target")+") can't be written");
        return false;
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC dv{}; dv.Format=dsvFormat;
    if(dd.ArraySize>1) { dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2DARRAY; dv.Texture2DArray.ArraySize=1; }
    else dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    D3D11_RENDER_TARGET_VIEW_DESC rv{}; rv.Format=rtvFormat;
    if(md.ArraySize>1) { rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY; rv.Texture2DArray.ArraySize=1; }
    else rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11DepthStencilView> dsv; ComPtr<ID3D11RenderTargetView> rtv, pyramidRtv;
    check(st.d11->CreateDepthStencilView(depth, &dv, &dsv), "Create HDRP depth DSV");
    check(st.d11->CreateRenderTargetView(motion, &rv, &rtv), "Create HDRP motion RTV");
    // HDRP's depth pyramid (its fog and transparents read that copy, made before the composite): its first level is the
    // top left w x h of the texture, which the viewport covers.
    if(auto* pyramid=reinterpret_cast<ID3D11Resource*>(uintptr_t(pyramidHandle)); pyramid && on_game_device(pyramid)) {
        const auto pd=texture_desc(pyramid);
        const DXGI_FORMAT pf=(pd.Format==DXGI_FORMAT_R32_TYPELESS || pd.Format==DXGI_FORMAT_R32_FLOAT) ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_UNKNOWN;
        if(pf!=DXGI_FORMAT_UNKNOWN && (pd.BindFlags&D3D11_BIND_RENDER_TARGET) && pd.Width>=w && pd.Height>=h) {
            D3D11_RENDER_TARGET_VIEW_DESC pv{}; pv.Format=pf;
            if(pd.ArraySize>1) { pv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY; pv.Texture2DArray.ArraySize=1; }
            else pv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
            if(FAILED(st.d11->CreateRenderTargetView(pyramid, &pv, &pyramidRtv))) pyramidRtv.Reset();
        }
        if(!pyramidRtv) log_throttled("PT-112: HDRP's depth pyramid (format "+std::to_string(pd.Format)+", "+std::to_string(pd.Width)+" x "+std::to_string(pd.Height)+
                                      ") can't be written; its fog and transparents see HDRP's own depth");
    }
    tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
    auto* rt=rtv.Get(); st.c11->OMSetRenderTargets(1, &rt, dsv.Get());
    st.c11->OMSetBlendState(nullptr, nullptr, UINT_MAX); st.c11->OMSetDepthStencilState(st.depthAlways.Get(), 0);
    D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1}; st.c11->RSSetViewports(1, &viewport); st.c11->RSSetState(st.raster.Get());
    st.c11->IASetInputLayout(nullptr); st.c11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    st.c11->VSSetShader(st.vsDepthMotion.Get(), nullptr, 0); st.c11->PSSetShader(st.psDepthMotion.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[2]={st.outputs.dm.t.srv.Get(), st.outputs.mv.t.srv.Get()}; st.c11->PSSetShaderResources(0, 2, srvs);
    st.c11->Draw(3, 0);
    ++st.summaryOwnDepth;
    if(pyramidRtv && st.psPyramid) {   // its own draw (targets of different sizes are not bound together)
        auto* prt=pyramidRtv.Get(); st.c11->OMSetRenderTargets(1, &prt, nullptr); st.c11->OMSetDepthStencilState(st.noDepth.Get(), 0);
        st.c11->PSSetShader(st.psPyramid.Get(), nullptr, 0);
        st.c11->Draw(3, 0);
        ++st.summaryOwnPyramid;
    }
    ID3D11ShaderResourceView* none[2]{}; st.c11->PSSetShaderResources(0, 2, none);
    ID3D11RenderTargetView* noRts[2]{}; st.c11->OMSetRenderTargets(2, noRts, nullptr);
    return true;
}

void composite_event(uint32_t sequence) {
    uint64_t color=0, pyramid=0; TGPT_Frame frame{};
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& slot=e.frames[sequence%e.frames.size()];
        if(!slot.occupied || slot.sequence!=sequence) return;
        color=slot.color; pyramid=slot.pyramid; frame=slot.frame;
        slot.occupied=false;
    }
    const CompositeWork work=compositeWork[sequence%compositeWork.size()];
    // Ray Reconstruction's guides come first: they are needed whether or not there is light to apply yet.
    if((frame.flags&TGPT_FRAME_GUIDES) && work.inputs && st.d11 && st.csGuides && frame.renderWidth==st.width && frame.renderHeight==st.height) {
        tgb::InternalScope internal(&host());
        try { write_guides(frame, st.inputs[work.parity]); }
        catch(const DxError& e) { log_throttled(std::string("Ray Reconstruction guides failed: ")+e.what()+" "+tgb::hexhr(e.hr)); }
        catch(const std::exception& e) { log_throttled(std::string("Ray Reconstruction guides failed: ")+e.what()); }
    }
    // The frame's own traced light (a frame that was not traced leaves HDRP's color as it is).
    const Result& now=work.traced;
    if(!color || !work.inputs || !now.valid || !st.d11 ||
       now.width!=frame.renderWidth || now.height!=frame.renderHeight || frame.renderWidth!=st.width || frame.renderHeight!=st.height) {
        ++st.compositeSkipped; return;
    }
    tgb::InternalScope internal(&host());
    try {
        auto* target=reinterpret_cast<ID3D11Resource*>(uintptr_t(color));
        if(!on_game_device(target)) { log_throttled("HDRP's color buffer is not on the game's device; the traced light is not applied"); return; }
        const auto desc=texture_desc(target);
        const uint32_t w=frame.renderWidth, h=frame.renderHeight;
        if(desc.Width<w || desc.Height<h) { log_throttled("HDRP's color buffer is smaller than the render size"); return; }
        // Ray Reconstruction's specular hit distance and color before transparency, written with the light it applies
        // (the compute path only), for frames that asked for its guides.
        const bool extras=(frame.flags&TGPT_FRAME_GUIDES) && st.guides[3].uav && st.guideWidth==w && st.guideHeight==h;
        const bool transparency=extras && st.guides[4].uav;
        // The frame's own trace is on the GPU now (it began at render event 1): D3D11's commands from here on wait there for
        // its last pass. (GetCompletedValue first: a trace that is already done costs no wait.)
        d11_stamp(sequence, 2); // (PT-107: [2] and [3], the game's device's clock either side of the wait)
        if(st.consumer.d12->GetCompletedValue()<now.retire) { check(st.c11->Wait(st.consumer.d11.Get(), now.retire), "D3D11 wait for the traced light"); ++st.compositeWaits; }
        d11_stamp(sequence, 3);
        // PT-112 (A), on trial: the traced frame's own depth and motion into HDRP's buffers.
        if(frame.flags&TGPT_FRAME_OWN_DEPTH) write_depth_motion(frame, w, h, pyramid);
        CompositeConstants c{};
        c.frameSize[0]=w; c.frameSize[1]=h; c.frameSize[3]=frame.debugView<TGPT_VIEW_COUNT ? frame.debugView : 0;
        c.extras[0]=(extras ? 1u : 0u)|(transparency ? 2u : 0u);
        st.c11->UpdateSubresource(st.compositeConstants.Get(), 0, nullptr, &c, 0, 0);
        const auto& in=st.inputs[work.parity];
        const auto& out=st.outputs;
        // The frame's G-buffer color, GBuffer3 and depth, its traced light (A, B), the reflections' hit distance (C) and the
        // tracer's own guides for Ray Reconstruction (d3d11.hlsl traced_guides).
        constexpr UINT kCompositeSrvs=8;
        ID3D11ShaderResourceView* srvs[kCompositeSrvs]={in.gb0.t.srv.Get(), in.gb3.t.srv.Get(), in.depth.t.srv.Get(), out.a.t.srv.Get(), out.b.t.srv.Get(),
                                                        out.c.t.srv.Get(), out.g0.t.srv.Get(), out.g1.t.srv.Get()};
        ID3D11Buffer* cbs[2]={st.constants.Get(), st.compositeConstants.Get()};
        const auto uavFormat=tgb::typed_write_format(desc.Format);
        ++st.composited;
        { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); ++e.status.composited; }
        if((desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS) && uavFormat!=DXGI_FORMAT_UNKNOWN && float_format(uavFormat) && typed_uav_load(uavFormat)) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC v{}; v.Format=uavFormat; v.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
            ComPtr<ID3D11UnorderedAccessView> uav;
            if(SUCCEEDED(st.d11->CreateUnorderedAccessView(target, &v, &uav))) {
                tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
                st.c11->CSSetShader(st.csComposite.Get(), nullptr, 0); st.c11->CSSetConstantBuffers(0, 2, cbs);
                st.c11->CSSetShaderResources(0, kCompositeSrvs, srvs);
                // With the extras, the own-camera guides too: shared with the D3D11 handoff.
                ID3D11UnorderedAccessView* u[6]={uav.Get(), extras ? st.guides[3].uav.Get() : nullptr, transparency ? st.guides[4].uav.Get() : nullptr,
                                                 extras ? st.guides[0].uav.Get() : nullptr, extras ? st.guides[1].uav.Get() : nullptr, extras ? st.guides[2].uav.Get() : nullptr};
                st.c11->CSSetUnorderedAccessViews(0, 6, u, nullptr);
                st.c11->Dispatch((w+7)/8, (h+7)/8, 1);
                ID3D11ShaderResourceView* none[16]{}; st.c11->CSSetShaderResources(0, 16, none);
                ID3D11UnorderedAccessView* noUavs[6]{}; st.c11->CSSetUnorderedAccessViews(0, 6, noUavs, nullptr);
                ID3D11Buffer* noCbs[2]{}; st.c11->CSSetConstantBuffers(0, 2, noCbs);
                if(extras) {
                    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
                    e.guides.extrasFrameIndex=frame.frameIndex;
                    e.guides.specularHitDistance=uint64_t(uintptr_t(st.guides[3].texture.Get()));
                    e.guides.colorBeforeTransparency=transparency ? uint64_t(uintptr_t(st.guides[4].texture.Get())) : 0;
                }
                return;
            }
        }
        const auto rtvFormat=tgb::typed_read_format(desc.Format, tgb::ReadAs::Color);
        if(!(desc.BindFlags&D3D11_BIND_RENDER_TARGET) || rtvFormat==DXGI_FORMAT_UNKNOWN) { log_throttled("HDRP's color buffer can't be written"); return; }
        D3D11_RENDER_TARGET_VIEW_DESC v{}; v.Format=rtvFormat; v.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> rtv; check(st.d11->CreateRenderTargetView(target, &v, &rtv), "Create composite RTV");
        tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
        auto* rt=rtv.Get(); st.c11->OMSetRenderTargets(1, &rt, nullptr);
        st.c11->OMSetBlendState(st.compositeBlend.Get(), nullptr, UINT_MAX); st.c11->OMSetDepthStencilState(st.noDepth.Get(), 0);
        D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1}; st.c11->RSSetViewports(1, &viewport); st.c11->RSSetState(st.raster.Get());
        st.c11->IASetInputLayout(nullptr); st.c11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        st.c11->VSSetShader(st.vsComposite.Get(), nullptr, 0); st.c11->PSSetShader(st.psComposite.Get(), nullptr, 0);
        st.c11->PSSetConstantBuffers(0, 2, cbs); st.c11->PSSetShaderResources(0, kCompositeSrvs, srvs);
        st.c11->Draw(3, 0);
        ID3D11ShaderResourceView* none[16]{}; st.c11->PSSetShaderResources(0, 16, none);
        ID3D11Buffer* noCbs[2]{}; st.c11->PSSetConstantBuffers(0, 2, noCbs);
    } catch(const DxError& e) { log_throttled(std::string("Path tracing composite failed: ")+e.what()+" "+tgb::hexhr(e.hr)); }
      catch(const std::exception& e) { log_throttled(std::string("Path tracing composite failed: ")+e.what()); }
}


} // namespace

void device_ready(ID3D12Device* device, uint32_t vendor) {
    std::string reason;
    ComPtr<ID3D12Device5> d5;
    if(!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&d5)))) reason="The Direct3D 12 runtime is too old for ray tracing (no ID3D12Device5).";
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    if(reason.empty() && (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5))) || o5.RaytracingTier<D3D12_RAYTRACING_TIER_1_1))
        reason="This GPU or driver has no DirectX Raytracing 1.1 (inline ray tracing); path tracing needs an RTX 20, RX 6000, Arc or newer GPU.";
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
    if(reason.empty() && (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) || sm.HighestShaderModel<D3D_SHADER_MODEL_6_6))
        reason="The GPU driver does not support Shader Model 6.6; updating the driver may help.";
    D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
    if(reason.empty() && (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o))) || o.ResourceBindingTier<D3D12_RESOURCE_BINDING_TIER_3))
        reason="The GPU does not support resource binding tier 3.";
    // The passes read their textures back through typed UAVs: half and single precision color formats (the additional
    // formats a driver supports together) and the packed surfaces as R32G32_UINT, which a driver supports on its own.
    if(reason.empty() && !o.TypedUAVLoadAdditionalFormats)
        reason="The GPU driver can't load the tracer's textures through typed UAVs (no TypedUAVLoadAdditionalFormats).";
    if(reason.empty()) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{DXGI_FORMAT_R32G32_UINT};
        if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))) || !(fs.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD))
            reason="The GPU driver can't load R32G32_UINT textures through typed UAVs, which the tracer keeps its surfaces in.";
    }
    // DirectX Raytracing 1.2: Shader Model 6.9 and tier 1.2 bring Shader Execution Reordering and opacity micromaps.
    D3D12_FEATURE_DATA_SHADER_MODEL sm69{D3D_SHADER_MODEL_6_9};
    // TGPT_TEST_SM66=1 (a test's; PT-133, docs/agents/nosun.md): the Shader Model 6.6 set of passes on a card that offers
    // 6.9, as on a card without DirectX Raytracing 1.2 (no micromaps, no reordering): the same frames under both sets, by
    // the same runtime and driver, show a pass that the driver's compile of its 6.9 build gets wrong (the fog cells' pass
    // returned 1e33 in frames without a sun). The game never sets it.
    const bool sm66Asked=GetEnvironmentVariableA("TGPT_TEST_SM66", nullptr, 0)>0;
    if(sm66Asked) log("TGPT_TEST_SM66 is set (a test's switch): the Shader Model 6.6 passes run, whatever this device offers");
    const bool dxr12=reason.empty() && !sm66Asked && o5.RaytracingTier>=D3D12_RAYTRACING_TIER_1_2 &&
        SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm69, sizeof(sm69))) && sm69.HighestShaderModel>=D3D_SHADER_MODEL_6_9;
    st.dxr12=dxr12;
    // D3D12_FEATURE_D3D12_OPTIONS22 (Agility SDK 1.619, newer than this Windows SDK's header).
    struct Options22 { BOOL serActuallyReorders, byteOffsetViews; UINT max1DDispatch, max1DDispatchMesh; } o22{};
    st.serReorders=dxr12 && SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE(65), &o22, sizeof(o22))) && o22.serActuallyReorders;
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.sharedDevice=device; e.vendor=vendor; e.unsupported=reason;
    }
    st.sharedDevice=device;
    uint32_t denoisers=0;
#if TGPT_MEGA_GEOMETRY
    std::vector<std::string> megaLines; // (PT-109: what the driver answers, logged with the device's line)
#endif
    if(reason.empty()) {
        // PT-87 (memory3): what Windows counts before the tracer has made anything on the shared device (count_now's comment),
        // then the two probes (the passes are being made on their own thread beside them: start_passes has its own lines).
        StartSteps steps(device, nullptr);
        if(steps.first().known) {
            char line[256];
            std::snprintf(line, sizeof line, "PT memory at start: the shared device is here, nothing of the tracer's made on it yet (Windows counts %.0f MB; the PC's own memory "
                          "beside it %.0f MB)", steps.first().local, steps.first().other);
            log(line);
        }
#if TGPT_MEGA_GEOMETRY
        // PT-109 (mega_geometry.hpp): what the driver itself reports of RTX Mega Geometry on this device, asked once, before
        // the passes are made: where it reports clusters, every pipeline is made with its support for them (the trial's 5).
        // The plain acceleration structures serve whenever it reports nothing, and on every card that is not NVIDIA's.
        st.scene.megaCaps=mega_probe(device, vendor, megaLines);
        mega_enable_cluster_pipelines(device, st.scene.megaCaps, megaLines);
#endif
        // Before the device is published as ready: the plugin never sees it ready and its passes not being made.
        start_passes(d5.Get(), dxr12, st.serReorders);
        rrg_probe(device, vendor);
        steps.step("Ray Regeneration's probe (the passes are being made beside it)");
        nrd_probe(device);
        steps.step("NRD's probe (the passes are being made beside it)");
        denoisers=(rrg_available() ? TGPT_FEATURE_RAY_REGENERATION : 0u)|(nrd_available() ? TGPT_FEATURE_NRD : 0u);
    }
    {
        // The device is ready once its denoisers are probed, and the two are published together: the managed side takes a
        // ready device's features as final, and a Denoising pick of NRD or Ray Regeneration was replaced mid-probe.
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.status.features|=denoisers;
        e.status.deviceReady=1; e.status.supported=reason.empty() ? 1u : 0u;
    }
    if(reason.empty()) {
        char model[8]; std::snprintf(model, sizeof(model), "%X", unsigned(sm.HighestShaderModel));
        log("Shared D3D12 device: DirectX Raytracing tier "+std::to_string(int(o5.RaytracingTier))+", shader model 0x"+model+"; path tracing available; "+
            (dxr12 ? std::string("DirectX Raytracing 1.2 (opacity micromaps) available; Shader Execution Reordering ")+(st.serReorders ? "reorders" : "does not reorder on this GPU")
                   : std::string("no DirectX Raytracing 1.2 (Shader Model 6.9 and tier 1.2)")));
#if TGPT_MEGA_GEOMETRY
        for(const auto& line:megaLines) log(line); // (PT-109: what the driver answered, asked above)
#endif
    } else log("Path tracing is unavailable: "+reason);
}

void __stdcall render_event(int eventId, void* data) {
    try {
        const uint32_t sequence=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        // (PT-87, memory3: every call below in Windows' count, for the tally of the ten minutes after a start.)
        TallyCall counting;
        if(eventId==7 && st.d3d12Ready && !st.recording) {
            counting.enter(st.d12.Get());
            st.heap.set_retire(st.consumer.value);
            if(st.scene.intake(&st.heap, st.release, st.consumer.value)) release_scene_extras();
            std::shared_ptr<const SceneList> list; std::shared_ptr<const MovingList> moving; std::vector<TGPT_SkinnedInstance> skinned;
            { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); list=e.scene; moving=e.moving; skinned=e.skinned; }
            st.scene.set_lists(list,moving); st.scene.maintenance(st.heap,st.release,st.consumer.value,skinned);
            const auto completed=st.consumer.d12->GetCompletedValue();
            st.scene.collect(completed); st.release.collect(completed); st.heap.collect(completed); share::collect();
            // A descriptor slot given back twice, or written while nobody holds it (gpu.hpp DescriptorHeap): the lines that did it.
            {
                uint64_t total=0; const auto misuse=st.heap.take_misuse(&total);
                for(const auto& m:misuse) {
                    auto base=[](const char* path) { if(!path) return std::string("?"); std::string s(path); const size_t at=s.find_last_of("/\\"); return at==std::string::npos ? s : s.substr(at+1); };
                    log(std::string("DESCRIPTOR SLOT MISUSE: slot ")+std::to_string(m.index)+(m.write ? " was written" : " was given back")+" at "+base(m.file)+":"+std::to_string(m.line)+
                        " while it was "+(m.state==0 ? "free" : m.state==2 ? "given back and waiting" : m.state==255 ? "outside the heap" : "handed out")+
                        (m.gaveBackFile ? "; it was last given back at "+base(m.gaveBackFile)+":"+std::to_string(m.gaveBackLine) : std::string("; never given back before"))+
                        (m.write ? "" : " (the second give-back is refused)")+"; "+std::to_string(total)+" in all so far");
                }
            }
            // PT-87: off, says the plugin (data 1), and no frame handed over for kOffMs: release_frame_memory.
            if(sequence==1 && st.width && GetTickCount64()-st.lastTraceTick>=kOffMs && !keep_idle()) release_frame_memory();
            // PT-87 (memory4): no frame traced for a second, so no bake can come: the game's device lets go of the atlases.
            tend_sides_quiet();
            counting.leave(st.d12.Get(), tally.others);
            publish_memory();
        }
        else if(eventId==TGPT_EVENT_MARK) { // (PT-107: State::renderMarks; data is Unity's frame count times 4 plus the mark)
            LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
            const uint32_t frameCount=sequence>>7, which=sequence&127u;
            waits::lastMark=which; // (the measurement: which step of the frame the render thread is in)
            if(which==0) d11_stamp_open(8); else if(which==1) d11_stamp_open(9); // (the game's device's clock there)
            else if(which>=4 && which<=8) d11_stamp_open(which+6); // (PT-112: HDRP's injection points, [10] to [14])
            auto& mark=st.renderMarks[frameCount%uint32_t(st.renderMarks.size())];
            if(mark.frameCount!=frameCount) { mark=State::RenderMark{}; mark.frameCount=frameCount; }
            if(which<mark.at.size()) mark.at[which]=now.QuadPart;
        }
        else if(eventId==TGPT_EVENT_TRACE) trace_event(sequence);
        else if(eventId==TGPT_EVENT_COMPOSITE) { counting.enter(st.d12.Get()); composite_event(sequence); d11_stamp(sequence, 5); counting.leave(st.d12.Get(), tally.others); }
    } catch(...) { log_throttled("Path tracing event failed with an exception; the frame keeps HDRP's lighting"); }
}
void shader_progress(TGPT_Diagnostics& d) { d.passesDone=pass_cache::completed.load(); d.passesTotal=pass_cache::total.load(); d.cacheHits=pass_cache::hits.load(); }

} // namespace tgpt
