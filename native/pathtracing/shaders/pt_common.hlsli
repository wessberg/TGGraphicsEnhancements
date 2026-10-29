// SPDX-License-Identifier: MIT
// Shared by every path tracing pass: the frame constants, scene records, packing, sampling, BRDFs,
// ray traversal with alpha testing, and the shading of path vertices. Shader Model 6.6: every resource
// is reached through ResourceDescriptorHeap by the index the constants give.
#pragma once
#include "pt_root.hlsli"

static const float PI = 3.14159265358979;
static const uint INVALID = 0xFFFFFFFFu;
static const uint ATLAS_SIZE = 4096;
static const uint TILE_SIZE = 64;
static const uint TILES_PER_ROW = ATLAS_SIZE / TILE_SIZE;
static const uint ALPHA_TILE_SIZE = 256;   // the alpha atlas: alpha-tested materials' alpha, R8
static const uint ALPHA_TILES_PER_ROW = ATLAS_SIZE / ALPHA_TILE_SIZE;
static const uint MAX_TILES = TILES_PER_ROW * TILES_PER_ROW;
static const uint DETAIL_TILE_SIZE = 512;  // detail tiles (runtime.hpp kDetail*): below the 64x64 tiles, numbered after them
static const uint DETAIL_TILES_PER_ROW = ATLAS_SIZE / DETAIL_TILE_SIZE;
static const uint DETAIL_TILES = 128;     // runtime.hpp kMaxDetailTiles: 16 rows of them
static const uint COOKIE_TILE_SIZE = 64;     // lights' cookies (runtime.hpp kCookie*)
static const uint COOKIE_TILES_PER_ROW = 1024 / COOKIE_TILE_SIZE;

#define VIEW_NONE 0
#define VIEW_ALBEDO 1
#define VIEW_NORMALS 2
#define VIEW_COVERAGE 3
#define VIEW_MATERIALS 4
#define VIEW_INDIRECT 5
#define VIEW_INDIRECT_RAW 6
#define VIEW_DIRECT 7
#define VIEW_SURFACES 8
#define VIEW_SPECULAR 9
#define VIEW_EMISSIVE 10
#define VIEW_YARDSTICK 11 // the tracer's own first hit against HDRP's G-buffer, pixel by pixel (pt_primary yardstick)
#define OPTION_NO_SPATIAL 1
#define OPTION_NO_TEMPORAL 2
#define OPTION_NO_DENOISER 4
#define OPTION_NO_SPECULAR 8
#define OPTION_RAY_RECONSTRUCTION 16
#define OPTION_RAY_REGENERATION 64
#define OPTION_NO_PASS_THROUGH 128
#define OPTION_DI_VISIBILITY 256
#define OPTION_SPATIAL_FEEDBACK 512
// (1024: TGPT_OPTION_NRD, which no shader reads since 2026-10-11: tracer.cpp clears it for them.)
#define OPTION_NO_BOILING 2048        // for testing (tgpt_api.h)
#define OPTION_NO_CLAMPS 4096         // for testing; nothing since PT-50 took the clamps out (the bit stays taken)
#define OPTION_EXACT_JACOBIAN 8192    // for testing
#define OPTION_OFFSET_RECEIVER 16384  // for testing
#define OPTION_GI_VISIBILITY 2097152  // bounced light's reuse with visibility (tgpt_api.h)
#define OPTION_GI_OWN_RAYS 524288      // the capture's comparison: the bounced light from the pixel's own rays alone (tgpt_api.h)
#define OPTION_DI_NO_REUSE 1048576     // the capture's comparison: the direct light without ReSTIR DI's reuse
#define OPTION_NO_CAUSTICS 16777216    // light through glass tinted along a straight line, not bent and focused (caustic_through)
#define OPTION_CAUSTICS_LIT 4194304    // ON TRIAL (tgpt_api.h): under water the caustics' light comes from its direction (caustic_lit)
#define OPTION_FACETED 33554432        // for testing: round things shaded by their flat faces as before (fetch_hit, terminator_softening, shadow_origin, pt_atrous)
#define OPTION_GLOSSY_REFLECTIONS 262144 // Reflections on Glossy Surfaces (tgpt_api.h)
// The old route (tgpt_api.h): a pixel's surface from HDRP's G-buffer, the tracer's own camera rays for hair alone. Without it
// every pixel's surface is what its own camera ray finds in the traced scene (pt_primary, PT-59).
#define OPTION_GBUFFER_SURFACES 0x80000000u
// Miss distance for the denoisers' hit-distance channels (FP16's largest, as AMD's sample writes it).
#define HIT_DISTANCE_MISS 65504.0

// DirectX Raytracing 1.2 (Shader Model 6.9, built with PT_DXR12): ray queries see opacity micromaps, which
// settle most alpha-tested triangles without the alpha test. With PT_SER the passes whose threads shade
// the most different things run as ray generation shaders, and Shader Execution Reordering regroups
// their threads before shading: the reflections' by the material their ray hit (PT_REORDER, regroup_key),
// the bounced light's once after its first ray with no hint of its own (pt_gi.hlsl, since 2026-10-10).
#ifdef PT_DXR12
#define PT_RAYQUERY(flags) RayQuery<flags, RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS>
#else
#define PT_RAYQUERY(flags) RayQuery<flags>
#endif
// A query that can meet only the water's own surface (MASK_WAVES: its units, the one kind of instance with that bit, built
// with no micromaps, scene.hpp kMaskWaves): no opacity micromaps asked for. Microsoft's DXR spec ("Opacity micromaps and
// TraceRayInline"): leaving them out is the more efficient where none can be met (and undefined where one can).
// docs/agents/review-perf-astra.md 10.
#define PT_RAYQUERY_WAVES(flags) RayQuery<flags>
#ifdef PT_SER
#define PT_REORDER(key) dx::MaybeReorderThread(uint(key) & 0xFFFFu, 16)
#define PT_ENTRY(body, name) [shader("raygeneration")] void name() { body(DispatchRaysIndex().xy); }
#else
#define PT_REORDER(key)
#define PT_ENTRY(body, name) [RootSignature(PT_ROOT_SIGNATURE)] [numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) { body(p); }
#endif
// What the reflections' pass regroups its threads by (pt_specular), and the bounced light's since round 20 (pt_gi.hlsl, by its
// first ray's hit; with no hint from 2026-10-10 to then, docs/agents/rule-2.md): the material
// their ray met (0xFFFF: nothing to shade), and whether the pixel's path feeds the radiance cache this frame. A quarter of
// the screen's 8 x 8 blocks feed it each frame (cache_feeds: whole blocks, so that a compute pass's threads take the extra
// ray together); regrouped by material alone, the feeding threads were spread over every group, and each group ran the
// cache's ray and its shading for them. No regrouping is added: the one there is has this key. No thread's own numbers
// change (each keeps its random numbers and draws them in the same order): the picture is the same. PT-99; on trial until
// 2026-10-10 (docs/agents/perf-1.md, rule-1.md), the rule since: the game, run 20261010-092027, the tavern's reflections
// 11.4 -> 9.9 ms.
uint regroup_key(uint material, bool feeds) { return material == 0xFFFFu ? material : ((material & 0x7FFFu) | (feeds ? 0x8000u : 0u)); }
#define FLAG_RESET 1
#define FLAG_DENOISER_RESET 2 // the built-in denoiser resumes after frames it skipped: its history is stale
#define FLAG_TRACED_FOG 8 // the game's fog is the tracer's (TGPT_FRAME_TRACED_FOG, the rule since 2026-10-11): pt_medium.hlsli
#define MATERIAL_DOUBLE_SIDED 1
#define MATERIAL_ALPHA_TEST 2
#define MATERIAL_VERTEX_COLOR 4
#define MATERIAL_ALPHA_TILE 8
#define MATERIAL_CAMERA_ONLY 16
#define MATERIAL_GLASS 64
#define MATERIAL_MASK_TILE 1024
#define MATERIAL_EMISSIVE_ALBEDO 128 // the emission times the albedo (HDRP's Albedo Affects Emissive)
#define MATERIAL_STRANDS 4096 // hair: its alpha tile's coverage is the chance a ray stops there (alpha_opaque)
#define MATERIAL_TRANSLUCENT 256 // grass, leaves: lit through from behind at path vertices (direct_diffuse)
#define MATERIAL_THIN_CLOTH 512  // banners, laundry, canvas: lit through from behind, less than a leaf (CLOTH_TRANSMISSION)
#define MATERIAL_SWAYS 8192 // moved by the wind as HDRP draws it (MASK_SWAYS)
#define MATERIAL_EYE 16384  // an eye (with MATERIAL_CAMERA_ONLY): shaded by the tracer as a wet surface
#define MATERIAL_HAIR_LOBES 32768 // hair with its highlights (with MATERIAL_STRANDS): hair_specular, from its hair fields
#define MATERIAL_HAIR_LAYER 65536 // hair the tracer's own camera rays find, card by card by its strands' cover (pt_primary): other camera rays pass through it
#define MATERIAL_OWN_SURFACE 131072 // a glowing surface a kept light stands for: it lights nothing itself (EMITTER_PAIRED)
#define MATERIAL_SHARED 262144 // set by the tracer: some of the material's own textures are read at a hit (PT-46: material_color)
#define MATERIAL_FIRST_PERSON 1048576 // the hero's first-person body: a ray that shades what it meets passes its back faces (first_person_inside)
#define MATERIAL_SUBSURFACE 2097152 // light scatters inside it (HDRP's subsurface scattering): MaterialRecord.subsurface, subsurface_walk
#define OPTION_NO_SUBSURFACE 268435456 // for testing: no walk inside subsurface materials, lit as plain surfaces (subsurface_on)
#define TEXTURE_MASK_OCCLUSION 16u // MaterialRecord.textureFlags: the mask map's green is HDRP Lit's ambient occlusion
// PT-64 (tgpt_api.h TGPT_TEXTURE_*): Shader Graph's Normal Strength; HDRP's Double-Sided "Flip" mode; the emissive map's red
// alone; the mask map's occlusion through the material's own range (its ends in bits 12-19 and 20-27).
#define TEXTURE_GRAPH_NORMAL 32u
#define TEXTURE_FLIP_BACK 64u
#define TEXTURE_EMISSIVE_RED 128u
#define TEXTURE_OCCLUSION_REMAP 256u
#define OPTION_ATLAS_TEXTURES 67108864 // for testing: rays read the atlas tiles even where the game's own textures are shared
#define OPTION_REFLECTION_TOP_MIP 134217728 // for testing (pt_specular): what a reflection meets is read at its textures' top mip
#define MATERIAL_SEE_THROUGH 2048 // transparent glass classification; optical transport uses MATERIAL_GLASS
// HDRP's water as the plugin hands it over, level (TGPT_INSTANCE_WATER), is met by the level-water query alone (MASK_WATER,
// pt_waves water_base: where the caustics launch their light). Every other ray asks for everything else (MASK_ALL), never
// for 0xFF: the scene, and the water's own surface, its waves as triangles (MASK_WAVES, scene.cpp kMaskWaves: INSTANCE_WAVES
// instances with a slot in bits 8 and 9, pt_water_surface.hlsli), which a ray meets as it meets anything. MASK_SOLID: all
// but that surface, for a ray that must pass it or looks for it apart (the camera's first walk, the caustics' own rays).
#define MASK_WATER 0x08u
#define MASK_ALL 0xF7u
#define MASK_WAVES 0x80u
#define MASK_SOLID 0x77u
// Still instances that are neither leaves nor grass, glass, hair nor what the wind sways (scene.hpp kMaskStillPlain): the
// ground, rock and buildings. What can lie between a point and water over it (pt_water_surface water_medium, PT-131).
#define MASK_GROUND 0x21u
#define MASK_STRANDS 0x10u // instances holding such hair (scene.cpp kMaskStrands): pt_primary's first walk meets them alone
// Instances holding leaves or grass (a translucent cut-out: scene.cpp kMaskFoliage), round 20: every still instance had
// 0x61, now foliage 0x40 and the rest 0x21, so that every ray mask meets both as before (each holds all three bits) and a
// light's ray can meet foliage in a pass of its own (foliage_blocks).
#define MASK_FOLIAGE 0x40u
#define MASK_STILL 0xF5u      // still instances (scene.cpp kMaskStill) and those with see-through glass
#define MASK_SWAYS 0x02u      // what the wind moves, traced where it stands still (SWAY_REACH, below)
#define INSTANCE_WATER 64 // HDRP's water; bits 8 and 9: its slot in TGPT_SetWaterCaustics
#define INSTANCE_WAVES 1024 // that water's surface, displaced (a water unit of scene.cpp); the same slot bits
// Set while a camera ray is traced (pt_prepare): only those see MATERIAL_CAMERA_ONLY surfaces.
static bool g_cameraRays = false;
// And while it is the tracer's own first hit that makes the pixel's surface (pt_primary primary_walk over the whole scene):
// what covers part of a pixel then stops the ray by chance, as much as it covers (alpha_opaque), where a camera ray that
// only looks for the surface HDRP drew keeps HDRP's own cut.
static bool g_primaryRays = false;
// Set while the ray of a reflection in a surface that reflects as a mirror does (smooth, not skin) is traced (pt_specular,
// PT-46): it sees what only the camera sees, an eye or an unlit decal (MATERIAL_CAMERA_ONLY, not hair's layer), as the camera
// there would.
static bool g_mirrorRay = false;
// The instance transform of trace_closest's last hit (uv_frame).
static float3x4 g_hitToWorld = (float3x4)0;
// The camera ray for a pixel HDRP drew forward (pt_prepare) looks for the surface HDRP drew there: hair it meets nearer than
// this is a card HDRP cut away at that pixel, passed through (a lash card a few millimetres over the eye it drew made the
// eye's pixels the lash's black, 2026-09-28). -1e30 elsewhere; g_candidateT is the candidate's distance (trace_closest).
static float g_strandsFrom = -1e30;
static float g_candidateT = 0.0;
// The ray being traced's own number (ray_seed), for glass to let it through by.
static uint g_raySeed = 0;
// The ray cone of the ray being traced (PT-46, after Akenine-Moller et al., "Texture Level of Detail Strategies for Real-Time
// Ray Tracing"): its width where it left (metres) and how fast it widens (radians; < 0: one pixel's angle, a camera ray's),
// and where it left from (ray_seed). Its width at a hit is the footprint the game's own textures are read over there
// (fetch_hit, material_texture): a camera ray's is one pixel's, as the game's own samplerIndex reads; a reflection's grows
// with the way to the mirror and on to what it shows; a diffuse bounce's is wide (DIFFUSE_SPREAD) and reads small mips.
static float g_coneWidth = 0.0;
static float g_coneSpread = -1.0;
static float3 g_rayOrigin = 0.0;
static const float DIFFUSE_SPREAD = 0.5;
void ray_cone(float width, float spread) { g_coneWidth = max(width, 0.0); g_coneSpread = spread; }
// Set while the refraction path of see-through glass looks for the glass's far side (pt_specular): glass is solid to it.
static bool g_glassSolid = false;
// Set while a shadow ray takes glass's color instead of its chance (transmittance): glass lets it on, counting how often it
// was met, and glass_walk tints the ray by it afterwards.
static bool g_glassTint = false;
static uint g_glassMet = 0;
// Set while a light's shadow ray is traced (transmittance, transmittance_from_surface, pt_rtxdi's rtxdi_visible): a leaf or a
// blade of grass in its way lets it through part of the time (alpha_opaque).
static bool g_lightRay = false;
// Set while a ray that gathers the sky's or bounced light for a diffuse surface is traced (trace_bounce; pt_gi's rays from HDRP's
// surfaces, and the ray that checks a bounced-light sample is seen: pt_gi_spatial): thin cloth in its way lets it through its
// weave as it lets a light's ray through (alpha_opaque), counting how often, and cloth_walk tints what the ray brings back
// by the cloth it passed.
static bool g_bounceRay = false;
static uint g_clothMet = 0;
static const float GLASS_IOR = 1.5;

// NVIDIA RTXDI's parameter blocks (its ReSTIR DI and GI reuse, pt_rtxdi.hlsli), shared with tracer.cpp.
#include "Rtxdi/DI/ReSTIRDIParameters.h"
#include "Rtxdi/GI/ReSTIRGIParameters.h"

// Kept in step with FrameConstants in tracer.cpp.
struct FrameConstants {
    float4x4 invViewProj;   // clip to camera-relative world (HDRP's, with the frame's sub-pixel shift: a pixel's centre is where HDRP drew its depth)
    float4x4 viewProj;      // camera-relative world to clip
    float4x4 prevViewProj;  // previous camera-relative world to clip
    float4 cameraPos;       // xyz: camera relative to the scene origin; w: Reflection Boost (0 off)
    float4 prevCameraPos;   // xyz: previous camera relative to the current scene origin; w: water trials (cameraPosition.w)
    float4 prevOriginDelta; // xyz: previous origin - current origin; w: Indirect Color (1 as traced)
    float4 sunDir;          // xyz towards the sun; w: cos of its angular radius
    float4 sunColor;        // rgb: lux; w: 1 with a sun
    uint4 size;             // width, height, frame index, flags (bits 8-15, ON TRIAL: glass_trial; 0: all off)
    uint4 settings;         // thick diffusion profiles (a bit each), debug view, bounces, spatial samples
    uint4 settings2;        // the previous frame's light count, options, light count, a-trous iterations
    float4 params;          // unused, max ray distance, sky intensity, spatial radius (pixels)
    uint4 inputs0;          // SRVs: albedo+deferred flag, normal buffer, GBuffer2, GBuffer3
    uint4 inputs1;          // SRVs: depth, motion, exposure, sky
    uint4 scene0;           // SRVs: instances, meshes, geometries, materials
    uint4 scene1;           // SRVs: material indices, lights, atlas; UAV: output
    uint4 surface;          // UAVs: surface cur/prev, normal cur/prev
    uint4 material;         // UAVs: material cur, RTXDI GI reservoirs, what each pixel's reflection shows (pack_seen), RTXDI DI reservoirs
    uint4 signals;          // SRV: lights' cookies; UAVs: diffuse raw, sun raw, specular raw
    uint4 denoise0;         // UAVs: diffuse history cur/prev, moments cur/prev
    uint4 denoise1;         // UAVs: the direct light's result; with NRD its filtered direction of the last frame (INVALID: none); the reflections' result; specular history prev
    uint4 denoise2;         // UAVs: a-trous ping, pong; the index holding the final diffuse; unused
    float4 gridMin;         // xyz: the light grid's corner relative to the origin; w: 1 / cell size
    uint4 grid;             // SRVs: grid cells (first, count), light indices; dimensions (8 bits each); rays per pixel
    uint4 extra;            // SRV: the alpha atlas; UAV: material prev; SRV: RTXDI's neighbour offsets; SRV: the previous frame's lights
    // RTXDI's ReSTIR DI reuse, with the parameters ReSTIRDI.cpp documents.
    RTXDI_RuntimeParameters rtxdiRuntime;          // neighbour offset mask, no checkerboard, frame index
    RTXDI_ReservoirBufferParameters rtxdiReservoirs;
    RTXDI_DITemporalResamplingParameters diTemporal;
    RTXDI_BoilingFilterParameters diBoiling;
    RTXDI_DISpatialResamplingParameters diSpatial;
    uint4 rtxdi;                                   // reservoir arrays: DI this frame, DI previous, GI this frame, GI previous
    // RTXDI's ReSTIR GI reuse, with the parameters ReSTIRGI.cpp documents.
    RTXDI_GITemporalResamplingParameters giTemporal;
    RTXDI_GISpatialResamplingParameters giSpatial;
    RTXDI_BoilingFilterParameters giBoiling;
    // x: the reference radiance traced light is stored relative to (a white surface in full sun, or lit by the brightest
    // light 1 m away; tracer.cpp); y: the previous frame's; z: ON TRIAL, the highlights' shoulder packed (highlight_shoulder;
    // 0: off. Until then the reference the radiance clamps measured against before PT-50 took them out, which no shader
    // read); w: 1 when the scene has instances the wind moves (MASK_SWAYS), plus 2 with thin cloth.
    float4 radiance;
    uint4 cache;        // the radiance cache's buffers (UAVs, raw): its keys, this frame's sums, its light; w: 1 while it is on
    float4 cacheOrigin; // xyz: the scene origin in the world, in metres (the cache's cells stay where they are when it moves)
    // Emissive triangles (scene.cpp build_emitters): the emitter records (SRV), the previous frame's, and their counts.
    uint4 emissive;
    // The instance table (SRV: an instance's submeshes' first slots), the static instances it covers, the clusters sampled as
    // lights (0: none, or the Emissive Lights row off) and the first one's record.
    uint4 emissive2;
    // HDRP's height fog, as its Fog volume sets it (height_fog_depth): extinction per metre (0 without fog), its base height in
    // the world, 1 / its scale height; w: the player's Exposure as a factor, the part of traced_gain that isn't automatic.
    float4 fog;
    // Caustics (TGPT_SetWaterCaustics, pt_caustics.hlsl): each slot's displacement array (SRV, INVALID for still water), and per
    // slot WATER_ROWS rows (water_row): per band b, [3b] and [3b + 1] the position relative to the scene origin to its texture
    // coordinates' x and y (rows of an affine map, HDRP's scale and offset folded in), [3b + 2] its amplitude and HDRP's distance
    // fade (a, b); [9..11] the world-to-water rotation's rows; [12] the bands, the band size in texels, the waves' largest move,
    // 1 when the slot holds water; [13] the water's extent (x, z min, x, z max); [14] its absorption per metre.
    float4 waterScatter[4];
    float4 waterPrevious[52]; uint4 waterPreviousTextures;
    float4 waterFoam[24]; int4 waterFoamMaps[4];
    uint4 waterGeometry; // transform records (SRV), count; the sun's kept wave corners (raw UAV, INVALID: none: sunGrid), a test's count of tiles to sum
    uint4 waterTextures;
    float4 water[60];
    float4 waterTop; // per slot: its water's highest point relative to the scene origin (-1e30 when empty)
    float4 waterBottom; // and its lowest (1e30 when empty)
    // x: hair's own a pixel (pt_prepare: its strand's direction and its material; UAV); y: 1 while the traced scene holds hair
    // the tracer's own camera rays find (pt_primary: TGPT_MATERIAL_HAIR_LAYER), INVALID without; z: the strand atlas (SRV:
    // hair's alpha at its textures' own size, with its mips); w: an upscaler's mip bias (a float's bits, TGPT_Frame.forward.y).
    uint4 hair;
    // Per detail tile (a hair material's color tile, by its index past MAX_TILES: tracer.cpp bake_hair), what a camera ray
    // reads such hair's strands' alpha with (pt_primary): [2i] its block of the strand atlas (x | y << 16 in texels, as a float's bits), the
    // block's size (bits; bit 31 once written, 0 without a block), RealHair's alpha clip and depth prepass; [2i + 1] its
    // diffuse map's tiling (xy) and offset (zw).
    float4 strandTiles[2 * DETAIL_TILES];
    // A mirror reflectance per HDRP diffusion profile (its GBuffer index; TGPT_SetProfileReflectance), four to a row: HDRP's
    // Fresnel0 of the profile's index of refraction, which its transmission features take in place of a dielectric's 0.04 (a
    // leaf's: profile_f0). 0: none sent.
    float4 profileF0[4];
    // The game's own textures (PT-46): x the heap index of shared texture number 0 (texture_share.hpp; INVALID without the
    // range), y hair's and eyes' map records (SRV, uint4s: map_record; INVALID while none); z, w unused (z was Fog
    // Adaptation's gain until 2026-10-11).
    uint4 textures;
    // The Fog volume's color (TGPT_Frame.fogColor's: the albedo times the tint or the constant color; 0 without fog; rgb:
    // pt_medium fog_albedo's stand-by when the medium's albedo is not sent), w HDRP's maximum fog distance (metres; how far
    // the traced fog reaches over the sky, 0: not given).
    float4 fogColor;
    // HDRP's mip fog (near, far, max mip; read by nothing since 2026-10-11: HDRP's fog colour went with its fog), w the
    // fog's sky (SRV, a uint's bits: HDRP's sky at the mips its fog color was read at; the grass trial reads it).
    float4 fogMip;
    // Caustics: each slot's surface gradient array (SRV, INVALID for still water).
    uint4 waterNormals;
    // The caustics' light where it landed (pt_caustics.hlsl, caustic_light): its triangles' light (raw UAV; x INVALID: none this
    // frame), their acceleration structure (SRV), the counters (raw UAV), the triangles this frame may write (its build's count).
    uint4 caustics;
    // The finest grid cell over the water (metres), the triangles' corners (raw UAV, a uint's bits), the levels, the frame's random
    // number (bits).
    float4 causticGrid;
    // Per level: its grid's corner (x, z relative to the scene origin) and, in its own cells, the corner of the finer level's
    // square it leaves to that level (-1e9: none).
    float4 causticLevels[8];
    // This frame's view-projection without jitter, camera-relative: with prevViewProj (which takes positions relative to this
    // frame's camera too) the rule HDRP makes its motion vectors by, and the tracer its own (pt_primary hit_motion).
    float4x4 motionViewProj;
    // The tracer's own camera rays (pt_primary; UAVs, INVALID without): x each pixel's motion to the previous frame from its
    // hit's own previous position (texture coordinates, as HDRP's motion vectors: this frame's less the previous one's); y
    // its hit's emission (HDRP's units); z the yardstick (its hit against HDRP's G-buffer: yardstick_code); w the moving
    // things' previous transforms (SRV of float4 rows: InstanceRecord.previous), INVALID without.
    uint4 decals; // records, ordered box BVH, node count, receiver layer filtering
    uint4 primary;
    // Where the water has been (pt_wet.hlsl, wet_state; PT-52): the store's cells (raw UAV; INVALID: no water near), its levels,
    // the frame's clock as the game's shader time (seconds, a float's bits: Time.time, which stands still with the world), and
    // the clock of the store's last update (bits).
    uint4 wet;
    // The finest cell (metres), the scene origin's height in the world (metres), the height above which no ground is wet;
    // w: ON TRIAL, the small-lamp switches (lamp_trial; 0: all off), nothing of the wet ground's; its bit 22, ON TRIAL
    // too (PT-93): a light's ray through grass by the product (leaf_product_on).
    float4 wetGrid;
    // Per level: its square's corner (x, z relative to the scene origin) and that corner cell's index in the world's cells of
    // its size (x, z; ints' bits).
    float4 wetLevels[8];
    // x: the hold's W this frame (highlight_kept; 0: HIGHLIGHT_KEPT). y, z: where the previous frame's pixel grid lies
    // against this one's, in pixels (this frame's sub-pixel shift less the previous one's: previous_place). w: that on
    // trial (PT-85, TGPT_Frame.jitter's z): 0 off (y and z are 0: the motion alone, as before), 1 on, 2 the tracer's own
    // histories alone (pt_rtxdi rtxdi_motion).
    float4 look;
    // The water's wave corners kept for the sun's question (pt_sun_grid.hlsl, pt_water_sun.hlsli; the buffer: waterGeometry.z).
    // Per slot two rows: the grid's two steps on the slot's level plane (x, z of each; all 0: no grid), then corner (0, 0)'s
    // x and z relative to the scene origin, the plane's height, and the slot's first byte in the buffer (a uint's bits).
    float4 sunGrid[8];
    // The rain's wetness (rain_roughness; PT-108, ON TRIAL: TGPT_SetRain): x the game's moisture while its wetness pass
    // is on (0: dry, nothing is computed); y how smooth a soaked surface is at the camera (the game's shader's own 0.9,
    // 0.33 in its snow variant); z what stands for the game's second reading of its noise, the one over the screen; w the
    // game's noise texture, its number among the game's own textures (a uint's bits; INVALID: none).
    float4 rain;
    // x: the highest corner of the scene's own water of every slot, relative to the scene origin (-1e30: none), the ones the
    // plugin describes and the ones it does not (pt_water_surface water_ceiling; round 20). y, z, w: 0.
    float4 waterCeiling;
    // The fog as a medium (TGPT_Frame.fogMedium, pt_medium.hlsli): rgb the Fog volume's albedo as its volumetric fog takes it,
    // w its anisotropy (all 0: not sent).
    float4 fogMedium;
    // The game's fog boxes (TGPT_SetFogBoxes, PT-112; pt_medium.hlsli fog_box): x their records' view (INVALID: none), y how
    // many; z, w 0.
    uint4 fogBoxes;
    // PT-112, the sky, ON TRIAL (docs/agents/sky-2.md; pt_sky.hlsli reads them, tracer.cpp fill_sky writes them; all 0 and
    // skyMaps2.z 0 unless the plugin's hidden "Sky trial" asks): the atmosphere's numbers (sky_*: the camera's way up and
    // the planet's radius; air's extinction and 1 / its scale height; air's scattering and the atmosphere's depth; the
    // aerosols' scattering and extinction; ozone's extinction and 1 / the aerosols' scale height; ozone's middle height,
    // 2 / its width, the aerosols' anisotropy and Cornette-Shanks' constant; the ground's albedo and the camera's height;
    // the horizon's tint and the Horizon Zenith Shift's x; the zenith's tint and its y; Color Saturation, Alpha Saturation,
    // Alpha Multiplier and the bodies' count; towards HDRP's sun and the sky's intensity), the celestial bodies (sky_body,
    // eight rows each), the cloud layers (sky_layer, five rows each, then the cloud layer's opacity), and the textures:
    // skyMaps the transmittance, multiple scattering and ground tables and the sky map (SRVs); skyMaps2 the baked clouds
    // (SRV), their ambient light (UAV), the mode and flags (tgpt_api.h TGPT_SKY_*: mode | flags << 8), and the map's mip
    // count when the fog's sky (fogMip.w) is the map (0: HDRP's tiles).
    float4 sky[11];
    float4 skyBodies[32];
    float4 skyClouds[11];
    uint4 skyMaps;
    uint4 skyMaps2;
    uint4 psr; // PT-127 (pt_psr_record.hlsli). x: the mirrors' records for NRD (UAV), or INVALID; y: none; z: a test's view of the records (0 in the game); w: the frame's serial for the records
};
ConstantBuffer<FrameConstants> g : register(b0);
#include "pt_psr_record.hlsli"
struct PassConstants { uint4 a; uint4 b; };
ConstantBuffer<PassConstants> pass : register(b1);
RaytracingAccelerationStructure g_scene : register(t0);
SamplerState s_linearClamp : register(s0);
SamplerState s_linearWrap : register(s1);
SamplerState s_pointClamp : register(s2);
SamplerState s_linearRepeat : register(s3); // wrapping every way: a pattern that tiles (the water's caustics)
SamplerState s_anisoWrap : register(s4);    // the game's own textures at a hit (PT-46): anisotropic 16x, as the game samples them
SamplerState s_anisoClamp : register(s5);   // and clamped: the eye shader's one samplerIndex for its sclera and iris

// ON TRIAL (PT-99, docs/agents/speed-1.md; never a row): speed that leaves the picture as it is. TGPT_Frame.prevCameraPosition's
// w (the plugin's hidden "Speed trial", a whole number, 0 from every other caller: everything as before) reaches the passes
// in the frame flags' bits 16-31 (tracer.cpp). Its bits:
//   1  a shadow ray that nothing stopped asks which water its start lies in (water_medium: one more ray, straight up, as far
//      as the scene reaches) only where water can stand over it: under the highest any slot's waves reach (water_ceiling).
//      tracer.cpp takes the bit out again in a frame whose water's own triangles do not bear that height out.
//   8  for finding causes, the tracer's alone (tracer.cpp dispatch_pass): the bounced light's and the reflections' passes as
//      plain compute passes, without Shader Execution Reordering. No shader reads it.
//   2, 4  ON TRIAL in a build made with CMake's TGPT_REGROUP_WATER alone (docs/agents/perf-1.md; pt_water_path.hlsli
//      water_regroup): threads regrouped by what they do next, in the two reordering passes: the water's path at every
//      turn (2), a point that makes its own patch of waves (4). In every other build no pass holds a line of them.
// (64 turned the water's rule off for a run's comparison on 2026-10-12, PT-131: pt_water_sun.hlsli sun_cell_by_width,
// a patch's cell no finer than half the path's own ray's width. The rule has no switch now; the number does nothing.)
// (16, 32 and 32768 were trials until 2026-10-10, docs/agents/rule-1.md. Two are the rule in every build since, with no
// switch: the lamps' search tree, reflected_lamps, and the feeders' key, regroup_key. The third, the water's short rays,
// came out. In a build for players those three numbers do nothing.)
// Bits 4 to 14 are the measuring build's alone (PT_SPEED_MEASURE, CMake's TGPT_SPEED_MEASURE: never in a build for
// players, which holds none of the lines between that word's #ifdef and #endif): each leaves a part of the work out,
// and the picture with it, so that the stages' GPU times with and without it tell what that part costs.
uint speed_trial() { return g.size.w >> 16; }
static const uint SPEED_WATER_CEILING = 1u;
#ifdef PT_SPEED_MEASURE
static const uint SPEED_M_NO_RAY = 0x0010u;        // pt_gi, pt_specular: the pass's own ray is not traced (the pass's floor)
static const uint SPEED_M_NO_SHADE = 0x0020u;      // pt_gi, pt_specular: what the ray met is not shaded (the ray alone)
static const uint SPEED_M_NO_MATERIAL = 0x0040u;   // hit_material: the material's constants, no texture, program, wet ground or decal
static const uint SPEED_M_NO_DECALS = 0x0080u;     // apply_decals: nothing looked up
static const uint SPEED_M_NO_SUN = 0x0100u;        // direct_light: no sun (its shadow ray with it)
static const uint SPEED_M_NO_LAMPS = 0x0200u;      // direct_light: no lamps (their weighing and the winner's shadow ray)
static const uint SPEED_M_NO_LAMP_RAY = 0x0400u;   // direct_light: the lamps weighed, the winner's shadow ray not traced
static const uint SPEED_M_NO_FEED = 0x0800u;       // cache_feeds: no path feeds the radiance cache (no cache ray)
static const uint SPEED_M_NO_CACHE_READ = 0x1000u; // cache_light: the cache is not read
static const uint SPEED_M_NO_WATER_ASK = 0x2000u;  // water_medium: never asked (every point in the air)
static const uint SPEED_M_NO_SPECIAL = 0x4000u;    // glass is not followed (hit_is_glass) and the water's own path not run (shade_path)
// (0x8000 is not taken. A switch that left Shader Execution Reordering's regrouping out by a branch around PT_REORDER was
// taken out again on 2026-10-09: the one start of a build that held it was stopped at 33 GB of memory while the driver
// was still making the two reordering passes, and whether that branch was why is not known. The tracer's bit 8 answers
// the same question with the plain compute passes: docs/agents/speed-1.md.)
// 0x8000, in a measuring build: the second page. With it the numbers 16 to 2048 are the second page's switches (below),
// and none of the first page's is on.
bool speed_measure(uint bit) { return (speed_trial() & 0x8000u) == 0u && (speed_trial() & bit) != 0u; }
static bool g_speedNoRay = false; // set for the one shadow ray a caller leaves out (transmittance reads and clears it)
// In a measuring build the numbers 2 and 4 are measuring switches too (docs/agents/perf-1.md, 2026-10-09: the game's
// measuring run found the bounce ray alone at 28 of the tavern's 81 ms, in a closed room, and could not say why). In a
// build made with TGPT_REGROUP_WATER 2 and 4 are that trial's: the two are never built together (CMakeLists.txt refuses).
static const uint SPEED_M_SOLID = 0x0002u;      // every triangle a ray meets is solid: no triangle is handed to the shader's
                                                // alpha test (trace_from_surface's queries: the two big passes' own rays; a
                                                // path's further rays and a light's shadow rays keep their test)
static const uint SPEED_M_ONE_QUERY = 0x0004u;  // trace_from_surface: the first query's answer is taken (no second look past a
                                                // near back face, the hero's own body or what the wind moves)
uint speed_ray_flags() { return speed_measure(SPEED_M_SOLID) ? RAY_FLAG_FORCE_OPAQUE : RAY_FLAG_NONE; }
// The second page (32768 and one or more of these): the water's own path by its parts (pt_water_path.hlsli, pt_water_sun.hlsli;
// the game's measuring run: 133 to 151 of the beach's 163 to 181 ms), and the rays' length.
static const uint SPEED_M2_NO_SUN = 0x0010u;    // water_sun gives none: no sun in the water's body
static const uint SPEED_M2_NO_SUM = 0x0020u;    // the sun's tiles are asked, none is summed, and no patch is made at the point
static const uint SPEED_M2_NO_PATCH = 0x0040u;  // no patch is made at the point (the tiles as they are)
static const uint SPEED_M2_NO_GLINT = 0x0080u;  // the sun's glint off the surface is not asked for (its shadow ray)
static const uint SPEED_M2_NO_BALLS = 0x0100u;  // no lamp's ball is looked for along the water's path (reflected_lamps)
static const uint SPEED_M2_NO_SOLID = 0x0200u;  // what the water's path meets that is no water is not shaded: the path ends there
static const uint SPEED_M2_ONE_SCATTER = 0x0400u; // the water's path ends after its first scatter point
static const uint SPEED_M2_NO_LANDED = 0x1000u; // the water's path does not ask for the caustics' landed light where it meets the bed
// The floor of the two big passes by its parts (the game's measuring run: the tavern's bounced light is 14 ms with no ray
// of its own), on this page so that they can be asked together and with the tracer's 8 (the plain compute passes):
static const uint SPEED_M2_NO_RAY = 0x2000u;    // pt_gi, pt_specular: the pass's own ray is not traced (the first page's 16)
static const uint SPEED_M2_NO_REUSE = 0x4000u;  // pt_gi: no reuse across frames (RTXDI's temporal resampling and the ray it checks its sample with)
// The caustics' stage by its parts (pt_caustics.hlsl): its floor (the grid's corners on the waves and each tile's lights,
// the landed triangles' structure built over none), then with each corner's shadow rays, then (neither) with the rays on.
static const uint SPEED_M2_CAUSTIC_BARE = 0x0002u;   // no ray of a light's: no shadow ray, no ray on from the wave
static const uint SPEED_M2_CAUSTIC_SHADOW = 0x0004u; // the shadow rays, no ray on from the wave
// 0x0800 of the second page is the tracer's alone (tracer.cpp): every ray ends after 60 m. No shader reads it.
bool speed_measure2(uint bit) { return (speed_trial() & 0x8000u) != 0u && (speed_trial() & bit) != 0u; }
// The third page (PT-131, docs/agents/campspeed.md: a camp in the forest at 3 frames a second, all of it in shade_path's
// two special branches, glass followed and the water's own path): each of the two by its parts. Asked with the number
// 32769 and one or more of these (the second page's 32768 with the 1 that is the rule in every frame and so asks nothing
// by itself). The tracer hands a third-page number's switches over in a constant of their own (waterCeiling's y, 0 in
// every other frame) and none of the first two pages' with them (tracer.cpp). Not 128 or 256 (with those the tracer
// bakes the leaves' micromaps at another level, on every page); 2048 is the tracer's here too: every ray ends after 60 m.
static const uint SPEED_M3_NO_GLASS = 0x0002u;       // glass is not followed (hit_is_glass: shaded as a plain surface)
static const uint SPEED_M3_NO_WATER = 0x0004u;       // the water's own path is not run (shade_path)
static const uint SPEED_M3_NO_SUN = 0x0010u;         // the water's path: no sun through the waves at a scatter point (water_sun gives none)
static const uint SPEED_M3_NO_PATCH = 0x0020u;       // water_sun: no patch of waves is made at the point (the kept tiles alone)
static const uint SPEED_M3_NO_SCATTER = 0x0040u;     // the water's path: the water's body scatters nothing (it takes its share; the path goes on)
static const uint SPEED_M3_ONE_TURN = 0x0200u;       // the water's path ends after its first turn (one ray, one event)
// (1024 was "water_sun sends its two shadow rays first" until 2026-10-12: its question was answered, and the exact form
// of it gained nothing in the game, docs/agents/campfix.md. The number does nothing now.)
static const uint SPEED_M3_NO_LANDED = 0x1000u;      // the water's path asks for no caustics' landed light at a solid point under water
// 8192: the rule of the sun's patch made at a point OFF, "a cell no finer than half the ray's width"
// (pt_water_sun.hlsli sun_cell_by_width; a build for players has the rule with no switch). Until
// 2026-10-12 it was this page's trial of that rule (docs/agents/campspeed.md, campfix.md): it is in every build
// since, and the path's ray width with it.
static const uint SPEED_M3_SUN_FOOTPRINT = 0x2000u;
static const uint SPEED_M3_GLASS_ONE = 0x4000u;      // the glass walk ends at its first interface (its light with it)
bool speed_measure3(uint bit) { return (uint(g.waterCeiling.y) & bit) != 0u; }
// The counters (PT-131, docs/agents/campspeed.md): what the two special branches do, counted by each thread as it goes
// and added at its pixel's end to a small buffer (speed_count_flush), which the tracer copies back every frame and logs
// once a second (tracer.cpp, "Special branches"). The bounced light's pass and the reflections' alone hand the buffer
// in (their pass constants' last word, which no other pass sets) and call the flush, with 0 and 1 for their block of
// SPEED_COUNTERS words. The buffer holds running sums (nothing empties it: the tracer takes one frame's from the next).
// A thread that met nothing special adds nothing.
static const uint SPEED_COUNTERS = 48; // (tracer.cpp kSpeedBlock)
#define SPEED_COUNT_LIST(X) \
    X(1, g_scWaterPaths)      /* the water's own path run (water_path) */ \
    X(2, g_scWaterBelow)      /* ... that starts in the water (the surface met from below, or a point in it) */ \
    X(3, g_scWaterFar)        /* ... that starts more than 60 m from the camera */ \
    X(4, g_scWaterTurns)      /* turns of its loop (a ray each) */ \
    X(5, g_scWaterTurnsIn)    /* ... that start in the water */ \
    X(6, g_scWaterScatter)    /* scatter points in the water's body */ \
    X(7, g_scWaterSurface)    /* the surface crossed or mirrored */ \
    X(8, g_scWaterSolid)      /* what is no water (or foam) met and shaded */ \
    X(9, g_scWaterLong8)      /* paths of 8 turns and more */ \
    X(10, g_scWaterLong32)    /* ... of 32 and more */ \
    X(11, g_scWaterLong128)   /* ... of 128 and more */ \
    X(12, g_scSunAsked)       /* water_sun asked through the waves (the Caustics row on, a sun) */ \
    X(13, g_scSunAbove)       /* ... and found the surface over the point */ \
    X(14, g_scSunTiles)       /* ... answered from the kept tiles */ \
    X(15, g_scSunTileTests)   /* tiles' records asked */ \
    X(16, g_scSunTileCorners) /* tiles' corners bent (sun_bend) */ \
    X(17, g_scSunPatches)     /* patches made at the point */ \
    X(18, g_scSunPatchTurns)  /* their loop's turns (each makes the whole patch anew) */ \
    X(19, g_scSunPatchCorners) /* their corners (water_displace: 15 texture reads each) */ \
    X(20, g_scSunPatchBig)    /* patches of 256 corners and more in all */ \
    X(21, g_scSunNoGrid)      /* a patch because the slot keeps no corners */ \
    X(22, g_scSunNotLevel)    /* ... because the surface over the point is not the kept corners' level plane */ \
    X(23, g_scSunOffGrid)     /* ... because the point's block of tiles leaves the kept corners */ \
    X(24, g_scSunWay)         /* the points' way to the surface along the bent ray, metres, summed (1000 at most each) */ \
    X(25, g_scLanded)         /* the caustics' landed light asked at a solid point under water */ \
    X(26, g_scGlassWalks)     /* the glass walk run (dielectric_follow) */ \
    X(27, g_scGlassInterfaces) /* its interfaces */ \
    X(28, g_scGlassCandidates) /* triangles its "which glass am I in" rays were handed (optical_medium) */ \
    X(29, g_scGlassToWater)   /* walks that ended on water */ \
    X(30, g_scSunStraight)    /* water_sun answered by the straight ray (the Caustics row off) */ \
    X(31, g_scWaterHere)      /* the water's path from a point (the camera in the water, a pixel under it) */ \
    X(32, g_scWaterOver)      /* the water's path starts over the camera's height */ \
    X(33, g_scWaterUp)        /* ... metres over it, summed (1000 at most each) */ \
    X(34, g_scWaterDown)      /* metres under the camera's height, summed, of the paths that start under it */ \
    X(35, g_scWaterAway)      /* the paths' starts' distance from the camera, metres, summed (10000 at most each) */ \
    X(36, g_scSunCoarse)      /* a patch because the kept corners' cell is coarser than the waves there want */ \
    X(37, g_scSunTilt)        /* ... because the facets' tilt cannot be bounded (or the rays lie along the plane) */ \
    X(38, g_scSunGrows)       /* ... because the block's size cannot be solved for (it grows with its own reach) */ \
    X(39, g_scSunWide)        /* ... because the block holds more tiles than are ever asked */ \
    X(40, g_scSunLit)         /* water_sun's light reached the point (its shadow rays let some through) */ \
    X(41, g_scSunBlocked)     /* ... was stopped on its way (the tiles summed or the patch made for nothing) */ \
    X(42, g_scSunNone)        /* ... no light landed round the point (or the patch was left to chance) */ \
    /* (43 is free: it counted the third page's "shadow first", which came out on 2026-10-12) */ \
    X(44, g_scSunWidth)       /* the width of the path's ray at the points that ask for the sun, decimetres, summed (1 km at most each) */ \
    X(45, g_scSunCoarser)     /* patches whose cell the rule, the cell by the ray's width, made coarser than the waves' */ \
    X(46, g_scSunNarrow)      /* points that ask for the sun with a ray under 10 cm wide (a pattern as fine as the waves' cell can show) */
#define SPEED_COUNT_DECLARE(at, name) static uint name = 0u;
SPEED_COUNT_LIST(SPEED_COUNT_DECLARE)
#define SPEED_COUNT_ANY(at, name) any |= name;
#define SPEED_COUNT_ADD(at, name) if (name != 0u) counters.InterlockedAdd(base + 4u * at, name);
void speed_count_flush(uint which) {
    if (pass.b.w == 0xFFFFFFFFu) return;
    uint any = 0u;
    SPEED_COUNT_LIST(SPEED_COUNT_ANY)
    if (any == 0u) return;
    RWByteAddressBuffer counters = ResourceDescriptorHeap[pass.b.w];
    const uint base = which * SPEED_COUNTERS * 4u;
    counters.InterlockedAdd(base, 1u); // word 0: the pixels that met anything special
    SPEED_COUNT_LIST(SPEED_COUNT_ADD)
}
#define PT_RAY_FLAGS speed_ray_flags() // trace_from_surface's query flags: the first page's 2
#else
#define PT_RAY_FLAGS RAY_FLAG_NONE
#endif

// previous: a thing the game moves (TGPT_SetMoving): its row in the frame's previous transforms (FrameConstants.primary.w, three
// float4 rows each: object to world a frame ago, relative to this frame's origin); INVALID for everything else.
struct InstanceRecord { uint mesh; uint materialBase; uint flags; uint previous; uint renderingLayerMask; };
// previous: a character's positions as the previous frame skinned them (pt_skin; INVALID for a mesh that is not skinned, or
// in the first frame it is): what a hit's own motion is made of (pt_primary hit_motion).
// uvs2: the mesh's fourth set of texture coordinates (TEXCOORD3; INVALID without), which the game's trunk shader lays its bark by.
struct MeshRecord { uint positions; uint uvs; uint colors; uint indices; uint geometryBase; uint geometryCount; uint vertexCount; uint normals; uint previous; uint uvs2; uint tangents; uint decalColors; };
// flags: GEOMETRY_CLOSED where the submesh's surface closes a volume (scene.cpp mark_closed).
// layers: a plant group's submesh keeps its member's rendering layers (GEOMETRY_OWN_LAYERS), which decals ask for.
struct GeometryRecord { uint indexStart; uint indexCount; uint layers; uint flags; };
static const uint GEOMETRY_CLOSED = 1u;
static const uint GEOMETRY_OWN_LAYERS = 2u;
// flags >> GEOMETRY_SOLID_SHIFT: how many of the submesh's triangles, from its first, are of pieces that close a volume
// (mark_closed puts them first): piece_solid.
static const uint GEOMETRY_SOLID_SHIFT = 8u;
struct MaterialRecord {
    float4 baseColor; float4 uvST;
    float3 emissive; float metallic;
    int albedoTile; int emissiveTile; float smoothness; float alphaCutoff;
    uint flags; int alphaTile; int maskTile; uint maskRemap; // alphaTile: with MATERIAL_ALPHA_TILE, the alpha atlas's tile;
                                                              // maskTile, maskRemap: with MATERIAL_MASK_TILE (metallic_smoothness)
    // Hair's highlights (TGPT_Material's hair fields, hair_specular): with MATERIAL_HAIR_LOBES, its lobe tile, the lobes'
    // shifts, the second's smoothness, the first's ends, their strengths and the tint.
    int hairLobeTile; float hairShift; float hairShift2; float hairSmooth2;
    float hairSmoothMin; float hairSmoothMax; float hairStrength; float hairStrength2;
    float3 hairTint;
    uint hairAlpha; // with MATERIAL_HAIR_LAYER: RealHair's _AlphaPower (low half) and _AlphaRemap (high), halves (pt_hair)
    // With MATERIAL_SHARED (PT-46): the game's own color, normal and mask maps by their number in the heap (g.textures.x
    // on; -1 none: the tiles above serve), the normal map's strength, and textureFlags (the mask map's channels, its occlusion).
    int colorTexture; int normalTexture; int maskTexture; float normalScale;
    // subsurface (TGPT_Material.subsurface, PT-40): with MATERIAL_SUBSURFACE, Burley's shape of its diffusion profile per
    // channel, per metre (1 / its scattering distance in the world: subsurface_walk); with MATERIAL_TRANSLUCENT, the share of
    // the light from behind a thin sheet of it lets through per channel (thin_through); 0: not given.
    uint textureFlags; float3 subsurface;
    // PT-64: the game's shader that computes it at a hit and its record (pt_materials.hlsli; 0: HDRP Lit's way from the
    // fields above); its emissive map by its number (-1: emissiveTile's copy serves); glass's own index of refraction (0: none).
    uint program; uint programRow; int emissiveTexture; float ior;
    // subsurfaceMask (TGPT_Material.subsurfaceMask, PT-40): with MATERIAL_SUBSURFACE, the game's subsurface mask for the
    // material, the share of its diffuse light that scatters under the surface (hit_material: SurfaceMaterial.subsurface).
    float3 absorption; uint surfaceModel; float subsurfaceMask; uint2 opticalReserved;
};
struct LightRecord {
    float3 position; uint type;
    float3 color; float range;
    float3 direction; float radius;
    float angleScale; float angleOffset; uint id; uint flags; // flags: 1 + its cookie, 0 without one
    float3 up; float cookieScale;
};

// ---------------------------------------------------------------- packing
float luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 srgb_to_linear(float3 c) { return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4)); }
float3 linear_to_srgb(float3 c) { return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055); }
// HDRP's FastSRGBToLinear, the inverse of the FastLinearToSRGB it writes GBuffer2 with.
float3 fast_srgb_to_linear(float3 c) { return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878); }
float2 oct_wrap(float2 v) { return (1.0 - abs(v.yx)) * select(v.xy >= 0.0, 1.0, -1.0); }
uint pack_normal(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
    uint2 q = uint2(round(saturate(e * 0.5 + 0.5) * 65535.0));
    return q.x | (q.y << 16);
}
float3 unpack_normal(uint p) {
    float2 e = float2(p & 0xFFFF, p >> 16) / 65535.0 * 2.0 - 1.0;
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}
// The same as two numbers from 0 to 1, for a guide texture of 8 bits a channel (traced_guides; d3d11.hlsl guide_normal).
float2 guide_octahedral(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    const float2 e = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
    return saturate(e * 0.5 + 0.5);
}
// The same in 22 bits (11:11), for the geometric normal beside the roughness.
uint pack_normal22(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
    uint2 q = uint2(round(saturate(e * 0.5 + 0.5) * 2047.0));
    return q.x | (q.y << 11);
}
// And in 16 (8 each: half a degree), for a normal a rough lobe is drawn about (pt_primary write_traced: glass's words).
uint pack_normal16(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
    uint2 q = uint2(round(saturate(e * 0.5 + 0.5) * 255.0));
    return q.x | (q.y << 8);
}
float3 unpack_normal16(uint p) {
    float2 e = float2(p & 0xFF, (p >> 8) & 0xFF) / 255.0 * 2.0 - 1.0;
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}
float3 unpack_normal22(uint p) {
    float2 e = float2(p & 0x7FF, (p >> 11) & 0x7FF) / 2047.0 * 2.0 - 1.0;
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}
// HDRP's normal buffer: octahedral normal in 12:12 bits over RGB, perceptual roughness in A.
float3 decode_hdrp_normal(float4 v) {
    uint3 i = (uint3)(v.rgb * 255.5);
    uint hi = i.z >> 4, lo = i.z & 15;
    uint2 cb = i.xy | uint2(lo << 8, hi << 8);
    float2 f = float2(cb) / 4095.0 * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.xy += float2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}
// What a pixel's reflection (or its view through glass) shows, for the reflections' filter (PT-80; pt_specular writes it,
// pt_atrous and pt_temporal read it; the texture: g.material.z): the kind, the normal of the surface the ray met (22 bits)
// and that surface's reflectance (its diffuse color plus what its specular lobe reflects: 10 bits a channel of its square
// root). The filter exchanges a mirror's light only between pixels that show the same surface: one normal, one plane, one
// reflectance to within a few hundredths. It never divides the light by the reflectance: the surface's own lamp highlights
// are in the same signal, and filtered so they doubled a mirror's light (docs/agents/filters.md).
static const uint SEEN_NONE = 0u;      // no surface: the sky, or no ray (a rough surface's light from the radiance cache)
static const uint SEEN_REFLECTED = 1u; // a surface met along the reflected ray
static const uint SEEN_WATER = 2u;     // the water's own path: its filter stays as it was
static const uint SEEN_THROUGH = 3u;   // a surface seen through glass, along the camera's ray
// A surface as smooth as this shows a picture (a mirror's): its reflection is filtered by what it shows, and seen in a
// mirror itself it has no guide (seen_picture). A rougher one's blends into the filter across the surface (SEEN_BLUR_ROUGHNESS).
static const float SEEN_PICTURE_ROUGHNESS = 0.15, SEEN_BLUR_ROUGHNESS = 0.35;
uint2 pack_seen(uint kind, float3 normal, float3 reflectance) {
    uint3 q = uint3(0u, 0u, 0u);
    if (any(reflectance > 0.0)) q = max(uint3(round(sqrt(saturate(reflectance)) * 1023.0)), uint3(1u, 1u, 1u));
    return uint2(pack_normal22(dot(normal, normal) > 0.0 ? normal : float3(0.0, 0.0, 1.0)) | (kind << 22), q.x | (q.y << 10) | (q.z << 20));
}
uint seen_kind(uint2 v) { return (v.x >> 22) & 3u; }
// A reflected or seen-through surface with no guide of its own (a mirror in the mirror, water, the sky behind glass): its
// samples are no one else's to share.
bool seen_picture(uint2 v) { return (seen_kind(v) == SEEN_REFLECTED || seen_kind(v) == SEEN_THROUGH) && (v.y & 0x3FFFFFFFu) == 0u; }
float3 seen_normal(uint2 v) { return unpack_normal22(v.x & 0x3FFFFFu); }
float3 seen_reflectance(uint2 v) {
    const float3 r = max(float3(v.y & 1023u, (v.y >> 10) & 1023u, (v.y >> 20) & 1023u), 1.0) / 1023.0;
    return r * r;
}
uint2 pack_half3(float3 c, uint flags) { return uint2(f32tof16(c.x) | (f32tof16(c.y) << 16), f32tof16(c.z) | (flags << 16)); }
float3 unpack_half3(uint2 p) { return float3(f16tof32(p.x & 0xFFFF), f16tof32(p.x >> 16), f16tof32(p.y & 0xFFFF)); }
uint pack_rgba8(float4 c) { uint4 q = uint4(round(saturate(c) * 255.0)); return q.x | (q.y << 8) | (q.z << 16) | (q.w << 24); }
float4 unpack_rgba8(uint p) { return float4(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF, p >> 24) / 255.0; }

// The primary surface as the prepare pass stores it.
struct Surface {
    bool water, underwater; // dielectric water, or a camera segment in its medium; packed kind 10
    float3 position;   // relative to the scene origin
    float distance;    // from the camera; 0 for the sky
    float3 normal;     // shading normal, including the material normal map
    float3 geoNormal;  // geometric normal of the first surface
    float roughness;   // perceptual
    float3 albedo;     // first-surface colour, linear
    float3 f0;
    bool deferred;     // shaded by the tracer: HDRP's deferred material, or a surface HDRP draws forward that the camera ray found
    bool translucent;  // HDRP's transmission (grass, leaves): lit from behind too (translucent_cos)
    bool complex;      // thin or finely detailed where the pixel is (pt_prepare): ReSTIR's reuse isn't permuted there
    bool valid;
    bool forward;      // drawn forward by HDRP (hair, some props): its color is the traced material's, not in the GBuffer
    bool glass;        // a dielectric first surface; all its radiance is in the optical signal
    bool eye;          // iris/sclera material, shared by camera and secondary rays
    float through;     // the share a translucent surface lets through from behind (translucent_cos): a leaf's, or thin cloth's
    // A leaf's or a blade's own occlusion (pt_prepare's bit 128, HDRP's from its material): of its bounced and sky light
    // (pt_compose) and of its reflections (pt_specular). 1 everywhere else.
    float ambientOcclusion;
    float specularOcclusion;
    // Hair with its highlights (pt_prepare, hair_specular), this frame's surface only: the strand's direction, its material,
    // its specular map (x) and smoothness (y) texel.
    bool hair;
    float3 strand;
    uint hairMaterial;
    float2 hairLobe;
    // Hair the tracer's own camera ray stopped at (pt_primary write_hair: HAIR_STRAND in the hair texture), with or without
    // highlights, this frame's surface only: its direct light is a strand's (strand_cos), its rays leave from its own card.
    bool strands;
    bool skin;         // HDRP's skin (transmission through a solid diffusion profile, pt_prepare): terminator_softening is for it alone
    // On the traced mesh itself (pt_prepare, PT-43, PT-44): a character's skin whose own triangle the camera ray met under
    // HDRP's surface, or an eye (MATERIAL_EYE: the camera ray's hit, seen by camera rays alone). Its rays leave from just above
    // that triangle (surface_offset), and a back face they meet is a real hit (surface_near_back): the face's own lids, brow
    // and nose shadow it. From ray_offset's 2 cm out they began in front of the lids, and the eye was lit edge to edge.
    bool onMesh;
};
// HDRP's reflectance of hair (DEFAULT_HAIR_SPECULAR_VALUE), which the game's RealHair keeps.
static const float HAIR_F0 = 0.0465;
// In the hair texture's second word, beside the material: hair the tracer's own camera ray stopped at (Surface.strands).
static const uint HAIR_STRAND = 0x80000000u;
// The cosine a light arriving along l lights a surface with: from in front, and for a translucent one also from behind, the
// part it lets through (through): a grass blade's or a leaf's (TRANSMISSION: without it every blade facing away from the
// sun was black, where HDRP lights it through), or thin cloth's, the light through its weave (CLOTH_TRANSMISSION, as white
// linen lets through a quarter to a third of it: a banner held up to the sun shows the light behind it, dimmer than a leaf).
// What the two shares stand for since PT-40 (2026-10-07; the owner: thin things "pass light by their own material"):
//   A leaf or a blade (HDRP's transmission through a thin diffusion profile) passes what its material says: HDRP's
//   transmittance of its profile at its own thickness, tinted, times its transmission mask: per pixel from the GBuffer where
//   the camera sees it (leaf_transmittance), per material where a path meets it (thin_through, from the plugin). TRANSMISSION
//   is then only the grey share ReSTIR weighs such a pixel's samples by and its rays' split between the two sides (the
//   final shading divides it out: leaf_tint), the share lit through where the material says nothing (no profile read: an
//   older plugin, a shader without one), and how often a light's ray passes a blade (alpha_opaque: the owner's ruling on
//   grass shadows, 2026-10-06, "soft and partial"; a look, not a material's number).
//   Thin cloth has no such material data: the game draws its cloth (TG/Clothes/Cloth_Wind and the plain Lit materials
//   ClothRules finds by name) as HDRP's standard material, with no diffusion profile, no thickness and no transmission,
//   and HDRP itself lights it opaque. Its rule stays the stated one: CLOTH_TRANSMISSION of the light through the weave,
//   tinted by the cloth's own color (cloth_tint), nothing else to go on.
static const float TRANSMISSION = 0.5;
static const float CLOTH_TRANSMISSION = 0.3;
float translucent_cos(float3 normal, float3 l, float through) {
    float c = dot(normal, l);
    return c > 0.0 ? c : -c * through;
}
// A thin sheet's own share of the light from behind per channel (MaterialRecord.subsurface with MATERIAL_TRANSLUCENT), false
// where the plugin gave none.
bool thin_through(MaterialRecord m, out float3 share) {
    share = m.subsurface;
    return (m.flags & MATERIAL_TRANSLUCENT) != 0 && all(share >= 0.0) && any(share > 0.0) && all(share < 1e6);
}
// (Light through thin skin, an ear or a hand held up to a candle, is the walk's inside the traced mesh: pt_walk,
// subsurface_walk. HDRP's thin-object transmission by its thickness map, which stood in for it while the pixel's surface was
// HDRP's, is gone: PT-58.)
// Where a shadow ray towards a light along dir leaves the surface: a little above it, and for a translucent leaf lit
// from behind on the side the light is on (a ray from the camera's side would cross the leaf and be stopped by it).
// An opaque surface always starts outside: switching sides where its own flat triangle turned from the light moved
// the origin some 4 cm into a head, and the jump left a hard edge across faces (on skin terminator_softening fades the
// light out there instead).
float3 shadow_origin(float3 position, float3 geoNormal, float3 dir, float offset, bool translucent) {
    return position + geoNormal * (!translucent || dot(geoNormal, dir) >= 0.0 ? offset : -offset);
}
// The same for a surface the camera sees: a translucent one starts on the light's side only where its shading normal turns
// from the light as well (PT-04, PT-40). Where only its flat triangle turned away (the faces just before the shadow line of
// a closed, round translucent thing: candle wax, which HDRP gives transmission through a thin profile), the ray started
// inside it, a closed alpha-tested mesh stopped it, and each such face went black from edge to edge. A leaf's two normals
// differ by its normal map alone, and where that decides, the light lies along its plane and brings next to nothing.
float3 shadow_origin(Surface s, float3 dir, float offset) {
    // Hair's card (Surface.strands) always on the light's side: its strands take the light from either side, and a ray from
    // the camera's side would cross its own card.
    const bool lightSide = s.translucent && (s.strands || (g.settings2.y & OPTION_FACETED) != 0 || dot(s.normal, dir) < 0.0);
    return shadow_origin(s.position, s.geoNormal, dir, offset, lightSide);
}
int water_medium(float3 p);
static bool g_waterMediumKnown = false;
static int g_waterMedium = -1;
Surface load_surface(uint2 p, uint surfaceIndex, uint normalIndex, uint materialIndex) {
    RWTexture2D<float4> surf = ResourceDescriptorHeap[surfaceIndex];
    RWTexture2D<uint2> nrm = ResourceDescriptorHeap[normalIndex];
    Surface s = (Surface)0;
    if (g.primary.y != INVALID) {
        RWTexture2D<float4> own = ResourceDescriptorHeap[g.primary.y];
        s.underwater = own[p].a > 0.0;
    }
    float4 a = surf[p];
    uint2 n = nrm[p];
    s.position = a.xyz; s.distance = a.w;
    s.normal = unpack_normal(n.x);
    s.geoNormal = (n.y >> 10) != 0 ? unpack_normal22(n.y >> 10) : s.normal;
    s.roughness = float(n.y & 0xFF) / 255.0;
    s.deferred = (n.y & 0x100) != 0;
    s.translucent = (n.y & 0x200) != 0;
    s.through = s.translucent ? TRANSMISSION : 0.0;
    s.valid = a.w > 0.0;
    if (materialIndex != INVALID) {
        RWTexture2D<uint2> mat = ResourceDescriptorHeap[materialIndex];
        uint2 m = mat[p];
        float4 color = unpack_rgba8(m.x);
        s.albedo = srgb_to_linear(color.rgb);
        // The color's alpha (pt_prepare): 1 drawn forward, a quarter skin, seven sixteenths a character's skin on its traced
        // mesh (onMesh), 0 the rest.
        s.forward = color.a > 0.5;
        s.skin = color.a > 0.125 && color.a < 0.5;
        float4 spec = unpack_rgba8(m.y);
        s.f0 = srgb_to_linear(spec.rgb);
        const uint bits = m.y >> 24; // pt_prepare: 1 a complex surface, 2 glass, 4 thin cloth or hair, 8 an eye, 16 retired, 32 retired, 128 a leaf
        if (s.translucent && (bits & 4u) != 0) s.through = CLOTH_TRANSMISSION;
        s.complex = (bits & 1u) != 0;
        s.water = bits == 10u; // the water's own surface (pt_primary): the one kind with both bits
        s.glass = (bits & 2u) != 0 && !s.water;
        s.eye = (bits & 8u) != 0 && !s.water;
        // A leaf or a blade of grass (bit 128): its reflectance, a single number, in red; its ambient and specular occlusion in
        // green and blue.
        s.ambientOcclusion = 1.0; s.specularOcclusion = 1.0;
        if ((bits & 128u) != 0) { s.ambientOcclusion = spec.g; s.specularOcclusion = spec.b; s.f0 = srgb_to_linear(spec.rrr); }
        // Skin: its reflectance (a dielectric's grey) in red, its material's ambient occlusion in green (pt_prepare: HDRP's, from
        // GBuffer3, as for a leaf), which pt_compose takes off its bounced and sky light (PT-43: the game's skin shader darkens
        // its mouth, nostrils and lips so).
        if (s.skin) { s.ambientOcclusion = spec.g; s.f0 = srgb_to_linear(spec.rrr); }
        // An eye the tracer shades (bit 8: MATERIAL_EYE), or a character's skin on its traced mesh.
        s.onMesh = (s.eye) || (s.skin && color.a > 0.375);
        // Hair with its highlights (bit 64): its specular color's bytes hold its specular map's and its smoothness's texel (hair's
        // reflectance is HAIR_F0); its strand and material are in the hair texture, for this frame's surface (a previous
        // frame's is weighed with hair's reflectance alone, as a plain specular lobe).
        s.hair = false; s.strand = 0.0; s.hairMaterial = 0u; s.hairLobe = 0.0; s.strands = false;
        if ((bits & 64u) != 0) {
            s.f0 = HAIR_F0;
            s.hairLobe = spec.rg;
        }
        // Its strand and material are in the hair texture (pt_prepare writes it for every pixel: none, 0). Hair the tracer's own
        // camera ray stopped at (HAIR_STRAND, with bit 4) is lit about its strand and on its own card (onMesh: its rays leave a
        // millimetre off it, so the cards around it shadow it; from ray_offset's 2 cm the strands between two cards went unseen).
        if ((bits & (4u | 64u)) != 0 && materialIndex == g.material.x && g.hair.x != INVALID) {
            RWTexture2D<uint2> hairTex = ResourceDescriptorHeap[g.hair.x];
            const uint2 h = hairTex[p];
            s.strands = (h.y & HAIR_STRAND) != 0u && s.forward;
            if ((bits & 64u) != 0 || s.strands) { s.strand = unpack_normal(h.x); s.hairMaterial = h.y & ~HAIR_STRAND; }
            s.hair = (bits & 64u) != 0;
            if (s.strands) s.onMesh = true;
        }
    } else {
        s.albedo = 0.5; s.f0 = 0.04; s.forward = false; s.complex = false; s.glass = false; s.eye = false; s.skin = false; s.onMesh = false;
        s.ambientOcclusion = 1.0; s.specularOcclusion = 1.0;
        s.hair = false; s.strand = 0.0; s.hairMaterial = 0u; s.hairLobe = 0.0; s.strands = false;
    }
    return s;
}

// ---------------------------------------------------------------- random numbers
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
struct Rng {
    uint state;
    float next() { state = pcg(state); return float(state >> 8) * (1.0 / 16777216.0); }
    float2 next2() { return float2(next(), next()); }
};
Rng make_rng(uint2 pixel, uint frame, uint salt) {
    Rng r; r.state = pcg(pixel.x + pcg(pixel.y + pcg(frame * 7919u + salt * 0x9E3779B9u)));
    return r;
}

// ---------------------------------------------------------------- sampling
void basis(float3 n, out float3 t, out float3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = float3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = float3(c, s + n.y * n.y * a, -n.y);
}
float3 to_world(float3 v, float3 n) { float3 t, b; basis(n, t, b); return v.x * t + v.y * b + v.z * n; }
float3 cosine_sample(float3 n, float2 u) {
    float r = sqrt(u.x), phi = 2.0 * PI * u.y;
    return normalize(to_world(float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u.x))), n));
}
float3 cone_sample(float3 dir, float cosMax, float2 u) {
    float c = 1.0 - u.x * (1.0 - cosMax);
    float s = sqrt(max(0.0, 1.0 - c * c)), phi = 2.0 * PI * u.y;
    return normalize(to_world(float3(s * cos(phi), s * sin(phi), c), dir));
}
float2 disk_sample(float2 u) { float r = sqrt(u.x), phi = 2.0 * PI * u.y; return r * float2(cos(phi), sin(phi)); }
// Taming the Shadow Terminator (Chiang, Li and Burley 2019): a shading normal (smooth, normal-mapped) can face a light
// the surface's own flatter triangle faces away from, and the traced shadow then cuts the light off in a step along
// the triangles (faces, rounded armour, pillars). The light fades out towards the geometric terminator instead. Not
// for a translucent surface, lit from behind on purpose.
// Skin only (PT-04). The fade works face by face: where a flat face leans less towards the light than the smooth normal,
// the light it leaves is that face's own flat cosine, so every face near the shadow line was one tone with a step at its
// edge, and the shadow line ran straight along the mesh (a jug of 12 to 16 sides, a cup, a shoulder, a cloth fold). Their
// shadow rays start ray_offset outside the surface (shadow_origin), which clears a convex face's own mesh, so there was
// nothing for it to hide. Skin keeps it, as the owner judged faces with it.
float terminator_softening(Surface s, float3 l) {
    if (s.translucent) return 1.0;
    if (!s.skin && (g.settings2.y & OPTION_FACETED) == 0) return 1.0;
    float ns = dot(s.normal, l), ng = dot(s.geoNormal, l), nn = dot(s.geoNormal, s.normal);
    if (ns <= 0.0 || nn <= 0.0) return 1.0;
    float g = saturate(ng / (ns * nn));
    return g * (1.0 + g * (1.0 - g));
}

// Reflection Boost on a surface's finish (the owner, 2026-10-06: "we want metal to reflect more than stone and brick").
// Everything gets a little glossier; a metal twice that, and mirrors more, towards the brightest its own color goes (a gold
// stays gold). How much of a metal a surface is comes from its specular color: a non-metal's is 0.04, a metal's its own
// color. Until then the boost raised every surface's mirror share towards 0.6 by the same step: at 0.1 that doubled what
// stone, wood and soil mirror (0.04 to 0.079: up to three fifths of the lifted shadows outdoors) and left a metal at 0.7 as
// it was. d3d11.hlsl's guides for Ray Reconstruction say the same. It never roughens: a surface smoother than the boost's
// 0.05 (a mirror: smoothness 1, roughness 0) keeps its own roughness (PT-46: at the boost's 1 a mirror became 0.05 rough).
void reflection_boost(inout float roughness, inout float3 f0, float boost) {
    const float top = max(f0.r, max(f0.g, f0.b));
    const float metal = saturate((top - 0.08) / 0.4);
    const float unboosted = roughness;
    roughness = lerp(roughness, 0.05, saturate(boost * (1.0 + metal)));
    roughness = min(roughness, unboosted);
    f0 = lerp(f0, f0 / max(top, 1e-3), 0.7 * boost * metal);
}

// ---------------------------------------------------------------- BRDF
// With the grazing reflectance of a surface that barely reflects scaled down with it, as Unreal's and Frostbite's Schlick are
// (f90 = saturate(50 f0), 1 from a reflectance of 2 % on): below that no real material lies, and HDRP's Unlit (f0 0,
// pt_prepare) reflects nothing at all, as in the game.
float3 fresnel_schlick(float3 f0, float cosTheta) {
    const float f90 = saturate(50.0 * dot(f0, float3(0.3333, 0.3333, 0.3333)));
    return f0 + (f90 - f0) * pow(1.0 - saturate(cosTheta), 5.0);
}
// d is never below a2 (nh at most 1), so the denominator never nears 0 for a real roughness; the floor only keeps a NaN out.
// With 1e-7 added to it, the peak of any surface smoother than a roughness of about 0.12 was capped (a2 / 1e-7 against
// 1 / (pi a2)): a glossy floor's, a puddle's, polished metal's and Reflection Boost's highlights of the sun and the lamps
// lost up to 99.9 % of their light, and glass 97.5 %.
float ggx_d(float nh, float a2) { float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / max(PI * d * d, 1e-30); }
float smith_g1(float nx, float a2) { return 2.0 * nx / (nx + sqrt(a2 + (1.0 - a2) * nx * nx) + 1e-7); }
float roughness_alpha(float perceptual) { float r = max(perceptual, 0.045); return r * r; }
// GGX's alpha for drawing a reflection ray, and for how much its lobe widens the ray's cone: the surface's own, down to a
// mirror's. roughness_alpha's floor (0.045, alpha 0.002) is for evaluating the lobe at a light, whose peak it bounds; drawn
// from, it made every mirror a little rough (PT-46, mirrors not sharp): its rays strayed by about 0.004 radians, four pixels'
// angle at the game's 1924x1084, and its cone widened three times as fast as a pixel's, so it read its textures coarser than
// the camera does and the denoiser averaged the strays into a blur. Offline (tgpt_gpu_test, a checker in a perfect mirror
// against the camera's view of it from the mirror's place): mip 1.71 against 1.01, fine detail 0.35 of the direct view's;
// at 0.01 (alpha 1e-4, rays straying a tenth of a pixel) mip 1.00 and detail 0.96.
float reflection_alpha(float perceptual) { float r = max(perceptual, 0.01); return r * r; }
// The directional albedo of the specular lobe (Karis' analytic fit of the split-sum lookup table).
float3 specular_albedo(float3 f0, float perceptual, float nv) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = perceptual * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}
// Specular guide energy includes a dielectric's transmitted light. The actual
// radiance still comes from dielectric_follow and its real optical path.
float3 specular_guide_albedo(Surface s, float nv) {
    if (s.water || s.underwater) return 1.0;
    float3 a = specular_albedo(s.f0, s.roughness, nv);
    if (s.glass) a += (1.0 - fresnel_schlick(s.f0, nv));
    return a;
}
// Specular BRDF times cos(theta_l).
float3 ggx_specular(float3 n, float3 v, float3 l, float3 f0, float alpha) {
    float3 h = normalize(v + l);
    float nl = saturate(dot(n, l)), nv = saturate(dot(n, v)), nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
    if (nl <= 0.0 || nv <= 0.0) return 0.0;
    float a2 = alpha * alpha;
    return fresnel_schlick(f0, vh) * ggx_d(nh, a2) * smith_g1(nl, a2) * smith_g1(nv, a2) / (4.0 * nv + 1e-6);
}
// Visible-normal sampling of GGX (Heitz 2018); returns the reflected direction.
float3 ggx_sample(float3 n, float3 v, float alpha, float2 u) {
    float3 t, b; basis(n, t, b);
    float3 vl = float3(dot(v, t), dot(v, b), dot(v, n));
    float3 vh = normalize(float3(alpha * vl.x, alpha * vl.y, vl.z));
    float lensq = vh.x * vh.x + vh.y * vh.y;
    float3 t1 = lensq > 0.0 ? float3(-vh.y, vh.x, 0.0) * rsqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 t2 = cross(vh, t1);
    float r = sqrt(u.x), phi = 2.0 * PI * u.y;
    float p1 = r * cos(phi), p2 = r * sin(phi);
    float s = 0.5 * (1.0 + vh.z);
    p2 = (1.0 - s) * sqrt(1.0 - p1 * p1) + s * p2;
    float3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
    float3 hl = normalize(float3(alpha * nh.x, alpha * nh.y, max(0.0, nh.z)));
    float3 h = hl.x * t + hl.y * b + hl.z * n;
    return reflect(-v, h);
}

// ---------------------------------------------------------------- camera
// HDRP's ComputeWorldSpacePosition with UNITY_UV_STARTS_AT_TOP: pixel centers, reversed-Z depth.
float3 camera_relative_position(uint2 pixel, float depth) {
    float2 uv = (float2(pixel) + 0.5) / float2(g.size.xy);
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    float4 w = mul(g.invViewProj, clip);
    return w.xyz / w.w;
}
// The texture coordinate a camera-relative position projects to this frame, and whether it is on screen.
bool project(float3 cameraRelative, out float2 uv) {
    float4 clip = mul(g.viewProj, float4(cameraRelative, 1.0));
    if (clip.w <= 1e-4) { uv = 0; return false; }
    float2 ndc = clip.xy / clip.w;
    uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    return all(uv >= 0.0) && all(uv <= 1.0);
}
// Traced light is stored (signals, the built-in denoiser's history) relative to the reference radiance, which keeps
// it within half precision's range whatever units the game lights in (HDRP's sun: up to about 10^5 lux); the compose
// pass multiplies it back out.
float reference_radiance() { return g.radiance.x > 0.0 && g.radiance.x < 1e30 ? g.radiance.x : 1.0; }
float3 to_storage(float3 c) { return c / reference_radiance(); }
// NRD's units put the bounced light's median at a half; RELAX keeps the square of the light in half floats, which hold it to
// about 65504: what is brighter than NRD_LIMIT is handed to NRD at it (pt_nrd_prep), and the rest added back undenoised in
// the composite (pt_compose, PT-50: until 2026-10-07 it was cut).
static const float NRD_LIMIT = 250.0;
// The traced light's own exposure on top of HDRP's (params.x, from TGPT_Frame.tracedGain; HdrpPasses adapts it).
float traced_gain() { return g.params.x > 0.0 && g.params.x < 1e6 ? g.params.x : 1.0; }
// HDRP's height fog between two points (relative to the scene origin), as optical depth: its extinction (fog.x, per metre)
// below its base height (fog.y, in the world), falling off as e^-((h - base) / H) above it (fog.z: 1 / H). 0 without fog.
// Over a segment the height changes evenly with the distance: the part of it below the base is homogeneous, and above it
// the density's mean over the heights it spans.
float height_fog_depth(float3 a, float3 b) {
    const float k = g.fog.x, base = g.fog.y, rcpH = g.fog.z;
    if (!(k > 0.0) || !(k < 1e6) || !(rcpH >= 0.0)) return 0.0;
    const float len = length(b - a);
    const float lo = min(a.y, b.y) + g.cacheOrigin.y, hi = max(a.y, b.y) + g.cacheOrigin.y;
    const float span = hi - lo;
    if (span < 1e-3) return k * len * (lo < base ? 1.0 : exp(-(lo - base) * rcpH));
    const float below = saturate((base - lo) / span);
    const float from = max(lo, base), d = (hi - from) * rcpH;
    const float mean = exp(-(from - base) * rcpH) * (d > 1e-4 ? (1.0 - exp(-d)) / d : 1.0);
    return k * len * (below + (1.0 - below) * mean);
}
// HDRP dims every light by its height fog before the light reaches a surface: a directional light from the surface up through
// the fog along the light's direction (its LightEvaluation.hlsl, EvaluateLight_Directional: the fog's optical depth over an
// endless way, homogeneous below the fog's base height and thinning as e^-((h - base) / H) above it), a lamp over the way
// between the two (EvaluateLight_Punctual; height_fog_depth here). The game's lights are set for it: its day and night
// system keeps the fog's base at the hero's height, so by day at Horns of the South (a mean free path of 144 m) a fifth to
// two fifths of the sun reaches the ground, and at night (9 m) a few hundredths of the moon's 60,000 lux. Traced at full
// strength (until 2026-10-06), sunlit ground came out two to three times the game's, a rainy afternoon sunny, a torch-lit
// night moonlit like a day, and wet grass and rock glinted white.
float directional_fog(float3 position, float cosZenith) {
    const float k = g.fog.x, base = g.fog.y, rcpH = g.fog.z;
    if (!(k > 0.0) || !(k < 1e6) || !(rcpH > 0.0)) return 1.0;
    const float height = position.y + g.cacheOrigin.y;
    const float depth = max(base - height, 0.0) + exp(-max(height - base, 0.0) * rcpH) / rcpH;
    return exp(-k * depth / max(cosZenith, 0.001));
}
float sun_fog(float3 position) { return directional_fog(position, g.sunDir.y); }
// Highlights held (PT-73 e, the lantern): the gain's automatic part brightens a pixel by how dark it is. One gain for the
// whole picture (until 2026-10-08) lifted the wall beside a lantern as much as the dark room around it (x5.3 in a tavern),
// and the wall burned out to white. With L the pixel's traced light as the game's exposure and the player's Exposure show
// it, G the automatic gain there, b = G - 1 and x = L / W:  L' = W - (W - L) / (1 + b x (1 - x))  below W, and L' = L from
// W up. A dark pixel gets G (the slope at 0), a pixel at W is left as it is, and above W the picture is the game's own
// exposure, never darker: a flame and a lamp's glass stay the brightest things. It is "L G / (1 + L (G - 1) / W)", which
// is W - (W - L) / (1 + b x), with b x (1 - x) for b x: that one meets the line L' = L at W with a slope of 1 / G against
// the line's 1, a ring on a wall whose light falls off through W; this one arrives with the line's slope. The order of
// brightness is kept at every G (the slope, (1 + b (1 - x)^2) / (1 + b x (1 - x))^2, is never 0).
// W, HIGHLIGHT_KEPT, in the units the composite stores and HDRP's tone mapping reads (radiance times HDRP's exposure).
// 1.0 is the owner's choice, 2026-10-08 18:30, after a day at 2.0 (their choice by eye that morning, "hold 2, agreed", of
// the tavern's lantern room at 0.54, 1, 2, 4, 8 and none, the harness's recipe `lantern-hold`): in the tavern "some lights
// ... were still a bit overexposed/bloomy ... let's do hold 1 instead". (0.54, the first build's, is what the game's
// ACES tone mapping shows at 0.4 of the screen's white; 0.18 shows at 0.10, 1.0 at 0.62.) d3d11.hlsl's TGPT_FOG has its
// copy.
static const float HIGHLIGHT_KEPT = 1.0;
// The share of its gain a pixel's traced light keeps: lit is its luminance as stored with the whole gain (traced_gain),
// and the light is stored times this. 1 where the automatic part is 1 (daylight): the picture as it was, bit for bit.
// W this frame: what the frame brings (the Advanced setting "Highlights kept", for tuning by eye; 1e6: nothing held), or
// the constant above.
float highlight_kept() { return g.look.x > 0.0 && g.look.x < 1e30 ? g.look.x : HIGHLIGHT_KEPT; }
float highlight_hold(float lit, float gain) {
    const float player = g.fog.w > 0.0 && g.fog.w < 1e6 ? g.fog.w : 1.0;
    const float automatic = gain / player;
    if (!(automatic > 1.0)) return 1.0;
    const float b = automatic - 1.0, x = saturate(lit / (automatic * highlight_kept()));
    return (1.0 + b * (1.0 - x) * (1.0 - x) / (1.0 + b * x * (1.0 - x))) / automatic;
}
// And back: from a luminance as shown (with the gain and its hold) the one at HDRP's exposure alone, without the gain or
// the player's Exposure, for the meter (pt_meter): x from L' / W, the smaller root of (1 - y) b x^2 - ((1 - y) b + 1) x + y.
// ON TRIAL (PT-40, docs/agents/flame-near.md; never a row, to be taken out or made the rule once the game has answered):
// what is far past white shown as an eye would, bright with its shape, where the hold above only takes the automatic gain
// away. Above a knee K the pixel's traced light L (as stored: after the hold, in the units HDRP's tone mapping reads) rolls
// off towards a most M it never reaches:  L' = K + (L - K) / (1 + (L - K) / (M - K)).  At K it leaves with the line's own
// slope (no ring), the order of brightness is kept (the slope is never 0), and a pixel at or under K is multiplied by
// exactly 1: bit for bit as before. Diffuse and specular light are scaled alike (no change of hue); what a surface emits
// itself is outside it, as it is outside the hold: a flame stays the brightest thing. Two hidden settings of the plugin
// ("Highlight shoulder knee", "Highlight shoulder most"), packed into g.radiance.z (a whole number: the knee in 64ths in
// bits 0-11, 0: the hold's W; the most in 64ths in bits 12-23, 0: off; TGPT_Frame.frameReserved, tracer.cpp). x: K, y: M
// (0: off). d3d11.hlsl's TGPT_FOG has the way back as its copy.
float2 highlight_shoulder() {
    const uint packed = uint(max(g.radiance.z, 0.0) + 0.5);
    const float most = float(packed >> 12) / 64.0;
    const float knee = (packed & 0xFFFu) != 0u ? float(packed & 0xFFFu) / 64.0 : highlight_kept();
    return most > knee ? float2(knee, most) : float2(0.0, 0.0);
}
// The factor a pixel's traced light is stored times: lit is its luminance as stored after the hold.
float highlight_rolled(float lit) {
    const float2 k = highlight_shoulder();
    if (!(k.y > 0.0) || !(lit > k.x)) return 1.0;
    const float over = lit - k.x, room = k.y - k.x;
    return (k.x + over / (1.0 + over / room)) / lit;
}
// And back, for the meter (traced_ungained): the luminance the pixel had before the roll-off.
float highlight_unrolled(float shown) {
    const float2 k = highlight_shoulder();
    if (!(k.y > 0.0) || !(shown > k.x)) return shown;
    const float room = k.y - k.x, over = min(shown - k.x, room * 0.9999);
    return k.x + over / (1.0 - over / room);
}
float traced_ungained(float shown, float gain) {
    shown = highlight_unrolled(shown); // ON TRIAL: the shoulder first, as the composite applied it last
    const float player = g.fog.w > 0.0 && g.fog.w < 1e6 ? g.fog.w : 1.0;
    const float b = gain / player - 1.0, y = shown / highlight_kept();
    if (!(b > 0.0)) return shown / gain;
    if (!(y < 1.0)) return shown / player;
    const float a = (1.0 - y) * b;
    return 2.0 * shown / ((a + 1.0 + sqrt(max((a + 1.0) * (a + 1.0) - 4.0 * a * y, 0.0))) * player);
}
// The fog over a reflected or refracted ray's own way (PT-37, PT-46): the traced medium (pt_medium.hlsli traced_fog_on_way)
// while the fog is the tracer's (FLAG_TRACED_FOG: every frame of the traced picture since 2026-10-11); a debug view has
// none. Until 2026-10-11 HDRP's formula stood here (the sky's colour times a tint, the traced light's gain faded by HDRP's
// fog), with Fog Adaptation's gain for HDRP's own fog light: both went with HDRP's fog.
float3 traced_fog_on_way(float3 radiance, float3 from, float3 dir, float dist, inout Rng rng);
float3 fogged(float3 radiance, float3 from, float3 dir, float dist, inout Rng rng) {
    return (g.size.w & FLAG_TRACED_FOG) != 0u ? traced_fog_on_way(radiance, from, dir, dist, rng) : radiance;
}
// Whether every pixel's surface is what its own camera ray found (pt_primary), not HDRP's G-buffer.
// (The yardstick's view takes them for its frames whatever the route: it is the hit it shows.)
bool own_rays() { return ((g.settings2.y & OPTION_GBUFFER_SURFACES) == 0u || g.settings.y == VIEW_YARDSTICK) && g.primary.x != INVALID; }
// A pixel's motion to the previous frame (texture coordinates: this frame's less the previous one's): with the tracer's own
// camera rays its hit's own (pt_primary hit_motion), else HDRP's motion vector there. What the tracer's reuse across frames,
// its filters and NRD follow. (The upscaler follows HDRP's; the composite moves nothing: it shows the frame's own light.)
float2 motion_at(uint2 p) {
    if (own_rays()) { RWTexture2D<float4> own = ResourceDescriptorHeap[g.primary.x]; return own[p].xy; }
    Texture2D<float2> motion = ResourceDescriptorHeap[g.inputs1.y];
    return motion.Load(int3(p, 0));
}
// The view depth's change at pixel p, the previous frame's minus this one's (positive depths along the view): the third
// value of 2.5D motion (NRD, AMD Ray Regeneration). The tracer's own camera rays keep it beside the motion, from the hit's
// own previous position (pt_primary point_motion: a moving thing's too); without them, the point taken as still.
float motion_depth_change(uint2 p, float3 position) {
    if (own_rays()) { RWTexture2D<float4> own = ResourceDescriptorHeap[g.primary.x]; return own[p].z; }
    const float3 rel = position - g.cameraPos.xyz;
    return mul(g.prevViewProj, float4(rel, 1.0)).w - mul(g.viewProj, float4(rel, 1.0)).w;
}
// Where the point pixel p shows was in the PREVIOUS frame's own pixel grid (pixels; a pixel's centre is its index plus a
// half), given its motion (motion_at's, or a shown point's made by the same rule). Every frame's camera rays carry
// another sub-pixel shift (PT-84): a pixel's centre looks at the point +shift pixels from it in the picture without
// one, and the motion is HDRP's kind, between the two pictures WITHOUT their shifts. So the place is the pixel's centre
// less the motion plus this frame's shift less the previous one's (g.look's y and z, tracer.cpp). Everything that
// fetches the previous frame's data by a pixel's place goes through here (PT-85): left out, the fetch landed up to a
// render pixel beside the point in a still world, on another surface at every edge and thin thing, the history was
// refused there and the raw sample showed (the Direct layer's specks went from 6 to 90 a frame in the game).
float2 previous_place(uint2 p, float2 motion) {
    return float2(p) + 0.5 - motion * float2(g.size.xy) + g.look.yz;
}
float exposure() {
    Texture2D<float> e = ResourceDescriptorHeap[g.inputs1.z];
    float v = e.Load(int3(0, 0, 0));
    return (v > 0.0 && v < 1e20) ? v : 1.0;
}

// The angle one pixel spans at the screen's centre (a camera ray cone's spread), worked out once a thread.
static float g_pixelAngle = -1.0;
float pixel_angle() {
    if (g_pixelAngle < 0.0) {
        const uint2 c = g.size.xy / 2;
        g_pixelAngle = length(normalize(camera_relative_position(c + uint2(1, 0), 1.0)) - normalize(camera_relative_position(c, 1.0)));
        if (!(g_pixelAngle > 0.0 && g_pixelAngle < 1.0)) g_pixelAngle = 1e-3;
    }
    return g_pixelAngle;
}

// ---------------------------------------------------------------- scene
float4 atlas_sample(int tile, float2 uv) {
    Texture2D<float4> atlas = ResourceDescriptorHeap[g.scene1.z];
    // A detail tile (TGPT_RegisterDetailTexture: see-through glass, whose texture the camera sees from the atlas itself):
    // 512x512, filtered bilinearly within it (in linear color), wrapping as the texture repeats. Point-sampled from its
    // 64x64 tile, a jar's stains showed as blocks.
    if (uint(tile) >= MAX_TILES) {
        const uint d = uint(tile) - MAX_TILES;
        const uint2 base = uint2(d % DETAIL_TILES_PER_ROW, d / DETAIL_TILES_PER_ROW) * DETAIL_TILE_SIZE + uint2(0, ATLAS_SIZE);
        const float2 t = frac(uv) * DETAIL_TILE_SIZE - 0.5;
        const float2 f = frac(t);
        const int2 i0 = int2(floor(t));
        const uint2 a = uint2(i0) & (DETAIL_TILE_SIZE - 1u), b = uint2(i0 + 1) & (DETAIL_TILE_SIZE - 1u);
        const float4 c00 = atlas.Load(int3(base + uint2(a.x, a.y), 0)), c10 = atlas.Load(int3(base + uint2(b.x, a.y), 0));
        const float4 c01 = atlas.Load(int3(base + uint2(a.x, b.y), 0)), c11 = atlas.Load(int3(base + uint2(b.x, b.y), 0));
        const float4 l00 = float4(srgb_to_linear(c00.rgb), c00.a), l10 = float4(srgb_to_linear(c10.rgb), c10.a);
        const float4 l01 = float4(srgb_to_linear(c01.rgb), c01.a), l11 = float4(srgb_to_linear(c11.rgb), c11.a);
        return lerp(lerp(l00, l10, f.x), lerp(l01, l11, f.x), f.y);
    }
    uint2 origin = uint2(uint(tile) % TILES_PER_ROW, uint(tile) / TILES_PER_ROW) * TILE_SIZE;
    float2 f = frac(uv);
    uint2 texel = min(uint2(f * TILE_SIZE), TILE_SIZE - 1);
    float4 c = atlas.Load(int3(origin + texel, 0));
    return float4(srgb_to_linear(c.rgb), c.a);
}
// An alpha-tested material's alpha at 256x256, filtered bilinearly within its tile (wrapping, as the texture repeats).
// Point-sampled, a fringe's or leaf's edge stepped from texel to texel as the shading points moved with the camera's
// jitter, and the shadows of rug fringes flickered. The micromap bake reads the same 2x2 footprint (pt_omm_bake).
float alpha_sample(int tile, float2 uv) {
    Texture2D<float> alphas = ResourceDescriptorHeap[g.extra.x];
    uint2 origin = uint2(uint(tile) % ALPHA_TILES_PER_ROW, uint(tile) / ALPHA_TILES_PER_ROW) * ALPHA_TILE_SIZE;
    float2 t = frac(uv) * ALPHA_TILE_SIZE - 0.5;
    float2 f = frac(t);
    int2 i0 = int2(floor(t));
    uint2 a = uint2(i0) & (ALPHA_TILE_SIZE - 1u), b = uint2(i0 + 1) & (ALPHA_TILE_SIZE - 1u);
    float a00 = alphas.Load(int3(origin + uint2(a.x, a.y), 0)), a10 = alphas.Load(int3(origin + uint2(b.x, a.y), 0));
    float a01 = alphas.Load(int3(origin + uint2(a.x, b.y), 0)), a11 = alphas.Load(int3(origin + uint2(b.x, b.y), 0));
    return lerp(lerp(a00, a10, f.x), lerp(a01, a11, f.x), f.y);
}
struct Hit {
    float3 waterBase, waterPrevious;
    uint water; // 0: ordinary mesh; slot + 1: displaced dielectric boundary
    float3 position;
    float3 rayDirection, rayOrigin; // the actual incoming ray, also used by decal parallax
    float3 normal;     // geometric, world, towards the ray's origin
    float3 rounded;    // the mesh's own smooth normal there (its vertices', TGPT_MeshDesc), on normal's side; normal where it has none
    float2 uv;
    float4 color;      // vertex color, white without
    uint material;
    uint instance;     // the TLAS instance (a TGPT_SetScene index, then characters)
    uint geometry;     // the submesh
    uint primitive;    // the triangle in it
    bool front;        // its front face met (the side its triangles face in the mesh's own space, as DXR's front face)
    // PT-46: the triangle's directions of its texture coordinates in the world (dP/du, dP/dv: the tangent frame normal maps
    // are read in), the ray cone's width there (metres), and that footprint in the mesh's texture coordinates along its two
    // axes (stretched along the ray's slant, and across it): what a pixel shader's derivatives are to the game's samplerIndex.
    float3 dpdu; float3 dpdv;
    float footprint;
    float2 uvGradX; float2 uvGradY;
    // PT-64, for the game's shaders that lay textures over the world or the object (pt_materials.hlsli): the footprint's two
    // axes in the world (metres), the instance's transform (object to the world, relative to the scene origin), and the mesh's
    // fourth set of texture coordinates with the footprint in it (0 without one).
    float3 footX; float3 footY;
    float3x4 toWorld;
    float2 uv2; float2 uv2GradX; float2 uv2GradY;
};
// ---------------------------------------------------------------- the game's own textures at a hit (PT-46)
// Opened by the tracer with no copy (texture_share.cpp) and read as the game's own samplerIndex reads them: anisotropic, over the
// ray's footprint (Hit.uvGradX, uvGradY) by the material's tiling, and by the upscaler's mip bias (g.hair.w) as the game
// renders at its lower resolution. A material whose texture has no number (made before the hook, refused, 64x64 or less,
// gone) keeps its atlas tile, as before. Shared cutouts use the original alpha and cutoff; atlas micromaps must not
// pre-classify them (scene.cpp omm_matches). The camera's jitter samples pixel coverage, not a blurred coverage copy.
bool shared_on() { return g.textures.x != INVALID && (g.settings2.y & OPTION_ATLAS_TEXTURES) == 0; }
bool has_shared(MaterialRecord m, int number) { return (m.flags & MATERIAL_SHARED) != 0 && number >= 0 && shared_on(); }
bool shared_cutout(MaterialRecord m) {
    return (m.flags & (MATERIAL_STRANDS | MATERIAL_HAIR_LAYER)) == 0 && has_shared(m, m.colorTexture);
}
float4 shared_sample(int number, float2 st, float2 gx, float2 gy) {
    Texture2D<float4> t = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + uint(number))];
    return t.SampleGrad(s_anisoWrap, st, gx, gy);
}
float4 shared_sample_clamped(uint number, float2 st, float2 gx, float2 gy) {
    Texture2D<float4> t = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + number)];
    return t.SampleGrad(s_anisoClamp, st, gx, gy);
}
void material_gradients(Hit h, MaterialRecord m, out float2 gx, out float2 gy) {
    const float bias = asfloat(g.hair.w);
    const float s = bias < 0.0 && bias > -8.0 ? exp2(bias) : 1.0;
    gx = h.uvGradX * m.uvST.xy * s; gy = h.uvGradY * m.uvST.xy * s;
}
// Hair's and eyes' own maps (tracer.cpp bake_hair, bake_eyes, bake_hair_lobes; scene.hpp set_map_record): per detail tile
// MAP_DETAIL_ROWS uint4 rows (its maps' numbers, -1 none; then the blend map's, the kind, 1 hair or 2 an eye, and which maps
// it has; then the game shader's values, as the bake's constant buffer has them), then per 64x64 tile MAP_LOBE_ROWS (hair's
// strands: the flow, mask and specular maps' numbers and kind 1; their smoothness power, the flow's green flip, which maps
// it has). A record whose maps are not all open reads as none: the tile serves.
static const uint MAP_DETAIL_ROWS = 14, MAP_LOBE_ROWS = 2, MAP_LOBE_BASE = DETAIL_TILES * MAP_DETAIL_ROWS;
uint4 map_row(uint row) { StructuredBuffer<uint4> rows = ResourceDescriptorHeap[g.textures.y]; return rows[row]; }
bool map_records_on() { return g.textures.y != INVALID && shared_on(); }
// Whether every map a record says it has (bits of present) has a number.
bool maps_open(uint4 numbers, uint fifth, uint present) {
    const uint open = (int(numbers.x) >= 0 ? 1u : 0u) | (int(numbers.y) >= 0 ? 2u : 0u) | (int(numbers.z) >= 0 ? 4u : 0u) |
                      (int(numbers.w) >= 0 ? 8u : 0u) | (int(fifth) >= 0 ? 16u : 0u);
    return (present & ~open) == 0u;
}
float4 map_read(uint number, float2 uv, float2 gx, float2 gy) { return int(number) >= 0 ? shared_sample(int(number), uv, gx, gy) : 1.0; }
// A highlight band of RealHair's: rising from d.x to its peak at d.y, falling to d.z (smoothsteps, as d3d11.hlsl's band).
float hair_band(float id, float4 d) {
    const float rise = saturate((id - d.x) / max(d.y - d.x, 1e-5)), fall = saturate((id - d.y) / max(d.z - d.y, 1e-5));
    return id >= d.y ? 1.0 - fall * fall * (3.0 - 2.0 * fall) : rise * rise * (3.0 - 2.0 * rise);
}
// RealHair's color at the mesh's own coordinates uv, from its maps as the game's shader computes it (d3d11.hlsl TGPT_HAIR's
// maths for one texel): the diffuse map at its tiling toward the root-to-end gradient, the highlight bands, the strength,
// the blend map and the mask map's occlusion, times its color; alpha the diffuse map's.
float4 hair_color(uint row, uint4 r0, uint4 r1, float2 uv, float2 gx, float2 gy) {
    float4 v[12];
    [unroll] for (uint i = 0; i < 12; ++i) v[i] = asfloat(map_row(row + 2 + i));
    const float4 diffuse = map_read(r0.x, uv * v[0].xy + v[0].zw, gx * v[0].xy, gy * v[0].xy);
    const float root = map_read(r0.y, uv, gx, gy).g, id = map_read(r0.z, uv, gx, gy).g;
    const float along = lerp(root, 1.0 - root, v[9].x);
    float3 c = lerp(1.0, diffuse.rgb, v[8].x);
    c = lerp(c, lerp(v[2].rgb, v[3].rgb, along), lerp(v[8].z, v[8].w, along) * v[8].y);
    const float a = saturate(hair_band(id, v[6]) * v[9].z * (1.0 - v[6].w * lerp(root, 1.0 - root, v[10].x)) * v[9].y);
    c = lerp(c, v[4].rgb, a);
    const float b = saturate(hair_band(id, v[7]) * v[9].w * (1.0 - v[7].w * lerp(root, 1.0 - root, v[10].y)) * v[9].y);
    c = lerp(c, v[5].rgb, b);
    c *= v[10].z;
    c = lerp(c, c * map_read(r1.x, uv, gx, gy).g, v[10].w);
    c = lerp(c, c * lerp(1.0, map_read(r0.w, uv, gx, gy).g, v[11].x), v[11].y);
    return float4(saturate(c * v[1].rgb), saturate(diffuse.a));
}
// An eye's color at the mesh's own coordinates uv, from its sclera and iris maps as the game's eye shader computes it
// (d3d11.hlsl TGPT_EYE's maths for one texel, clamped as its one samplerIndex is).
float4 eye_color(uint row, uint4 r0, uint4 r1, float2 uv, float2 gx, float2 gy) {
    const float4 scleraTint = asfloat(map_row(row + 2)), irisTint = asfloat(map_row(row + 3));
    const float4 params = asfloat(map_row(row + 4)), params2 = asfloat(map_row(row + 5));
    const float2 d = uv - 0.5;
    const float3 sclera = (int(r0.x) >= 0 ? shared_sample_clamped(r0.x, d * params.x + 0.5, gx * params.x, gy * params.x).rgb : 1.0) * scleraTint.rgb;
    const float w = 1.0 - min(8.0 * length(d), 1.0);
    const float2 irisUv = lerp(d * params.y + 0.5, d * (params.y + 8.0 * params.z - 6.0) + 0.5, w);
    const float irisScale = lerp(params.y, params.y + 8.0 * params.z - 6.0, w);
    const float4 iris = int(r0.y) >= 0 ? shared_sample_clamped(r0.y, irisUv, gx * irisScale, gy * irisScale) : float4(1.0, 1.0, 1.0, 0.0);
    const float3 tint = lerp(irisTint.rgb, float3(0.093520, 0.118404, 0.169881), saturate((params2.y - 1.0) / 9.0));
    const float3 b = lerp(sclera, iris.rgb * tint, iris.a);
    const float r = length(2.0 * (irisUv - 0.5));
    const float ring = r > 1.0 - params.w ? pow(0.1 + 0.9 * max(1.0 - r, 0.0) / max(params.w, 1e-4), params2.x) : 1.0;
    const float3 over = select(b <= 0.5, 2.0 * b * ring, 1.0 - 2.0 * (1.0 - b) * (1.0 - ring));
    return float4(saturate(lerp(b, over, iris.a)), 1.0);
}
// A material's color map at st (its tiling applied), linear: the game's own, else (hair, an eye) the color the game's
// shader makes of its own maps, else its atlas tile, else white. Hair, brows, lashes and eyes never take their color map
// alone: RealHair tints it and the eye shader mixes sclera and iris (Ron's red hair came out pale blond from the map alone,
// run 20261007-024109), so theirs is the shader's color or their baked tile.
float4 material_texture(MaterialRecord m, float2 st, float2 gx, float2 gy) {
    const bool ownShader = (m.flags & (MATERIAL_STRANDS | MATERIAL_HAIR_LAYER | MATERIAL_EYE)) != 0;
    if (!ownShader && has_shared(m, m.colorTexture)) return shared_sample(m.colorTexture, st, gx, gy);
    if (m.albedoTile >= int(MAX_TILES) && m.albedoTile < int(MAX_TILES + DETAIL_TILES) && map_records_on()) {
        const uint row = (uint(m.albedoTile) - MAX_TILES) * MAP_DETAIL_ROWS;
        const uint4 r0 = map_row(row), r1 = map_row(row + 1);
        if (r1.y == 1u && maps_open(r0, r1.x, r1.z)) return hair_color(row, r0, r1, st, gx, gy);
        if (r1.y == 2u && maps_open(r0, r1.x, r1.z)) return eye_color(row, r0, r1, st, gx, gy);
    }
    return m.albedoTile >= 0 ? atlas_sample(m.albedoTile, st) : 1.0;
}
float4 material_texture(Hit h, MaterialRecord m) {
    float2 gx, gy; material_gradients(h, m, gx, gy);
    return material_texture(m, h.uv * m.uvST.xy + m.uvST.zw, gx, gy);
}
// The shading normal at a hit with the game's own normal map: HDRP's, Unity's packing either way (red and green, or alpha
// and green: UnpackNormalmapRGorAG) times the material's strength, in the frame of the mesh's smooth normal and the
// triangle's texture directions (dP/du, its bitangent along dP/dv: a mirrored island flips it), kept on the side the ray
// came from. n as given without one.
// The map's z is the unscaled bump's (as Unity's UnpackNormalAG has it), and its x and y are then scaled: scaled first, a
// strength of 2 flattened every bump steeper than 30 degrees. The game's graphs scale with Shader Graph's Normal Strength
// (TEXTURE_GRAPH_NORMAL: z toward 1 under a strength of 1). The tangent is the mesh's own direction of u whatever the
// material's tiling (the game's shaders take the mesh's tangent; a negative tiling mirrors the map, not the frame: until
// PT-64 the frame was turned with it, which bent Mat_Furniture_Wood_01's bumps, tiling -1.64, the other way from the
// game's). A back face of HDRP's Double-Sided "Flip" mode turns the bumps around with the surface (TEXTURE_FLIP_BACK).
float3 material_tangent_normal(Hit h, MaterialRecord m, float2 st, float2 gx, float2 gy) {
    float4 p = shared_sample(m.normalTexture, st, gx, gy);
    p.a *= p.r;
    const float2 xy = p.ag * 2.0 - 1.0;
    const float z = max(1e-16, sqrt(1.0 - saturate(dot(xy, xy))));
    float3 tn = float3(xy * m.normalScale, (m.textureFlags & TEXTURE_GRAPH_NORMAL) != 0u ? lerp(1.0, z, saturate(m.normalScale)) : z);
    if ((m.textureFlags & TEXTURE_FLIP_BACK) != 0u && !h.front) tn.xy = -tn.xy;
    return tn;
}
float3 hit_frame_normal(Hit h, float3 tn, float3 n);
float3 material_normal(Hit h, MaterialRecord m, float2 st, float2 gx, float2 gy, float3 n) {
    if (!has_shared(m, m.normalTexture)) return n;
    return hit_frame_normal(h, material_tangent_normal(h, m, st, gx, gy), n);
}
uint3 triangle_indices(MeshRecord mesh, uint geometry, uint primitive) {
    StructuredBuffer<GeometryRecord> geometries = ResourceDescriptorHeap[g.scene0.z];
    GeometryRecord geo = geometries[mesh.geometryBase + geometry];
    ByteAddressBuffer ib = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.indices)];
    return ib.Load3((geo.indexStart + primitive * 3) * 4);
}
float2 interpolate_uv(MeshRecord mesh, uint3 idx, float2 bary) {
    if (mesh.uvs == INVALID) return 0.0;
    ByteAddressBuffer ub = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.uvs)];
    float2 t0 = asfloat(ub.Load2(idx.x * 8)), t1 = asfloat(ub.Load2(idx.y * 8)), t2 = asfloat(ub.Load2(idx.z * 8));
    return t0 * (1.0 - bary.x - bary.y) + t1 * bary.x + t2 * bary.y;
}
// ON TRIAL (PT-13, PT-35; docs/agents/glass-3.md; never a row, to be taken out or made the rule once the game has answered):
// why the game's jars are cloudy with dark blotches. The plugin's hidden "Glass trial" (TGPT_Frame.cameraPosition's w, handed
// on in the frame flags' bits 8-15; 0: everything as before), a bit each:
//   1, GLASS_TRIAL_CLEAR: what goes THROUGH see-through glass crosses the mesh's own smooth face, where the game's shader
//      shows what is behind neither bent by the material's bumps nor blurred by its roughness (HDRP Lit with no refraction
//      model: glass_clear). Its reflection keeps the material's roughness and normal map, as the game's highlight has them.
//      Until then each of a jar's four faces bent the view at a microfacet of the material's roughness (the Crypt's jars:
//      0.36 to 0.40, a spread of centimetres on the wall behind) about a normal-mapped normal of its own: frosted, embossed glass.
//   2, GLASS_TRIAL_DRAWN: its paint (glass_cover) only on the faces the game draws: the front faces of a material HDRP draws
//      with its back faces culled. Until then a ray met the paint at the back of each wall as well: a jar's film twice.
//   4, GLASS_TRIAL_PIECES: glass has an inside where its PIECE of the mesh closes a volume (piece_solid), not only where its
//      whole submesh does. The Crypt's jar is one submesh: a closed body (764 triangles) and a lid that is open (325, with 29
//      edges that lack their twin), so until then the whole jar was open sheets: each of a wall's two faces a pane of its own
//      (a pane's two reflections at each: four a wall), nothing bent, no rim.
//   8, GLASS_TRIAL_BOUNDARY (PT-13, docs/agents/glass-4.md): with 1, a closed wall's transmission crosses its actual
//      triangle plane. The jar's inner and outer shading normals differ even where its walls are nearly parallel;
//      bending by those unrelated normals turns a 5 mm wall into a lens. Reflections keep the material's finish.
static const uint GLASS_TRIAL_CLEAR = 1u, GLASS_TRIAL_DRAWN = 2u, GLASS_TRIAL_PIECES = 4u, GLASS_TRIAL_BOUNDARY = 8u;
uint glass_trial() { return (g.size.w >> 8) & 0xFFu; }
// MaterialRecord.opticalReserved's x, bit 0 (the plugin's GlassFinish.ReadOptics): the game's shader refracts what is behind
// this glass by its own index, roughness and normal (HDRP Lit with a refraction model on), so the tracer does too.
static const uint OPTICAL_REFRACTS = 1u;
bool glass_clear(MaterialRecord m) {
    return (glass_trial() & GLASS_TRIAL_CLEAR) != 0u && (m.flags & MATERIAL_SEE_THROUGH) != 0 && (m.opticalReserved.x & OPTICAL_REFRACTS) == 0u;
}
// Whether a face of see-through glass carries the material's paint: every face, or (ON TRIAL) only one the game draws.
bool glass_painted(MaterialRecord m, bool front) {
    return (glass_trial() & GLASS_TRIAL_DRAWN) == 0u || front || (m.flags & MATERIAL_DOUBLE_SIDED) != 0;
}
// How much of a light's ray the paint on glass leaves where the ray crosses it. Glass HDRP draws as a transparent surface
// lets the ray through what its alpha leaves uncovered, untinted: where it covers there is paint, a label or a lid, a lit
// surface that stops the light (see_through_cover, glass_cover). Until 2026-10-08 the covered share let its color through
// instead: a jar's wooden lid cast the shadow of brown glass. Glass's own tint is its material's absorption (glass_walk,
// PT-76), never its base color: until 2026-10-09 glass without the see-through mark (which only the offline test sends)
// filtered a light's ray by its base color at each crossing, a stand-in for a tint that no other ray took.
float glass_open(InstanceRecord inst, MaterialRecord m, uint geometry, uint primitive, float2 bary) {
    if ((m.flags & MATERIAL_SEE_THROUGH) == 0) return 1.0;
    float a = m.baseColor.a;
    if (m.albedoTile >= 0) {
        StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
        MeshRecord glassMesh = meshes[inst.mesh];
        float2 guv = interpolate_uv(glassMesh, triangle_indices(glassMesh, geometry, primitive), bary);
        a *= atlas_sample(m.albedoTile, guv * m.uvST.xy + m.uvST.zw).a;
    }
    return 1.0 - saturate(a);
}
// See-through glass where a ray met it, from its texture at the hit's own coordinates: its color (rgb) and how much of it
// that color covers (a, the game's alpha). HDRP blends it over what lies behind as a surface lit there over that share,
// the rest letting through what is behind, untinted (PT-35): the game's jars are 17 % covered by a pale green, their lids
// whole by brown wood in the same texture. Taken as a filter (what lies behind times the color where it covers), the
// jars lost their green and the lids were brown glass.
float4 see_through_cover(Hit hit) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    const MaterialRecord m = materials[hit.material];
    float3 c = m.baseColor.rgb;
    float a = m.baseColor.a;
    if (m.albedoTile >= 0 || has_shared(m, m.colorTexture)) { const float4 t = material_texture(hit, m); c *= t.rgb; a *= t.a; }
    return float4(saturate(c), saturate(a));
}
// How much of the glass a ray met is that lit surface (0: none of it, clear glass; glass the game does not draw as a
// transparent surface has none). A ray meets the paint with this chance, a new draw for each ray, and the glass's own
// interface otherwise (pt_dielectric dielectric_follow, pt_primary write_traced): a pixel a third covered is the label in a
// third of the frames. PT-35's rule, which the glass migration of 2026-10-07 had dropped (every texel clear glass: the
// alchemist's jars without their green, their lids and their labels).
static const uint GLASS_PAINT_SALT = 0x7061696Eu;
float glass_cover(Hit hit) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    if (!glass_painted(materials[hit.material], hit.front)) return 0.0; // ON TRIAL: a face the game does not draw has none
    return (materials[hit.material].flags & MATERIAL_SEE_THROUGH) != 0 ? see_through_cover(hit).a : 0.0;
}
// Whether the glass a ray met has an inside there: its submesh's surface closes a volume (GEOMETRY_CLOSED, scene.cpp
// mark_closed: every edge shared by two triangles). A ray that enters such glass meets its far side and leaves through it.
// An open sheet of glass (a pane of one quad, a shard, a cornea's shell, a cup drawn as one skin) has no inside: it is a
// thin pane, which reflects as glass's two faces do and lets the rest straight through (pt_dielectric dielectric_follow),
// and no point is inside it (optical_medium). Taken for a solid (until 2026-10-08), what lay behind an open sheet was
// seen from inside glass the ray never left: bent once, and dimmed to 1 / index^2 of its light.
bool glass_solid(uint instanceId, uint geometry) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    StructuredBuffer<GeometryRecord> geometries = ResourceDescriptorHeap[g.scene0.z];
    return (geometries[meshes[instances[instanceId].mesh].geometryBase + geometry].flags & GEOMETRY_CLOSED) != 0u;
}
// Whether the triangle a ray met is of a piece of its mesh that closes a volume (scene.cpp mark_closed: the triangles
// joined through shared edges, every edge with its one twin; each submesh's such triangles come first, and its flags say
// how many they are). A candle's wax body is one, with its wick's card in the same submesh (which is not): the body has an
// inside, the card is a sheet. Light walks inside a material drawn from both sides only there (subsurface_hit).
bool piece_solid(uint instanceId, uint geometry, uint primitive) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    StructuredBuffer<GeometryRecord> geometries = ResourceDescriptorHeap[g.scene0.z];
    return primitive < (geometries[meshes[instances[instanceId].mesh].geometryBase + geometry].flags >> GEOMETRY_SOLID_SHIFT);
}
// Whether the glass a ray met at a triangle has an inside there: its submesh's whole surface closes a volume, or (ON TRIAL,
// GLASS_TRIAL_PIECES) the triangle's own piece of it does: a jar's body with its open lid in the same submesh.
bool glass_solid(uint instanceId, uint geometry, uint primitive) {
    return (glass_trial() & GLASS_TRIAL_PIECES) != 0u ? piece_solid(instanceId, geometry, primitive) : glass_solid(instanceId, geometry);
}
// A ray's own chance at a triangle, for what stops it by chance (u uniform in [0, 1)): the same whenever the ray meets that
// triangle again (traversal may report it more than once), independent between triangles, of every submesh (a primitive's
// index is its submesh's own: without the submesh, two layers of hair with the same indices let a ray through both or neither,
// and a pair of half-covered layers cast the shadow of one), and between kinds of decision (salt).
float ray_chance(uint instanceId, uint geometry, uint primitive, uint salt) {
    return float(pcg(g_raySeed ^ pcg(primitive * 0x9E3779B9u + pcg(instanceId + geometry * 0x85EBCA6Bu) + salt)) >> 8) * (1.0 / 16777216.0);
}
// Whether thin cloth's texel covers where a ray other than the camera's meets it, as alpha_opaque decides for its texture (a its
// alpha there, the alpha tile's coverage where it has one): by chance as much as the tile covers, or by its cutoff.
bool cloth_covers(MaterialRecord m, uint instanceId, uint geometry, uint primitive, float a) {
    if (shared_cutout(m)) return a >= m.alphaCutoff;
    return (m.flags & MATERIAL_ALPHA_TILE) != 0 ? ray_chance(instanceId, geometry, primitive, 0x616C7068u) < a : a >= m.alphaCutoff;
}
// The color thin cloth tints the light through it with: its own (base color by texture) at full brightness, its hue alone; how
// much passes is CLOTH_TRANSMISSION's chance (alpha_opaque).
float3 cloth_tint(InstanceRecord inst, MaterialRecord m, uint geometry, uint primitive, float2 bary) {
    float3 c = m.baseColor.rgb;
    if (m.albedoTile >= 0) {
        StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
        const MeshRecord mesh = meshes[inst.mesh];
        c *= atlas_sample(m.albedoTile, interpolate_uv(mesh, triangle_indices(mesh, geometry, primitive), bary) * m.uvST.xy + m.uvST.zw).rgb;
    }
    const float top = max(c.r, max(c.g, c.b));
    return top > 1e-3 ? saturate(c / top) : float3(1.0, 1.0, 1.0);
}
// A material's alpha where a ray meets it, as alpha_opaque reads it: the alpha tile's coverage, or the base color's alpha by its
// texture's.
float material_alpha(InstanceRecord inst, MaterialRecord m, uint geometry, uint primitive, float2 bary) {
    float a = m.baseColor.a;
    if (m.albedoTile >= 0 || shared_cutout(m)) {
        StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
        const MeshRecord mesh = meshes[inst.mesh];
        const float2 st = interpolate_uv(mesh, triangle_indices(mesh, geometry, primitive), bary) * m.uvST.xy + m.uvST.zw;
        // A geometric ray tests the original cutout at its hit, at the finest resident mip. Independent camera/light
        // rays provide antialiasing and partial shadows. Filtering a 256px coverage tile here erased near grass blades.
        if (shared_cutout(m)) a *= shared_sample(m.colorTexture, st, 0.0, 0.0).a;
        else a = (m.flags & MATERIAL_ALPHA_TILE) ? alpha_sample(m.alphaTile, st) : a * atlas_sample(m.albedoTile, st).a;
    }
    return a;
}
// ON TRIAL (PT-93, docs/agents/grass-boil.md; never a row, to be taken out or made the rule once the game has answered):
// grass lit by a fire on the ground flickers in every frame. A light's shadow ray flips a coin at every blade of grass
// and every leaf it meets (alpha_opaque, TRANSMISSION: one half), and a ray that runs level through a tuft meets many: with
// n blades in its way it arrives in one frame of 2^n, carrying all of the light. The mean is the shadow the owner chose
// (2026-10-06, "soft and partial"); a single frame is black or 2^n times too bright, and no history of 16 frames holds that.
// With the plugin's hidden setting "Grass shadow trial" at 1 (bit 22 of g.wetGrid.w, above the lamp trial's 22 bits; 0: off,
// every ray as before) the final shading's shadow rays (transmittance, transmittance_from_surface) take the product of
// what each blade lets through: the coin's own mean, so the picture converges to the same thing, and no coin. ReSTIR's reuse
// (pt_rtxdi rtxdi_visible, which needs a yes or a no) keeps the coin.
bool leaf_product_on() { return ((uint(max(g.wetGrid.w, 0.0) + 0.5) >> 22) & 1u) != 0u; }
// g_leafProduct: set while such a ray is traced with the trial on. g_leafMet: the blades it met (at most: traversal may
// report a triangle twice, so the product is taken by leaf_walk, which meets each crossing once). g_leafThrough: the last
// such ray's product (1 with the trial off), for the light that takes a bent way in the straight ray's place
// (caustic_through): the coin stopped the straight ray, and that light with it, as often.
static bool g_leafProduct = false;
static uint g_leafMet = 0;
static float g_leafThrough = 1.0;
// Alpha test of a candidate triangle: false means the ray passes through.
bool alpha_opaque(uint instanceId, uint geometry, uint primitive, float2 bary) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    InstanceRecord inst = instances[instanceId];
    MaterialRecord m = materials[materialIndices[inst.materialBase + geometry]];
    if ((m.flags & MATERIAL_ALPHA_TEST) == 0) return true;
    // Legacy camera-only hair shells: other rays pass, except mirror rays through non-layer shells.
    if ((m.flags & MATERIAL_CAMERA_ONLY) != 0 && !g_cameraRays && !(g_mirrorRay && (m.flags & MATERIAL_HAIR_LAYER) == 0)) return false;
    // Hair the tracer's own camera rays find (pt_primary primary_walk, which stops at it by its strands' cover and never asks
    // here): the other camera rays, looking for the surface HDRP drew, pass it.
    if ((m.flags & MATERIAL_HAIR_LAYER) != 0 && g_cameraRays) return false;
    // A legacy G-buffer lookup stops at hair wherever it meets it: the pixel is one HDRP drew
    // forward, at the depth the ray looks around, and HDRP's own alpha test decided what covers it. By the tracer's, a
    // camera ray slipped between strands HDRP drew there to the scalp behind, and the traced skin took the hair's place
    // from frame to frame (far hair flickering grey and dark). Other rays keep the strands' own alpha.
    // (The tracer's own first hit, g_primaryRays, takes hair as every other ray does, below: by its strands' cover.)
    if ((m.flags & MATERIAL_STRANDS) != 0 && g_cameraRays && !(g_primaryRays && (m.flags & MATERIAL_STRANDS) != 0))
        return (m.flags & MATERIAL_STRANDS) == 0 || g_candidateT >= g_strandsFrom;
    // Shadow connections retain the caustic solver's tint walk. Every radiance ray
    // meets the actual interface; dielectric_follow samples its reflection/refraction.
    if ((m.flags & MATERIAL_GLASS) != 0) {
        if (g_glassTint) { ++g_glassMet; return false; }
        // ReSTIR's conservative connection must not discard a light merely because
        // a dielectric lies in front. Final shading evaluates the connection's tint.
        if (g_lightRay && !g_glassSolid) return false;
        return true;
    }
    // A leaf or a blade of grass (TGPT_MATERIAL_TRANSLUCENT) in a light's way (g_lightRay) lets the light through as often as
    // the tracer lights such a blade through from behind (TRANSMISSION, half): one blade's shadow is half one, softer still
    // where the blade covers part of a texel, and it deepens with every further blade in the way. Stopped whole, every traced
    // tuft threw a hard, black shadow on the ground and on the grass behind it (the game's grass casts none at all: its
    // vegetation settings turn grass's shadows off. The owner, 2026-10-06: "We want grass shadows, but not those horrible
    // looking ones"). A tree's crown, many leaves deep, still shades as a whole. Its micromaps leave its covered texels to
    // this test (pt_omm_bake).
    // ON TRIAL (PT-93, g_leafProduct): no coin. The blade is counted and passed, and the ray's light is the product of what
    // each blade in its way lets through (leaf_pass, leaf_walk): the coin's own mean, the same in every frame.
    if (g_leafProduct && g_lightRay && !g_cameraRays && (m.flags & (MATERIAL_TRANSLUCENT | MATERIAL_THIN_CLOTH | MATERIAL_STRANDS)) == MATERIAL_TRANSLUCENT) { ++g_leafMet; return false; }
    if ((m.flags & MATERIAL_TRANSLUCENT) != 0 && g_lightRay && !g_cameraRays && ray_chance(instanceId, geometry, primitive, 0x6C656166u) < TRANSMISSION) return false;
    // Without a texture the base color's alpha decides alone (a cutoff above 1 makes a submesh invisible to rays). An
    // alpha tile holds coverage for the material's cutoff (its base alpha included), which alphaCutoff is compared with.
    float a = material_alpha(inst, m, geometry, primitive, bary);
    const bool originalCutout = shared_cutout(m);
    // Thin cloth (TGPT_MATERIAL_THIN_CLOTH: a canopy, a banner, laundry) in a light's way lets the light through its weave as
    // often as the tracer lights such cloth through from behind (CLOTH_TRANSMISSION), tinted by its color (cloth_tint, which
    // glass_walk applies: the crossing counts as glass's do); where its texture is bare (a hole) the light passes as before.
    // As a full blocker a holed red canopy over a market's candle corner left it black (32 of 255 against the game's 92, PT-39).
    // The sky's and bounced light pass it the same way (g_bounceRay: the rays that gather light for a diffuse surface), tinted
    // by cloth_walk: under an open sky with no sun on it, a canopy was a black lid over everything beneath it, where the
    // game's corner under it is evenly lit. Not a character's clothes (8: INSTANCE_CHARACTER, a material named like cloth on
    // a body): a tunic is no canopy, and the body under it is often not drawn at all.
    if ((m.flags & MATERIAL_THIN_CLOTH) != 0 && (g_lightRay || g_bounceRay) && !g_cameraRays && (inst.flags & 8u) == 0) {
        if (!cloth_covers(m, instanceId, geometry, primitive, a)) return false;
        if (ray_chance(instanceId, geometry, primitive, 0x636C6F74u) >= CLOTH_TRANSMISSION) return true;
        if (g_glassTint) ++g_glassMet;
        if (g_bounceRay) ++g_clothMet;
        return false;
    }
    // Hair stops a ray by chance, as much of the texel as its strands cover (a camera ray stopped above: HDRP shades hair),
    // so a layer of strands lets part of the light through between them. Cut out where a fifth of a texel was covered,
    // every card of hair cast a full, hard-edged shadow, as dark as a solid sheet.
    if ((m.flags & MATERIAL_STRANDS) != 0) return ray_chance(instanceId, geometry, primitive, 0x68616972u) < a;
    // Every alpha tile's coverage stops the rays other than the camera's by chance too, as much of the texel as is covered:
    // what is finer than the tile's texels (straw on a floor, a grass blade's tip, a chain's links, a fringe) lets the light
    // between its parts through. Cut out where half a texel was covered, the straw lying on a tavern's floor was a solid mat
    // to rays, and its shadow blacked out the floor seen between the strands. A camera ray keeps HDRP's cutoff: the surface
    // it finds must be the one HDRP drew.
    // The tracer's own first hit too (g_primaryRays): a leaf's or a fence's edge covers its share of the pixel, frame by frame.
    if (!originalCutout && (m.flags & MATERIAL_ALPHA_TILE) != 0 && (!g_cameraRays || g_primaryRays)) return ray_chance(instanceId, geometry, primitive, 0x616C7068u) < a;
    return a >= (!originalCutout && (m.flags & MATERIAL_ALPHA_TILE) != 0 ? 0.5 : m.alphaCutoff);
}
// ON TRIAL (PT-93, leaf_product_on): what a blade of grass or a leaf lets through of a light's ray where the ray crosses it:
// the chance alpha_opaque's two coins let such a ray pass. Where the blade covers, TRANSMISSION; where its texel is bare, all
// of it (by its alpha tile's coverage, as much as is bare, or by its cutoff). 1 for whatever alpha_opaque does not count as a
// blade in a light's way (keep the two side by side: this is that test's mean, term by term).
float leaf_pass(uint instanceId, uint geometry, uint primitive, float2 bary) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    const InstanceRecord inst = instances[instanceId];
    const MaterialRecord m = materials[materialIndices[inst.materialBase + geometry]];
    const uint leaf = MATERIAL_ALPHA_TEST | MATERIAL_TRANSLUCENT;
    if ((m.flags & (leaf | MATERIAL_GLASS | MATERIAL_THIN_CLOTH | MATERIAL_STRANDS)) != leaf) return 1.0;
    if ((m.flags & MATERIAL_CAMERA_ONLY) != 0 && !(g_mirrorRay && (m.flags & MATERIAL_HAIR_LAYER) == 0)) return 1.0;
    const float a = material_alpha(inst, m, geometry, primitive, bary);
    const float cover = !shared_cutout(m) && (m.flags & MATERIAL_ALPHA_TILE) != 0 ? saturate(a) : (a >= m.alphaCutoff ? 1.0 : 0.0);
    return 1.0 - (1.0 - TRANSMISSION) * cover;
}
void water_hit(inout Hit h, InstanceRecord inst, MeshRecord mesh, uint3 idx, float2 bary); // pt_water_surface.hlsli
Hit fetch_hit(uint instanceId, uint geometry, uint primitive, float2 bary, float3x4 objectToWorld, float3 rayDir, float3 hitPos, float3 rayOrigin) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    InstanceRecord inst = instances[instanceId];
    MeshRecord mesh = meshes[inst.mesh];
    uint3 idx = triangle_indices(mesh, geometry, primitive);
    ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    float3 p0 = asfloat(pb.Load3(idx.x * 12)), p1 = asfloat(pb.Load3(idx.y * 12)), p2 = asfloat(pb.Load3(idx.z * 12));
    float3 w0 = mul(objectToWorld, float4(p0, 1.0)), w1 = mul(objectToWorld, float4(p1, 1.0)), w2 = mul(objectToWorld, float4(p2, 1.0));
    Hit h;
    h.water = 0; h.waterBase = 0.0; h.waterPrevious = 0.0;
    h.instance = instanceId;
    h.geometry = geometry; h.primitive = primitive;
    h.position = hitPos; h.rayDirection = rayDir; h.rayOrigin=rayOrigin;
    float3 n = cross(w1 - w0, w2 - w0);
    float len = length(n);
    // (len < 1e30, PT-129, docs/agents/nosun.md: corners that are garbage, as a mesh's buffer may hold for a frame while a
    // scene loads, give a product past what a float holds, and n / len was then no number: every ray that left the hit
    // started nowhere. No real triangle's is near it.)
    n = len > 1e-12 && len < 1e30 ? n / len : -rayDir;
    // The front face is the side cross(v1 - v0, v2 - v0) faces in the mesh's own space (Unity's clockwise winding), which a
    // mirroring transform turns around in the world.
    const float3x3 linearPart = (float3x3)objectToWorld;
    h.front = dot(n, rayDir) * determinant(linearPart) < 0.0;
    h.normal = dot(n, rayDir) > 0.0 ? -n : n;
    // The mesh's smooth normal: its vertices' normals over the triangle, to the world by the transform's cofactors (as
    // caustic_vertex takes them), on the side of the triangle the ray met. What the tracer draws from its own mesh (glass HDRP
    // draws as a transparent surface, a prop HDRP draws forward) is shaded with it (PT-04): by the triangle's own normal a
    // round jar was a prism, each of its faces one flat mirror. A mesh without normals (a character's), or whose normals
    // cancel there, keeps its triangle's. Passes that never read it compile the loads away.
    // ON TRIAL (PT-92): a character's mesh has them in the frames the game's skinning output brings its skinned normals
    // (pt_skin.hlsl; TGPT_SetSkinned), its vertices' as the game posed them this frame: its helmet, its cloak, its skin are
    // then as round to every ray as the game shades them. Its hair cards keep their flat faces as before: hair is lit about
    // its strand (pt_primary hair_point), not about its card's vertex normals (8: INSTANCE_CHARACTER).
    h.rounded = h.normal;
    if (mesh.normals != INVALID && (g.settings2.y & OPTION_FACETED) == 0) {
        ByteAddressBuffer vn = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.normals)];
        const float3 local = unpack_normal(vn.Load(idx.x * 4)) * (1.0 - bary.x - bary.y) + unpack_normal(vn.Load(idx.y * 4)) * bary.x +
                             unpack_normal(vn.Load(idx.z * 4)) * bary.y;
        const float3 r0 = objectToWorld[0].xyz, r1 = objectToWorld[1].xyz, r2 = objectToWorld[2].xyz;
        const float3 world = mul(float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1)), local);
        const float worldLength = length(world);
        if (worldLength > 1e-12) h.rounded = (dot(world, h.normal) < 0.0 ? -world : world) / worldLength;
        if ((inst.flags & 8u) != 0u) { // a character's hair card: its flat face, as before
            StructuredBuffer<MaterialRecord> hitMaterials = ResourceDescriptorHeap[g.scene0.w];
            const uint hitFlags = hitMaterials[materialIndices[inst.materialBase + geometry]].flags;
            if ((hitFlags & MATERIAL_STRANDS) != 0 || ((hitFlags & MATERIAL_HAIR_LAYER) != 0 && (hitFlags & MATERIAL_CAMERA_ONLY) == 0)) h.rounded = h.normal;
        }
    }
    // The ray cone's footprint (PT-46): its width here, and that in the mesh's texture coordinates through the triangle's own
    // map from its plane to them, along the footprint's two axes on the plane: the ray's slant (as long as the width over the
    // cosine) and across it.
    h.uv = 0.0; h.dpdu = 0.0; h.dpdv = 0.0; h.uvGradX = 0.0; h.uvGradY = 0.0;
    h.uv2 = 0.0; h.uv2GradX = 0.0; h.uv2GradY = 0.0;
    h.toWorld = objectToWorld;
    h.footprint = g_coneWidth + (g_coneSpread >= 0.0 ? g_coneSpread : pixel_angle()) * length(hitPos - g_rayOrigin);
    // The footprint's two axes on the triangle's plane: along the ray's slant (as long as the width over the cosine) and across.
    const float slant = dot(rayDir, h.normal);
    float3 along = rayDir - h.normal * slant;
    const float alongLength = length(along);
    along = alongLength > 1e-6 ? along / alongLength : normalize(abs(h.normal.x) < 0.9 ? cross(h.normal, float3(1.0, 0.0, 0.0)) : cross(h.normal, float3(0.0, 1.0, 0.0)));
    const float3 ax = along * (h.footprint / max(abs(slant), 0.05)), ay = cross(h.normal, along) * h.footprint;
    h.footX = ax; h.footY = ay;
    const float3 e1 = w1 - w0, e2 = w2 - w0;
    const float g00 = dot(e1, e1), g01 = dot(e1, e2), g11 = dot(e2, e2), gd = g00 * g11 - g01 * g01;
    // The footprint's axes in the triangle's own two edges (a, b: ax = a.x e1 + a.y e2).
    float2 abX = 0.0, abY = 0.0;
    if (gd > 1e-30) {
        const float ig = 1.0 / gd;
        abX = float2(dot(ax, e1) * g11 - dot(ax, e2) * g01, dot(ax, e2) * g00 - dot(ax, e1) * g01) * ig;
        abY = float2(dot(ay, e1) * g11 - dot(ay, e2) * g01, dot(ay, e2) * g00 - dot(ay, e1) * g01) * ig;
    }
    if (mesh.uvs != INVALID) {
        ByteAddressBuffer ub = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.uvs)];
        const float2 t0 = asfloat(ub.Load2(idx.x * 8)), t1 = asfloat(ub.Load2(idx.y * 8)), t2 = asfloat(ub.Load2(idx.z * 8));
        h.uv = t0 * (1.0 - bary.x - bary.y) + t1 * bary.x + t2 * bary.y;
        const float2 d1 = t1 - t0, d2 = t2 - t0;
        const float det = d1.x * d2.y - d1.y * d2.x;
        if (abs(det) > 1e-20) { const float r = 1.0 / det; h.dpdu = (e1 * d2.y - e2 * d1.y) * r; h.dpdv = (e2 * d1.x - e1 * d2.x) * r; }
        h.uvGradX = abX.x * d1 + abX.y * d2;
        h.uvGradY = abY.x * d1 + abY.y * d2;
    }
    if (mesh.uvs2 != INVALID) {
        ByteAddressBuffer ub2 = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.uvs2)];
        const float2 t0 = asfloat(ub2.Load2(idx.x * 8)), t1 = asfloat(ub2.Load2(idx.y * 8)), t2 = asfloat(ub2.Load2(idx.z * 8));
        h.uv2 = t0 * (1.0 - bary.x - bary.y) + t1 * bary.x + t2 * bary.y;
        h.uv2GradX = abX.x * (t1 - t0) + abX.y * (t2 - t0);
        h.uv2GradY = abY.x * (t1 - t0) + abY.y * (t2 - t0);
    }
    h.color = 1.0;
    if (mesh.colors != INVALID) {
        ByteAddressBuffer cb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.colors)];
        float4 c0 = unpack_rgba8(cb.Load(idx.x * 4)), c1 = unpack_rgba8(cb.Load(idx.y * 4)), c2 = unpack_rgba8(cb.Load(idx.z * 4));
        h.color = c0 * (1.0 - bary.x - bary.y) + c1 * bary.x + c2 * bary.y;
    }
    h.material = materialIndices[inst.materialBase + geometry];
    if ((inst.flags & INSTANCE_WAVES) != 0) water_hit(h, inst, mesh, idx, bary);
    return h;
}
// A material's metallic and smoothness where a ray meets it (st: the base color map's coordinates, which HDRP Lit samples
// its mask map with): with a mask map (MATERIAL_MASK_TILE), the lerp of each one's ends (maskRemap, 8 bits each) by its
// texel's value, stored inverted (d3d11.hlsl TGPT_ATLAS: a tile not baked yet, white, reads as their low ends); without,
// the material's constants.
float2 metallic_smoothness(MaterialRecord m, float2 st) {
    if ((m.flags & MATERIAL_MASK_TILE) == 0) return float2(m.metallic, m.smoothness);
    Texture2D<float4> atlas = ResourceDescriptorHeap[g.scene1.z];
    uint2 origin = uint2(uint(m.maskTile) % TILES_PER_ROW, uint(m.maskTile) / TILES_PER_ROW) * TILE_SIZE;
    uint2 texel = min(uint2(frac(st) * TILE_SIZE), TILE_SIZE - 1);
    const float4 t = atlas.Load(int3(origin + texel, 0));
    const float4 ends = unpack_rgba8(m.maskRemap); // metallic low, high; smoothness low, high
    return float2(lerp(ends.x, ends.y, 1.0 - t.r), lerp(ends.z, ends.w, 1.0 - t.a));
}
// Metallic, smoothness and ambient occlusion where a ray meets the material: from the game's own mask map texel by texel
// where it is shared (its channels as textureFlags names them, through maskRemap's ends; HDRP Lit's green its occlusion),
// else metallic_smoothness's, with no occlusion.
float3 material_finish(MaterialRecord m, float2 st, float2 gx, float2 gy) {
    if (has_shared(m, m.maskTexture)) {
        const float4 t = shared_sample(m.maskTexture, st, gx, gy);
        const float4 ends = unpack_rgba8(m.maskRemap);
        const uint mc = m.textureFlags & 3u, sc = (m.textureFlags >> 2) & 3u;
        const float metal = mc == 0u ? t.r : mc == 1u ? t.g : mc == 2u ? t.b : t.a;
        const float smooth = sc == 0u ? t.r : sc == 1u ? t.g : sc == 2u ? t.b : t.a;
        // Its occlusion through the material's own range (HDRP Lit's AO remap; PT-64: Mat_Wooden_Wall's ends at 0.39).
        float occlusion = (m.textureFlags & TEXTURE_MASK_OCCLUSION) != 0 ? t.g : 1.0;
        if ((m.textureFlags & TEXTURE_OCCLUSION_REMAP) != 0u) occlusion = lerp(float((m.textureFlags >> 12) & 0xFFu), float((m.textureFlags >> 20) & 0xFFu), occlusion) / 255.0;
        return float3(lerp(ends.x, ends.y, metal), lerp(ends.z, ends.w, smooth), occlusion);
    }
    return float3(metallic_smoothness(m, st), 1.0);
}
// How much of a pixel a card of strands covers where a camera ray met it (pt_prepare): its texture's alpha (its color tile's,
// with the base color's) averaged over the part of the card the pixel spans (pixelWorld: the pixel's size on the card),
// 3x3 samples over it in the card's texture frame (dP/du, dP/dv). The atlas has no mips: one sample a pixel flickered from
// frame to frame with the camera's jitter as thin strands came and went (2026-09-28, 11:49). Close up the samples meet
// (a strand's own edge), far off they average it (a brow's share of the pixel).
float strand_coverage(MaterialRecord m, float2 uv, float3 dpdu, float3 dpdv, bool frame, float pixelWorld) {
    const float2 st = uv * m.uvST.xy + m.uvST.zw;
    float2 du = 0.0, dv = 0.0;
    if (frame) {
        du = float2(pixelWorld / max(length(dpdu), 1e-8) * m.uvST.x, 0.0);
        dv = float2(0.0, pixelWorld / max(length(dpdv), 1e-8) * m.uvST.y);
    }
    float a = 0.0;
    [unroll] for (int j = -1; j <= 1; ++j)
        [unroll] for (int i = -1; i <= 1; ++i) a += atlas_sample(m.albedoTile, st + (float(i) * du + float(j) * dv) / 3.0).a;
    return saturate(m.baseColor.a * a / 9.0);
}
// Hair's strands at a point of its mesh's own coordinates, from its lobe tile (TGPT_RegisterHairLobes, d3d11.hlsl
// TGPT_HAIR_LOBES), texel by texel as stored: the flow's direction (x, y as 0.5 + 0.5 x), its smoothness share, its specular
// map's luminance.
float4 hair_lobe(int tile, float2 uv) {
    Texture2D<float4> atlas = ResourceDescriptorHeap[g.scene1.z];
    const uint2 origin = uint2(uint(tile) % TILES_PER_ROW, uint(tile) / TILES_PER_ROW) * TILE_SIZE;
    const uint2 texel = min(uint2(frac(uv) * TILE_SIZE), TILE_SIZE - 1);
    return atlas.Load(int3(origin + texel, 0));
}
// The same from the strands' own maps where they are open (PT-46: the lobe record of tracer.cpp bake_hair_lobes), at the hit's
// own footprint and coordinates, as d3d11.hlsl TGPT_HAIR_LOBES computes it for one texel; else from the tile.
float4 hair_lobe(int tile, Hit hit) {
    if (tile >= 0 && tile < int(MAX_TILES) && map_records_on()) {
        const uint row = MAP_LOBE_BASE + uint(tile) * MAP_LOBE_ROWS;
        const uint4 r0 = map_row(row), r1 = map_row(row + 1);
        if (r0.w == 1u && maps_open(uint4(r0.xyz, INVALID), INVALID, r1.z)) {
            float2 flow = float2(0.0, -1.0);
            if (int(r0.x) >= 0) {
                const float4 f = shared_sample(int(r0.x), hit.uv, hit.uvGradX, hit.uvGradY);
                flow = float2(f.r * f.a, f.g) * 2.0 - 1.0;
                flow.y += asfloat(r1.y) * (1.0 - 2.0 * flow.y);
            }
            const float mask = int(r0.y) >= 0 ? shared_sample(int(r0.y), hit.uv, hit.uvGradX, hit.uvGradY).a : 1.0;
            const float3 spec = int(r0.z) >= 0 ? shared_sample(int(r0.z), hit.uv, hit.uvGradX, hit.uvGradY).rgb : 1.0;
            return saturate(float4(flow * 0.5 + 0.5, pow(saturate(mask), max(asfloat(r1.x), 0.0)), dot(saturate(spec), float3(0.2126, 0.7152, 0.0722))));
        }
    }
    return hair_lobe(tile, hit.uv);
}
// Hair's highlights: its specular reflection of light arriving along l, times the cosine, as HDRP's Kajiya-Kay hair has it
// (Hair.hlsl, EvaluateBSDF and D_KajiyaKay, read only; the game's RealHair is its Hair master stack): two lobes around the
// strand's direction, each shifted along the normal (which moves its highlight toward the root or the tip), as sharp as its
// smoothness makes it (Blinn-Phong's exponent, 2 / roughness^2 - 2), tinted by the specular map, the material's tint and the
// lobe's strength, times Fresnel at hair's reflectance and a quarter; none where the strand's face turns from the camera. The
// exponent stops at HAIR_MAX_EXPONENT: HDRP's sharpest (smoothness 0.99, 2e8) is a line a lamp's rays meet one in millions of
// frames, and wider it keeps its light (the normalization, (n + 2) / 2 pi) spread over a band some 1.3 degrees across.
static const float HAIR_MAX_EXPONENT = 2000.0;
float hair_exponent(float smoothness) {
    const float r = (1.0 - smoothness) * (1.0 - smoothness); // PerceptualRoughnessToRoughness
    return clamp(2.0 / max(r * r, 1.192093e-7) - 2.0, 1.192093e-7, HAIR_MAX_EXPONENT);
}
float kajiya_kay(float3 t, float3 h, float exponent) {
    const float th = dot(t, h);
    return saturate(th + 1.0) * (exponent + 2.0) / (2.0 * PI) * pow(saturate(1.0 - th * th), 0.5 * exponent);
}
float3 hair_specular(Surface s, float3 v, float3 l) {
    const float nl = saturate(dot(s.normal, l));
    if (nl <= 0.0 || dot(s.geoNormal, v) <= 0.0) return 0.0;
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    const MaterialRecord m = materials[s.hairMaterial];
    const float3 h = normalize(v + l);
    const float3 t1 = normalize(s.strand + s.normal * m.hairShift), t2 = normalize(s.strand + s.normal * m.hairShift2);
    const float smooth1 = min(lerp(m.hairSmoothMin, m.hairSmoothMax, s.hairLobe.y), 0.99); // HDRP keeps the primary below 1
    const float lobes = m.hairStrength * kajiya_kay(t1, h, hair_exponent(smooth1)) + m.hairStrength2 * kajiya_kay(t2, h, hair_exponent(m.hairSmooth2));
    return 0.25 * fresnel_schlick((float3)HAIR_F0, dot(l, h)) * (s.hairLobe.x * m.hairTint) * lobes * nl;
}
// Each strand's own shading from the game's normal map (pt_primary hair_normal): RealHair lights hair by Lambert about its
// normal-mapped normal (read from its compiled forward pass: saturate(dot(N, L)) / pi with N the normal map's, at
// _NormalStrength), so the two sides of every strand on a card take the light differently, the fine light and dark lines
// along a head of hair. The tracer keeps its strand's light (the cylinder's, about the strand: lit from behind as from the
// front) and takes from the game's rule only how much more or less light a texel's own normal gets than the strand's (n,
// facing the camera, or for light from behind the far side's: the normal mirrored through the card), with a floor that keeps
// a light along the card from dividing by nothing. Over a strand it averages to about 1: the hair keeps its brightness.
// Without a normal map (bumped is n), exactly 1.
static const float HAIR_BUMP_FLOOR = 0.1;
float hair_bump(float3 bumped, float3 n, float3 l) {
    const float c = dot(n, l);
    const float3 b = c >= 0.0 ? bumped : bumped - 2.0 * dot(bumped, n) * n;
    const float3 m = c >= 0.0 ? n : -n;
    return (saturate(dot(b, l)) + HAIR_BUMP_FLOOR) / (saturate(dot(m, l)) + HAIR_BUMP_FLOOR);
}
// The cosine a light arriving along l lights hair with, seen from v (Surface.strands: hair the tracer's own camera ray
// stopped at): Lambert's about the strand's own normal facing the light (a strand is a thin cylinder, lit on whichever side
// the light comes from, from behind as from the front: the light through a head of hair lit from behind), which is the sine
// of the angle between the light and the strand, times hair_bump for the game's normal map (s.normal: the strand's normal
// facing the camera with the map). The card's flat face as the normal lit each card as one piece, and cards crossing at
// every angle came out patchy. The direct light's (pt_di_spatial, rtxdi_shade); the bounced light keeps the surface's.
float strand_cos(Surface s, float3 v, float3 l) {
    const float tl = dot(s.strand, l);
    float3 nv = v - s.strand * dot(s.strand, v);
    nv = dot(nv, nv) > 1e-8 ? normalize(nv) : v;
    return sqrt(saturate(1.0 - tl * tl)) * hair_bump(s.normal, nv, l);
}
// A surface's cosine for a light along l, seen from v: hair's about its strand, any other's translucent_cos.
float surface_cos(Surface s, float3 v, float3 l) { return s.strands ? strand_cos(s, v, l) : translucent_cos(s.normal, l, s.through); }
// A pixel whose own camera ray passed every hair card and met nothing behind them, where HDRP drew a card and so no sky
// (pt_primary write_sky): marked as no other pixel is, drawn forward and not lit. pt_compose gives it the sky.
bool sky_through(Surface s) { return s.valid && !s.deferred && s.forward; }
// The yardstick's word for a pixel (pt_primary yardstick_code says what each part means), and the table pt_yardstick counts
// it into: YARD_COUNTERS summary counters, then two for each of YARD_MATERIALS materials.
static const uint YARD_SKY = 0u, YARD_SAME = 1u, YARD_NEARER = 2u, YARD_FARTHER = 3u, YARD_MISSING = 4u, YARD_EXTRA = 5u;
static const uint YARD_NORMAL = 8u, YARD_COLOUR = 16u, YARD_ROUGH = 32u, YARD_MOTION = 64u, YARD_FORWARD = 128u;
// YARD_KIND_MOVING: a thing the game moves, placed where HDRP draws it this frame (InstanceRecord.previous).
static const uint YARD_KIND_STILL = 0u, YARD_KIND_CHARACTER = 1u, YARD_KIND_SWAYS = 2u, YARD_KIND_MOVING = 3u, YARD_KIND_HAIR = 4u;
static const uint YARD_NO_MATERIAL = 0xFFFFFu;
// Bit 11, beside the yardstick's own: the pixel's own camera ray took the scattering walk this frame (PT-40: pt_primary
// write_traced decides it at the hit, pt_walk walks where it is set, pt_yardstick counts it).
static const uint YARD_WALKS = 0x800u;
static const uint YARD_COUNTERS = 64u, YARD_MATERIALS = 4096u, YARD_PER_MATERIAL = 6u;
// Its colour in the view: black, both sky; dim green, the same surface and the same reading of it; yellow, another colour;
// blue, another normal; cyan, another roughness (the first of those that holds); grey, the same surface where HDRP drew
// forward (nothing to compare); orange, the tracer's hit nearer; red, farther; magenta, the tracer met nothing; purple, the
// tracer met something where HDRP has sky. A pixel whose motion differs is half white on top.
float3 yardstick_color(uint code) {
    const uint where = code & 7u;
    float3 c = 0.0;
    if (where == YARD_SAME) {
        c = (code & YARD_FORWARD) != 0u ? float3(0.25, 0.25, 0.25) : (code & YARD_COLOUR) != 0u ? float3(1.0, 0.9, 0.0) : (code & YARD_NORMAL) != 0u ? float3(0.1, 0.3, 1.0) :
            (code & YARD_ROUGH) != 0u ? float3(0.0, 0.9, 0.9) : float3(0.03, 0.2, 0.03);
    } else if (where == YARD_NEARER) c = float3(1.0, 0.45, 0.0);
    else if (where == YARD_FARTHER) c = float3(1.0, 0.0, 0.0);
    else if (where == YARD_MISSING) c = float3(1.0, 0.0, 1.0);
    else if (where == YARD_EXTRA) c = float3(0.45, 0.0, 0.9);
    if ((code & YARD_MOTION) != 0u) c = lerp(c, 1.0, 0.5);
    return c;
}
// The material of the surface a pixel's own camera ray stopped at (pt_primary primary_all; the yardstick's word holds it for
// every pixel of the frame): what the passes after pt_prepare read of that material where the surface record has no room for
// it: a subsurface material's medium (pt_walk), a leaf's own share of the light from behind (leaf_transmittance). False where
// the pixel has none: the sky, see-through glass, the route before the tracer's own rays.
bool own_material(uint2 p, out uint material) {
    material = YARD_NO_MATERIAL;
    if (!own_rays() || g.primary.z == INVALID) return false;
    RWTexture2D<uint> yard = ResourceDescriptorHeap[g.primary.z];
    material = yard[p] >> 12;
    return material < YARD_NO_MATERIAL - 1u;
}
// Whether the pixel's own camera ray took the scattering walk this frame (YARD_WALKS): decided where the ray stopped, with
// the hit in hand (pt_primary write_traced: the material's mask at that point, the piece of the mesh met), which the
// surface record does not keep.
bool own_walks(uint2 p) {
    if (!own_rays() || g.primary.z == INVALID) return false;
    RWTexture2D<uint> yard = ResourceDescriptorHeap[g.primary.z];
    return (yard[p] & YARD_WALKS) != 0u;
}
// A material's emission at a point (st: its texture coordinates with the material's tiling applied), in nits, as HDRP's Lit
// computes it: the emissive color (with its intensity and exposure weight, from the plugin) times the emissive map, and times
// the albedo where Albedo Affects Emissive is on. Path vertices and the light sampling of emissive triangles both read it.
// The emissive map is the game's own where the tracer has it open (PT-64: MaterialRecord.emissiveTexture, read at its top mip:
// an emitter's point has no ray footprint), else its 64x64 tile; the game's grass and leaves scale by its red alone.
float3 material_emission(MaterialRecord m, float2 st) {
    float3 e = m.emissive;
    if (!any(e > 0.0)) return 0.0;
    if (has_shared(m, m.emissiveTexture)) {
        const float3 map = shared_sample(m.emissiveTexture, st, 0.0, 0.0).rgb;
        e *= (m.textureFlags & TEXTURE_EMISSIVE_RED) != 0u ? map.rrr : map;
    } else if (m.emissiveTile >= 0) {
        const float3 map = atlas_sample(m.emissiveTile, st).rgb;
        e *= (m.textureFlags & TEXTURE_EMISSIVE_RED) != 0u ? map.rrr : map;
    }
    if (m.flags & MATERIAL_EMISSIVE_ALBEDO) {
        float3 c = m.baseColor.rgb;
        if (has_shared(m, m.colorTexture)) c *= shared_sample(m.colorTexture, st, 0.0, 0.0).rgb;
        else if (m.albedoTile >= 0) c *= atlas_sample(m.albedoTile, st).rgb;
        e *= c;
    }
    return e;
}
// How often a ray other than a camera ray meets the surface at a point, as alpha_opaque decides: its emission reaches as
// much of the world. By chance, as much as an alpha tile covers (hair's too), or cut out by its alpha test; glass by chance,
// as bright as its color is; seen by camera rays only, never.
float emission_coverage(MaterialRecord m, float2 st) {
    if ((m.flags & MATERIAL_ALPHA_TEST) == 0) return 1.0;
    if ((m.flags & MATERIAL_CAMERA_ONLY) != 0) return 0.0;
    if ((m.flags & MATERIAL_GLASS) != 0) {
        float3 c = m.baseColor.rgb;
        if (m.albedoTile >= 0) c *= atlas_sample(m.albedoTile, st).rgb;
        return 1.0 - sqrt(saturate(dot(c, float3(0.2126, 0.7152, 0.0722))));
    }
    float a = m.baseColor.a;
    if (shared_cutout(m)) return a * shared_sample(m.colorTexture, st, 0.0, 0.0).a >= m.alphaCutoff ? 1.0 : 0.0;
    if (m.albedoTile >= 0) a = (m.flags & MATERIAL_ALPHA_TILE) ? alpha_sample(m.alphaTile, st) : a * atlas_sample(m.albedoTile, st).a;
    if ((m.flags & (MATERIAL_ALPHA_TILE | MATERIAL_STRANDS)) != 0 && m.albedoTile >= 0) return saturate(a);
    return a >= m.alphaCutoff ? 1.0 : 0.0;
}
// albedo, f0 and roughness (perceptual) are the diffuse color and the finish of a surface the camera sees (pt_prepare takes
// them for one HDRP draws forward); reflectance is what a path vertex, which is shaded diffusely, sends on; through: the share
// it lets through from behind (direct_diffuse), a leaf's (TGPT_MATERIAL_TRANSLUCENT) or thin cloth's (TGPT_MATERIAL_THIN_CLOTH).
// normal: what a path vertex is lit by (the triangle's own, or with the game's own normal map HDRP's normal-mapped one:
// material_normal); occlusion: the mask map's ambient occlusion, which HDRP Lit takes off the bounced light alone.
// tint: the color of the light let through from behind against the grey share (through x tint: the share per channel).
// subsurface (PT-40): the share of its diffuse light that scatters under the surface at the point, the game's subsurface
// mask there (0 for a material light does not walk inside): how often a vertex on it takes the walk (subsurface_vertex).
struct SurfaceMaterial { float3 albedo; float3 emissive; float3 f0; float roughness; float3 reflectance; float through; float3 normal; float occlusion; float3 tint; float subsurface; };
// ---------------------------------------------------------------- wet ground (PT-52, pt_wet.hlsl)
// Where HDRP's waves have been, kept in a store of the world's cells round the camera (levels of WET_GRID x WET_GRID, the finest
// g.wetGrid.x, each next twice as coarse; tracer.cpp kWetGrid): per cell, how long ago the water last stood at or above each
// height, as points (height, time) on a falling line, the oldest highest (pt_wet.hlsl keeps them). Wet ground is the ground's
// own surface with water on and in it, by the optics and the drying times below; every ray's hit reads the store in one
// place (hit_material), the camera's own ray's as any other's, so a mirror shows the wet sand the camera sees.
static const uint WET_GRID = 256;           // cells a level's side (tracer.cpp kWetGrid)
static const uint WET_POINTS = 6;           // the points a cell keeps
static const uint WET_CELL_BYTES = 64;      // key, count, the slot, its last update's clock; six points of two floats
// The optics of water on a rough surface (Lekner and Dorf, "Why some things are darker when wet", Applied Optics 27, 1988):
// light scattered by the surface under a water layer meets the water's surface from below, and of diffuse light there a share
// r_i is reflected back down (total internal reflection beyond 48.6 degrees, and Fresnel within); the surface takes it again
// and absorbs 1 - A of it, so what leaves is (1 - r_e)(1 - r_i) A / (1 - r_i A). r_e is the water's reflectance for diffuse
// light from above, the Fresnel term of an index of 1.3333 (HDRP's) averaged over a Lambertian hemisphere: 0.0665; r_i = 1 -
// (1 - r_e) / n^2 (Stokes' relation) = 0.4749. Water in sand's pores works the same way at the grains' scale (their paper's
// second case). A wet albedo of 0.3 is 0.57 of the dry one, 0.7 is 0.73: the darker a surface, the darker it gets wet.
static const float WATER_RE = 0.0665;
static const float WATER_RI = 0.4749;
float3 wet_albedo(float3 a) { return (1.0 - WATER_RE) * (1.0 - WATER_RI) * a / (1.0 - WATER_RI * a); }
// A film of water over the ground is one smooth mirror: the air-water interface's reflectance (HDRP's water f0 for an index of
// 1.3333) and a still water surface's roughness (HDRP's water's own: smoothness 0.95 near the camera, WaterSurface's
// startSmoothness default).
static const float WET_FILM_F0 = 0.02037318784;
static const float WET_FILM_ROUGHNESS = 0.05;
// How it dries (all in seconds of the game's shader time, which stands still when the world does).
//   The film runs off by gravity: a viscous film draining down a slope a from where the water stood above it is
//   h(t) = sqrt(nu x / (g sin(a) t)) thick at a run x below its top (Jeffreys 1930, the draining film), here x = (how far above
//   the point the water reached) / sin(a). It stays one mirror while thicker than the surface's own roughness: WET_GRAIN, 0.1
//   mm (fine sand's grains are 0.125 to 0.25 mm across, Wentworth; a rock's grain at that scale), so for t_drain = nu (height
//   above) / (g sin^2(a) WET_GRAIN^2); after that the share of the surface still under it falls as h / WET_GRAIN (the
//   roughness's heights taken as evenly spread over WET_GRAIN). nu = 1.0e-6 m^2/s (water at 20 C), g = 9.81 m/s^2.
//   Sand soaks it up: a 0.5 mm film into sand of a saturated conductivity of 1e-4 m/s (medium sand, 1e-4 to 1e-3; Darcy's
//   law with a unit gradient) is gone in WET_SAND_SOAK = 5 s.
//   What the surface still holds then evaporates at WET_EVAPORATION, 0.2 mm an hour (about 5 mm a day, a temperate potential
//   evaporation): a rock's roughness holds half its grain's depth of water (0.05 mm: about 15 minutes), sand the pores of its
//   top millimetre (porosity 0.4: 0.4 mm, about 2 hours). Its share of the surface still wet falls evenly with the water left.
//   Which is sand: the tracer can't tell sand from rock by its material, so it takes the slope: sand lies no steeper than its
//   angle of repose (34 degrees dry), and no ground steeper than 45 degrees (damp sand's steepest) is taken for sand.
static const float WET_VISCOSITY = 1.0e-6;
static const float WET_GRAVITY = 9.81;
static const float WET_GRAIN = 1.0e-4;
static const float WET_SAND_SOAK = 5.0;
static const float WET_EVAPORATION = 0.2e-3 / 3600.0;
static const float WET_ROCK_HOLDS = 0.5e-4;
static const float WET_SAND_HOLDS = 0.4e-3;
static const float WET_SAND_REPOSE = 0.8290;   // cos(34 degrees): a normal at least this upright may be sand
static const float WET_SAND_STEEPEST = 0.7071; // cos(45 degrees): none less upright is
bool wet_on() { return g.wet.x != INVALID && g.wet.y > 0u; }
float wet_now() { return asfloat(g.wet.z); }
// A cell of a level by its place in the level's square (local cells); its record's byte offset and its key (the world cell's).
uint wet_offset(uint level, int2 local, out uint key) {
    const int2 world = local + asint(g.wetLevels[level].zw);
    key = (uint(world.x) & 0xFFFFu) | (uint(world.y) << 16);
    const uint2 slot = uint2(world) & (WET_GRID - 1u);
    return ((level * WET_GRID + slot.y) * WET_GRID + slot.x) * WET_CELL_BYTES;
}
// What a cell remembers of the water at height y (world): how long ago the water last stood at or above y (age), how far above
// y it stood then (above), and whether it stands over y now (the store's last update). False: not within the cell's memory.
bool wet_cell(uint level, int2 local, float y, out float age, out float above, out bool now) {
    age = 1e30; above = 0.0; now = false;
    uint key;
    const uint at = wet_offset(level, local, key);
    RWByteAddressBuffer store = ResourceDescriptorHeap[g.wet.x];
    const uint4 head = store.Load4(at);
    const uint n = min(head.y, WET_POINTS);
    if (head.x != key || n == 0u) return false;
    const uint4 q0 = store.Load4(at + 16), q1 = store.Load4(at + 32), q2 = store.Load4(at + 48);
    const float h[6] = { asfloat(q0.x), asfloat(q0.z), asfloat(q1.x), asfloat(q1.z), asfloat(q2.x), asfloat(q2.z) };
    const float t[6] = { asfloat(q0.y), asfloat(q0.w), asfloat(q1.y), asfloat(q1.w), asfloat(q2.y), asfloat(q2.w) };
    const float clock = wet_now();
    if (y <= h[n - 1]) {
        age = max(clock - t[n - 1], 0.0); above = h[n - 1] - y;
        now = t[n - 1] >= asfloat(g.wet.w);
        return true;
    }
    // Between two points the water fell from the older one's height to the newer one's: it last stood at y in between.
    [loop] for (int i = int(n) - 2; i >= 0; --i) {
        if (h[i] < y) continue;
        const float span = max(h[i] - h[i + 1], 1e-6);
        const float when = t[i] + (h[i] - y) / span * (t[i + 1] - t[i]);
        age = max(clock - when, 0.0); above = h[i] - y;
        return true;
    }
    return false;
}
// The drying of one remembered wetting: x the share of the surface under a film (a mirror), y the share still wet in its pores
// and roughness (darker), z 1 where the water stands over it now. n: the ground's geometric normal (its slope).
float3 wet_dry(float age, float above, bool now, float3 n) {
    if (now) return float3(1.0, 1.0, 1.0);
    const float up = saturate(abs(n.y));
    const float sin2 = max(1.0 - up * up, 1e-6);
    const float sand = saturate((up - WET_SAND_STEEPEST) / (WET_SAND_REPOSE - WET_SAND_STEEPEST));
    const float drain = WET_VISCOSITY * max(above, 0.0) / (WET_GRAVITY * sin2 * WET_GRAIN * WET_GRAIN);
    const float filmTime = lerp(drain, min(drain, WET_SAND_SOAK), sand);
    const float film = saturate(sqrt(filmTime / max(age, 1e-4)));
    const float holds = lerp(WET_ROCK_HOLDS, WET_SAND_HOLDS, sand);
    const float moist = age <= filmTime ? 1.0 : saturate(1.0 - (age - filmTime) * WET_EVAPORATION / holds);
    return float3(film, max(moist, film), 0.0);
}
// The wet state of the ground at p (relative to the scene origin) with geometric normal n: wet_dry's, read from the four cells
// round p (bilinear, a cell with no memory of water counting as dry).
float3 wet_state(float3 p, float3 n) {
    if (!wet_on() || p.y > g.wetGrid.z) return 0.0;
    uint level = 0; float cell = 0.0; float2 at = 0.0;
    bool placed = false;
    for (uint l = 0; l < g.wet.y && !placed; ++l) {
        cell = g.wetGrid.x * exp2(float(l));
        at = (p.xz - g.wetLevels[l].xy) / cell - 0.5;
        if (all(at >= 0.0) && all(at < float(WET_GRID) - 1.0)) { level = l; placed = true; }
    }
    if (!placed) return 0.0;
    const int2 c = int2(floor(at));
    const float2 f = at - float2(c);
    const float y = p.y + g.wetGrid.y;
    float3 sum = 0.0;
    [unroll] for (uint k = 0; k < 4; ++k) {
        const int2 o = int2(k & 1u, k >> 1);
        const float w = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
        float age, above; bool now;
        if (w > 0.0 && wet_cell(level, c + o, y, age, above, now)) sum += w * wet_dry(age, above, now, n);
    }
    return sum;
}
// The ground's material made wet by that state (not where the water covers it now: the water's own passes see it through the
// water). The darkening is the moist share's; the film's mirror is chosen with its share (u in [0, 1)), so the two kinds of
// surface within a pixel average over frames to their mixture.
void wet_material(float3 state, float u, inout float3 albedo, inout float3 reflectance, inout float3 f0, inout float roughness) {
    if (!(state.y > 0.0) || state.z > 0.5) return;
    albedo = lerp(albedo, wet_albedo(saturate(albedo)), state.y);
    reflectance = lerp(reflectance, wet_albedo(saturate(reflectance)), state.y);
    if (u < state.x) { f0 = WET_FILM_F0; roughness = WET_FILM_ROUGHNESS; }
}

// Set while the hit is of something inside a solid thing (subsurface_walk: a wick in wax, a holder's spike): no wave has
// left a film of water there, so the wet ground's store is not read for it.
static bool g_hitInside = false;
// The subsurface mask a material's own program computed at the hit (PT-40; program_skin: its map times its scale, by its
// regions), below 0 where it computed none: hit_material resets it, and takes the material's own number then.
static float g_pointSubsurface = -1.0;
#include "pt_materials.hlsli"
float3 hit_frame_normal(Hit h, float3 tn, float3 n) {
    const TangentFrame f = hit_frame(h);
    return f.ok ? frame_normal(f, tn, h) : n;
}
// What the material's own shader makes of the hit (PT-64): true with o filled where it has a program and its textures are
// open. HDRP Lit's detail map is laid over HDRP Lit's own reading, which the caller hands in (o).
bool material_program(Hit h, MaterialRecord m, Program p, float2 st, float2 gx, float2 gy, inout MaterialPoint o) {
    if (!p.ready) return false;
    const TangentFrame f = hit_frame(h);
    if (p.kind == PROGRAM_ARCHITECTURE) o = program_architecture(h, m, p, f);
    else if (p.kind == PROGRAM_CLIFF) o = program_cliff(h, m, p, f);
    else if (p.kind == PROGRAM_TRUNK) o = program_trunk(h, m, p, f);
    else if (p.kind == PROGRAM_TERRAIN) o = program_terrain(h, m, p, f);
    else if (p.kind == PROGRAM_TRIPLANAR) o = program_triplanar(h, m, p, f);
    else if (p.kind == PROGRAM_SKIN) o = program_skin(h, p, f);
    else if (p.kind == PROGRAM_GRASS || p.kind == PROGRAM_LEAVES) o.color = program_plant_color(h, m, p, p.kind == PROGRAM_LEAVES, o.normal);
    else if (p.kind == PROGRAM_DETAIL) {
        const float3 tn = has_shared(m, m.normalTexture) ? material_tangent_normal(h, m, st, gx, gy) : float3(0.0, 0.0, 1.0);
        const float detailMask = has_shared(m, m.maskTexture) ? shared_sample(m.maskTexture, st, gx, gy).b : 1.0;
        program_detail(h, m, p, detailMask, tn, f, o);
    } else return false;
    return true;
}


// Decals use the same live texture descriptors as ordinary materials. The ordered box tree visits every
// covering projector in draw order; there is no fixed overlap array, truncation, or screen-space lookup.
#define MATERIAL_NO_DECALS 33554432u
struct DecalRecord {
    float4 toDecal[3], normalToWorld[3];
    float3 lo; uint flags; float3 hi; uint layerMask;
    float4 color, emissionBlend, uvST, remapMin, remapMax;
    int4 textures; float4 fade; uint4 kind; float4 parameters[4];
    float4 graph[24]; int graphTextures[8]; float4 graphWorld[3];
    float4 graphEnv, graphEnv2, graphViewZ;
    uint samplers[12]; float samplerBias[12];
};
// A node of the decals' tree (scene.cpp, split by place since round 20): its box, the node after its subtree, the decal of a
// leaf, whether a mesh decal lies under it, and the least (span.x) and the most (span.y) draw-order index under it.
struct DecalNode { float3 lo; uint next; float3 hi; uint decal; uint mesh; uint2 span; uint pad; };
float4 decal_texture(int texture,uint samplerIndex,float mipBias,float2 uv,float2 gx,float2 gy,float4 fallback) {
    if(texture<0 || g.textures.x==INVALID) return texture==-2 ? 1.0 : texture==-3 ? float4(0,0,0,1) : texture==-4 ? float4(0.5,0.5,1,1) : fallback;
    Texture2D<float4> map=ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x+uint(texture))];
    SamplerState state=SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
    const float bias=exp2(mipBias); return map.SampleGrad(state,uv,gx*bias,gy*bias);
}


struct DecalGraphInput { float2 uv,gx,gy; float3 position,dx,dy,normal; float4 tangent,color; };
struct DecalGraphOutput { float4 color,normal,mask; };
float4 decal_graph_sample(int slot,uint samplerIndex,float mipBias,float2 uv,float2 dx,float2 dy,float4 fallback) {
    return decal_texture(slot,samplerIndex,mipBias,uv,dx,dy,fallback);
}
float4 decal_graph_sample_level(int slot,uint samplerIndex,float2 uv,float lod,float4 fallback) {
    if(slot<0) return slot==-2 ? 1.0 : slot==-3 ? float4(0,0,0,1) : slot==-4 ? float4(0.5,0.5,1,1) : fallback;
    Texture2D<float4> t=ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x+uint(slot))];
    SamplerState state=SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
    return t.SampleLevel(state,uv,lod);
}
float4 decal_graph_object_column(DecalRecord d,uint c) {
    return float4(d.graphWorld[0][c],d.graphWorld[1][c],d.graphWorld[2][c],c==3u ? 1.0 : 0.0);
}
float4 decal_graph_inverse_column(DecalRecord d,uint c) {
    return c==3u ? float4(d.toDecal[0].w-dot(d.toDecal[0].xyz,g.cacheOrigin.xyz),d.toDecal[1].w-dot(d.toDecal[1].xyz,g.cacheOrigin.xyz),d.toDecal[2].w-dot(d.toDecal[2].xyz,g.cacheOrigin.xyz),1.0) : float4(d.toDecal[0][c],d.toDecal[1][c],d.toDecal[2][c],0.0);
}
#include "pt_decal_graphs.hlsli"
void decal_graph(DecalRecord d, DecalGraphInput at, out DecalGraphOutput paint, out float3 emission) {
    paint=(DecalGraphOutput)0; emission=0.0;
    switch(d.kind.x) {
    case 7u: paint=decal_graph_7_paint(d,at); break;
    case 8u: paint=decal_graph_8_paint(d,at); emission=decal_graph_8_emission(d,at).color.rgb/max(d.graphEnv.y*d.graphEnv2.x,1e-12); break;
    case 9u: paint=decal_graph_9_paint(d,at); emission=decal_graph_9_emission(d,at).color.rgb/max(d.graphEnv.y*d.graphEnv2.x,1e-12); break;
    case 10u: paint=decal_graph_10_paint(d,at); emission=decal_graph_10_emission(d,at).color.rgb/max(d.graphEnv.y*d.graphEnv2.x,1e-12); break;
    case 11u: paint=decal_graph_11_paint(d,at); emission=decal_graph_11_emission(d,at).color.rgb/max(d.graphEnv.y*d.graphEnv2.x,1e-12); break;
    case 12u: paint=decal_graph_12_paint(d,at); emission=decal_graph_12_emission(d,at).color.rgb/max(d.graphEnv.y*d.graphEnv2.x,1e-12); break;
    case 13u: paint=decal_graph_13_paint(d,at); emission=decal_graph_13_emission(d,at).color.rgb/max(d.graphEnv.y*d.graphEnv2.x,1e-12); break;
    case 14u: paint=decal_graph_14_paint(d,at); emission=decal_graph_14_emission(d,at).color.rgb/max(d.graphEnv.y*d.graphEnv2.x,1e-12); break;
    case 15u: paint=decal_graph_15_paint(d,at); emission=decal_graph_15_emission(d,at).color.rgb/max(d.graphEnv.y*d.graphEnv2.x,1e-12); break;
    case 16u: paint=decal_graph_16_paint(d,at); emission=decal_graph_16_emission(d,at).color.rgb/max(d.graphEnv.y*d.graphEnv2.x,1e-12); break;
    }
}

float decal_noise_map(int texture,uint samplerIndex,float2 uv) {
    if(texture==-3) return 0.0;
    if (texture<0 || g.textures.x==INVALID) return 1.0;
    Texture2D<float4> t=ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x+uint(texture))];
    SamplerState state=SamplerDescriptorHeap[NonUniformResourceIndex(samplerIndex)];
    return t.SampleLevel(state,uv,0.0).r;
}
float decal_three_panners(int texture,uint samplerIndex,float2 uv,float2 pan) {
    return decal_noise_map(texture,samplerIndex,uv+pan)*decal_noise_map(texture,samplerIndex,uv*0.6+pan*(5.0/3.0))*
           decal_noise_map(texture,samplerIndex,uv*(5.0/6.0)+pan*1.2);
}
float3 decal_unpack_normal(float4 texel) {
    const float2 xy=float2(texel.r*texel.a,texel.g)*2.0-1.0;
    return float3(xy,sqrt(saturate(1.0-dot(xy,xy))));
}

// Value noise used by the shipped wet-decal graph, derived from its integer DXBC operations.
float decal_noise_hash(int2 p) {
    uint y=asuint(p.y)^0x41c64e6du;
    uint h=y*(y+asuint(p.x)); h^=h>>5; h*=0x27d4eb2du;
    return float(h>>8)*(1.0/16777216.0);
}
float decal_noise(float2 uv) {
    const int2 p=int2(floor(uv)); const float2 f=frac(uv), t=f*f*(3.0-2.0*f);
    return lerp(lerp(decal_noise_hash(p),decal_noise_hash(p+int2(1,0)),t.x),
                lerp(decal_noise_hash(p+int2(0,1)),decal_noise_hash(p+1),t.x),t.y);
}
// Blend one actual draw fragment; projectors and mesh intersections use exactly the same arithmetic.
void blend_decal(Hit h, DecalRecord d, DecalGraphInput input, float fade,
    inout SurfaceMaterial s, inout float metal, inout float3 normal, inout float smoothness,
    inout float3 paint, inout float receiverShare, inout bool changed, inout bool normalChanged) {
    const float2 uv=input.uv, gx=input.gx*exp2(asfloat(g.hair.w)), gy=input.gy*exp2(asfloat(g.hair.w));
        float4 colour=d.color*decal_texture(d.textures.x,d.samplers[0],d.samplerBias[0],uv,gx,gy,1.0);
        float4 mask=decal_texture(d.textures.z,d.samplers[2],d.samplerBias[2],uv,gx,gy,1.0);
        float colourAlpha=saturate(colour.a*fade);
        const float maskAlpha=saturate(mask.b*d.remapMin.w*fade);
        float normalAlpha=(d.flags & 64u)!=0u ? maskAlpha : colourAlpha;
        float finishAlpha=(d.flags & 128u)!=0u ? maskAlpha : colourAlpha;
        float3 finish=lerp(d.remapMin.xyz,d.remapMax.xyz,float3(mask.r,mask.g,mask.a));
        float3 tangentNormal=decal_unpack_normal(decal_texture(d.textures.y,d.samplers[1],d.samplerBias[1],uv,gx,gy,float4(0.5,0.5,1,1)));
        if (d.kind.x==1u) {
            // TG/Decal/Decal_Mask: the step map's red erodes colour, normal and finish.
            float step=saturate((mask.r-(1.0-d.parameters[0].x))/max(d.parameters[0].y,1e-8));
            step=step*step*(3.0-2.0*step);
            normalAlpha=finishAlpha=colourAlpha*step;
            colourAlpha=normalAlpha*d.parameters[0].z;
            // The shipped graph unpacks its normal twice; keep its math, including the second alpha multiply.
            const float4 raw=decal_texture(d.textures.y,d.samplers[1],d.samplerBias[1],uv,gx,gy,float4(0.5,0.5,1,1));
            tangentNormal=decal_unpack_normal(float4((raw.r*raw.a*2.0-1.0),raw.g*2.0-1.0,0,raw.a));
        }

        float emissionOpacity=1.0;
        if (d.kind.x==2u) {
            // TG/Decal/Decal_Wet: round footprint times three octaves of the graph's value noise.
            const float edge=uv.x*(1.0-uv.x)*uv.y*(1.0-uv.y);
            const float round=saturate(edge*edge*d.parameters[0].x)*0.7;
            const float2 at=uv*d.parameters[0].y;
            const float noise=decal_noise(at)*0.125+decal_noise(at*0.5)*0.25+decal_noise(at*0.25)*0.5;
            normalAlpha=finishAlpha=fade*round*saturate(pow(max(noise,1e-30),d.parameters[0].z)*d.parameters[0].w);
            tangentNormal=decal_unpack_normal(decal_texture(d.textures.y,d.samplers[1],d.samplerBias[1],uv*d.parameters[1].z+d.parameters[1].xy,
                gx*d.parameters[1].z,gy*d.parameters[1].z,float4(0.5,0.5,1,1)));
        }
        if (d.kind.x==3u) {
            // TG/Decal/Decal_AlphaErosion: smoothstep about 1-intensity, independent of the tint's alpha.
            const float alpha=decal_texture(d.textures.x,d.samplers[0],d.samplerBias[0],uv,gx,gy,1.0).a;
            float eroded=saturate((alpha-(1.0-d.parameters[0].x-d.parameters[0].y))/max(2.0*d.parameters[0].y,1e-8));
            eroded=eroded*eroded*(3.0-2.0*eroded);
            colourAlpha=normalAlpha=finishAlpha=fade*eroded;
            emissionOpacity=eroded*exp2(decal_texture(d.textures.w,d.samplers[3],d.samplerBias[3],uv,gx,gy,1.0).a-3.0);
        }

        if (d.kind.x==4u) {
            // TG/Decal/Decal_Wet_Ocean: animated wet colour and the graph's bent normal.
            const float2 maskUv=float2(uv.x,uv.y*d.parameters[0].w);
            const float4 waterMask=decal_texture(d.textures.x,d.samplers[0],d.samplerBias[0],maskUv,float2(gx.x,gx.y*d.parameters[0].w),float2(gy.x,gy.y*d.parameters[0].w),1.0);
            colour=waterMask*d.color;
            const float2 centred=uv-0.5;
            const float2 rotated=float2(dot(centred,float2(0.0707372,0.997495)),dot(centred,float2(-0.997495,0.0707372)))+0.5;
            const float wave=saturate(decal_three_panners(d.textures.w,d.samplers[3],rotated*d.parameters[1].xy,d.parameters[0].xy)*waterMask.a);
            tangentNormal=float3(0.0,wave,1.0-wave);
            colourAlpha=finishAlpha=fade*colour.a;
            normalAlpha=fade*waterMask.a*lerp(1.0,d.color.a,d.parameters[0].z);
            finish.z=(1.0-wave*3.0)*d.remapMin.z;
        }
        if (d.kind.x==5u) {
            // Decal_Lit_EmissionPanning's glowing-looking colour is albedo in its shipped DBuffer pass.
            const float noise=decal_three_panners(d.textures.w,d.samplers[3],uv,d.parameters[0].xy)*d.parameters[0].z;
            colour.rgb+=noise*d.parameters[1].rgb*max(colour.rgb,d.parameters[0].w);
            colourAlpha=normalAlpha=finishAlpha=colour.a*fade*d.parameters[1].w;
            finish.z=mask.a*d.remapMin.z;
        }
        if (d.kind.x==6u) {
            // The shipped blood graph uses base red for coverage, and Animator dries both its colour and normal.
            const float alpha=decal_texture(d.textures.x,d.samplers[0],d.samplerBias[0],uv,gx,gy,1.0).r;
            float coverage=saturate(alpha/max(0.01+1.99*d.parameters[0].x,1e-8));
            coverage=coverage*coverage*(3.0-2.0*coverage);
            colour.rgb*=d.parameters[0].y*(1.0-saturate(3.0*d.parameters[0].x));
            colourAlpha=normalAlpha=finishAlpha=fade*coverage;
        }
        bool graphNormal=false; float3 graphEmission=0.0;
        if(d.kind.x>=7u) {
            DecalGraphInput at=input;
            // Parallax follows this incoming ray, including a reflection.
            if(d.kind.x==12u || d.kind.x==15u || d.kind.x==16u) at.position=h.rayDirection*max(length(h.position-h.rayOrigin),1e-4);
            DecalGraphOutput paint; decal_graph(d,at,paint,graphEmission);
            colour=paint.color; colourAlpha=colour.a*fade;
            normalAlpha=paint.normal.a*fade; finishAlpha=paint.mask.a*fade;
            finish=paint.mask.rgb; tangentNormal=paint.normal.rgb*2.0-1.0; graphNormal=true;
        }
        if(!graphNormal) {
        tangentNormal.xy*=d.fade.w;
        tangentNormal.z=lerp(1.0,tangentNormal.z,saturate(d.fade.w));
        if (d.kind.x==6u) tangentNormal=lerp(tangentNormal,float3(0,0,1),d.parameters[0].x);
        }
        if ((d.flags & 1u)!=0u) { paint=saturate(lerp(paint,colour.rgb,colourAlpha)); receiverShare*=1.0-colourAlpha; changed=true; }
        if ((d.flags & 2u)!=0u && normalAlpha>0.0) {
            const float3 n=graphNormal ? tangentNormal : float3(dot(d.normalToWorld[0].xyz,tangentNormal),dot(d.normalToWorld[1].xyz,tangentNormal),dot(d.normalToWorld[2].xyz,tangentNormal));
            normal=lerp(normal,n,normalAlpha); normalChanged=true;
        }
        if ((d.flags & 4u)!=0u) { metal=lerp(metal,finish.x,finishAlpha); changed=true; }
        if ((d.flags & 8u)!=0u) s.occlusion=lerp(s.occlusion,finish.y,finishAlpha);
        if ((d.flags & 16u)!=0u) smoothness=lerp(smoothness,finish.z,finishAlpha);
        if ((d.flags & 32u)!=0u) s.emissive+=graphNormal ? graphEmission*fade : d.emissionBlend.rgb*decal_texture(d.textures.w,d.samplers[3],d.samplerBias[3],uv,gx,gy,1.0).rgb*fade*emissionOpacity;
}

bool decal_segment_box(float3 origin,float3 dir,float distance,float3 lo,float3 hi) {
    float near=0.0, far=distance;
    [unroll] for(uint a=0;a<3;++a) {
        if(abs(dir[a])<1e-30) { if(origin[a]<lo[a] || origin[a]>hi[a]) return false; }
        else { const float2 t=float2(lo[a]-origin[a],hi[a]-origin[a])/dir[a]; near=max(near,min(t.x,t.y)); far=min(far,max(t.x,t.y)); }
    }
    return near<=far;
}

DecalGraphInput decal_mesh_input(Hit receiver, inout DecalRecord d, uint primitive,float2 bary,float distance) {
    StructuredBuffer<MeshRecord> meshes=ResourceDescriptorHeap[g.scene0.y];
    const MeshRecord mesh=meshes[d.kind.y];
    const uint3 idx=triangle_indices(mesh,d.kind.z,primitive);
    const float3 weights=float3(1.0-bary.x-bary.y,bary);
    ByteAddressBuffer pb=ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    const float3 p0=asfloat(pb.Load3(idx.x*12)),p1=asfloat(pb.Load3(idx.y*12)),p2=asfloat(pb.Load3(idx.z*12));
    const float3x3 world=float3x3(d.graphWorld[0].xyz,d.graphWorld[1].xyz,d.graphWorld[2].xyz);
    const float3 e1=mul(world,p1-p0), e2=mul(world,p2-p0);
    DecalGraphInput at=(DecalGraphInput)0;
    const float3 position=receiver.rayOrigin+receiver.rayDirection*distance;
    at.position=position-g.cameraPos.xyz; at.color=1.0;
    float3 n=normalize(cross(p1-p0,p2-p0));
    if(mesh.normals!=INVALID) {
        ByteAddressBuffer nb=ResourceDescriptorHeap[NonUniformResourceIndex(mesh.normals)];
        n=unpack_normal(nb.Load(idx.x*4))*weights.x+unpack_normal(nb.Load(idx.y*4))*weights.y+unpack_normal(nb.Load(idx.z*4))*weights.z;
    }
    // inverse-transpose handles non-uniform and reflected object scales.
    at.normal=normalize(mul(n,float3x3(d.toDecal[0].xyz,d.toDecal[1].xyz,d.toDecal[2].xyz)));
    at.tangent=0.0;
    if(mesh.tangents!=INVALID) {
        ByteAddressBuffer tb=ResourceDescriptorHeap[NonUniformResourceIndex(mesh.tangents)];
        const float4 t=asfloat(tb.Load4(idx.x*16))*weights.x+asfloat(tb.Load4(idx.y*16))*weights.y+asfloat(tb.Load4(idx.z*16))*weights.z;
        at.tangent=float4(normalize(mul(world,t.xyz)),t.w*(determinant(world)<0.0 ? -1.0 : 1.0));
    }
    const float3 bitangent=cross(at.normal,at.tangent.xyz)*at.tangent.w;
    d.normalToWorld[0]=float4(at.tangent.x,bitangent.x,at.normal.x,0.0);
    d.normalToWorld[1]=float4(at.tangent.y,bitangent.y,at.normal.y,0.0);
    d.normalToWorld[2]=float4(at.tangent.z,bitangent.z,at.normal.z,0.0);
    if(mesh.decalColors!=INVALID) {
        ByteAddressBuffer cb=ResourceDescriptorHeap[NonUniformResourceIndex(mesh.decalColors)];
        at.color=asfloat(cb.Load4(idx.x*16))*weights.x+asfloat(cb.Load4(idx.y*16))*weights.y+asfloat(cb.Load4(idx.z*16))*weights.z;
    } else if(mesh.colors!=INVALID) {
        ByteAddressBuffer cb=ResourceDescriptorHeap[NonUniformResourceIndex(mesh.colors)];
        at.color=unpack_rgba8(cb.Load(idx.x*4))*weights.x+unpack_rgba8(cb.Load(idx.y*4))*weights.y+unpack_rgba8(cb.Load(idx.z*4))*weights.z;
    }
    if(mesh.uvs!=INVALID) {
        ByteAddressBuffer ub=ResourceDescriptorHeap[NonUniformResourceIndex(mesh.uvs)];
        const float2 t0=asfloat(ub.Load2(idx.x*8)),t1=asfloat(ub.Load2(idx.y*8)),t2=asfloat(ub.Load2(idx.z*8));
        at.uv=t0*weights.x+t1*weights.y+t2*weights.z;
        const float3 face=normalize(cross(e1,e2)); const float c=dot(receiver.rayDirection,face);
        float3 along=receiver.rayDirection-face*c;
        along=dot(along,along)>1e-12 ? normalize(along) : normalize(abs(face.x)<0.9 ? cross(face,float3(1,0,0)) : cross(face,float3(0,1,0)));
        const float footprint=receiver.footprint*(d.graphEnv.w!=0.0 ? 1.0 : distance/max(length(receiver.position-receiver.rayOrigin),1e-12));
        at.dx=along*(footprint/max(abs(c),1e-12)); at.dy=cross(face,along)*footprint;
        const float g00=dot(e1,e1),g01=dot(e1,e2),g11=dot(e2,e2),det=g00*g11-g01*g01;
        if(det>1e-30) {
            float2 ab=float2(dot(at.dx,e1)*g11-dot(at.dx,e2)*g01,dot(at.dx,e2)*g00-dot(at.dx,e1)*g01)/det;
            at.gx=ab.x*(t1-t0)+ab.y*(t2-t0);
            ab=float2(dot(at.dy,e1)*g11-dot(at.dy,e2)*g01,dot(at.dy,e2)*g00-dot(at.dy,e1)*g01)/det;
            at.gy=ab.x*(t1-t0)+ab.y*(t2-t0);
        }
    }
    at.uv=at.uv*d.uvST.xy+d.uvST.zw; at.gx*=d.uvST.xy; at.gy*=d.uvST.xy;
    return at;
}

void apply_decals(Hit h, inout SurfaceMaterial s, float3 base, float metal) {
    if (g.decals.z==0u) return;
#ifdef PT_SPEED_MEASURE
    if (speed_measure(SPEED_M_NO_DECALS)) return;
#endif
    StructuredBuffer<MaterialRecord> materials=ResourceDescriptorHeap[g.scene0.w];
    if ((materials[h.material].flags & MATERIAL_NO_DECALS)!=0u) return; // receiver's _DISABLE_DECALS
    StructuredBuffer<DecalRecord> decals=ResourceDescriptorHeap[g.decals.x];
    StructuredBuffer<DecalNode> nodes=ResourceDescriptorHeap[g.decals.y];
    StructuredBuffer<InstanceRecord> instances=ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes=ResourceDescriptorHeap[g.scene0.y];
    StructuredBuffer<GeometryRecord> geometries=ResourceDescriptorHeap[g.scene0.z];
    const GeometryRecord geo=geometries[meshes[instances[h.instance].mesh].geometryBase+h.geometry];
    const uint receiverLayers=(geo.flags&GEOMETRY_OWN_LAYERS)!=0u ? geo.layers : instances[h.instance].renderingLayerMask;
    // Match the primary hit's smooth-normal fallback before blending a partial decal on a curved receiver.
    const float3 receiverNormal=all(s.normal==h.normal) ? h.rounded : s.normal;
    float3 normal=receiverNormal;
    float smoothness=1.0-s.roughness;
    // The DBuffer accumulates paint independently of its receiver, clamping the stored colour at each draw.
    float3 paint=0.0; float receiverShare=1.0;
    bool changed=false, normalChanged=false;
    const float rayLength=length(h.position-h.rayOrigin);
    // A few floating-point ulps admit a coplanar triangle, not a guessed projection thickness.
    const float rayEnd=rayLength+max(1e-7,rayLength*8.0*1.1920929e-7);
    // The decals a point lies in, in draw order (their index), one at a time: each turn walks the tree (split by place) for
    // the least index after the last one taken among the leaves whose box holds the point (a mesh decal's: whose box the
    // ray's way to the point crosses), pruning a subtree whose indices all lie at or before it, or at or after the best
    // found so far. A mesh decal is then asked for its fragments in its own order before the next decal.
    uint current=INVALID, after=0u, lastPrimitive=0u; bool taken=false, firstPrimitive=true;
    [loop] while (true) {
        if (current==INVALID) {
            uint best=INVALID, node=0u;
            [loop] while (node<g.decals.z) {
                const DecalNode b=nodes[node];
                if ((taken && b.span.y<=after) || b.span.x>=best ||
                    (b.mesh!=0u ? !decal_segment_box(h.rayOrigin,h.rayDirection,rayEnd,b.lo,b.hi) : (any(h.position<b.lo) || any(h.position>b.hi)))) { node=b.next; continue; }
                ++node;
                if (b.decal!=INVALID) best=b.decal; // (after the last taken and before the best: the prune above)
            }
            if (best==INVALID) break;
            current=best; after=best; taken=true; firstPrimitive=true;
        }
        DecalRecord d=decals[current];
        if (d.flags==0u || ((d.flags & 512u)==0u && (d.layerMask & receiverLayers)==0u)) { current=INVALID; continue; }
        if ((materials[h.material].flags & MATERIAL_SEE_THROUGH)!=0u && (d.flags & 256u)==0u) { current=INVALID; continue; }
        DecalGraphInput input; float fade=saturate(d.fade.x*d.emissionBlend.w);
        if((d.flags&1024u)!=0u) {
            RaytracingAccelerationStructure accel=ResourceDescriptorHeap[NonUniformResourceIndex(d.kind.w)];
            RayDesc ray; ray.Origin=h.rayOrigin; ray.Direction=h.rayDirection; ray.TMin=0.0; ray.TMax=rayEnd;
            // Re-query the next primitive in draw order. No overlap array and no truncation.
            {
                uint best=INVALID; float2 bary=0.0; float distance=0.0;
                PT_RAYQUERY(RAY_FLAG_FORCE_NON_OPAQUE) query;
                query.TraceRayInline(accel,RAY_FLAG_NONE,255,ray);
                [loop] while(query.Proceed()) {
                    if(query.CandidateType()!=CANDIDATE_NON_OPAQUE_TRIANGLE || query.CandidateGeometryIndex()!=d.kind.z) continue;
                    const uint primitive=query.CandidatePrimitiveIndex();
                    if((!firstPrimitive && primitive<=lastPrimitive) || primitive>=best) continue;
                    if(((d.flags&2048u)!=0u && !query.CandidateTriangleFrontFace()) || ((d.flags&4096u)!=0u && query.CandidateTriangleFrontFace())) continue;
                    best=primitive; bary=query.CandidateTriangleBarycentrics(); distance=query.CandidateTriangleRayT();
                }
                if(best==INVALID) { firstPrimitive=true; current=INVALID; continue; }
                input=decal_mesh_input(h,d,best,bary,distance);
                lastPrimitive=best; firstPrimitive=false; // (current stays: the same mesh's next fragment on the next turn)
            }
        } else {
        current=INVALID; // (a projector is taken once)
        const float4 pos=float4(h.position,1.0);
        const float3 local=float3(dot(d.toDecal[0],pos),dot(d.toDecal[1],pos),dot(d.toDecal[2],pos));
        if (any(abs(local)>=0.5)) continue;
        const float2 uv=(local.xz+0.5)*d.uvST.xy+d.uvST.zw;
        const float3 dx=h.dpdu*h.uvGradX.x+h.dpdv*h.uvGradX.y;
        const float3 dy=h.dpdu*h.uvGradY.x+h.dpdv*h.uvGradY.y;
        float2 gx=float2(dot(d.toDecal[0].xyz,dx),dot(d.toDecal[2].xyz,dx))*d.uvST.xy;
        float2 gy=float2(dot(d.toDecal[0].xyz,dy),dot(d.toDecal[2].xyz,dy))*d.uvST.xy;
        const float bias=exp2(asfloat(g.hair.w)); gx*=bias; gy*=bias;
        const float3 facing=float3(d.normalToWorld[0].z,d.normalToWorld[1].z,d.normalToWorld[2].z);
        const float c=dot(h.front ? receiverNormal : -receiverNormal,facing);
        const float angle=d.fade.y>0.0 ? saturate(c*(c*c+1.25)*d.fade.y+d.fade.z) : 1.0;
        fade*=angle;
        input.uv=uv; input.gx=gx/bias; input.gy=gy/bias; input.position=h.position-g.cameraPos.xyz;
        input.dx=dx; input.dy=dy; input.normal=facing;
        const float3 tangent=float3(d.normalToWorld[0].x,d.normalToWorld[1].x,d.normalToWorld[2].x);
        const float3 bitangent=float3(d.normalToWorld[0].y,d.normalToWorld[1].y,d.normalToWorld[2].y);
        input.tangent=float4(tangent,dot(cross(facing,tangent),bitangent)<0.0 ? -1.0 : 1.0); input.color=1.0;
        }
        blend_decal(h,d,input,fade,s,metal,normal,smoothness,paint,receiverShare,changed,normalChanged);
    }
    // DBuffer normals blend linearly and normalize once when the receiver reads them.
    if (normalChanged && dot(normal,normal)>1e-12) s.normal=normalize(normal);
    s.roughness=1.0-saturate(smoothness);
    if (changed) { base=saturate(base*receiverShare+paint); s.albedo=base*(1.0-metal); s.f0=lerp(0.04,base,metal); s.reflectance=base; }
}

// What a ready program replaces of HDRP Lit's own reading, which hit_material then does not read (PT-73 c: its color, normal
// and mask maps were read at every hit and thrown away, three reads of the game's textures): all of it where material_program
// assigns o whole (program_whole), the color alone for grass and leaves (program_colors). A program that comes to use what o
// holds on entry must leave these lists.
bool program_whole(Program p) {
    return p.ready && (p.kind == PROGRAM_ARCHITECTURE || p.kind == PROGRAM_CLIFF || p.kind == PROGRAM_TRUNK || p.kind == PROGRAM_TERRAIN || p.kind == PROGRAM_TRIPLANAR);
}
bool program_colors(Program p) { return program_whole(p) || (p.ready && (p.kind == PROGRAM_GRASS || p.kind == PROGRAM_LEAVES)); }
#ifdef PT_SPEED_MEASURE
// The measuring build alone (SPEED_M_NO_MATERIAL): a hit's material from its record's own numbers, with no texture, no
// program of the game's, no wet ground and no decal read: what those reads cost is the stage's time with and without.
SurfaceMaterial speed_plain_material(Hit h, MaterialRecord m) {
    SurfaceMaterial s = (SurfaceMaterial)0;
    const float3 base = saturate(m.baseColor.rgb);
    const float2 finish = saturate(float2(m.metallic, m.smoothness));
    s.normal = h.normal; s.occlusion = 1.0;
    s.subsurface = (m.flags & MATERIAL_SUBSURFACE) != 0 ? saturate(m.subsurfaceMask) : 0.0;
    s.albedo = base * (1.0 - finish.x); s.f0 = lerp(0.04, base, finish.x); s.roughness = 1.0 - finish.y; s.reflectance = base;
    s.through = (m.flags & MATERIAL_TRANSLUCENT) != 0 ? TRANSMISSION : (m.flags & (MATERIAL_THIN_CLOTH | MATERIAL_STRANDS)) != 0 ? CLOTH_TRANSMISSION : 0.0;
    s.tint = 1.0;
    s.emissive = h.front || (m.flags & MATERIAL_DOUBLE_SIDED) != 0 ? m.emissive : 0.0;
    return s;
}
#endif
#ifdef PT_RAIN_WETNESS
// ---------------------------------------------------------------- the rain's wetness (PT-108, PT-104; ON TRIAL)
// When it rains the game makes what stands under the open sky wet with one pass over the screen (its ScreenSpaceWetness,
// read from its compiled shader: docs/agents/rain-1.md), which rewrites the roughness HDRP lights its G-buffer with and
// nothing else: no normal, no colour. That pass sees only what is on screen; here the same roughness is made at a hit,
// from the hit's own place in the world, so the camera's ray, a mirror's, a bounce's and a light's all meet the same wet
// ground ("a mirror must show the world as the camera sees it"). The game's numbers, each frame (g.rain, TGPT_SetRain):
//   noise     its noise picture laid over the world, a tile per 10 m of x and z, its red channel
//   soaked    how smooth a soaked surface is: g.rain.y at the camera, fading over 100 m of distance from the camera to
//             0.75 x noise (the camera's distance, whatever ray asks: the mirror shows what the camera would see there)
//   puddle    smoothstep of 5 x (1.2 x moisture - noise), on what faces up (the shading normal's y to the fifth)
//   wet       clamp(clamp(puddle + moisture) - 0.33 x the game's second reading of the noise)
//   smoothness + wet x (soaked - smoothness): a blend both ways, so a surface smoother than soaked gets rougher
// The second reading is laid over the SCREEN in the game (the screen shows one tenth of the picture, always the same
// part): a blotch that stays on the glass as the camera turns. Nothing here may hang on the screen, so the mean of that
// part stands for it (g.rain.z, measured by the plugin from the game's texture): the one place this differs from the
// game's pass, by about 0.14 of `wet` at most with the game's texture as it is (that part runs from 0.03 to 0.74).
// NOT BUILT: the game's shelter (its top-down height array: dry from 5 m under a roof). Every hit is under the open sky.
bool rain_on() { return g.rain.x > 0.0 && asuint(g.rain.w) != INVALID && g.textures.x != INVALID; }
float rain_roughness(Hit h, float3 normal, float roughness) {
    const float2 world = hit_world(h).xz;
    Texture2D<float4> picture = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + asuint(g.rain.w))];
    const float cloud = picture.SampleGrad(s_linearRepeat, world * 0.1, h.footX.xz * 0.1, h.footY.xz * 0.1).r;
    const float moisture = g.rain.x;
    const float away = min(length(h.position - g.cameraPos.xyz) * 0.01, 1.0);
    const float soaked = saturate(g.rain.y + away * (0.75 * cloud - g.rain.y));
    float puddle = saturate(5.0 * (1.2 * moisture - cloud));
    puddle = puddle * puddle * (3.0 - 2.0 * puddle);
    const float up = saturate(normal.y);
    const float wet = saturate(saturate(puddle * (up * up * up * up * up) + moisture) - 0.33 * g.rain.z);
    const float smoothness = 1.0 - roughness;
    return saturate(1.0 - (smoothness + wet * (soaked - smoothness)));
}
#endif
SurfaceMaterial hit_material(Hit h) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    MaterialRecord m = materials[h.material];
#ifdef PT_SPEED_MEASURE
    if (speed_measure(SPEED_M_NO_MATERIAL)) return speed_plain_material(h, m);
#endif
    SurfaceMaterial s;
    float3 base = m.baseColor.rgb;
    const float2 st = h.uv * m.uvST.xy + m.uvST.zw;
    float2 gx, gy; material_gradients(h, m, gx, gy);
    const Program program = program_of(m);
    const bool whole = program_whole(program);
    if (!program_colors(program)) base *= material_texture(m, st, gx, gy).rgb;
    if (m.flags & MATERIAL_VERTEX_COLOR) base *= h.color.rgb;
    s.normal = h.normal;
    if (!whole) s.normal = material_normal(h, m, st, gx, gy, h.normal);
    // ON TRIAL (PT-92): a character's hit whose material laid no normal map on it (material_normal hands back the triangle's
    // own then) is shaded by its smooth normal where its skinned normals reach the tracer (Hit.rounded: fetch_hit; the
    // triangle's own until they do, so nothing changes without them), kept on the ray's side as a normal-mapped one is
    // (frame_normal): what a mirror or a bounce meets of a character is then as round as what the camera sees of it
    // (write_traced takes Hit.rounded there). (8: INSTANCE_CHARACTER.)
    if (all(s.normal == h.normal) && any(h.rounded != h.normal)) {
        StructuredBuffer<InstanceRecord> hitInstances = ResourceDescriptorHeap[g.scene0.x];
        if ((hitInstances[h.instance].flags & 8u) != 0u) {
            const float c = dot(h.rounded, h.normal);
            s.normal = c >= 0.05 ? h.rounded : normalize(h.rounded + h.normal * (0.05 - c));
        }
    }
    // HDRP Lit's metallic workflow, as the game's GBuffer passes compute it: a metal's base color is its specular color, and
    // it has no diffuse color (diffuse baseColor * (1 - metallic), fresnel0 lerp(0.04, baseColor, metallic)). A path vertex
    // is shaded diffusely and can't follow a metal's reflection, so it sends the reflected light on as diffuse light of the
    // same energy: a metal's reflectance, its specular color (averaged over directions, about fresnel0), is its base color,
    // as a dielectric's diffuse color is. With its diffuse color alone a metal bounced no light and was black in a
    // reflection or behind glass (armour, pots, weapons); before mask maps, a metal kept a fifth of its color there.
    float3 finishAo = float3(0.0, 0.0, 1.0);
    if (!whole) finishAo = saturate(material_finish(m, st, gx, gy));
    float2 finish = finishAo.xy;
    s.occlusion = finishAo.z;
    // The game's own shader for it, where the tracer has one: its color, finish, occlusion and normal in place of these.
    MaterialPoint made;
    made.color = base; made.metallic = finish.x; made.smoothness = finish.y; made.occlusion = s.occlusion; made.normal = s.normal;
    g_pointSubsurface = -1.0;
    if (material_program(h, m, program, st, gx, gy, made)) {
        base = made.color; finish = saturate(float2(made.metallic, made.smoothness)); s.occlusion = saturate(made.occlusion); s.normal = made.normal;
    }
    // The share of its diffuse light that scatters under the surface here (PT-40): the game's subsurface mask, its
    // material's number or what its shader computes at the point; none for a material light does not walk inside.
    s.subsurface = (m.flags & MATERIAL_SUBSURFACE) != 0 ? saturate(g_pointSubsurface >= 0.0 ? g_pointSubsurface : m.subsurfaceMask) : 0.0;
    const float3 unlit = max(base, 0.0);
    base = saturate(base);
    s.albedo = base * (1.0 - finish.x);
    s.f0 = lerp(0.04, base, finish.x);
    s.roughness = 1.0 - finish.y;
    s.reflectance = base;
    // Hair's card (MATERIAL_STRANDS) is lit through from behind where a path, a bounce or a mirror's ray meets it, as the
    // camera's own pixel of it is (pt_primary write_hair, pt_prepare: CLOTH_TRANSMISSION, near RealHair's own
    // _TransmittanceMultiplier of 0.33). With none (until 2026-10-08), the underside of a lit card was black to every ray but
    // the camera's: the hair and the scalp under the top cards took no light through them, and showed as dark lines between
    // the strands of the cards above (run 20261007-224654, hair-moving).
    s.through = (m.flags & MATERIAL_TRANSLUCENT) != 0 ? TRANSMISSION : (m.flags & (MATERIAL_THIN_CLOTH | MATERIAL_STRANDS)) != 0 ? CLOTH_TRANSMISSION : 0.0;
    // A leaf's or a blade's own share where the plugin read it from the material (thin_through: HDRP's transmittance of its
    // diffusion profile at its thickness, tinted, times its transmission mask), in place of the grey half.
    s.tint = 1.0;
    float3 own;
    if (thin_through(m, own)) { s.through = max(luminance(own), 1e-4); s.tint = own / s.through; }
    // Emission from the front face only, unless the material is double-sided: HDRP culls a single-sided surface's back, so
    // nothing ever sees it glow from there.
    s.emissive = h.front || (m.flags & MATERIAL_DOUBLE_SIDED) != 0 ? material_emission(m, st) : 0.0;
    // Wet where HDRP's waves have been (PT-52): one store and one place for every ray, so a mirror, the water's reflection
    // and bounced light see the wet sand the camera sees, whose own ray's hit is read here too (pt_primary write_traced,
    // PT-59). The film's share chosen by a number of the point and frame.
    // The ground and what stands on it: not a material light walks inside (skin, wax: its color is the walk's medium), an
    // eye, glass or hair.
    if (!g_hitInside && wet_on() && h.position.y <= g.wetGrid.z) {
        float3 state = 0.0;
        if ((m.flags & (MATERIAL_SUBSURFACE | MATERIAL_GLASS | MATERIAL_EYE | MATERIAL_CAMERA_ONLY | MATERIAL_STRANDS | MATERIAL_HAIR_LAYER)) == 0)
            state = wet_state(h.position, h.normal);
        if (state.y > 0.0) {
            const float u = float(pcg(asuint(h.position.x) ^ pcg(asuint(h.position.z) ^ pcg(g.size.z ^ 0x5EED3u)))) * (1.0 / 4294967296.0);
            wet_material(state, u, s.albedo, s.reflectance, s.f0, s.roughness);
            base = s.reflectance; // partial decal paint blends over the wet receiver, not its earlier dry colour
        }
    }
    if (m.surfaceModel == 1u) {
        s.emissive += unlit;
        s.albedo = 0.0; s.reflectance = 0.0; s.f0 = 0.0; s.roughness = 1.0;
    }
    if ((m.flags & (MATERIAL_EYE | MATERIAL_GLASS)) != 0)
        s.normal = material_normal(h, m, st, gx, gy, h.rounded);

    // Apply once, after programs and wet ground: every ray sees decals on the finished receiver.
    apply_decals(h, s, base, finish.x);
#ifdef PT_RAIN_WETNESS
    // The rain, last, as the game's pass comes after its G-buffer and its decals: on what HDRP lights from the G-buffer.
    // Not on what it shades forward with a shine of its own (glass, eyes, hair) nor on an unlit surface.
    if (!g_hitInside && rain_on() && m.surfaceModel != 1u &&
        (m.flags & (MATERIAL_GLASS | MATERIAL_EYE | MATERIAL_CAMERA_ONLY | MATERIAL_STRANDS | MATERIAL_HAIR_LAYER)) == 0)
        s.roughness = rain_roughness(h, s.normal, s.roughness);
#endif
    return s;
}

// A smooth normal (Hit.rounded) turned to face the viewer where the curve it describes passes the outline of the flat
// triangle it lies on (toViewer: unit, from the surface): at least about 3 degrees to the viewer's side, so the surface
// reflects, refracts and is lit there.
float3 facing_normal(float3 n, float3 toViewer) {
    const float c = dot(n, toViewer);
    return c >= 0.05 ? n : normalize(n + toViewer * (0.05 - c));
}

static const uint WATER_ROWS = 15;
float4 water_row(uint slot, uint row) { return g.water[slot * WATER_ROWS + row]; }
bool water_slot(uint slot) { return water_row(slot, 12).w > 0.5; }
#include "pt_waves.hlsli"
#include "pt_interface.hlsli"
#include "pt_water_surface.hlsli"
// ---------------------------------------------------------------- traversal
// A ray's own number for the glass it crosses: its origin, direction and the frame, and g_raySalt (0 but for a ray that must
// decide apart from the ray that found what it checks: pt_gi_spatial's).
static uint g_raySalt = 0;
void ray_seed(float3 origin, float3 dir) {
    g_rayOrigin = origin;
    g_raySeed = pcg(asuint(origin.x) ^ pcg(asuint(origin.y) ^ pcg(asuint(origin.z) ^ pcg(asuint(dir.x) ^ pcg(asuint(dir.y) ^ pcg(g.size.z ^ g_raySalt))))));
}
// The nearest hit, as it is: the hero's first-person body is met from both sides here (a gap, docs/agents/mirror-2.md).
// The rule that passes its inside (first_person_inside) lives in trace_from_surface alone: in this function too, as a
// second query from behind what it met, the reflections pass (pt_specular, with the mirror job's water and fog code) hung the
// GPU on its first frame on the RTX 5080 (driver 32.0.16.1692), the same with the query unrolled, declared once, or run
// once; this function as it is, with all of the rest, ran (2026-10-07, the mirror-2 agent's experiments). So the rays
// traced here (bounced light's further rays, the radiance cache's, what is seen through glass) still meet the inside of
// the back of the hero's head. Its lasting fix: the body's instances made non-opaque (scene.cpp) and its back faces left
// uncommitted in the query's candidate loop, with no second query.
bool trace_closest(float3 origin, float3 dir, float tmin, float tmax, out Hit hit) {
    ray_seed(origin, dir);
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = tmin; ray.TMax = tmax;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, water_mask(MASK_ALL), ray);
    while (q.Proceed()) {
        if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
        g_candidateT = q.CandidateTriangleRayT();
        if (alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) { hit = (Hit)0; return false; }
    float t = q.CommittedRayT();
    g_hitToWorld = q.CommittedObjectToWorld3x4();
    hit = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                    g_hitToWorld, dir, origin + dir * t, origin);
    return true;
}
// The world directions in which a hit triangle's texture coordinates u and v grow (dP/du, dP/dv): the tangent frame of the
// mesh's own coordinates (Unity's, for coordinates that aren't mirrored: its tangent along u, its bitangent along v), which
// hair's flow map is in. False where the triangle has no coordinates or they don't span it. For trace_closest's last hit.
bool uv_frame(Hit hit, out float3 dpdu, out float3 dpdv) {
    dpdu = 0.0; dpdv = 0.0;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    const MeshRecord mesh = meshes[instances[hit.instance].mesh];
    if (mesh.uvs == INVALID) return false;
    const uint3 idx = triangle_indices(mesh, hit.geometry, hit.primitive);
    ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    ByteAddressBuffer ub = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.uvs)];
    const float3 w0 = mul(g_hitToWorld, float4(asfloat(pb.Load3(idx.x * 12)), 1.0));
    const float3 w1 = mul(g_hitToWorld, float4(asfloat(pb.Load3(idx.y * 12)), 1.0));
    const float3 w2 = mul(g_hitToWorld, float4(asfloat(pb.Load3(idx.z * 12)), 1.0));
    const float2 t0 = asfloat(ub.Load2(idx.x * 8)), t1 = asfloat(ub.Load2(idx.y * 8)), t2 = asfloat(ub.Load2(idx.z * 8));
    const float3 e1 = w1 - w0, e2 = w2 - w0;
    const float2 d1 = t1 - t0, d2 = t2 - t0;
    const float det = d1.x * d2.y - d2.x * d1.y;
    if (!(abs(det) > 1e-20)) return false;
    dpdu = (e1 * d2.y - e2 * d1.y) / det;
    dpdv = (e2 * d1.x - e1 * d2.x) / det;
    return all(isfinite(dpdu)) && all(isfinite(dpdv));
}
// A light's ray (g_lightRay) through leaves and grass: their covered micromap pieces are opaque to every other ray (pt_omm_bake),
// but a light's ray passes a leaf or a blade by chance where it covers (alpha_opaque: TRANSMISSION), so it meets the instances
// holding them (MASK_FOLIAGE, and what the wind sways where it stands still: MASK_SWAYS, whose mesh carries the same micromaps)
// in a pass of their own, every triangle forced to the test: the same answer, one chance at each crossing, as one pass gave
// before round 20. A bare piece is still left out by its micromap. mask: the ray's mask; the foliage of it is met here.
bool foliage_blocks(float3 origin, float3 dir, float tmin, float tmax, uint mask) {
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = tmin; ray.TMax = tmax;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) f;
    f.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, mask, ray);
    while (f.Proceed()) {
        if (f.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            alpha_opaque(f.CandidateInstanceID(), f.CandidateGeometryIndex(), f.CandidatePrimitiveIndex(), f.CandidateTriangleBarycentrics()))
            f.CommitNonOpaqueTriangleHit();
    }
    return f.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}
// A straight ray's any blocker between tmin and tmax, everything in mask; a light's ray meets the foliage of it on its own.
bool any_blocks(float3 origin, float3 dir, float tmin, float tmax, uint mask) {
    const uint foliage = g_lightRay ? (mask & (MASK_FOLIAGE | MASK_SWAYS)) : 0u;
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = tmin; ray.TMax = tmax;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, mask & ~foliage, ray);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) return true;
    return foliage != 0u && foliage_blocks(origin, dir, tmin, tmax, foliage);
}
bool occluded_any(float3 origin, float3 dir, float tmin, float tmax) {
    // The water's surface stops a straight ray like anything else (MASK_ALL holds it): light crosses it bent, never straight.
    // (With the Caustics row off nothing traces the bent light, and the row's "off" is the straight light: light_mask.)
    return any_blocks(origin, dir, tmin, tmax, light_mask(MASK_ALL));
}
bool occluded(float3 origin, float3 dir, float tmin, float tmax) {
    ray_seed(origin, dir);
    return occluded_any(origin, dir, tmin, tmax);
}
// HDRP Lit's transmission features (2, and 3 with subsurface scattering) through a diffusion profile the managed side
// marks solid (settings.x, a bit per profile: skin, thick objects), where transmission otherwise means a thin surface lit
// through from behind. The profile is in GBuffer2's blue channel with the transmission mask (HDRP's PackFloatInt8bit with
// 16 profiles), except for colored transmission (bit 3 of its alpha), whose blue holds the color: always thin.
bool solid_transmission(float4 gb2, uint feature) {
    if (feature != 2u && feature != 3u) return false;
    if ((uint(gb2.a * 255.0 + 0.5) & 8u) != 0) return false;
    uint profile = uint(gb2.b / (16.0 / 255.0) + 1.0 / 255.0) & 15u;
    return ((g.settings.x >> profile) & 1u) != 0;
}
// A leaf's or a blade of grass's diffusion profile (HDRP Lit's transmission features through a thin profile, as pt_prepare's
// translucent pixels are), -1 where GBuffer2 holds none: colored transmission (bit 3 of its alpha) keeps its color in blue.
int leaf_profile(float4 gb2, uint feature) {
    if (feature != 2u && feature != 3u) return -1;
    if ((uint(gb2.a * 255.0 + 0.5) & 8u) != 0) return -1;
    return int(uint(gb2.b / (16.0 / 255.0) + 1.0 / 255.0) & 15u);
}
// Its mirror reflectance as HDRP gives it (FillMaterialTransmission: its profile's Fresnel0, TGPT_SetProfileReflectance), 0 where
// the plugin sent none.
float profile_f0(int profile) { return profile >= 0 ? g.profileF0[uint(profile) >> 2u][uint(profile) & 3u] : 0.0; }
// The light from behind a leaf or a blade of grass (s.through: the tracer's grey share it lights them with, which ReSTIR weighs
// its samples by) as its own material lets it through, where the pixel's surface is what the tracer's own camera ray met
// (PT-59): the share of the material the ray stopped at, as a path that meets the leaf has it (thin_through: HDRP's
// transmittance of its diffusion profile at the material's thickness, tinted, times its transmission mask; the plugin's
// reading, one number a material: its thickness map's texel is not read at a hit yet). For the game's foliage that is 1.4 to
// 6 times the light the grey half let through, yellow-green: trees came out dull and grey-green with the half
// (forest-pine-needles: saturation 0.10 traced, 0.16 in the game's).
// leaf_transmittance: that share itself, and whether the pixel has one. False where the plugin gave none, on thin cloth and
// hair, and on the route before the tracer's own rays (HDRP's GBuffer2 is no longer asked: PT-58): the grey half then.
// This frame's surface only.
bool leaf_transmittance(uint2 p, Surface s, out float3 t) {
    t = 0.0;
    uint material;
    if (!s.translucent || !s.deferred || s.strands || !(s.through > CLOTH_TRANSMISSION + 0.01) || !own_material(p, material)) return false;
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    return thin_through(materials[material], t);
}
float3 leaf_tint(uint2 p, Surface s) {
    float3 t;
    return leaf_transmittance(p, s, t) ? t / s.through : 1.0;
}
// The cosine a light along l lights such a leaf with, as HDRP's Lit does (EvaluateBSDF's diffR and diffT): its front by the
// plain cosine, and through it from behind by its transmittance times HDRP's wrapped cosine (ComputeWrappedDiffuseLighting
// of -NdotL with TRANSMISSION_WRAP_LIGHT, 15 degrees: (w - NdotL) / (1 + w)^2). Over all directions that is as much light as
// the plain cosine from behind gives, laid out otherwise: 0.795 straight from behind (the plain cosine's 1 made the blades
// with the sun right behind them a quarter brighter than the game's), and some light for a blade edge-on to the light.
float3 leaf_cos(float3 normal, float3 l, float3 transmittance) {
    const float w = 0.258819; // cos(90 - 15 degrees)
    const float c = dot(normal, l);
    return saturate(c) + transmittance * saturate((w - c) / ((1.0 + w) * (1.0 + w)));
}
// Whether a surface gathers bounced and sky light from behind as well (pt_gi, pt_gi_spatial, pt_rtxdi), as HDRP adds the far
// side's light times a leaf's transmittance (Lit's ModifyBakedDiffuseLighting): a leaf or a blade of grass. And thin cloth
// (a deferred pixel the camera ray found as cloth), by the share it lets through: under an open sky a canopy's underside
// glows in its own color (the composite's albedo), where it was black whenever no sun or lamp stood behind it (PT-39). Not
// hair (drawn forward), whose look was set without it (hair's bounced-light add made blonde hair glow).
bool gathers_behind(Surface s) { return s.translucent && (s.through > CLOTH_TRANSMISSION + 0.01 || !s.forward); }
// Rays leave a surface a little above it: raster and traced geometry can differ in LOD and precision.
float ray_offset(float distanceFromCamera) { return 0.02 + distanceFromCamera * 0.002; }

// Rays from HDRP's surface can start inside the traced mesh where the two differ by more than that offset: an
// arch drawn as a smooth curve over the flat triangles it is traced with (tessellation), displacement, a depth
// offset. The first thing such a ray meets is the inside of that mesh, a back face, close by, and every ray from
// there finds no light: black patches in the shape of the traced triangles. A back face that near is passed
// through, the ray going on from just beyond it. Farther away, or on an alpha-tested (two-sided) material, it is a
// real hit. The material decides, not the object: a character's hair or a building's ivy made every back face of
// the object a real hit, and on a face, where the sun's shadow ray leaves the smooth drawn surface into its flatter
// triangles at the terminator, the inside of the head stopped it (hard-edged shadows across faces). Front faces
// are the side a mesh's triangles face in its own space (Unity's clockwise winding, which DXR's default matches),
// so mirrored instances keep theirs. Only for rays from the G-buffer's surfaces: paths continue from traced hits,
// which lie on the traced mesh.
// A stand-in (a shadow-only mesh the game casts a building's shadows with, mostly the drawn geometry but coarser
// on curves: arches, vaults) is passed through within three times that distance whichever way it faces: the drawn
// surface can lie inside or behind it there. Farther away it shadows and bounces light like any mesh.
#define INSTANCE_STAND_IN 4
#define INSTANCE_CHARACTER 8
#define INSTANCE_SWAYS 32 // every material of it sways (MASK_SWAYS)
// What the wind moves (TGPT_MATERIAL_SWAYS: grass, leaves, a banner, laundry, as HDRP's shaders bend them) is traced where it
// stands still, and near a surface HDRP drew the two need not agree: a ray from a blade of grass met the blade's own still copy
// beside it, a strip of laundry the strip's (its inner side black), and the ground the drawn grass swayed away from lay in the
// still grass's shadow (black spots swaying with the grass). Rays from HDRP's surfaces meet an instance all of whose
// materials the wind moves (its own instance mask, scene.cpp) only from SWAY_REACH on; paths go on from traced hits, which
// lie on the still meshes, and see all of it. (MASK_STILL and MASK_SWAYS: with the other masks above.)
static const float SWAY_REACH = 0.2;
bool scene_sways() { return (uint(g.radiance.w + 0.5) & 1u) != 0; }
bool scene_cloth() { return (uint(g.radiance.w + 0.5) & 2u) != 0; } // any TGPT_MATERIAL_THIN_CLOTH in it
// The nearest see-through glass (TGPT_MATERIAL_SEE_THROUGH) a camera ray meets before tmax: a ray that meets only the
// instances holding such glass (their mask bit), every candidate tested so that only the glass itself stops it (not a
// jar's lid, which HDRP draws into the GBuffer).
float near_back(float distanceFromCamera) { return 0.15 + distanceFromCamera * 0.01; }
// Where a surface's rays leave from and which back faces they pass: on the traced mesh itself (Surface.onMesh: a character's
// skin on its own triangle, an eye) a millimetre above that triangle (and a fiftieth of a percent of the distance, for the
// precision of positions far from the scene's origin; the interiors float 500 m up), every back face a real hit; elsewhere
// HDRP's surface may lie off the traced mesh (ray_offset, near_back). A face's lids, brow and nose stand a few millimetres to
// two centimetres off the skin and the eye under them: rays that started ray_offset's 2 cm out passed them all, and the eye
// was lit edge to edge, the sockets as the cheeks (PT-43, PT-44). Nothing else is near: a ray above its own flat triangle
// meets a neighbour only where the surface bends towards it (a socket, a fold), and the eye is seen by camera rays alone.
float surface_offset(Surface s) { return s.onMesh ? 0.001 + s.distance * 0.0002 : ray_offset(s.distance); }
float surface_near_back(Surface s) { return s.onMesh ? 0.0 : near_back(s.distance); }
// How near the surface a ray to a lamp passes through back faces (passes_through's nearBack). From a surface lit through
// from behind (its ray starts on the light's side, inside whatever it belongs to) only as far as a thin thing is thick
// (THIN_THROUGH, 1 cm): HDRP's thin transmission is for leaves and cloth, and with near_back's 15 cm a candle's flame lit
// its own wax's sides through the wax's top, 3 to 12 cm of it (lights round 2: the tavern's candles white-hot). Otherwise
// near_back: the traced mesh's level of detail may differ from what HDRP drew there.
static const float THIN_THROUGH = 0.01;
float near_back_towards(Surface s, float3 dir) {
    const bool lightSide = s.translucent && ((g.settings2.y & OPTION_FACETED) != 0 || dot(s.normal, dir) < 0.0) && dot(s.geoNormal, dir) < 0.0;
    // A character's skin and eyes on their own mesh pass no back face at all (surface_near_back, the face work).
    if (s.onMesh) return surface_near_back(s);
    return lightSide ? THIN_THROUGH : near_back(s.distance);
}
bool passes_through(uint instanceId, uint geometry, bool frontFace, float t, float nearBack) {
    if (t >= nearBack * 3.0 || (g.settings2.y & OPTION_NO_PASS_THROUGH) != 0) return false;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    InstanceRecord inst = instances[instanceId];
    if (inst.flags & INSTANCE_STAND_IN) return true;
    // The water's surface is never the inside of the thing a ray leaves: met from below near a shore, it stops the ray as
    // it does from above (a floor 10 cm under a wave's edge lost that wave's shadow: tests/water_surface_gpu.inl).
    if (frontFace || t >= nearBack || (inst.flags & INSTANCE_WAVES) != 0) return false;
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    return (materials[materialIndices[inst.materialBase + geometry]].flags & MATERIAL_ALPHA_TEST) == 0;
}
// The hero's first-person body (MATERIAL_FIRST_PERSON) met from behind: passed by the rays through trace_from_surface
// (reflections, a reflection in a reflection, what is seen through water, bounced light's first ray), at any distance. Not
// yet by trace_closest's (bounced light's further rays, what is seen through glass): see there. The game loads no head in first person (VHeroController loads its first-person body prefab; the face, hair
// and eyes come only with the third-person one). That body's mesh (by its shape Kandra's Mesh_BodyCutout, the one cut-out
// human body in the game's mesh archive: the hands and forearms, and one piece from the collarbones up) has the face and the
// front of the neck cut out, so that the camera, which sits where the eyes would be, looks out through the hole: what is left
// of the head is the back of the skull and the crown, an open shell whose triangles all face outwards (read from the game's
// mesh data, docs/agents/mirror-2.md). HDRP draws the skin
// one-sided, so from in front (the camera's side, a mirror's side) it shows nothing of it: the game's own view from the
// mirror's side has nothing above the collar. The tracer met the shell's inside there and showed it: a bare, faceless egg
// on the hero in every mirror (the harness's mirror runs, 2026-10-07). A first rule passed it within 30 cm of the camera,
// which took the middle of the shell (20 to 35 cm behind the camera) and left its rim as a bowling pin. Its front faces
// (the back of the head, a hand's skin) are met as before, and shadow rays meet both sides: the hero's shadow keeps its head.
// A material drawn two-sided (MATERIAL_DOUBLE_SIDED) is drawn from behind too, and met.
bool first_person_inside(uint instanceId, uint geometry, bool frontFace) {
    if (frontFace) return false;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    return (materials[materialIndices[instances[instanceId].materialBase + geometry]].flags & (MATERIAL_FIRST_PERSON | MATERIAL_DOUBLE_SIDED)) == MATERIAL_FIRST_PERSON;
}
// (pt_gi.hlsl query_from_surface is this function up to its fetch_hit, for the bounced light's first ray: KEEP THE TWO SIDE
// BY SIDE.)
bool trace_from_surface(float3 origin, float3 dir, float tmin, float tmax, float nearBack, out Hit hit) {
    ray_seed(origin, dir);
    hit = (Hit)0;
    // Everything first. Only where the closest hit is something the wind moves within SWAY_REACH (a blade of grass beside
    // the one drawn there, the grass over the ground) does the ray look again: for what stands still up to SWAY_REACH, and
    // then for everything from there. Rays elsewhere took two queries each (what stands still, then what sways).
    uint mask = MASK_ALL;
    float t0 = tmin;
    for (uint pass = 0; pass < 5 && t0 < tmax; ++pass) {
        const float end = mask == MASK_STILL ? min(tmax, SWAY_REACH) : tmax;
        if (t0 >= end) { mask = MASK_ALL; t0 = max(t0, SWAY_REACH); continue; }
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = end;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        q.TraceRayInline(g_scene, PT_RAY_FLAGS, water_mask(mask), ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            // Nothing that stands still before SWAY_REACH: everything from there.
            if (mask == MASK_STILL) { mask = MASK_ALL; t0 = max(t0, min(tmax, SWAY_REACH)); continue; }
            break;
        }
        const float t = q.CommittedRayT();
#ifdef PT_SPEED_MEASURE // the measuring build alone: the first query's answer, with no second look
        if (speed_measure(SPEED_M_ONE_QUERY)) { hit = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), q.CommittedObjectToWorld3x4(), dir, origin + dir * t, origin); return true; }
#endif
        StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
        if (mask != MASK_STILL && t < SWAY_REACH && (instances[q.CommittedInstanceID()].flags & INSTANCE_SWAYS) != 0) { mask = MASK_STILL; continue; }
        if (pass < 4 && passes_through(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace(), t, nearBack)) { t0 = t + 1e-3; continue; }
        if (pass < 4 && first_person_inside(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace())) { t0 = t + 1e-3; continue; }
        hit = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                        q.CommittedObjectToWorld3x4(), dir, origin + dir * t, origin);
        return true;
    }
    hit = (Hit)0;
    return false;
}
// Near the surface, where a hit may be passed through, the nearest hit decides: with the first hit found accepted,
// a passable one found first moved the ray on past a nearer real blocker (a leak). Beyond that, any hit blocks.
bool occluded_from_surface_rays(float3 origin, float3 dir, float tmin, float tmax, float nearBack) {
    const float nearEnd = min(tmax, nearBack * 3.0);
    float t0 = tmin;
    // What the wind moves blocks from SWAY_REACH on (to nearEnd here, beyond it with everything else).
    const float s0 = max(tmin, SWAY_REACH);
    if (scene_sways() && s0 < nearEnd) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = s0; ray.TMax = nearEnd;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) q;
        // (A light's ray: every triangle to the test, its leaves' covered pieces among them: foliage_blocks.)
        q.TraceRayInline(g_scene, g_lightRay ? RAY_FLAG_FORCE_NON_OPAQUE : RAY_FLAG_NONE, MASK_SWAYS, ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) return true;
    }
    for (uint pass = 0; pass < 3 && t0 < nearEnd; ++pass) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = nearEnd;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        // (A light's ray: every triangle of this short stretch to the test, a leaf's covered pieces among them.)
        q.TraceRayInline(g_scene, g_lightRay ? RAY_FLAG_FORCE_NON_OPAQUE : RAY_FLAG_NONE, light_mask(MASK_STILL), ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        const float t = q.CommittedRayT();
        if (pass < 2 && passes_through(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace(), t, nearBack)) { t0 = t + 1e-3; continue; }
        return true;
    }
    if (nearEnd >= tmax) return false;
    return any_blocks(origin, dir, max(nearEnd, tmin), tmax, light_mask(MASK_ALL));
}
bool occluded_from_surface(float3 origin, float3 dir, float tmin, float tmax, float nearBack) {
    ray_seed(origin, dir);
    return occluded_from_surface_rays(origin, dir, tmin, tmax, nearBack);
}
// The cosine between a ray and the flat face of a triangle it crossed (the triangle's own plane, in the world).
float triangle_slant(uint instanceId, uint geometry, uint primitive, float3x4 toWorld, float3 dir) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    const MeshRecord mesh = meshes[instances[instanceId].mesh];
    const uint3 idx = triangle_indices(mesh, geometry, primitive);
    ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    const float3 w0 = mul(toWorld, float4(asfloat(pb.Load3(idx.x * 12)), 1.0)), w1 = mul(toWorld, float4(asfloat(pb.Load3(idx.y * 12)), 1.0));
    const float3 w2 = mul(toWorld, float4(asfloat(pb.Load3(idx.z * 12)), 1.0));
    const float3 n = cross(w1 - w0, w2 - w0);
    const float len = length(n);
    return len > 1e-12 ? abs(dot(n, dir)) / len : 1.0;
}
// A thin pane's own tint (PT-76): what its material's absorption leaves of a ray over the way through its thickness, slanted
// as the ray is inside it (Snell's law at its index). The thickness is the material's own (HDRP Lit's refraction thickness
// in metres, where its shader refracts: MaterialRecord.opticalReserved's y, a float's bits; 0: none known, no tint).
// cosIncident: between the ray and the pane's face, outside it.
float3 pane_transmittance(MaterialRecord m, float cosIncident) {
    const float thickness = asfloat(m.opticalReserved.y);
    if (!(thickness > 0.0) || !any(m.absorption > 0.0)) return 1.0;
    const float ior = m.ior >= 1.0 ? m.ior : GLASS_IOR;
    const float sine2 = saturate(1.0 - cosIncident * cosIncident) / (ior * ior);
    return segment_transmittance(m.absorption, thickness / sqrt(max(1.0 - sine2, 1e-4)));
}
// The light a shadow ray brings through the glass it crosses, channel by channel: what the glass's own absorption leaves of
// it over its way inside (PT-76: by the distance where the glass closes a volume, a thin pane's by its thickness), so the
// sun through a green bottle reaches the table green, not just dimmer; and what the paint on see-through glass leaves
// uncovered at each crossing (glass_open). The crossings are found in order along the ray, the nearest past the last each
// time, by a query that commits glass alone: the visibility test can't take the product itself, as it may report a
// triangle more than once (the geometry is built without NO_DUPLICATE_ANYHIT_INVOCATION) and its closest-hit part reports
// candidates beyond the hit it keeps. It met glass that many times at most (met), and the ray found nothing else in the
// way: only such rays pay for the walk.
float3 glass_walk(float3 origin, float3 dir, float tmin, float tmax, uint met) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    float3 through = 1.0;
    float t0 = tmin;
    // The glass the ray is inside of on its way (PT-76): from a front face of glass that closes a volume there (glass_solid) to
    // that object's next back face, its material's absorption over the way between them (a back face met first: the ray
    // started inside it). An open sheet is a thin pane at each crossing (pane_transmittance).
    uint insideOf = INVALID; float insideFrom = tmin;
    for (uint crossing = 0; crossing < met && t0 < tmax; ++crossing) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = tmax;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_ALL, ray);
        while (q.Proceed()) {
            if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
            const InstanceRecord inst = instances[q.CandidateInstanceID()];
            if ((materials[materialIndices[inst.materialBase + q.CandidateGeometryIndex()]].flags & MATERIAL_GLASS) != 0) q.CommitNonOpaqueTriangleHit();
            // And thin cloth where its weave covers (alpha_opaque let the light through it there; a hole tints nothing).
            else {
                const MaterialRecord cm = materials[materialIndices[inst.materialBase + q.CandidateGeometryIndex()]];
                if ((cm.flags & MATERIAL_THIN_CLOTH) != 0 && (cm.flags & MATERIAL_ALPHA_TEST) != 0 &&
                    cloth_covers(cm, q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(),
                                 material_alpha(inst, cm, q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics())))
                    q.CommitNonOpaqueTriangleHit();
            }
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        const InstanceRecord inst = instances[q.CommittedInstanceID()];
        const MaterialRecord m = materials[materialIndices[inst.materialBase + q.CommittedGeometryIndex()]];
        // (ON TRIAL, glass_painted: see-through glass's back face the game does not draw carries no paint to stop the light.)
        const float t = q.CommittedRayT();
        if ((m.flags & MATERIAL_GLASS) != 0) {
            if (glass_painted(m, q.CommittedTriangleFrontFace()))
                through *= glass_open(inst, m, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
            if (any(m.absorption > 0.0)) {
                const uint glassId = q.CommittedInstanceID();
                if (!glass_solid(glassId, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex()))
                    through *= pane_transmittance(m, triangle_slant(glassId, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedObjectToWorld3x4(), dir));
                else if (q.CommittedTriangleFrontFace()) { insideOf = glassId; insideFrom = t; }
                else if (insideOf == glassId || crossing == 0) {
                    through *= segment_transmittance(m.absorption, t - (insideOf == glassId ? insideFrom : tmin));
                    insideOf = INVALID;
                }
            }
        }
        else through *= cloth_tint(inst, m, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
        t0 = t + max(1e-4, t * 1e-5);
    }
    return through;
}
// HDRP's water in the caustics' slots (TGPT_SetWaterCaustics): row r of slot s's constants (FrameConstants.water).
// The Caustics row on and water in a slot: light reaches what lies under the water only through its surface (pt_caustics.hlsl).
bool caustics_on() { return (g.settings2.y & OPTION_NO_CAUSTICS) == 0 && (water_slot(0) || water_slot(1) || water_slot(2) || water_slot(3)); }
// A light's straight shadow ray that meets the water's surface stops there (occluded_any: the surface is in the structure): the
// light beyond it came through the waves, bent, and is the caustic's (caustic_light) or a refracted path's, not a straight
// ray's. The caustics' own rays, which start on the surface, pass it (g_passWater). With the Caustics row off every light's
// straight ray passes it (light_mask): that row's "off" is the straight light, for water as for glass.
// What the water takes of a light's straight ray that nothing stopped. The Caustics row on: the surface stops the ray, so
// only a ray that starts in the water has a way through it, and it ends in it: all of the ray (tmax). The row off: the
// surface stops nothing (light_mask), and each part of the ray under water takes its share (water_straight).
float3 water_through(float3 origin, float3 dir, float tmin, float tmax) {
    const int medium = g_waterMediumKnown ? g_waterMedium : water_medium(origin);
    if (water_passes_light()) return water_straight(origin, dir, tmin, tmax, medium);
    return medium >= 0 ? segment_transmittance(water_row(uint(medium), 14).xyz, tmax) : 1.0;
}
// Final shading's shadow rays (a light sample's at a G-buffer surface, the sun's and a light's at path vertices): 0 where
// something stops the ray (the water's surface too: it is in the structure), otherwise the glass's tint along it
// (glass_walk). RTXDI's reuse keeps the binary test, with glass passed by chance.
// Each ray its own number (ray_seed), for what stops it by chance (hair's strands): left from the ray before, the sun's
// shadow ray took the decisions of the pixel's earlier rays (a half-covered hair card's shadow 0.6 as dark, not 0.5).
// ON TRIAL (PT-93, leaf_product_on): with the trial on, a blade of grass or a leaf in the way never stops such a ray by a coin:
// the ray's light is the product of what each lets through (leaf_walk), the coin's mean.
// leaf_walk: that product. Each crossing is met once, in order along the ray, the nearest past the last each time, as
// glass_walk finds glass (the visibility test may report a triangle more than once, so it only counts them: met, at least
// as many as there are). What the wind moves counts from swayFrom on alone, as in the visibility test
// (occluded_from_surface_rays meets such instances from SWAY_REACH on near the surface; 0 for a ray that meets them from
// its start). Two crossings nearer each other than a hundredth of a millimetre along the ray count as one.
// Below LEAF_WALK_FLOOR of the light the walk goes on by chance, as often as keeps the mean (the one coin left: its stake is
// that thousandth of the light, not all of it), so a ray through a tree's crown costs a dozen crossings, not hundreds.
static const float LEAF_WALK_FLOOR = 1.0 / 1024.0;
float leaf_walk(float3 origin, float3 dir, float tmin, float tmax, float swayFrom, uint met) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    float through = 1.0;
    float t0 = tmin;
    uint lastInstance = INVALID, lastGeometry = 0u, lastPrimitive = 0u;
    for (uint crossing = 0; crossing < met + 2u && t0 < tmax; ++crossing) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = tmax;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) q;
        // (Leaves and grass alone, every triangle to the test: their covered micromap pieces are opaque since round 20.)
        q.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, light_mask(MASK_ALL) & (MASK_FOLIAGE | MASK_SWAYS), ray);
        float nearest = 1.0;
        while (q.Proceed()) {
            if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
            const uint ci = q.CandidateInstanceID(), cg = q.CandidateGeometryIndex(), cp = q.CandidatePrimitiveIndex();
            if (ci == lastInstance && cg == lastGeometry && cp == lastPrimitive) continue; // the crossing just counted
            if (q.CandidateTriangleRayT() < swayFrom && (instances[ci].flags & INSTANCE_SWAYS) != 0) continue;
            const float lets = leaf_pass(ci, cg, cp, q.CandidateTriangleBarycentrics());
            if (lets < 1.0) { nearest = lets; q.CommitNonOpaqueTriangleHit(); }
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        through *= nearest;
        lastInstance = q.CommittedInstanceID(); lastGeometry = q.CommittedGeometryIndex(); lastPrimitive = q.CommittedPrimitiveIndex();
        if (through < LEAF_WALK_FLOOR) {
            if (ray_chance(lastInstance, lastGeometry, lastPrimitive, 0x726F756Cu) * LEAF_WALK_FLOOR >= through) return 0.0;
            through = LEAF_WALK_FLOOR;
        }
        const float t = q.CommittedRayT();
        t0 = t + max(1e-5, t * 1e-6);
    }
    return through;
}
float3 transmittance(float3 origin, float3 dir, float tmin, float tmax) {
#ifdef PT_SPEED_MEASURE
    if (g_speedNoRay) { g_speedNoRay = false; return 1.0; }
#endif
    ray_seed(origin, dir);
    g_glassTint = true; g_glassMet = 0; g_lightRay = true;
    g_leafProduct = leaf_product_on(); g_leafMet = 0; g_leafThrough = 1.0;
    const bool blocked = occluded_any(origin, dir, tmin, tmax);
    g_glassTint = false; g_lightRay = false; g_leafProduct = false;
    if (blocked) return 0.0;
    float3 waterThrough = water_through(origin, dir, tmin, tmax);
    // ON TRIAL: and what the blades let through.
    if (g_leafMet > 0) { g_leafThrough = leaf_walk(origin, dir, tmin, tmax, 0.0, g_leafMet); waterThrough *= g_leafThrough; }
    return waterThrough * (g_glassMet > 0 ? glass_walk(origin, dir, tmin, tmax, g_glassMet) : 1.0);
}
float3 transmittance_from_surface(float3 origin, float3 dir, float tmin, float tmax, float nearBack) {
    ray_seed(origin, dir);
    g_glassTint = true; g_glassMet = 0; g_lightRay = true;
    g_leafProduct = leaf_product_on(); g_leafMet = 0; g_leafThrough = 1.0;
    const bool blocked = occluded_from_surface_rays(origin, dir, tmin, tmax, nearBack);
    g_glassTint = false; g_lightRay = false; g_leafProduct = false;
    if (blocked) return 0.0;
    float3 waterThrough = water_through(origin, dir, tmin, tmax);
    // ON TRIAL: and what the blades let through. What the wind moves is met from SWAY_REACH on up to the near part's end,
    // and from that end on (occluded_from_surface_rays).
    if (g_leafMet > 0) { g_leafThrough = leaf_walk(origin, dir, tmin, tmax, min(min(tmax, nearBack * 3.0), max(tmin, SWAY_REACH)), g_leafMet); waterThrough *= g_leafThrough; }
    return waterThrough * (g_glassMet > 0 ? glass_walk(origin, dir, tmin, tmax, g_glassMet) : 1.0);
}
// The tint of the thin cloth a ray passed on its way to what it met at tmax (a bounce ray: alpha_opaque let it through the
// weave by chance and counted how often, met): each covered crossing before tmax its cloth's color (cloth_tint), found in order
// as glass_walk finds glass. Right after the ray's own query (the same ray_seed decides where an alpha tile covers). crossed:
// how many it passed.
float3 cloth_walk(float3 origin, float3 dir, float tmin, float tmax, uint met, out uint crossed) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    float3 through = 1.0;
    float t0 = tmin;
    crossed = 0;
    for (uint crossing = 0; crossing < min(met, 4u) && t0 < tmax; ++crossing) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = tmax;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_ALL, ray);
        while (q.Proceed()) {
            if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
            const InstanceRecord inst = instances[q.CandidateInstanceID()];
            const MaterialRecord cm = materials[materialIndices[inst.materialBase + q.CandidateGeometryIndex()]];
            if ((cm.flags & MATERIAL_THIN_CLOTH) != 0 && (cm.flags & MATERIAL_ALPHA_TEST) != 0 &&
                cloth_covers(cm, q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(),
                             material_alpha(inst, cm, q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics())))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        const InstanceRecord inst = instances[q.CommittedInstanceID()];
        through *= cloth_tint(inst, materials[materialIndices[inst.materialBase + q.CommittedGeometryIndex()]], q.CommittedGeometryIndex(),
                              q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics());
        ++crossed;
        const float t = q.CommittedRayT();
        t0 = t + max(1e-4, t * 1e-5);
    }
    return through;
}
// Whether what a bounce ray met is thin cloth that stopped it by that chance (the per-ray test's: alpha-tested, as the plugin
// makes every thin cloth): the other 0.3 of such rays went on through it, so the cloth's own light counts for 1 / 0.7 of this
// one's, and a cloth sends on as much bounced light as before it let any through.
bool cloth_stops(Hit hit) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    const uint flags = materials[hit.material].flags;
    return (flags & MATERIAL_THIN_CLOTH) != 0 && (flags & MATERIAL_ALPHA_TEST) != 0;
}
// A bounce ray's closest hit (a path's next ray, the radiance cache's): thin cloth lets it through by chance, and weight is
// what the light it brings back counts for: the tint of the cloth it passed (cloth_walk), and 1 / 0.7 where cloth stopped it
// (cloth_stops), from what it met or from the sky.
bool trace_bounce(float3 origin, float3 dir, float tmax, out Hit hit, out float3 weight) {
    g_bounceRay = true; g_clothMet = 0;
    const bool found = trace_closest(origin, dir, 0.0, tmax, hit);
    g_bounceRay = false;
    weight = 1.0;
    // Just short of what it met: the surface that stopped it tints nothing.
    uint crossed;
    if (g_clothMet > 0) weight = cloth_walk(origin, dir, 0.0, found ? max(length(hit.position - origin) - 1e-3, 0.0) : tmax, g_clothMet, crossed);
    if (found && cloth_stops(hit)) weight /= 1.0 - CLOTH_TRANSMISSION;
    return found;
}
// ---------------------------------------------------------------- caustics through glass
// The light a lamp or the sun sends through a closed glass object (a bottle, a flask, a potion) onto what lies in its
// shadow, bent by both of its surfaces: manifold next event estimation (Hanika, Droske and Fascione, EGSR 2015), solved
// as Blender Cycles' shadow caustics solve it (its Apache-2.0 kernel read for reference; the constraint is Jakob and
// Marschner's generalized half-vector). From the straight shadow ray's two crossings of the glass, Newton steps along the
// glass's smooth surface (its vertex normals, TGPT_MeshDesc) find the entry and exit points where Snell's law holds at
// both, and the light that path brings is weighted by how strongly the glass focuses it there (its generalized geometry
// term) against the straight ray's. The straight tint stays where the ray crosses anything but one closed glass object
// twice (a hollow bottle's four crossings, two objects), or the glass has no normals; where no bent path reaches, none comes.
static const uint CAUSTIC_ITERATIONS = 24;
static const float CAUSTIC_THRESHOLD = 1e-3;
struct CausticVertex {
    float3 p, dpdu, dpdv;       // its position, and a tangent frame continuous over the surface (from its geometric normal)
    float3 n, dndu, dndv;       // its smooth normal (outward, as the mesh's normals point) and that normal's derivatives
    uint instance, geometry, primitive;
    float2 bary;
    float2 constraint;
    float4 a, b, c;             // the constraint's derivatives by the previous, this and the next vertex (2x2, row-major)
};
float4 m22_mul(float4 a, float4 b) { return float4(a.x * b.x + a.y * b.z, a.x * b.y + a.y * b.w, a.z * b.x + a.w * b.z, a.z * b.y + a.w * b.w); }
float2 m22_apply(float4 a, float2 v) { return float2(a.x * v.x + a.y * v.y, a.z * v.x + a.w * v.y); }
float m22_det(float4 m) { return m.x * m.w - m.y * m.z; }
bool m22_inverse(float4 m, out float4 inv) {
    const float d = m22_det(m);
    inv = abs(d) < 1e-10 ? 0.0 : float4(m.w, -m.y, -m.z, m.x) / d;
    return abs(d) >= 1e-10;
}
// A tangent frame about a unit normal, (t, b, n) right-handed (Duff et al., "Building an Orthonormal Basis, Revisited").
void tangent_frame(float3 n, out float3 t, out float3 b) {
    const float sgn = n.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (sgn + n.z), c = n.x * n.y * a;
    t = float3(1.0 + sgn * n.x * n.x * a, sgn * c, -sgn * n.x);
    b = float3(c, sgn + n.y * n.y * a, -n.y);
}
// Where a ray met glass: its world position, its smooth normal from the vertices' and both one's derivatives over the triangle.
bool caustic_vertex(uint instanceId, uint geometry, uint primitive, float2 bary, float3x4 objectToWorld, out CausticVertex v) {
    v = (CausticVertex)0;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    const MeshRecord mesh = meshes[instances[instanceId].mesh];
    if (mesh.normals == INVALID) return false;
    const uint3 idx = triangle_indices(mesh, geometry, primitive);
    ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    ByteAddressBuffer nb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.normals)];
    const float3 w0 = mul(objectToWorld, float4(asfloat(pb.Load3(idx.x * 12)), 1.0));
    const float3 w1 = mul(objectToWorld, float4(asfloat(pb.Load3(idx.y * 12)), 1.0));
    const float3 w2 = mul(objectToWorld, float4(asfloat(pb.Load3(idx.z * 12)), 1.0));
    // Normals to the world by the transform's cofactors (its inverse transpose up to scale): a scaled instance's stay
    // perpendicular to its surface; a mirroring one turns them around.
    const float3 r0 = objectToWorld[0].xyz, r1 = objectToWorld[1].xyz, r2 = objectToWorld[2].xyz;
    const float3x3 cofactors = float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1));
    const float flip = dot(r0, cross(r1, r2)) < 0.0 ? -1.0 : 1.0;
    const float3 m0 = normalize(mul(cofactors, unpack_normal(nb.Load(idx.x * 4)))) * flip;
    const float3 m1 = normalize(mul(cofactors, unpack_normal(nb.Load(idx.y * 4)))) * flip;
    const float3 m2 = normalize(mul(cofactors, unpack_normal(nb.Load(idx.z * 4)))) * flip;
    float3 dpdu = w1 - w0, dpdv = w2 - w0;
    const float3 ng = cross(dpdu, dpdv);
    if (dot(ng, ng) < 1e-20) return false;
    const float3 sum = m0 * (1.0 - bary.x - bary.y) + m1 * bary.x + m2 * bary.y;
    const float len = length(sum);
    if (len < 1e-6) return false;
    v.n = sum / len;
    // The normalized normal's derivatives by the barycentrics, then carried through the same change of frame as the edges:
    // orthonormalized, then turned to a frame of the geometric normal alone, continuous from triangle to triangle.
    float3 dndu = (m1 - m0) / len, dndv = (m2 - m0) / len;
    dndu -= v.n * dot(v.n, dndu); dndv -= v.n * dot(v.n, dndv);
    const float ilu = 1.0 / length(dpdu);
    dpdu *= ilu; dndu *= ilu;
    const float along = dot(dpdu, dpdv);
    dpdv -= along * dpdu; dndv -= along * dndu;
    const float ilv = 1.0 / max(length(dpdv), 1e-12);
    dpdv *= ilv; dndv *= ilv;
    float3 t, b;
    tangent_frame(normalize(ng), t, b);
    const float cosT = dot(dpdu, t), sinT = -dot(dpdv, t);
    v.dndu = cosT * dndu - sinT * dndv; v.dndv = sinT * dndu + cosT * dndv;
    v.dpdu = t; v.dpdv = b;
    v.p = w0 * (1.0 - bary.x - bary.y) + w1 * bary.x + w2 * bary.y;
    v.instance = instanceId; v.geometry = geometry; v.primitive = primitive; v.bary = bary;
    return true;
}
// Glass's own index of refraction (PT-64: MaterialRecord.ior, HDRP Lit's _Ior, which the plugin sends where the material's
// shader has one), ordinary glass's (GLASS_IOR) where the material names none. Until then every glass bent light by 1.5.
float material_ior(MaterialRecord m) { return m.ior >= 1.0 ? m.ior : GLASS_IOR; }
float glass_ior(uint instanceId, uint geometry) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    return material_ior(materials[materialIndices[instances[instanceId].materialBase + geometry]]);
}
bool caustic_is_glass(uint instanceId, uint geometry) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    return (materials[materialIndices[instances[instanceId].materialBase + geometry]].flags & MATERIAL_GLASS) != 0;
}
// A vertex's constraint (the generalized half-vector's part in its shading frame: 0 where Snell's law holds) and its
// derivatives by its neighbours' and its own positions. toPrev: towards the receiver's side; toNext: towards the light's
// side (with fixedDirection, the light's direction itself, which the vertex's position doesn't change).
bool caustic_constraint(inout CausticVertex v, float3 toPrev, CausticVertex prev, bool prevFree, float3 toNext, CausticVertex next,
                        bool nextFree, bool fixedDirection) {
    float ili = length(toPrev), ilo = length(toNext);
    if (ili < 1e-5 || ilo < 1e-5) return false;
    ili = 1.0 / ili; ilo = 1.0 / ilo;
    const float3 wi = toPrev * ili, wo = toNext * ilo;
    const float ior = glass_ior(v.instance, v.geometry);
    const float eta = dot(wi, v.n) >= 0.0 ? ior : 1.0 / ior;
    float3 H = -(wi + eta * wo);
    const float ilh = 1.0 / max(length(H), 1e-8);
    H *= ilh;
    ilo *= eta * ilh; ili *= ilh;
    const float dpn = dot(v.dpdu, v.n);
    float3 s = v.dpdu - dpn * v.n;
    const float ils = 1.0 / max(length(s), 1e-8);
    s *= ils;
    const float3 t = cross(v.n, s);
    float3 du, dv;
    v.a = 0.0; v.c = 0.0;
    if (prevFree) {
        du = (prev.dpdu - wi * dot(wi, prev.dpdu)) * ili; dv = (prev.dpdv - wi * dot(wi, prev.dpdv)) * ili;
        du -= H * dot(du, H); dv -= H * dot(dv, H);
        v.a = -float4(dot(du, s), dot(dv, s), dot(du, t), dot(dv, t));
    }
    if (!nextFree && fixedDirection) {
        du = ili * (-v.dpdu + wi * dot(wi, v.dpdu)); dv = ili * (-v.dpdv + wi * dot(wi, v.dpdv));
    } else {
        du = -v.dpdu * (ili + ilo) + wi * (dot(wi, v.dpdu) * ili) + wo * (dot(wo, v.dpdu) * ilo);
        dv = -v.dpdv * (ili + ilo) + wi * (dot(wi, v.dpdv) * ili) + wo * (dot(wo, v.dpdv) * ilo);
    }
    du -= H * dot(du, H); dv -= H * dot(dv, H);
    du = -du; dv = -dv;
    float3 dsdu = -ils * (dot(v.dpdu, v.dndu) * v.n + dpn * v.dndu), dsdv = -ils * (dot(v.dpdu, v.dndv) * v.n + dpn * v.dndv);
    dsdu -= s * dot(s, dsdu); dsdv -= s * dot(s, dsdv);
    const float3 dtdu = cross(v.dndu, s) + cross(v.n, dsdu), dtdv = cross(v.dndv, s) + cross(v.n, dsdv);
    v.b = float4(dot(du, s) + dot(H, dsdu), dot(dv, s) + dot(H, dsdv), dot(du, t) + dot(H, dtdu), dot(dv, t) + dot(H, dtdv));
    if (nextFree) {
        du = (next.dpdu - wo * dot(wo, next.dpdu)) * ilo; dv = (next.dpdv - wo * dot(wo, next.dpdv)) * ilo;
        du -= H * dot(du, H); dv -= H * dot(dv, H);
        v.c = -float4(dot(du, s), dot(dv, s), dot(du, t), dot(dv, t));
    }
    v.constraint = float2(dot(s, H), dot(t, H));
    return true;
}
bool caustic_constraints(inout CausticVertex v0, inout CausticVertex v1, float3 receiver, float3 light, bool directional) {
    const CausticVertex none = (CausticVertex)0;
    if (!caustic_constraint(v0, receiver - v0.p, none, false, v1.p - v0.p, v1, true, false)) return false;
    return caustic_constraint(v1, v0.p - v1.p, v0, true, directional ? light : light - v1.p, none, false, directional);
}
// The Newton step: the block tridiagonal system of both vertices' constraints solved for their moves in their frames.
bool caustic_step(CausticVertex v0, CausticVertex v1, out float2 dx0, out float2 dx1) {
    dx0 = 0.0; dx1 = 0.0;
    float4 li0, li1;
    if (!m22_inverse(v0.b, li0)) return false;
    const float4 A = m22_mul(v1.a, li0);
    if (!m22_inverse(v1.b - m22_mul(A, v0.c), li1)) return false;
    dx1 = m22_apply(li1, v1.constraint - m22_apply(A, v0.constraint));
    dx0 = m22_apply(li0, v0.constraint - m22_apply(v0.c, dx1));
    return true;
}
// Where a ray from `from` towards `to` (out to twice as far) meets that instance's glass: a vertex there.
bool caustic_project(uint instanceId, float3 from, float3 to, float tmin, out CausticVertex v) {
    v = (CausticVertex)0;
    float3 d = to - from;
    const float len = length(d);
    if (len < 1e-6) return false;
    d /= len;
    RayDesc ray; ray.Origin = from; ray.Direction = d; ray.TMin = tmin; ray.TMax = 2.0 * len;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_ALL, ray);
    while (q.Proceed())
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && q.CandidateInstanceID() == instanceId && caustic_is_glass(instanceId, q.CandidateGeometryIndex()))
            q.CommitNonOpaqueTriangleHit();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
    return caustic_vertex(instanceId, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), q.CommittedObjectToWorld3x4(), v);
}
// The straight shadow ray's two crossings of one glass object, when that is all the glass it crosses.
bool caustic_seed(float3 origin, float3 dir, float tmax, out CausticVertex v0, out CausticVertex v1) {
    v0 = (CausticVertex)0; v1 = (CausticVertex)0;
    float t0 = 0.0;
    uint found = 0, first = INVALID;
    [loop] for (uint k = 0; k < 3; ++k) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = tmax;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_ALL, ray);
        while (q.Proceed())
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && caustic_is_glass(q.CandidateInstanceID(), q.CandidateGeometryIndex()))
                q.CommitNonOpaqueTriangleHit();
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        const uint instance = q.CommittedInstanceID();
        if (k == 2 || (k == 1 && instance != first)) return false;
        CausticVertex v;
        if (!caustic_vertex(instance, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(), q.CommittedObjectToWorld3x4(), v)) return false;
        if (k == 0) { v0 = v; first = instance; } else v1 = v;
        ++found;
        const float t = q.CommittedRayT();
        t0 = t + max(1e-4, t * 1e-5);
    }
    return found == 2;
}
// The walk on the glass (Cycles' pseudo-Newton solver): each step moves both vertices along their tangent planes and back onto
// the glass, gently at first (the linearization holds near the seed), halving the step where a vertex falls off the glass or
// the path stops passing through it.
bool caustic_solve(inout CausticVertex v0, inout CausticVertex v1, float3 receiver, float3 receiverNormal, float3 light, bool directional) {
    float beta = 0.1;
    bool resolve = true;
    float2 dx0 = 0.0, dx1 = 0.0;
    [loop] for (uint it = 0; it < CAUSTIC_ITERATIONS; ++it) {
        if (resolve) {
            if (!caustic_constraints(v0, v1, receiver, light, directional)) return false;
            if (max(length(v0.constraint), length(v1.constraint)) < CAUSTIC_THRESHOLD) return true;
            if (!caustic_step(v0, v1, dx0, dx1)) return false;
        }
        // A vertex whose step is nothing stays where it is: through a flat pane the receiver's side is right from the start
        // (the light leaves the pane as it arrived), and Cycles' rule of failing a vertex that can't move failed every pane.
        // The walk is stuck only when neither can move.
        const float3 step0 = beta * (dx0.x * v0.dpdu + dx0.y * v0.dpdv), step1 = beta * (dx1.x * v1.dpdu + dx1.y * v1.dpdv);
        const bool move0 = dot(step0, step0) >= 1e-14, move1 = dot(step1, step1) >= 1e-14;
        if (!move0 && !move1) return false;
        bool reduce = false;
        CausticVertex t0 = v0, t1 = v1;
        if (move0) {
            float3 p0 = v0.p - step0;
            if (dot(receiverNormal, p0 - receiver) <= 0.0) p0 = v0.p + step0;
            if (!caustic_project(v0.instance, receiver, p0, 0.0, t0)) reduce = true;
        }
        if (!reduce && move1 && !caustic_project(v1.instance, v0.p, v1.p - step1, 1e-4, t1)) reduce = true;
        if (!reduce) {
            // Still a path through the glass: each vertex's neighbours on either side of its surface.
            const float3 toLight = directional ? light : light - t1.p;
            if (dot(t0.n, receiver - t0.p) * dot(t0.n, t1.p - t0.p) >= 0.0 || dot(t1.n, t0.p - t1.p) * dot(t1.n, toLight) >= 0.0) reduce = true;
        }
        if (reduce) {
            resolve = false;
            beta *= 0.5;
            if (beta < 1e-4) return false;
            continue;
        }
        v0 = t0; v1 = t1;
        resolve = true;
        beta = min(1.0, 2.0 * beta);
    }
    return false;
}
// The solved path's generalized geometry term (Cycles' transfer matrix): the solid angle at the receiver per unit of the
// light's direction (directional: per steradian) or of a small patch of the light facing the path (per square metre).
float caustic_geometry(CausticVertex v0, CausticVertex v1, float3 receiver, float3 light, bool directional) {
    float4 li0, li1;
    if (!m22_inverse(v0.b, li0)) return 0.0;
    const float4 u0 = m22_mul(li0, v0.c);
    if (!m22_inverse(v1.b - m22_mul(v1.a, u0), li1)) return 0.0;
    const float3 s = normalize(v1.dpdu - dot(v1.dpdu, v1.n) * v1.n), t = cross(v1.n, s);
    const float3 wi = normalize(v0.p - v1.p);
    const float ior1 = glass_ior(v1.instance, v1.geometry);
    const float eta = dot(wi, v1.n) >= 0.0 ? ior1 : 1.0 / ior1;
    float4 dc;
    float measure = 1.0;
    if (directional) {
        const float3 wo = light;
        float3 H = -(wi + eta * wo);
        const float ilh = 1.0 / max(length(H), 1e-8);
        H *= ilh;
        const float ilo = -eta * ilh;
        const float cosT = dot(wo, v1.n), sinT = sqrt(saturate(1.0 - cosT * cosT));
        const float cosP = sinT > 1e-6 ? dot(wo, s) / sinT : 1.0, sinP = sinT > 1e-6 ? dot(wo, t) / sinT : 0.0;
        float3 dth = ilo * (cosT * (cosP * s + sinP * t) - sinT * v1.n), dph = ilo * sinT * (-sinP * s + cosP * t);
        dth -= H * dot(dth, H); dph -= H * dot(dph, H);
        dc = float4(dot(dth, s), dot(dph, s), dot(dth, t), dot(dph, t));
        measure = 1.0 / max(sinT, 1e-3); // from dtheta dphi to solid angle
    } else {
        float3 wo = light - v1.p;
        float ilo = 1.0 / max(length(wo), 1e-6);
        wo *= ilo;
        float3 H = -(wi + eta * wo);
        const float ilh = 1.0 / max(length(H), 1e-8);
        H *= ilh;
        ilo *= eta * ilh;
        float3 lu, lv;
        tangent_frame(-wo, lu, lv);
        float3 du = (lu - wo * dot(wo, lu)) * ilo, dv = (lv - wo * dot(wo, lv)) * ilo;
        du -= H * dot(du, H); dv -= H * dot(dv, H);
        dc = -float4(dot(du, s), dot(dv, s), dot(du, t), dot(dv, t));
    }
    const float4 tp = m22_mul(u0, m22_mul(li1, dc)); // the two negations of Cycles' transfer matrix cancel
    const float3 w0 = v0.p - receiver;
    const float l2 = max(dot(w0, w0), 1e-10);
    return abs(dot(w0 * rsqrt(l2), v0.n)) / l2 * abs(m22_det(tp)) * measure;
}
// What the solved path lets through: both surfaces' Fresnel transmission, what the paint leaves at each crossing (as glass_walk).
float3 caustic_tint(CausticVertex v0, CausticVertex v1, float3 light, bool directional) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    const float3 toLight = normalize(directional ? light : light - v1.p), inside = normalize(v1.p - v0.p);
    const float ior1 = glass_ior(v1.instance, v1.geometry), ior0 = glass_ior(v0.instance, v0.geometry);
    const float enter = 1.0 - fresnel_dielectric(dot(toLight, v1.n), dot(toLight, v1.n) >= 0.0 ? ior1 : 1.0 / ior1);
    const float leave = 1.0 - fresnel_dielectric(dot(inside, v0.n), dot(inside, v0.n) >= 0.0 ? ior0 : 1.0 / ior0);
    const InstanceRecord inst = instances[v0.instance];
    const float open0 = glass_open(inst, materials[materialIndices[inst.materialBase + v0.geometry]], v0.geometry, v0.primitive, v0.bary);
    const float open1 = glass_open(inst, materials[materialIndices[inst.materialBase + v1.geometry]], v1.geometry, v1.primitive, v1.bary);
    // And the glass's own tint over the solved path's way inside it (PT-76: its material's absorption per metre).
    const float3 inside0 = segment_transmittance(materials[materialIndices[inst.materialBase + v0.geometry]].absorption, length(v1.p - v0.p));
    return enter * leave * open0 * open1 * inside0;
}
// Nothing but the glass itself in the way of one of the solved path's segments.
bool caustic_clear(float3 origin, float3 dir, float tmin, float tmax, uint glassInstance) {
    ray_seed(origin, dir);
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = tmin; ray.TMax = tmax;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) q;
    // The solved path skips its entire source instance, including solid character submeshes (M3).
    // Other opaque surfaces still answer true in alpha_opaque; micromaps still reject their bare pieces.
    q.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, MASK_ALL, ray);
    while (q.Proceed())
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE && q.CandidateInstanceID() != glassInstance &&
            alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    return q.CommittedStatus() != COMMITTED_TRIANGLE_HIT;
}
// A shadow ray's light through glass, focused (caustics), against the straight ray's; straight: that ray's tint
// (transmittance), kept where the path isn't one closed glass object's. origin: the receiver's shadow origin; normal and
// geoNormal its shading and geometric normals; dir the straight direction to the light, lightDistance how far (directional:
// the sun, 0).
float3 caustic_through(float3 origin, float3 normal, float3 geoNormal, float3 dir, float lightDistance, bool directional, float3 straight) {
    if ((g.settings2.y & OPTION_NO_CAUSTICS) != 0) return straight;
    CausticVertex v0, v1;
    if (!caustic_seed(origin, dir, directional ? g.params.y : lightDistance, v0, v1)) return straight;
    // Glass whose own index of refraction is 1 (PT-64: 87 of the game's HDRP Lit materials say so) bends nothing: its light
    // comes straight through, tinted. (The bent path's equations have nothing to solve there.)
    if (glass_ior(v0.instance, v0.geometry) < 1.001) return straight;
    // Nor does an open sheet of glass (glass_solid: its mesh closes no volume): two crossings of it are two thin panes.
    if (!glass_solid(v0.instance, v0.geometry, v0.primitive) || !glass_solid(v1.instance, v1.geometry, v1.primitive)) return straight;
    const float3 light = directional ? dir : origin + dir * lightDistance;
    if (!caustic_solve(v0, v1, origin, geoNormal, light, directional)) return 0.0;
    const float3 a = v0.p - origin, b = v1.p - v0.p, c = directional ? light : light - v1.p;
    const float la = length(a), lb = length(b), lc = directional ? g.params.y : length(c);
    if (la < 1e-5 || lb < 1e-5 || lc < 1e-5) return 0.0;
    if (!caustic_clear(origin, a / la, 0.0, la * 0.999, v0.instance) || !caustic_clear(v0.p, b / lb, lb * 1e-3, lb * 0.999, v0.instance) ||
        !caustic_clear(v1.p, directional ? light : c / lc, 1e-3, directional ? lc : lc * 0.999, v0.instance)) return 0.0;
    const float geometry = caustic_geometry(v0, v1, origin, light, directional);
    const float straightCos = max(dot(normal, dir), 1e-3);
    const float scale = (directional ? geometry : geometry * lightDistance * lightDistance) * max(dot(normal, a / la), 0.0) / straightCos;
    // However bright the focus (PT-50: until 2026-10-07 at most 16 times the straight light, which cut a lens's bright line).
    // (g_leafThrough, ON TRIAL, PT-93: the share the blades in the straight ray's way let through, 1 with the trial off. The
    // coin stopped that ray, and this light with it, as often.)
    return caustic_tint(v0, v1, light, directional) * scale * g_leafThrough;
}
// A direction chosen around a normal-mapped normal can point into the surface itself: mirrored back out.
float3 above_surface(float3 dir, float3 geoNormal) { float d = dot(dir, geoNormal); return d < 0.0 ? dir - 2.0 * d * geoNormal : dir; }

// ---------------------------------------------------------------- lights and sky
float3 sky_radiance(float3 dir) {
    Texture2D<float4> sky = ResourceDescriptorHeap[g.inputs1.w];
    float u = atan2(dir.x, dir.z) * (0.5 / PI) + 0.5;
    float v = acos(clamp(dir.y, -1.0, 1.0)) / PI;
    return sky.SampleLevel(s_linearWrap, float2(u, v), 0).rgb * g.params.z;
}
float smooth_window(float d2, float scale, float bias) { float f = d2 * scale; float s = saturate(bias - f * f); return s * s; }
// HDRP's cookie: a mask on a light's intensity by direction, in the light's own space (x right, y up, z forward): a
// point light's cubemap (baked as an octahedral map), a spot's texture across its cone. The game's candle and torch
// lights carry them (PointLight_Cookies): without, the table right under a candlestick got its full light.
float3 light_cookie(LightRecord l, float3 fromLight) {
    uint cookie = l.flags & 0xFFFFu;
    if (cookie == 0u || g.signals.x == INVALID) return 1.0;
    float3 right = cross(l.up, l.direction); // Unity's left-handed axes: right = up x forward
    float3 ls = float3(dot(fromLight, right), dot(fromLight, l.up), dot(fromLight, l.direction));
    float2 uv;
    if (l.type == 1u) {
        if (ls.z <= 1e-4) return 0.0;
        uv = saturate(ls.xy / ls.z * l.cookieScale * 0.5 + 0.5);
    } else {
        float3 n = ls / max(abs(ls.x) + abs(ls.y) + abs(ls.z), 1e-6);
        float2 e = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
        uv = saturate(e * 0.5 + 0.5);
    }
    Texture2D<float4> cookies = ResourceDescriptorHeap[g.signals.x];
    uint index = cookie - 1u;
    int2 origin = int2(index % COOKIE_TILES_PER_ROW, index / COOKIE_TILES_PER_ROW) * int(COOKIE_TILE_SIZE);
    float2 t = uv * COOKIE_TILE_SIZE - 0.5;
    float2 f = frac(t);
    int2 i0 = int2(floor(t));
    int2 a = clamp(i0, 0, int(COOKIE_TILE_SIZE) - 1), b = clamp(i0 + 1, 0, int(COOKIE_TILE_SIZE) - 1);
    float3 c00 = cookies.Load(int3(origin + int2(a.x, a.y), 0)).rgb, c10 = cookies.Load(int3(origin + int2(b.x, a.y), 0)).rgb;
    float3 c01 = cookies.Load(int3(origin + int2(a.x, b.y), 0)).rgb, c11 = cookies.Load(int3(origin + int2(b.x, b.y), 0)).rgb;
    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}
#define LIGHT_NO_RANGE 0x80000u   // TGPT_LIGHT_NO_RANGE: no window at its range; the range says how far the light grid lists it
// ON TRIAL (PT-85 and PT-40, docs/agents/flame-specks.md; never a row, to be taken out once the game has answered): why
// lamps placed in their flames, at their flames' size, make twice the specks of lamps at the game's place and size. Four
// hidden settings of the plugin, one a suspect, all 0 (off: everything as before) unless set, packed into g.wetGrid.w
// (TGPT_Frame.jitter's w, tracer.cpp):
//   bits 0-7   NOT on trial any more: a lamp's near size in centimetres (light_falloff below; the plugin's default 10,
//              0 from the offline test). It was the suspect that was guilty, and the owner chose it in the game.
//   bits 8-9   B, the lamp as a reflection ray on see-through glass or water meets it (reflected_lamps): 0 its own size,
//              1 the radius HDRP gave it, 2 five times its own.
//   bits 10-11 C, the highlight a light's sample makes (pt_rtxdi rtxdi_shade, direct_light): evaluated towards a point of a
//              disc of that other radius (0, 1, 2 as B's) in place of the lamp's own.
//   bits 12-13 D, the disc a lamp's shadow ray aims at (how soft its shadow is): the same three.
//   bits 14-21 the job after's (docs/agents/flame-near.md), not a suspect but a way out: what stands within this many
//              centimetres of a lamp is found on purpose (lamp_around, above direct_light; 0 off).
// HDRP's radius for a lamp: bits 20-31 of its flags, in millimetres (TGPT_LIGHT_HDRP_RADIUS_SHIFT; the plugin fills them
// only while a setting at 1 asks; 0: not given, the lamp's own radius serves). Each switch changes that one use of the
// radius and nothing else: the lamp stays in its flame, and its light on a surface a metre away is the same.
#define LIGHT_HDRP_RADIUS_SHIFT 20u // TGPT_LIGHT_HDRP_RADIUS_SHIFT
#define LAMP_TRIAL_REFLECTED 8u
#define LAMP_TRIAL_HIGHLIGHT 10u
#define LAMP_TRIAL_SHADOW 12u
uint lamp_trial() { return uint(max(g.wetGrid.w, 0.0) + 0.5); }
uint lamp_trial_mode(uint shift) { return (lamp_trial() >> shift) & 3u; }
float lamp_trial_radius(LightRecord l, uint shift) {
    const uint mode = lamp_trial_mode(shift);
    if (mode == 0u || l.type > 1u) return l.radius;
    if (mode >= 2u) return l.radius * 5.0;
    const float hdrp = float(l.flags >> LIGHT_HDRP_RADIUS_SHIFT) * 0.001;
    return hdrp > 0.0 ? hdrp : l.radius;
}
// How a lamp's light on a surface falls with the square of its distance, d2 (PT-85, PT-40). The game's lamps carry
// strengths its artists set for HDRP's law, I / (d^2 + r^2) with a radius r of up to a metre (a tavern candle: 21,000
// candela): put in its flame, a centimetre across, and left to I / d^2, such a lamp gives its own wax a thousand times
// what the game's did. The wax burnt white, and the room's bounce and reflection rays that met it by chance were the
// picture's specks. So within its near size n (the plugin's "Lamp near size", centimetres, bits 0-7 of lamp_trial) a
// lamp's light stops growing: I / sqrt(d^4 + n^4), which is I / n^2 at the flame, I / d^2 from two n on (at 2 n: 3 %
// less, at 3 n: 0.6 %), and has no edge between the two. The room, a table by a candle and the wall behind one keep
// their light; only what is right at the flame changes. The lamp stays in its flame, at its flame's size, for its
// shadows and its reflection. In the game (tavern-room, NRD, 16 frames, run 20261009-064121, held flat within n): n 0,
// 3, 5, 10, 20 cm: 27.5, 23.6, 14.8, 15.2, 16.0 speck spots a frame, 1,598, 1,242, 722, 32, 28 burnt pixels, the
// picture's mean 23.9, 23.5, 23.2, 23.1, 23.1: 10 is where the burn ends and nothing more is won. A lamp larger than n
// (a hearth's fire) is as before; 0: off, I / d^2 down to every lamp's own size.
// The setting's size is that of a lamp as strong as a tavern candlestick's (LAMP_NEAR_STRENGTH: 13,500 candela of
// brightness; its red 27,600), and a lamp's own grows with the square root of its strength: so the most ANY lamp puts
// on a surface is the same, as if every flame's glow were equally bright and a stronger lamp's only larger. (At one
// size for all, the tavern's wall candelabra, 45,000 to 76,000 candela, still burnt its wax at 10 cm where the
// candlesticks' was cured: run 20261009-071155, glass-jars, 440 burnt pixels against 41 and 3.)
#define LAMP_NEAR_STRENGTH 13500.0
float light_falloff(LightRecord l, float d2) {
    const float own = l.radius * l.radius + 1e-4, n = float(lamp_trial() & 0xFFu) * 0.01;
    const float n2 = n * n * max(luminance(l.color), 0.0) * (1.0 / LAMP_NEAR_STRENGTH);
    return n2 > own && l.type <= 1u ? rsqrt(d2 * d2 + n2 * n2) : 1.0 / max(d2, own);
}
// Light arriving at position from light: irradiance on a surface facing it, and the direction and distance to it.
float3 light_incident(LightRecord l, float3 position, out float3 dir, out float dist) {
    if (l.type == 2) { dir = -l.direction; dist = 1e7; return l.color * directional_fog(position, dir.y); }
    float3 d = l.position - position;
    float d2 = max(dot(d, d), 1e-4);
    dist = sqrt(d2); dir = d / dist;
    // A light falls off with distance alone (LIGHT_NO_RANGE: the plugin's lights, PT-50); one without the flag keeps HDRP's
    // window at its range (an effect's light, which the game makes a glow on the effect itself).
    const float window = (l.flags & LIGHT_NO_RANGE) != 0u ? 1.0 : smooth_window(d2, 1.0 / max(l.range * l.range, 1e-4), 1.0);
    float atten = window * light_falloff(l, d2);
    if (l.type == 1) { float s = saturate(dot(-dir, l.direction) * l.angleScale + l.angleOffset); atten *= s * s; }
    return l.color * (atten * exp(-height_fog_depth(position, l.position))) * light_cookie(l, -dir); // HDRP's fog on the way
}
// The traced scene's emissive triangles as lights (scene.cpp build_emitters, pt_emissive.hlsl): light indices from
// EMITTER_BASE on are records of the emitter buffer. First each emissive submesh's triangles, in slots that keep their place
// from frame to frame (a slot holding the same triangle, by its identity, is the same light for ReSTIR's temporal reuse, as
// a light's slot is), then the clusters of up to 32 of them the light grid lists. A triangle's record: its first corner
// (position) and two edges (direction, up) wound so that their cross product faces its front; its reach (range), past which
// its light is left to the paths that meet it; within its cluster, its cumulative share of the cluster's light (color.x), and
// its area (color.y); its material (radius's bits), its corners' texture coordinates with the material's tiling as halves
// (angleScale, angleOffset, cookieScale); its identity (id, 0 for an empty slot); what it is (flags, EMITTER_*). A cluster's
// (LIGHT_CLUSTER): its first slot (flags) and its triangles (cookieScale's bits).
static const uint EMITTER_BASE = 1u << 20;
static const uint CLUSTER_TRIANGLES = 32; // scene.hpp kClusterTriangles: a submesh's triangles in clusters of this many
#define LIGHT_CLUSTER 3u
#define LIGHT_TRIANGLE 4u
#define EMITTER_SAMPLED 1u   // sampled as a light: a path that meets it counts what the sampling leaves (emission_weight)
#define EMITTER_PAIRED 2u    // a kept light's own surface (a lantern's glass round its candle's light): it lights nothing itself
#define EMITTER_TWO_SIDED 4u // emits from both faces (a double-sided material)
LightRecord load_emitter(uint slot, bool previousFrame) {
    StructuredBuffer<LightRecord> records = ResourceDescriptorHeap[previousFrame ? g.emissive.y : g.emissive.x];
    LightRecord t = records[slot];
    // The previous frame's records are relative to its origin (when the origin moves, a build writes this frame's into the
    // other buffer): moved to this frame's, as the previous frame's surfaces and lights are.
    if (previousFrame) t.position += g.prevOriginDelta.xyz;
    return t;
}
// Whether anything is sampled as a light this frame: the lights, or emissive clusters in the light grid.
bool frame_has_lights() { return g.settings2.z > 0 || g.emissive2.z > 0; }
LightRecord load_light(uint i) {
    if (i >= EMITTER_BASE) return load_emitter(i - EMITTER_BASE, false);
    StructuredBuffer<LightRecord> lights = ResourceDescriptorHeap[g.scene1.y]; return lights[i];
}
// A light see-through glass reflects as the sphere it is traced as (pt_specular reflected_lamps), whose highlight on glass
// the direct light's samples then leave out (pt_di_spatial): a point light's or a spot's.
#define LIGHT_NO_SPECULAR 0x20000u // TGPT_LIGHT_NO_SPECULAR: HDRP's light makes no highlight
bool reflected_as_sphere(LightRecord l) { return l.type <= 1u && l.radius > 0.0 && (l.flags & LIGHT_NO_SPECULAR) == 0u; }
// How far from a light its shadow rays stop being tested: 5 cm for a light HDRP placed inside its lamp's or candle's mesh
// (the mesh around it does not block it), the light's own size and a centimetre for one the plugin put at its flame's centre
// (TGPT_LIGHT_AT_EMITTER): 5 cm let a candle's light through the top of its own wax 3 cm under the flame, and the wax's
// sides glowed as if lit from inside (the tavern's candles, lights round 2).
#define LIGHT_AT_EMITTER 0x40000u // TGPT_LIGHT_AT_EMITTER
float light_own_reach(LightRecord l) { return (l.flags & LIGHT_AT_EMITTER) != 0u ? min(l.radius + 0.01, 0.05) : 0.05; }
// The previous frame's light in the same slot, its position relative to this frame's origin (none: count 0).
LightRecord load_previous_light(uint i) {
    if (i >= EMITTER_BASE) return load_emitter(i - EMITTER_BASE, true);
    StructuredBuffer<LightRecord> lights = ResourceDescriptorHeap[g.extra.w]; return lights[i];
}
float2 unpack_st(float packed) { const uint u = asuint(packed); return float2(f16tof32(u & 0xFFFF), f16tof32(u >> 16)); }
// A point of an emissive triangle (u uniform over the unit square: uniform over its area) and its texture coordinates.
float3 triangle_point(LightRecord t, float2 u, out float2 st) {
    const float su = sqrt(u.x);
    const float b1 = su * (1.0 - u.y), b2 = su * u.y;
    const float2 st0 = unpack_st(t.angleScale), st1 = unpack_st(t.angleOffset), st2 = unpack_st(t.cookieScale);
    st = st0 + (st1 - st0) * b1 + (st2 - st0) * b2;
    return t.position + t.direction * b1 + t.up * b2;
}
// The light arriving at position from a point of an emissive triangle (u picks it, uniformly over its area), times the
// triangle's area: uniform sampling's estimate of the triangle's irradiance on a surface facing it, as light_incident gives a
// point light's. Its emission there (texture and coverage, as a path meeting it there sees), the cosine it leaves the
// triangle at (its front only unless two-sided), and its reach's window (emission_weight gives paths the rest). y: the point.
float3 triangle_incident(LightRecord t, float3 position, float2 u, out float3 dir, out float dist, out float3 y) {
    float2 st;
    y = triangle_point(t, u, st);
    const float3 d = y - position;
    const float d2 = max(dot(d, d), 1e-8);
    dist = sqrt(d2); dir = d / dist;
    if ((t.flags & (EMITTER_SAMPLED | EMITTER_PAIRED)) != EMITTER_SAMPLED) return 0.0;
    const float3 n = cross(t.direction, t.up);
    const float twiceArea = length(n);
    float c = -dot(n, dir) / max(twiceArea, 1e-20);
    if ((t.flags & EMITTER_TWO_SIDED) != 0) c = abs(c);
    const float w = smooth_window(d2, 1.0 / max(t.range * t.range, 1e-8), 1.0);
    if (c <= 0.0 || w <= 0.0) return 0.0;
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    const MaterialRecord m = materials[asuint(t.radius)];
    return material_emission(m, st) * (emission_coverage(m, st) * c * w * 0.5 * twiceArea / max(d2, 1e-4));
}
// A cell candidate that is a cluster of emissive triangles (cell_candidate's): one of its triangles, drawn by its share of
// the cluster's light (their cumulative shares), as the light index returned; invPdf is divided by that share.
uint cluster_triangle(LightRecord cluster, float u, inout float invPdf) {
    const uint first = cluster.flags, count = max(asuint(cluster.cookieScale), 1u);
    uint lo = 0, hi = count - 1;
    while (lo < hi) { uint mid = (lo + hi) >> 1; if (load_emitter(first + mid, false).color.x < u) lo = mid + 1; else hi = mid; }
    const float p = load_emitter(first + lo, false).color.x - (lo > 0 ? load_emitter(first + lo - 1, false).color.x : 0.0);
    invPdf = p > 0.0 ? invPdf / p : 0.0;
    return EMITTER_BASE + first + lo;
}
// A shadow ray to a point of an emissive triangle, ending just short of it (the triangle itself would stop it).
float3 triangle_shadow_ray(float3 origin, float3 y, out float tmax) {
    const float3 v = y - origin;
    const float len = max(length(v), 1e-4);
    tmax = max(len - max(1e-3 * len, 2e-3), 0.0);
    return v / len;
}

// A shadow ray towards a random point of the light's disc as seen from origin, the size HDRP gives the light
// (its Radius, 2.5 cm unless an artist changed it; the plugin sends at least 2 cm): a candle or a torch casts a
// crisp shadow that softens with distance from what casts it, a large light a soft one. (A 10 cm minimum here
// blurred every lamp's shadow; the several lights of a fire or a chandelier each cast theirs, as real flames do.)
float3 shadow_ray(LightRecord light, float3 origin, float2 u, out float tmax) {
    float3 c = light.position - origin;
    float3 n = c * rsqrt(max(dot(c, c), 1e-8));
    float3 t, b; basis(n, t, b);
    // ON TRIAL (D, lamp_trial): the disc at the lamp's other radius, for this line alone; its own again below (its reach).
    const float ownRadius = light.radius;
    light.radius = lamp_trial_radius(light, LAMP_TRIAL_SHADOW);
    float2 disk = disk_sample(u) * light.radius;
    light.radius = ownRadius;
    float3 v = c + t * disk.x + b * disk.y;
    float dist = length(v);
    tmax = max(dist - light_own_reach(light), 0.0); // the lamp's own mesh around the light does not block it
    return v / max(dist, 1e-4);
}
// ON TRIAL (C, lamp_trial): the direction from a point to a random point of a lamp's disc of the given radius, as seen from it.
float3 lamp_disc_dir(LightRecord light, float3 from, float radius, float2 u) {
    const float3 c = light.position - from;
    float3 t, b; basis(c * rsqrt(max(dot(c, c), 1e-8)), t, b);
    const float2 disk = disk_sample(u) * radius;
    return normalize(c + t * disk.x + b * disk.y);
}

// The lights whose range reaches a position: its light grid cell's list. Outside the grid, none.
bool light_cell(float3 position, out uint first, out uint count) {
    first = 0; count = 0;
    if (g.grid.x == INVALID || !frame_has_lights()) return false;
    int3 dims = int3(g.grid.z & 0xFF, (g.grid.z >> 8) & 0xFF, (g.grid.z >> 16) & 0xFF);
    int3 c = int3(floor((position - g.gridMin.xyz) * g.gridMin.w));
    if (any(c < 0) || any(c >= dims)) return false;
    StructuredBuffer<uint2> cells = ResourceDescriptorHeap[g.grid.x];
    uint2 cell = cells[(c.z * dims.y + c.y) * dims.x + c.x];
    first = cell.x; count = cell.y;
    return count > 0;
}
uint cell_light(uint first, uint count, inout Rng rng) {
    StructuredBuffer<uint2> entries = ResourceDescriptorHeap[g.grid.y];
    return entries[first + min(uint(rng.next() * count), count - 1)].x;
}
// Candidate c of n from a cell of count lights, and one over the probability it was drawn with. With no more lights
// than candidates, every light once (as if each were drawn with probability 1 / count). With more, the n are drawn
// systematically along the cell's cumulative weights from one random u (each light's luminance over its squared
// distance from the cell: scene.cpp build_light_grid), so the lights that can matter here come up about as often as
// they do: drawn evenly, a room's chandelier came up as rarely as the faintest candle glow among 40 lights, and its
// light was noise the denoiser smoothed away. Weighted by one over the light's own probability, the candidates are
// an unbiased estimate as before; drawn independently, the same light came up several times while others were missed.
uint cell_candidate(uint first, uint count, uint n, uint c, float u, out float invPdf) {
    StructuredBuffer<uint2> entries = ResourceDescriptorHeap[g.grid.y];
    if (count <= n) { invPdf = float(count); return entries[first + (min(uint(u * count), count - 1) + c) % count].x; }
    float t = (float(c) + u) / float(n);
    uint lo = 0, hi = count - 1;
    while (lo < hi) { uint mid = (lo + hi) >> 1; if (asfloat(entries[first + mid].y) < t) lo = mid + 1; else hi = mid; }
    float p = asfloat(entries[first + lo].y) - (lo > 0 ? asfloat(entries[first + lo - 1].y) : 0.0);
    invPdf = p > 0.0 ? 1.0 / p : 0.0;
    return entries[first + lo].x;
}
// Whether light index is on the cell list (first, count): a cell's entries are in the order of their light indices
// (scene.cpp build_light_grid), so a binary search finds it.
bool cell_lists(uint first, uint count, uint index) {
    if (count == 0) return false;
    StructuredBuffer<uint2> entries = ResourceDescriptorHeap[g.grid.y];
    uint lo = 0, hi = count - 1;
    while (lo < hi) { uint mid = (lo + hi) >> 1; if (entries[first + mid].x < index) lo = mid + 1; else hi = mid; }
    return entries[first + lo].x == index;
}
// Every light a position can get light from, with no reach past which a light stops (PT-50, "There is no budgets or limits
// there, this is PT"): a light falls off with distance alone (LIGHT_NO_RANGE), and the light grid lists it only where it
// can matter most (its range is how far that is). Its cell's list (first, count) gives up to eight candidates drawn by how
// much each can matter there; the far list (farFirst, farCount: the coarse cell of 4x4x4 cells around it, every light
// that is far from part of it, by its power over its squared distance from the coarse cell; outside the grid, every
// light by its power) one more, which counts only a light the cell does not list. The two split the lights between them,
// each light counted by exactly one wherever the position is, so their sum is unbiased however far a light is: the far
// list costs noise, never light. near and far: the candidates each gives; candidates: both.
struct LightLists { uint first, count, farFirst, farCount, near, far, candidates; };
static const uint GRID_COARSE = 4; // scene.hpp kGridCoarse: the far lists' cells, this many of the grid's a side
bool light_lists(float3 position, out LightLists s) {
    s = (LightLists)0;
    if (g.grid.x == INVALID || !frame_has_lights()) return false;
    const int3 dims = int3(g.grid.z & 0xFF, (g.grid.z >> 8) & 0xFF, (g.grid.z >> 16) & 0xFF);
    const int3 coarse = (dims + int(GRID_COARSE) - 1) / int(GRID_COARSE);
    const uint cellCount = uint(dims.x * dims.y * dims.z);
    StructuredBuffer<uint2> cells = ResourceDescriptorHeap[g.grid.x];
    const int3 c = int3(floor((position - g.gridMin.xyz) * g.gridMin.w));
    uint far = cellCount + uint(coarse.x * coarse.y * coarse.z); // outside the grid: every light, by its power
    if (all(c >= 0) && all(c < dims)) {
        const uint2 cell = cells[(c.z * dims.y + c.y) * dims.x + c.x];
        s.first = cell.x; s.count = cell.y;
        const int3 k = c / int(GRID_COARSE);
        far = cellCount + uint((k.z * coarse.y + k.y) * coarse.x + k.x);
    }
    const uint2 farCell = cells[far];
    s.farFirst = farCell.x; s.farCount = farCell.y;
    s.near = min(s.count, 8u);
    s.far = min(s.farCount, 1u);
    s.candidates = s.near + s.far;
    return s.candidates > 0;
}
// Candidate c of light_lists' candidates, and its weight: one over its probability, over its list's share of the candidates
// (a stream of all of them divides by their number). A far candidate the cell lists weighs nothing (the cell's own count it).
uint lists_candidate(LightLists s, uint c, float u, out float invPdf) {
    if (c < s.near) {
        const uint i = cell_candidate(s.first, s.count, s.near, c, u, invPdf);
        invPdf *= float(s.candidates) / float(s.near);
        return i;
    }
    const uint i = cell_candidate(s.farFirst, s.farCount, s.far, c - s.near, u, invPdf);
    invPdf = cell_lists(s.first, s.count, i) ? 0.0 : invPdf * float(s.candidates) / float(s.far);
    return i;
}

// ON TRIAL (PT-85 and PT-40, docs/agents/flame-near.md; never a row, to be taken out or made the rule once the game has
// answered): what stands right by a lamp, found on purpose. A lamp in its flame puts I / d^2 on the wax, the holder and the
// wall a few centimetres from it; a bounce or reflection ray from across the room meets those few square centimetres by
// chance, rarely, and brings back hundreds of times what it brings from anywhere else: a speck. The hidden setting "Lamp
// trial lit surroundings" (centimetres; bits 14-21 of lamp_trial; 0 off: everything as before) adds a second way to find
// that light, and weighs the two against each other so each point's light counts once (multiple importance sampling, the
// balance heuristic: Veach and Guibas 1995), the mean unchanged:
//   on purpose (lamp_around_light): from the vertex a ray leaves, one of the lamps its light lists give, a ray from that
//     lamp's centre in a direction drawn evenly, the first thing it meets within the setting's distance (a point y, drawn
//     with the density a = cos / (4 pi r^2) per area: most often where the lamp's light is strongest), the lamp's light
//     there as a path vertex there takes it (direct_light's: its shadow ray too), and a ray back to the vertex. The r^2 of
//     the density takes the 1 / r^2 of the light out: what it brings is as large 2 cm from the flame as 20.
//   by chance (as before): the vertex's own ray meets y (density b per area: its lobe's or its cosine's, over the squared
//     distance), and y's direct light counts for 1 - s a / (a + b) of the lamp's diffuse light there, where the lamp's
//     centre sees y as its first hit (near_first: the very points the first way can draw).
//   s: 1 up to half the setting's distance from the lamp, falling smoothly to 0 at it (beyond, and closer than the lamp's
//     own reach, all is left to chance); the on-purpose way counts s b / (a + b) of what it finds.
// Left to chance alone, both ways agreeing: see-through glass (met or in the way), thin cloth (met or passed), a leaf lit
// from behind, water, and every ray this file has not given the second way (NearRay.alpha 0: a mirror ray's second
// reflection, the water's path, a walk's own direct light).
#define LAMP_TRIAL_AROUND 14u
#ifdef PT_NO_LAMP_AROUND // PT-99 (docs/agents/speed-1.md; CMake's TGPT_LAMP_AROUND OFF): the trial is compiled out of every pass
float lamp_around() { return 0.0; }
#else
float lamp_around() { return float((lamp_trial() >> LAMP_TRIAL_AROUND) & 0xFFu) * 0.01; }
#endif
// The ray that meets a vertex next, for the weighing: where it leaves from, the normal it is drawn about and the surface's
// own (no ray is traced below it), and how it is drawn: alpha < 0 cosine-distributed, > 0 from a GGX lobe of that width
// seen from view (visible normals, mirrored back above the surface: pt_specular), 0 none (all left to chance).
struct NearRay { float3 from; float3 normal; float3 geo; float3 view; float alpha; };
// pending: set by the pass that traces the ray, read (and cleared) by shade_path or shade_seen for their first vertex.
// armed: set just before the one direct_light call it is for, which reads and clears it.
static NearRay g_nearPending = { float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), 0.0 };
static NearRay g_nearArmed = { float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), float3(0.0, 0.0, 0.0), 0.0 };
NearRay near_ray(float3 from, float3 normal, float3 geo, float3 view, float alpha) {
    NearRay r; r.from = from; r.normal = normal; r.geo = geo; r.view = view; r.alpha = alpha; return r;
}
NearRay near_cosine(float3 from, float3 normal, float3 geo) { return near_ray(from, normal, geo, normal, -1.0); }
float near_vndf(float3 n, float3 v, float a2, float3 l) {
    const float nv = dot(n, v);
    const float3 h = v + l;
    const float len2 = dot(h, h);
    if (!(nv > 0.0) || !(len2 > 1e-12)) return 0.0;
    return smith_g1(nv, a2) * ggx_d(saturate(dot(n, h) * rsqrt(len2)), a2) / (4.0 * nv);
}
// The density, per solid angle, with which the ray takes direction l.
float near_ray_pdf(NearRay r, float3 l) {
    const float up = dot(r.geo, l);
    if (r.alpha == 0.0 || !(up > 0.0)) return 0.0;
    if (r.alpha < 0.0) return max(dot(r.normal, l), 0.0) / PI;
    const float a2 = r.alpha * r.alpha;
    return min(near_vndf(r.normal, r.view, a2, l) + near_vndf(r.normal, r.view, a2, l - 2.0 * up * r.geo), 1e30);
}
// The weighing at a point y (its geometric normal ny) lit by lamp l, for the ray: s, a and b as above; all 0 where the
// point is left to chance.
void near_split(NearRay ray, float3 y, float3 ny, LightRecord l, out float s, out float a, out float b) {
    s = 0.0; a = 0.0; b = 0.0;
    const float around = lamp_around();
    if (!(around > 0.0) || ray.alpha == 0.0 || l.type > 1u) return;
    const float3 toL = l.position - y;
    const float r2 = dot(toL, toL), r = sqrt(r2);
    if (!(r < around) || !(r > light_own_reach(l))) return;
    const float3 toX = ray.from - y;
    const float d2 = max(dot(toX, toX), 1e-8), d = sqrt(d2);
    a = abs(dot(ny, toL)) / (4.0 * PI * r2 * r);
    b = min(near_ray_pdf(ray, -toX / d) * abs(dot(ny, toX)) / (d2 * d), 1e30);
    const float t = saturate(2.0 - 2.0 * r / around);
    s = a > 0.0 ? t * t * (3.0 - 2.0 * t) : 0.0;
}
// Whether the lamp's centre meets the point as its ray's first hit: the on-purpose way's own ray (lamp_around_light), from
// the lamp's own reach on, ending half a millimetre short of the point.
bool near_first(LightRecord l, float3 y) {
    const float3 v = y - l.position;
    const float r = length(v), own = light_own_reach(l), end = r - 5e-4;
    if (!(end > own)) return true;
    Hit h;
    return !trace_closest(l.position, v / r, own, end, h);
}

// Next-event estimation at a diffuse path vertex: the sun, and one of the lights that reach it, each with
// a shadow ray (tinted by the glass it crosses). Up to eight of the cell's lights compete by their unshadowed irradiance
// (resampled importance sampling), so a light next to the vertex is chosen as often as it matters. An emissive cluster
// among them competes with one of its triangles and a point of it, whose shadow ray goes there. A translucent vertex (a
// blade of grass, a leaf: TGPT_MATERIAL_TRANSLUCENT) is lit from behind too, as the camera's translucent pixels are
// (translucent_cos), its shadow rays leaving from the side the light is on: lit from its own side alone, the underside of
// every sunlit blade was black to the rays that met it, and the ground under grass took no light through the grass.
// shading: the normal the cosines are taken with (a normal-mapped one: SurfaceMaterial.normal); n, the triangle's, decides
// which side a ray leaves from, and an opaque surface takes no light its triangle faces away from.
// With alpha > 0 (direct_light) the surface's GGX specular lobe (f0, alpha, seen from v) takes the same samples' light as well,
// as pt_rtxdi shades a camera pixel's light sample: unbiased, as the lobe takes light only where the diffuse target does (the
// sun's from a point of its disc, as the camera pixel's). A sharp highlight is noisier for it. Path vertices take the diffuse
// light alone; what a reflection shows takes both (shade_seen, PT-46).
// tint: the color of the light from behind (a thin sheet's own share per channel over through, SurfaceMaterial.tint).
#ifdef PT_PSR_CAPTURE
// PT-127, primary surface replacement (pt_psr_record.hlsli; the reflections' pass alone compiles this): the light of what a
// still pure mirror shows, taken apart as it is added up, for NRD. g_psrSlot: the pixel's record while the end of its
// reflection ray is shaded (INVALID: no mirror), the one word of it the pass's thread carries across its rays: the parts
// go straight into the record, in memory. The last direct_light's two parts, and the fog's transmittance over the
// reflection ray (pt_medium fogged).
static float3 g_psrDirectDiffuse = 0.0, g_psrDirectSpecular = 0.0;
static uint g_psrSlot = INVALID;
static float g_psrFogTransmission = 1.0;
// An ordinary surface at the end of the mirror's ray: its shading normal, roughness and colours and its diffuse and specular
// light (storage units; the distances and the mirror's own throughput come after: pt_psr_capture psr_finish). NRD's
// material class as pt_nrd_prep gives it to a camera's surface: 2 a surface of no diffuse colour, 0 any other.
float3 psr_finite(float3 c) { return all(c >= 0.0) && all(c < 1e30) ? c : 0.0; } // (a part that is no number is left to the rest of the pixel's light)
void psr_capture(float3 normal, float roughness, float3 albedo, float3 f0, float3 diffuse, float3 specular) {
    RWStructuredBuffer<PsrRecord> records = ResourceDescriptorHeap[g.psr.x];
    records[g_psrSlot].light = psr_light(float4(psr_finite(to_storage(diffuse)), 0.0), float4(psr_finite(to_storage(specular)), 0.0));
    records[g_psrSlot].surface = uint4(pack_normal(normal), 0u, psr_state_word(roughness, all(albedo == 0.0) ? 2u : 0u, PSR_CAPTURED), g.psr.w);
    records[g_psrSlot].material = uint4(psr_halves(albedo.r, albedo.g), psr_halves(albedo.b, f0.r), psr_halves(f0.g, f0.b), 0u);
}
// And a glossy metal's own reflection ray, which shade_seen follows after that: its light is the end's specular light too,
// and its length the specular hit distance (the first bounce after the replaced surface, as it is).
void psr_capture_reflection(float3 light, float distance) {
    RWStructuredBuffer<PsrRecord> records = ResourceDescriptorHeap[g.psr.x];
    const PsrRecord r = records[g_psrSlot];
    if (psr_state(r.surface) != PSR_CAPTURED) return;
    const float4 specular = psr_specular(r);
    records[g_psrSlot].light = psr_light(psr_diffuse(r), float4(specular.rgb + psr_finite(to_storage(light)), min(distance, HIT_DISTANCE_MISS)));
    records[g_psrSlot].surface = uint4(r.surface.xy, (r.surface.z & 0x00FFFFFFu) | (PSR_REFLECTED << 24u), r.surface.w);
}
#endif
float3 direct_light(float3 position, float3 n, float3 shading, float3 albedo, float3 f0, float alpha, float3 v, float offset, inout Rng rng, float through, float3 tint) {
    float3 result = 0.0, specular = 0.0;
    const bool lobe = alpha > 0.0;
    // ON TRIAL (lamp_around): the ray that met this vertex, where its caller armed it for this one call (alpha 0: not).
    const NearRay arrive = g_nearArmed;
    g_nearArmed.alpha = 0.0;
#ifdef PT_SPEED_MEASURE
    if (g.sunColor.w > 0.0 && !speed_measure(SPEED_M_NO_SUN)) {
#else
    if (g.sunColor.w > 0.0) {
#endif
        float3 l = cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2());
        float nl = through > 0.0 || dot(n, l) > 0.0 ? translucent_cos(shading, l, through) : 0.0;
        if (nl > 0.0) {
            // HDRP's fog between the surface and the sun: only the sun's light is in it.
            const float3 sun = g.sunColor.rgb * transmittance(position + n * (dot(n, l) >= 0.0 ? offset : -offset), l, 0.0, g.params.y) * sun_fog(position);
            result += sun * nl * (dot(shading, l) > 0.0 ? float3(1.0, 1.0, 1.0) : tint);
            if (lobe && dot(n, l) > 0.0) specular += sun * ggx_specular(shading, v, l, f0, alpha);
        }
    }
    LightLists lists;
#ifdef PT_SPEED_MEASURE
    if (!speed_measure(SPEED_M_NO_LAMPS) && light_lists(position, lists)) {
#else
    if (light_lists(position, lists)) {
#endif
        const uint candidates = lists.candidates;
        const float u = rng.next();
        float wsum = 0.0, chosenTarget = 0.0;
        float3 chosenE = 0.0, chosenL = 0.0, chosenPoint = 0.0;
        LightRecord chosen = (LightRecord)0;
        for (uint c = 0; c < candidates; ++c) {
            float invPdf;
            const uint picked = lists_candidate(lists, c, u, invPdf);
            if (!(invPdf > 0.0)) continue; // weight 0 (a far candidate its cell's list holds too: weight 0, review-perf-astra 7): nothing to weigh
            LightRecord light = load_light(picked);
            float3 l, y; float dist;
            float3 e;
            if (light.type == LIGHT_CLUSTER) {
                light = load_light(cluster_triangle(light, rng.next(), invPdf));
                e = triangle_incident(light, position, rng.next2(), l, dist, y);
            } else {
                // A path vertex a few centimetres from a flame (a candle's own holder, the wick's surroundings) takes all of
                // its light, I / d^2 to the light's own size (PT-50: until 2026-10-07 as if 25 cm away at the nearest, which
                // took away the light of everything right by a candle to keep its rare bright bounce samples down).
                e = light_incident(light, position, l, dist); y = light.position;
            }
            float target = through > 0.0 || dot(n, l) > 0.0 ? luminance(e) * translucent_cos(shading, l, through) : 0.0;
            float w = target * invPdf;
            wsum += w;
            if (w > 0.0 && rng.next() * wsum < w) { chosenTarget = target; chosenE = e; chosenL = l; chosen = light; chosenPoint = y; }
        }
        if (chosenTarget > 0.0) {
            float3 origin = position + n * (dot(n, chosenL) >= 0.0 ? offset : -offset);
            float tmax;
            float3 dir;
            if (chosen.type == LIGHT_TRIANGLE) dir = triangle_shadow_ray(origin, chosenPoint, tmax);
            else { dir = shadow_ray(chosen, origin, rng.next2(), tmax); tmax = max(tmax - offset, 0.0); }
#ifdef PT_SPEED_MEASURE
            g_speedNoRay = speed_measure(SPEED_M_NO_LAMP_RAY);
#endif
            const float3 arriving = chosenE * (wsum / (float(candidates) * chosenTarget)) * transmittance(origin, dir, 0.0, tmax);
            // ON TRIAL (lamp_around; not armed: kept is exactly 1): the share of a lamp's diffuse light here that this chance
            // hit keeps, the on-purpose way (lamp_around_light) bringing the rest. The highlight below stays whole.
            float kept = 1.0;
            if (arrive.alpha != 0.0 && chosen.type <= 1u && any(arriving > 0.0)) {
                float ns, na, nb;
                near_split(arrive, position, n, chosen, ns, na, nb);
                if (ns > 0.0 && near_first(chosen, position)) kept = 1.0 - ns * na / (na + nb);
            }
            result += arriving * kept * translucent_cos(shading, chosenL, through) * (dot(shading, chosenL) > 0.0 ? float3(1.0, 1.0, 1.0) : tint);
            // ON TRIAL (C, lamp_trial): the lamp's highlight towards a point of a disc of its other radius, not its centre
            // (off: its centre, as before, and no random number more).
            float3 highlightL = chosenL;
            if (lobe && chosen.type <= 1u && lamp_trial_mode(LAMP_TRIAL_HIGHLIGHT) != 0u)
                highlightL = lamp_disc_dir(chosen, position, lamp_trial_radius(chosen, LAMP_TRIAL_HIGHLIGHT), rng.next2());
            if (lobe && dot(n, chosenL) > 0.0) specular += arriving * ggx_specular(shading, v, highlightL, f0, alpha);
        }
    }
#ifdef PT_PSR_CAPTURE
    g_psrDirectDiffuse = result * albedo / PI; g_psrDirectSpecular = specular;
#endif
    return result * albedo / PI + specular;
}
float3 direct_light(float3 position, float3 n, float3 shading, float3 albedo, float3 f0, float alpha, float3 v, float offset, inout Rng rng, float through) {
    return direct_light(position, n, shading, albedo, f0, alpha, v, offset, rng, through, float3(1.0, 1.0, 1.0));
}
float3 direct_diffuse(float3 position, float3 n, float3 shading, float3 albedo, float offset, inout Rng rng, float through, float3 tint) {
    return direct_light(position, n, shading, albedo, 0.0, 0.0, n, offset, rng, through, tint);
}
float3 direct_diffuse(float3 position, float3 n, float3 shading, float3 albedo, float offset, inout Rng rng, float through) {
    return direct_light(position, n, shading, albedo, 0.0, 0.0, n, offset, rng, through);
}
float3 direct_diffuse(float3 position, float3 n, float3 albedo, float offset, inout Rng rng, float through) { return direct_diffuse(position, n, n, albedo, offset, rng, through); }
float3 direct_diffuse(float3 position, float3 n, float3 albedo, float offset, inout Rng rng) { return direct_diffuse(position, n, n, albedo, offset, rng, 0.0); }
// Indirect Color (prevOriginDelta.w): bounced light's color scaled around its brightness (1 as traced, 0 grey), which it keeps,
// but never past the point where a channel would go negative: scaled plainly and clamped at 0, a candle's warm bounce off
// brown wood became pure red at 3 (green and blue at 1 to 5 of 255 across the traced tavern), and brighter. A camera pixel's
// bounced light takes it (pt_gi_spatial), and so does that of what a reflection shows (shade_seen).
float3 indirect_color(float3 c) {
    const float l = luminance(c);
    float scale = g.prevOriginDelta.w;
    [unroll] for (uint i = 0; i < 3; ++i)
        if (scale > 1.0 && c[i] < l) scale = max(1.0, min(scale, l / max(l - c[i], 1e-6)));
    return l + (c - l) * scale;
}
// ---------------------------------------------------------------- radiance cache
#include "pt_restir.hlsli" // finite_radiance, which its samples take
// A world-space hash grid of the bounced light arriving at surfaces: per cell, the mean radiance that cosine-distributed
// rays from its surfaces bring back (irradiance / pi, which a diffuse surface of albedo a sends on times a). Paths end on
// it: the last vertex a path shades takes its cell's light where it had none, so a path of the Bounces setting's length,
// a reflection (one vertex) and what glass shows carry the light of every bounce after theirs (a reflected wall lit only
// by bounced light was black). It is filled where it is read: the paths a pixel feeds it with (a quarter of them) trace one more
// ray from their last vertex for that vertex's cell, a ray that ends on the cache in turn, so light bounces on through it
// from frame to frame; and the bounced light's first rays from HDRP's surfaces count for theirs. Cells are the world's
// (the scene origin moves), CACHE_CELL across up to 5 m from the camera and twice as large with each doubling of the
// distance beyond, one per side of a surface (by the axis its normal is nearest); pt_cache.hlsl resolves them each frame.
static const uint CACHE_SLOTS = 1u << 20;          // tracer.cpp kCacheSlots
static const uint CACHE_BUCKET = 8u;               // the slots a cell may take, from its bucket's first
static const float CACHE_SCALE = 4096.0;           // the fixed point of a frame's sums, in storage units (to_storage)
static const uint CACHE_SAMPLES_PER_FRAME = 512u;  // a cell's samples a frame at most
static const float CACHE_SAMPLE_MOST = 2000.0;     // a sample's channel at most, in storage units: 512 of them fit 32 bits
static const float CACHE_CELL = 0.1;               // the finest cells, in metres
static const float CACHE_PER_METRE = 0.02;         // a cell's size for each metre from the camera, beyond the finest
static const float CACHE_FRAMES = 16.0;            // the longest a cell's light is averaged over, in frames
static const uint CACHE_MAX_AGE = 240u;            // frames without a sample before a cell is given up
bool cache_on() { return g.cache.w != 0u && g.cache.x != INVALID; }
// Whether a pixel's paths feed the cache this frame: a quarter of the screen's 8x8 tiles, another quarter each frame
// (whole tiles, so that the threads of a wave take the extra ray together or not at all). salt: a pass's own quarter.
#ifdef PT_SPEED_MEASURE
bool cache_feeds(uint2 p, uint salt) { return cache_on() && !speed_measure(SPEED_M_NO_FEED) && ((pcg((p.x >> 3) + (p.y >> 3) * 4096u) + g.size.z + salt) & 3u) == 0u; }
#else
bool cache_feeds(uint2 p, uint salt) { return cache_on() && ((pcg((p.x >> 3) + (p.y >> 3) * 4096u) + g.size.z + salt) & 3u) == 0u; }
#endif
uint cache_level(float3 position) {
    const float d = length(position - g.cameraPos.xyz);
    return uint(clamp(floor(log2(max(d * CACHE_PER_METRE, CACHE_CELL) / CACHE_CELL) + 0.5), 0.0, 15.0));
}
// The first slot of a cell's bucket (in bytes, per 4-byte key), and the key the slot holding it carries (never 0, an
// empty slot's).
uint2 cache_key(float3 position, float3 normal, uint level) {
    const int3 c = int3(floor((position + g.cacheOrigin.xyz) / (CACHE_CELL * exp2(float(level)))));
    const float3 a = abs(normal);
    const uint side = (a.x >= a.y && a.x >= a.z) ? (normal.x < 0.0 ? 1u : 0u) : (a.y >= a.z ? (normal.y < 0.0 ? 3u : 2u) : (normal.z < 0.0 ? 5u : 4u));
    const uint h = pcg(asuint(c.x) ^ pcg(asuint(c.y) ^ pcg(asuint(c.z) ^ pcg(level | (side << 4)))));
    return uint2((h % (CACHE_SLOTS / CACHE_BUCKET)) * CACHE_BUCKET, pcg(h ^ 0x6A09E667u) | 1u);
}
// The light the cache holds for a surface point's cell, 0 where it holds none yet. The point is moved at random across
// a cell's width within the surface's plane first: cells' edges become noise the denoisers take out, not steps.
float3 cache_light(float3 position, float3 normal, inout Rng rng) {
#ifdef PT_SPEED_MEASURE
    if (speed_measure(SPEED_M_NO_CACHE_READ)) return 0.0;
#endif
    const uint level = cache_level(position);
    float3 t, b; basis(normal, t, b);
    const float2 u = rng.next2() - 0.5;
    const uint2 key = cache_key(position + (t * u.x + b * u.y) * (CACHE_CELL * exp2(float(level))), normal, level);
    RWByteAddressBuffer keys = ResourceDescriptorHeap[g.cache.x];
    RWByteAddressBuffer light = ResourceDescriptorHeap[g.cache.z];
    for (uint i = 0; i < CACHE_BUCKET; ++i) {
        if (keys.Load((key.x + i) * 4u) != key.y) continue;
        const uint4 v = light.Load4((key.x + i) * 16u);
        return (v.w & 0xFFFFu) != 0u ? asfloat(v.xyz) : 0.0;
    }
    return 0.0;
}
// One sample of the light arriving at a surface point (along a cosine-distributed ray) into this frame's sums of its
// cell, which it takes a slot for if it has none. A full bucket drops it.
// Each channel is bounded on its own, so that a cell's sums never pass 32 bits (wrapping its light to a sliver of it).
// A limit that is left (PT-50, docs/agents/caps.md): a cell's light is its sums over its samples, so it holds no more
// than CACHE_SAMPLE_MOST times the reference radiance, and what glows brighter than 2,000 times a white wall 1 m from
// the brightest lamp loses the rest in the paths that end on the cache (the radiance clamps held a sample to 64 times
// until 2026-10-07, so this was never met before). Taking a brighter sample at the most with a chance cannot mend it: its
// mean is then below the most. The ways out: a smaller CACHE_SCALE or fewer samples a frame (noise in dim cells), or a
// reference radiance that counts the scene's brightest glow.
void cache_add(float3 position, float3 normal, float3 radiance, inout Rng rng) {
    float3 v = to_storage(radiance);
    if (!all(and(v >= 0.0, v < 1e30))) return; // NaN, infinite or negative
    v = min(v, CACHE_SAMPLE_MOST) * CACHE_SCALE;
    const uint2 key = cache_key(position, normal, cache_level(position));
    RWByteAddressBuffer keys = ResourceDescriptorHeap[g.cache.x];
    RWByteAddressBuffer sums = ResourceDescriptorHeap[g.cache.y];
    uint slot = INVALID;
    for (uint i = 0; i < CACHE_BUCKET && slot == INVALID; ++i) if (keys.Load((key.x + i) * 4u) == key.y) slot = key.x + i;
    for (uint j = 0; j < CACHE_BUCKET && slot == INVALID; ++j) {
        uint prior;
        keys.InterlockedCompareExchange((key.x + j) * 4u, 0u, key.y, prior);
        if (prior == 0u || prior == key.y) slot = key.x + j;
    }
    if (slot == INVALID) return;
    uint count;
    sums.InterlockedAdd(slot * 16u + 12u, 1u, count);
    if (count >= CACHE_SAMPLES_PER_FRAME) return;
    const uint3 q = uint3(v + rng.next()); // rounded at random: unbiased however small the light
    sums.InterlockedAdd(slot * 16u, q.x); sums.InterlockedAdd(slot * 16u + 4u, q.y); sums.InterlockedAdd(slot * 16u + 8u, q.z);
}
// Where the path shade_path follows was sampled from (a vertex's shading position), and whether its first ray gathers light
// for a diffuse surface there: set by the pass that calls it. pt_gi's rays do (the camera's surface, whose ReSTIR DI samples
// the emissive lights); a reflection's or a view through glass does not (ReSTIR DI gives no emissive triangle's specular
// light, and nothing samples what glass lets through). Each later ray of a path gathers for the vertex it leaves, whose light
// sampling (direct_diffuse) takes the emissive lights in.
static float3 g_pathFrom = 0.0;
static bool g_pathGathers = false;
// The share of its emission a path counts where it meets an emissive triangle, having come from `from`. A triangle sampled as
// a light was taken in there by the light sampling as far as its reach's window, 1 at the triangle falling to 0 at its reach
// (triangle_incident): a ray that gathers for a diffuse surface counts the rest, so the two count each point's light exactly
// once between them, unbiased whatever the reach, and the image converges to the same light with the Emissive Lights row on
// or off (only the noise differs). A kept light's own surface (EMITTER_PAIRED) lights nothing, its light being the light's,
// but is seen: in a reflection, through glass. The rest counts whole, as does everything seen from a vertex outside the light
// grid (nothing is sampled there). Whatever gathers a path's light at its vertices (the radiance cache) weighs it the same.
// The record of the emissive triangle a ray met, if it is one of the emitters' (the instance table: an instance's entry, then
// its submeshes' first slots).
bool emitter_record(Hit hit, out LightRecord t) {
    t = (LightRecord)0;
    if (g.emissive2.x == INVALID || hit.instance >= g.emissive2.y) return false;
    StructuredBuffer<uint> table = ResourceDescriptorHeap[g.emissive2.x];
    const uint entry = table[hit.instance];
    if (entry == INVALID) return false;
    const uint slot = table[entry + hit.geometry];
    if (slot == INVALID) return false;
    t = load_emitter(slot + hit.primitive, false);
    return true;
}
float emission_weight(Hit hit, float3 from, bool gathers) {
    LightRecord t;
    if (!gathers || !emitter_record(hit, t)) return 1.0;
    if ((t.flags & EMITTER_PAIRED) != 0) return 0.0;
    uint cellFirst, cellCount;
    if ((t.flags & EMITTER_SAMPLED) == 0 || !light_cell(from, cellFirst, cellCount)) return 1.0;
    const float3 d = hit.position - from;
    return 1.0 - smooth_window(dot(d, d), 1.0 / max(t.range * t.range, 1e-8), 1.0);
}
float3 path_emission(Hit hit, float3 emissive, float3 from, bool gathers) {
    if (!any(emissive > 0.0)) return 0.0;
    const float weight=emission_weight(hit, from, gathers);
    if (weight==1.0 || g.decals.z==0u) return emissive*weight;
    SurfaceMaterial decal=(SurfaceMaterial)0;
    StructuredBuffer<MaterialRecord> materials=ResourceDescriptorHeap[g.scene0.w];
    const MaterialRecord receiver=materials[hit.material];
    float2 gx,gy; material_gradients(hit,receiver,gx,gy);
    decal.normal=material_normal(hit,receiver,hit.uv*receiver.uvST.xy+receiver.uvST.zw,gx,gy,hit.normal);
    apply_decals(hit, decal, 0.0, 0.0);
    // The ordinary emitter samplerIndex reads the receiver's own emission; the decal is gathered by paths alone.
    return max(emissive-decal.emissive,0.0)*weight+decal.emissive;
}
// ---------------------------------------------------------------- light inside translucent materials (PT-40, PT-43)
// Wax, skin, teeth: what HDRP draws with subsurface scattering. Light that meets such a thing goes in, is scattered and
// absorbed inside it, and comes out somewhere else: a random walk through the traced mesh itself (volumetric path tracing
// of a homogeneous medium inside the mesh, as production renderers do it: Chiang, Kutz and Burley 2016, "Practical and
// Controllable Subsurface Scattering for Production Path Tracing"; Wrenninge, Villemin and Hery 2017), which replaces HDRP's screen-space blur of the light (its Burley filter) and its thin-object transmission
// while tracing. A candle's folds and pockets, which no light reaches from outside, are lit by what went in at the top; an
// ear or a nostril with a lamp behind it glows, because the walk comes out on the lamp's side.
//   The medium is the material's own, from HDRP's diffusion profile and the surface's color. Its mean-free-path conversion
//   follows HDRP's SubSurface.hlsl (RemapSubSurfaceScatteringParameters); the bright-end albedo fit there is replaced by the
//   transport inverse below. Per channel the profile's scattering distance d (1 / shape; Christensen and Burley's d, "Approximate
//   Reflectance Profiles for Efficient Subsurface Scattering", 2015) is a mean free path of d x s(A), with their fit
//   s(A) = 1.9 - A + 3.5 (A - 0.8)^2 for light that enters and leaves diffusely, and the color A becomes the share of
//   the light a scattering keeps, from the inverse half-space reflectance below, so a thick flat piece under even
//   light sends back A of it, as a painted surface of that color does (checked in tgpt_gpu_test against a walk on the CPU).
//   The walk: in at the surface in a cosine-distributed direction (a diffuse entry, as the fit assumes), then free flights
//   drawn from one channel's exponential, the channel picked by what the walk still carries and the three channels'
//   densities weighed together (the hero-wavelength balance heuristic, Wilkie et al. 2014: unbiased in every channel),
//   scattered evenly in all directions. A flight that crosses the thing's own surface from inside ends the walk there (a
//   surface of the same substance entered from outside is another piece of it grown into this one: the walk goes on
//   inside, and counts how deep it is); anything else inside it (a wick, a holder's spike) reflects the walk diffusely by
//   its own color. The light is then gathered at the exit as a diffuse surface gathers it (the callers).
//   No walk is cut short: it ends by leaving the mesh or by Russian roulette on what it carries, which keeps the mean.
//   Roulette bounds the expected work, never the path length. There is no fixed flight cutoff.
static const uint WALK_LATE = 1024u;
bool subsurface_on() { return (g.settings2.y & OPTION_NO_SUBSURFACE) == 0; }
// Inverse diffuse reflectance of an isotropic half-space, from the transport
// H equation (tools/candle/albedo.py). Fit sqrt(absorption)/(1-A), preserving
// black and conservative white exactly. Max diffuse-reflectance error <2e-5.
// Horner form avoids a dynamically indexed constant array in every ray shader.
float walk_albedo(float a) {
    a = saturate(a);
    float q = -0.6949370474;
    q = q * a + 2.7925121644;
    q = q * a - 4.8544449522;
    q = q * a + 5.0000026126;
    q = q * a - 3.6805076413;
    q = q * a + 2.3141009961;
    q = q * a - 1.4438572734;
    q = (q * a + 1.0) * (1.0 - a);
    return 1.0 - q * q;
}
void subsurface_medium(float3 albedo, float3 shape, out float3 sigmaT, out float3 keeps) {
    const float3 a = saturate(albedo);
    keeps = float3(walk_albedo(a.r), walk_albedo(a.g), walk_albedo(a.b));
    sigmaT = shape / (1.9 - a + 3.5 * (a - 0.8) * (a - 0.8));
}
float3 sphere_sample(float2 u) {
    const float z = 1.0 - 2.0 * u.x, r = sqrt(max(0.0, 1.0 - z * z)), phi = 2.0 * PI * u.y;
    return float3(r * cos(phi), r * sin(phi), z);
}
// Whether a traced material is one the walk goes inside, and its profile's shape: one the game's own shader scatters light
// under the surface of (TGPT_MATERIAL_SUBSURFACE: the plugin sets it where the material's subsurface feature is on; naming a
// diffusion profile is not that, clothes, armour and the ground name one too), with its profile's scattering distances.
// Not a leaf, cloth, hair or glass: a thin thing has no inside to walk through (its light from behind is thin_through's).
// Who walks, of those:
//   by the material: all of the above;
//   by the mesh (subsurface_bounds): a material drawn one-sided is a solid's outside by how the game draws it (its inside
//   is never seen: skin, a candle of plain wax); one drawn from both sides is a solid only where the piece of the mesh
//   the ray met closes a volume (piece_solid: a candle's wax with cut-out bits, MATERIAL_DOUBLE_SIDED), and is entered
//   from its front. A sheet of it (a card, a leaf-like flap) has no inside and stays a plain surface;
//   by the point (SurfaceMaterial.subsurface, the game's subsurface mask there): the share of the diffuse light that
//   scatters. A vertex takes the walk with that chance and is a plain surface the rest of the time: the mean is the
//   mask's blend of the two, unbiased, and a walk's rays (3 to 50 where a plain vertex takes none) are traced only where
//   it is taken. A mask of 0 walks never (the game's terrain: a profile, the feature on, a mask of 0).
static const uint MATERIAL_NO_WALK = MATERIAL_TRANSLUCENT | MATERIAL_THIN_CLOTH | MATERIAL_STRANDS | MATERIAL_GLASS | MATERIAL_CAMERA_ONLY | MATERIAL_HAIR_LAYER;
bool subsurface_record(MaterialRecord m, out float3 shape) {
    shape = m.subsurface;
    return subsurface_on() && (m.flags & (MATERIAL_SUBSURFACE | MATERIAL_NO_WALK)) == MATERIAL_SUBSURFACE && all(shape > 0.0) && all(shape < 1e30);
}
bool subsurface_material(uint material, out float3 shape) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    return subsurface_record(materials[material], shape);
}
// Whether the surface a ray met bounds a volume of its material (by the mesh, above).
bool subsurface_bounds(MaterialRecord m, Hit h) {
    return (m.flags & MATERIAL_DOUBLE_SIDED) == 0 || piece_solid(h.instance, h.geometry, h.primitive);
}
// Whether light walks in where a ray met a surface from outside (by the material and the mesh; the caller has the mask).
bool subsurface_hit(Hit h, out float3 shape) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    const MaterialRecord m = materials[h.material];
    if (!subsurface_record(m, shape)) return false;
    return (m.flags & MATERIAL_DOUBLE_SIDED) == 0 || (h.front && piece_solid(h.instance, h.geometry, h.primitive));
}
// Where a walk came out: the point, the surface's own orientation there (out of the thing) and its mesh's smooth normal,
// what the walk carries per channel (the color is in it), and how many rays it took.
struct WalkExit { float3 position; float3 normal; float3 shading; float3 throughput; uint steps; };
// The walk from where a ray met the thing from outside (entry: its point, its orientation towards the outside, the side
// met, its material) with the surface's color there and its profile's shape (per metre). False: absorbed inside.
bool subsurface_walk(Hit entry, float3 albedo, float3 shape, inout Rng rng, out WalkExit e) {
    e = (WalkExit)0;
    float3 sigmaT, keeps;
    subsurface_medium(albedo, shape, sigmaT, keeps);
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    const MaterialRecord own = materials[entry.material];
    const float3 inward = -entry.normal;
    // A few steps of a float where the thing is (positions are relative to the scene's origin, up to 512 m away, a step
    // there 0.06 mm; skin scatters within tenths of a millimetre): how far along a ray that starts on a surface (the entry,
    // something met inside) that surface is not looked for, and a crossed one is stepped past. The walk starts on the entry
    // itself, not a step inside (a step is half a free flight in skin up there), and is kept relative to it, where a float
    // is exact to nanometres.
    const float eps = max(abs(entry.position.x), max(abs(entry.position.y), abs(entry.position.z))) * 2.5e-7 + 1e-6;
    float3 local = 0.0;
    float skip = eps;
    float3 dir = cosine_sample(inward, rng.next2());
    float3 throughput = 1.0;
    float3 pick = 1.0 / 3.0;
    float3 origin = entry.position;
    float t = 0.0, tmin = 0.0;
    int depth = 1;
    bool fresh = true;
    const float coneWidth = g_coneWidth, coneSpread = g_coneSpread;
    ray_cone(entry.footprint, 0.0);
    bool left = false;
    [loop] for (uint step = 0; ; ++step) {
        if (fresh) {
            // A new flight: the channel by what the walk carries, the distance from that channel's exponential.
            const float sum = throughput.r + throughput.g + throughput.b;
            if (!(sum > 0.0) || !(sum < 1e30)) break;
            pick = throughput / sum;
            const float u = rng.next();
            const uint c = u < pick.r ? 0u : (u < pick.r + pick.g ? 1u : 2u);
            t = -log(max(1.0 - rng.next(), 1e-30)) / sigmaT[c];
            origin = entry.position + local; tmin = skip; skip = 0.0; fresh = false;
        }
        Hit h;
        if (!trace_closest(origin, dir, tmin, t, h)) {
            // Scattered after t: the three channels' densities of that, weighed by how each was picked.
            const float3 tr = exp(-sigmaT * t);
            throughput *= keeps * sigmaT * tr / max(dot(pick, sigmaT * tr), 1e-37);
            local += dir * t;
            dir = sphere_sample(rng.next2());
            fresh = true;
        } else {
            const float th = max(dot(h.position - origin, dir), 0.0);
            const MaterialRecord met = materials[h.material];
            const bool same = h.material == entry.material ||
                ((met.flags & (MATERIAL_SUBSURFACE | MATERIAL_NO_WALK)) == MATERIAL_SUBSURFACE && (own.flags & MATERIAL_SUBSURFACE) != 0 && all(met.subsurface == own.subsurface));
            // A sheet of the same substance in the way (a card drawn from both sides, a wick's, stuck into the wax): it
            // has no inside to enter or leave, and the flight goes on through it.
            if (same && !subsurface_bounds(met, h)) { tmin = th + eps; continue; }
            if (same) {
                // The substance's own surface: another piece of it entered, or one left; out of the last, the walk is out.
                if (h.front == entry.front) { ++depth; tmin = th + eps; continue; }
                if (--depth > 0) { tmin = th + eps; continue; }
                const float3 tr = exp(-sigmaT * th);
                throughput *= tr / max(dot(pick, tr), 1e-37);
                e.position = h.position; e.normal = -h.normal; e.shading = -h.rounded; e.throughput = throughput; e.steps = step + 1u;
                left = all(throughput >= 0.0) && any(throughput > 0.0) && all(throughput < 1e30);
                break;
            }
            // Something else inside it: reached without scattering, and the walk goes on from it by its color.
            const float3 tr = exp(-sigmaT * th);
            g_hitInside = true;
            const float3 inside = hit_material(h).reflectance;
            g_hitInside = false;
            throughput *= tr / max(dot(pick, tr), 1e-37) * inside;
            local = h.position - entry.position;
            skip = eps;
            dir = cosine_sample(h.normal, rng.next2());
            fresh = true;
        }
        // Russian roulette by throughput, then periodic weighted survival for long walks.
        const float q = min(max(throughput.r, max(throughput.g, throughput.b)), 1.0);
        if (!(q > 0.0) || rng.next() >= q) break;
        throughput /= q;
        if (step >= WALK_LATE && (step & 127u) == 127u) { if (rng.next() >= 0.5) break; throughput *= 2.0; }
    }
    ray_cone(coneWidth, coneSpread);
    return left;
}
// A path vertex on a subsurface material (bounced light, the radiance cache, what a reflection shows): the light it sends
// on is what comes out where a walk from it leaves the thing, so the vertex moves there: its position and orientation
// the exit's, its diffuse color what the walk carries (0: absorbed inside), its rays leaving from just above the traced
// triangle it now lies on. False where the material has no such inside, the mesh is a sheet there, or the vertex is a
// plain surface this time by the mask at the point (everything stays as it was): who walks, above.
bool subsurface_vertex(inout Hit hit, inout SurfaceMaterial m, inout float offset, inout Rng rng) {
    float3 shape;
    if (!(m.subsurface > 0.0) || !subsurface_hit(hit, shape)) return false;
    if (m.subsurface < 1.0 && rng.next() >= m.subsurface) return false;
    WalkExit e;
    if (subsurface_walk(hit, m.reflectance, shape, rng, e)) {
        hit.position = e.position; hit.normal = e.normal; hit.rounded = e.shading;
        m.normal = e.shading; m.reflectance = e.throughput; m.albedo = e.throughput;
        offset = 0.001 + length(e.position - g.cameraPos.xyz) * 0.0002;
    } else { m.reflectance = 0.0; m.albedo = 0.0; }
    m.through = 0.0; m.tint = 1.0;
    return true;
}
// ON TRIAL (lamp_around, above direct_light): what stands right by a lamp, found on purpose for the vertex a ray leaves
// (ray: that ray, as the weighing knows it; offset: its start above the vertex's surface). Returns what the vertex's own ray
// would have brought back from the point found, weighed (s b / (a + b), over the chance of the lamp drawn): for a
// cosine-distributed ray that is its share of the mean such rays bring; a lobe's ray multiplies it by its estimator's weight
// for dir, the direction to the point (pt_specular). leavesAt, leavesNormal: where the light leaves for the vertex (ReSTIR GI's
// sample). seen: the point's diffuse color as what a reflection shows takes it (shade_seen: albedo), else as a path vertex
// does (reflectance). nearBack < 0: the ray back to the vertex as a path's (trace_closest), else as a camera surface's
// (trace_from_surface with that nearBack; mirror: g_mirrorRay for it).
// Wax, skin: the light goes in where the lamp's ray lands and the vertex moves to where a walk from there comes out
// (subsurface_vertex), the path a chance ray's walk takes backwards. The two agree as far as the walk is the same both
// ways (one substance, one color over the walk's reach); not checked on a GPU yet (docs/agents/flame-near.md).
float3 lamp_around_light(NearRay ray, float offset, float nearBack, bool seen, bool mirror, inout Rng rng, out float3 dir, out float3 leavesAt, out float3 leavesNormal) {
    dir = 0.0; leavesAt = 0.0; leavesNormal = 0.0;
    const float around = lamp_around();
    LightLists lists;
    if (!(around > 0.0) || ray.alpha == 0.0 || !light_lists(ray.from, lists)) return 0.0;
    // One lamp of the vertex's lists: a candidate at random of those a stream of them all would take (lists_candidate).
    float invPdf;
    const uint c = min(uint(rng.next() * float(lists.candidates)), lists.candidates - 1u);
    const uint index = lists_candidate(lists, c, rng.next(), invPdf);
    if (index >= EMITTER_BASE || !(invPdf > 0.0)) return 0.0;
    const LightRecord lamp = load_light(index);
    const float own = light_own_reach(lamp);
    if (lamp.type > 1u || !(around > own)) return 0.0;
    const float coneWidth = g_coneWidth, coneSpread = g_coneSpread;
    ray_cone(0.0, DIFFUSE_SPREAD); // what it meets is read wide, as a bounce's ray reads it
    float3 result = 0.0;
    Hit y;
    if (trace_closest(lamp.position, sphere_sample(rng.next2()), own, around, y) && y.water == 0) {
        StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
        const uint flags = materials[y.material].flags;
        const bool cloth = (flags & MATERIAL_THIN_CLOTH) != 0 && (flags & MATERIAL_ALPHA_TEST) != 0; // cloth_stops
        float s, a, b;
        near_split(ray, y.position, y.normal, lamp, s, a, b);
        if (s > 0.0 && (flags & MATERIAL_GLASS) == 0 && !cloth) {
            const SurfaceMaterial m = hit_material(y);
            if (!(m.through > 0.0)) {
                const float fromCamera = length(y.position - g.cameraPos.xyz);
                float litOffset = 0.01 + fromCamera * 0.001;
                Hit leaves = y; SurfaceMaterial inside = m; float leavesOffset = litOffset;
                const bool walked = subsurface_vertex(leaves, inside, leavesOffset, rng);
                if (walked) litOffset = 0.001 + fromCamera * 0.0002; // a walk's exit's (subsurface_vertex)
                // The lamp's light at y, as direct_light gives a path vertex there: on the side its triangle faces the lamp,
                // by its shading normal (a walk's exit: its smooth one), through a shadow ray to the lamp's disc.
                float3 l; float dist;
                const float3 e = light_incident(lamp, y.position, l, dist);
                const float cosLit = dot(y.normal, l) > 0.0 ? saturate(dot(walked ? y.rounded : m.normal, l)) : 0.0;
                const float3 color = walked ? inside.reflectance : (seen ? m.albedo : m.reflectance);
                const float3 way = leaves.position - ray.from;
                const float len2 = max(dot(way, way), 1e-8);
                dir = way * rsqrt(len2);
                const float facing = -dot(leaves.normal, dir); // the point's surface turned to the vertex
                const float bz = min(near_ray_pdf(ray, dir) * abs(facing) / len2, 1e30);
                if (cosLit > 0.0 && facing > 0.0 && bz > 0.0 && any(e * color > 0.0)) {
                    const float3 origin = y.position + y.normal * litOffset;
                    float tmax;
                    const float3 toLamp = shadow_ray(lamp, origin, rng.next2(), tmax);
                    const float3 leaving = e * transmittance(origin, toLamp, 0.0, max(tmax - litOffset, 0.0)) * (cosLit / PI) * color;
                    if (any(leaving > 0.0)) {
                        const float3 start = ray.from + ray.geo * offset;
                        const float3 to = leaves.position - start;
                        const float span = max(length(to), 1e-6), end = span - max(1e-3 * span, 5e-4);
                        bool blocked = false;
                        if (end > 0.0) {
                            Hit stop;
                            if (nearBack >= 0.0) {
                                g_mirrorRay = mirror;
                                blocked = trace_from_surface(start, to / span, 0.0, end, nearBack, stop);
                                g_mirrorRay = false;
                            } else blocked = trace_closest(start, to / span, 0.0, end, stop);
                        }
                        if (!blocked) { result = leaving * (s * bz / (a + b) * invPdf); leavesAt = leaves.position; leavesNormal = leaves.normal; }
                    }
                }
            }
        }
    }
    ray_cone(coneWidth, coneSpread);
    return result;
}
// The lamps a ray off water or see-through glass runs into before it meets anything else (pt_water_path water_path,
// pt_dielectric dielectric_follow), each shown as the ball it is traced as, by the light it sends `position`. They are
// found by the lamps' search tree (PT-99; on trial until 2026-10-10, docs/agents/perf-1.md "perf2" and rule-1.md; the
// rule since). Until then a loop asked every lamp of the point's two light lists, which together are every lamp of the
// scene: 500 to 1,000 records read and as many rays against a ball, at every turn of every water pixel's path and at
// every face of glass (the game, run 20261010-092027: the beach's frame 192 -> 161 ms with the tree, the same picture).
// The tree (scene.cpp, lamp_tree.hpp: a ball round every lamp, a ball round every two balls) lies after the frame's
// lights in their buffer, in the order of this walk; a node's `type` is where the walk goes on when the ray passes its
// ball by, its `range` the ball's radius, its `id` a leaf's lamp.
// The same lamps and the same light as that loop's: a leaf's lamp is asked the loop's own question, by the loop's own
// lines, and counted where the loop counted it (the far list holds every lamp that falls off with distance alone, and
// outside the grid every lamp; any other lamp counts where the cell lists it). The walk passes a ball by only where
// that question could not say yes. The question is single-precision arithmetic, and far from a lamp its yes reaches
// past the lamp's ball by a share of the lamp's distance: `slack`, worked out from the direction's own length (the
// reckoning: lamp_tree.hpp; checked against the loop, lamp by lamp, by tests/lamp_tree_test.cpp, which keeps the loop
// as what the walk is held against).
static const uint LAMPS_HELD = 4; // lamps a ray can run into at once and still sum to the loop's own bits
float3 reflected_lamps(float3 position, float3 origin, float3 dir, float tmax) {
    LightLists lists;
    if (!light_lists(position, lists)) return 0.0;
    StructuredBuffer<LightRecord> lights = ResourceDescriptorHeap[g.scene1.y];
    const uint root = g.settings2.z;      // the tree's records follow the frame's lights
    const uint count = lights[root].type; // the root goes on at the number of nodes (0: no lamp a ray could run into)
    const int3 dims = int3(g.grid.z & 0xFF, (g.grid.z >> 8) & 0xFF, (g.grid.z >> 16) & 0xFF);
    const int3 c = int3(floor((position - g.gridMin.xyz) * g.gridMin.w));
    const bool inGrid = all(c >= 0) && all(c < dims); // light_lists' own test: outside the grid the far list is every lamp
    const float length2 = dot(dir, dir), off = abs(length2 - 1.0);
    const float slack = 2.3 * sqrt(2.0e-6 + off) + off, perLength = 1.0 / length2;
    // The lamps met are added in the loop's own order (the cell's lamps by their index, then the others by theirs), so
    // that a ray through several lamps at once sums to the same bits as the loop's: up to LAMPS_HELD of them are held
    // in that order until the walk ends (a fifth at once is added as the walk meets it: the last bit may differ).
    float3 held[LAMPS_HELD]; uint keys[LAMPS_HELD];
    { for (uint z = 0; z < LAMPS_HELD; ++z) { held[z] = 0.0; keys[z] = INVALID; } }
    float3 sum = 0.0;
    uint i = 0;
    [loop] while (i < count) {
        const LightRecord node = lights[root + i];
        const float3 w = node.position - origin;
        const float3 q = w - clamp(dot(w, dir) * perLength, 0.0, tmax) * dir; // from the ray's stretch to the ball's centre
        const float reach = node.range + slack * (length(w) + node.range);
        if (dot(q, q) > reach * reach) { i = max(node.type, i + 1u); continue; } // passed by: on, past all that is under it
        ++i;
        if (node.id == INVALID) continue; // a node with children: on to the first
        const uint index = node.id;
        const LightRecord l = lights[index];
        if (!reflected_as_sphere(l)) continue;
        // ON TRIAL (B, lamp_trial): the ball the ray meets, and whose brightness it shows, at the lamp's other radius (off:
        // its own, as before). Its intensity is what light_incident took the distance out of (light_falloff): the ball
        // shows the lamp's whole strength whatever its near size is.
        const float ball = lamp_trial_radius(l, LAMP_TRIAL_REFLECTED);
        const float3 oc = origin - l.position;
        const float b = dot(oc, dir), disc = b * b - (dot(oc, oc) - ball * ball);
        if (disc <= 0.0) continue;
        const float t = -b - sqrt(disc) > 0.0 ? -b - sqrt(disc) : -b + sqrt(disc);
        if (t <= 0.0 || t >= tmax) continue;
        const bool listed = inGrid && cell_lists(lists.first, lists.count, index);
        if ((l.flags & LIGHT_NO_RANGE) == 0u && inGrid && !listed) continue;
        float3 toLight; float dist;
        const float3 e = light_incident(l, position, toLight, dist);
        float3 term = e / (light_falloff(l, dist * dist) * PI * ball * ball);
        // Into its place among the lamps held: the cell's lamps first, each kind by its index (an index is under 2^20).
        uint key = (listed ? 0u : 0x80000000u) | index;
        for (uint z = 0; z < LAMPS_HELD; ++z) {
            if (key < keys[z]) { const float3 heldTerm = held[z]; const uint heldKey = keys[z]; held[z] = term; keys[z] = key; term = heldTerm; key = heldKey; }
        }
        if (key != INVALID) sum += term; // pushed out past the last place: more than LAMPS_HELD lamps at once
    }
    { for (uint z = 0; z < LAMPS_HELD; ++z) if (keys[z] != INVALID) sum += held[z]; }
    return sum;
}

#include "pt_dielectric.hlsli"

// Whether a hit is on glass (dielectric_follow's own test). A caller reads the hit's material once and asks for the walk
// through the glass only there, reading the material again where the walk ends (PT-73 c: until then every path vertex's
// material, and what a reflection met, was read twice, once to hand to dielectric_follow and once after it, though only
// glass uses the first and only glass moves the hit: its normal and mask maps and all its program's but the colors, read
// a second time at every bounce for nothing). The callers read it in one place, a loop of at most two turns (the surface
// met; then, through glass, the surface the walk ended on): a second copy of hit_material beside the first makes every
// pass that much larger to compile.
bool hit_is_glass(Hit hit) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
#ifdef PT_SPEED_MEASURE
    if (speed_measure(SPEED_M_NO_SPECIAL) || speed_measure3(SPEED_M3_NO_GLASS)) return false; // glass shaded as a plain surface
#endif
    return (materials[hit.material].flags & MATERIAL_GLASS) != 0;
}

// What a ray from a path's last vertex brings back to it for the cache: the next surface's own light (its emission as a
// ray gathering for that vertex counts it: the vertex's light sampling takes the emissive lights in) and, past it, the
// cache's (or the sky's).
float3 cache_sample(Hit hit, float offset, inout Rng rng) {
    // ON TRIAL (lamp_around; off: nothing here runs and near is 0): what stands by a lamp, found on purpose for this vertex,
    // whose own ray below then counts the rest of it (arrive, armed for the direct light of what it meets).
    NearRay arrive = near_ray(hit.position, hit.normal, hit.normal, hit.normal, 0.0);
    float3 near = 0.0;
    if (lamp_around() > 0.0) {
        arrive.alpha = -1.0;
        float3 nearDir, nearPoint, nearNormal;
        near = finite_radiance(lamp_around_light(arrive, offset, -1.0, false, false, rng, nearDir, nearPoint, nearNormal));
    }
    float3 dir = cosine_sample(hit.normal, rng.next2());
    Hit next;
    float3 tint; // of the thin cloth the ray passed (trace_bounce)
    ray_cone(hit.footprint, DIFFUSE_SPREAD);
    if (!trace_bounce(hit.position + hit.normal * offset, dir, g.params.y, next, tint)) return near + tint * sky_radiance(dir);
    if (g_clothMet > 0) arrive.alpha = 0.0; // passed thin cloth: left to chance
    float travel; float3 interfaces = 0.0;
    SurfaceMaterial m;
    bool beyond = false, escaped = false; // beyond: on the surface a walk through glass ended on
    [loop] while (true) {
        m = hit_material(next); // read once (hit_is_glass)
        if (beyond || !hit_is_glass(next)) break;
        g_walkGathers = true; // the cache's cell takes the lamps' and the sun's own light where its vertices are lit
        if (!dielectric_follow(next, dir, m, tint, rng, travel, interfaces)) { escaped = true; break; }
        beyond = true;
    }
    if (escaped) return near + interfaces + tint * sky_radiance(dir);
    float nextOffset = 0.01 + length(next.position - g.cameraPos.xyz) * 0.001;
    const float3 emitted = path_emission(next, m.emissive, hit.position, true);
    const bool plain = arrive.alpha != 0.0 && !beyond && next.water == 0 && !(m.through > 0.0) && !cloth_stops(next); // ON TRIAL
    subsurface_vertex(next, m, nextOffset, rng); // a subsurface material's light comes out where the walk leaves it
    if (plain) g_nearArmed = arrive;
    interfaces += near; // ON TRIAL: what was found on purpose, beside what the ray brings (0 with the trial off)
    return interfaces + tint * (emitted + direct_diffuse(next.position, next.normal, m.normal, m.reflectance, nextOffset, rng, m.through, m.tint) +
                   m.reflectance * m.occlusion * cache_light(next.position, next.normal, rng));
}

// ---------------------------------------------------------------- caustics: the light where it landed
// The sun's and the lamps' light traced off and through HDRP's water (pt_caustics.hlsl) lands as triangles: each the image of a
// triangle of the water through the three rays traced from its corners, holding its light per area there (the light through the
// water's triangle over the area it lands on: rays that bunch make a bright line). They are built into an acceleration structure
// of their own every frame (tracer.cpp), and a point asks it with one short ray through it along its normal: every landed
// triangle that ray crosses (within its tolerance of the point) whose face turns to the point's side adds its light there, read
// across it by the point's barycentric weights. The hardware's search finds just the triangles at the point, however many lie
// elsewhere (the world-space hash it replaces walked every level's 8 buckets and their lists for each point: 40 to 50 ms of the
// lake's composite in game, run 20261007-140305; 33.7 ms against 4.4 ms on the test's lake with 25 lamps, the caustics' stage
// and the composite together). Its light is diffuse: irradiance / pi.
// A triangle's corners (CAUSTIC_CORNERS bytes: three float4s, xyz relative to the scene origin) are the structure's vertices; its
// light at each corner (CAUSTIC_LIGHT bytes: three float3s in storage units, to_storage, as floats: a focused line's light is
// far above a half's range) lies in a buffer of its own, with its tolerance.
static const uint CAUSTIC_NONE = 0xFFFFFFFFu;
static const uint CAUSTIC_CORNERS = 48;
static const uint CAUSTIC_LIGHT = 48;
// How far off its plane a point still lies on a landed triangle: the surface it landed on is not flat over its size. 0.5 cm and
// a tenth of its longest edge, 5 cm at most: a metre-wide triangle lit everything within 10 cm of its plane (a ledge beside the
// wall it landed on), and the longest tolerance of a frame is how far every point's ray looks (3.5 ms of the test lake's
// composite uncapped). Each triangle's own decides (caustic_light).
float caustic_tolerance(float edge) { return min(0.005 + 0.1 * edge, 0.05); }
// The caustics' light on a surface point (relative to the scene origin) facing n: irradiance / pi, in light units (a diffuse
// surface's radiance per albedo). None with the Caustics row off or no water.
float3 caustic_light(float3 p, float3 n) {
    if (g.caustics.x == INVALID) return 0.0;
    RaytracingAccelerationStructure landed = ResourceDescriptorHeap[g.caustics.y];
    RWByteAddressBuffer light = ResourceDescriptorHeap[g.caustics.x];
    RWByteAddressBuffer corners = ResourceDescriptorHeap[asuint(g.causticGrid.y)];
    RWByteAddressBuffer counters = ResourceDescriptorHeap[g.caustics.z];
    // The ray: through the point along its normal, either side twice as far as the largest tolerance among this frame's
    // triangles (the counters' second word, as a float's bits: pt_caustics keeps its maximum; twice, for a triangle whose plane
    // the normal crosses at a slant).
    const float reach = asfloat(counters.Load(4));
    if (!(reach > 0.0)) return 0.0;
    RayDesc ray; ray.Origin = p + n * (2.0 * reach); ray.Direction = -n; ray.TMin = 0.0; ray.TMax = 4.0 * reach;
    PT_RAYQUERY(RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
    q.TraceRayInline(landed, RAY_FLAG_NONE, 0xFF, ray);
    float3 sum = 0.0;
    while (q.Proceed()) {
        const uint i = q.CandidatePrimitiveIndex();
        const float3 a = asfloat(corners.Load3(i * CAUSTIC_CORNERS)), b = asfloat(corners.Load3(i * CAUSTIC_CORNERS + 16));
        const float3 c = asfloat(corners.Load3(i * CAUSTIC_CORNERS + 32));
        const float3 nt = cross(b - a, c - a); // towards the side its light came from (pt_caustics winds it so)
        const float len = length(nt);
        if (!(len > 1e-14) || !(dot(n, nt) > 0.2 * len)) continue;
        // Within its own tolerance of its plane (the ray reaches as far as the largest).
        const uint at = i * CAUSTIC_LIGHT;
        const uint4 tail = light.Load4(at + 24);
        if (abs(dot(p - a, nt)) > asfloat(tail.w) * len) continue;
        const float2 w = q.CandidateTriangleBarycentrics();
        sum += asfloat(light.Load3(at)) * (1.0 - w.x - w.y) + asfloat(light.Load3(at + 12)) * w.x + asfloat(tail.xyz) * w.y;
    }
    return sum * reference_radiance();
}
// ON TRIAL (PT-81, docs/agents/water-9.md; TGPT_OPTION_CAUSTICS_LIT, the plugin's hidden "Caustics light keeps its direction";
// off: caustic_light times the albedo, as before): the caustics' light at a surface point as light that comes from the
// direction it came from (each landed triangle keeps it: pt_caustics store_triangle, bytes 40 to 43 of its light record), so
// the surface answers it as it answers any light: by the cosine with its own normal (shading: a normal map's relief shows)
// and with its highlight (ggx_specular, seen from v). The light across the beam is the landed light over the cosine it
// landed at on its triangle: on a surface whose normal is its triangle's, the diffuse light is what caustic_light gives.
// through, tint: a thin sheet lit from behind, as direct_light takes it. Returns radiance (the albedo is in it).
float3 caustic_lit(float3 p, float3 n, float3 shading, float3 albedo, float3 f0, float alpha, float3 v, float through, float3 tint) {
    if (g.caustics.x == INVALID) return 0.0;
    RaytracingAccelerationStructure landed = ResourceDescriptorHeap[g.caustics.y];
    RWByteAddressBuffer light = ResourceDescriptorHeap[g.caustics.x];
    RWByteAddressBuffer corners = ResourceDescriptorHeap[asuint(g.causticGrid.y)];
    RWByteAddressBuffer counters = ResourceDescriptorHeap[g.caustics.z];
    const float reach = asfloat(counters.Load(4));
    if (!(reach > 0.0)) return 0.0;
    RayDesc ray; ray.Origin = p + n * (2.0 * reach); ray.Direction = -n; ray.TMin = 0.0; ray.TMax = 4.0 * reach;
    PT_RAYQUERY(RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
    q.TraceRayInline(landed, RAY_FLAG_NONE, 0xFF, ray);
    float3 sum = 0.0;
    while (q.Proceed()) {
        const uint i = q.CandidatePrimitiveIndex();
        const float3 a = asfloat(corners.Load3(i * CAUSTIC_CORNERS)), b = asfloat(corners.Load3(i * CAUSTIC_CORNERS + 16));
        const float3 c = asfloat(corners.Load3(i * CAUSTIC_CORNERS + 32));
        const float3 nt = cross(b - a, c - a);
        const float len = length(nt);
        if (!(len > 1e-14) || !(dot(n, nt) > 0.2 * len)) continue;
        const uint at = i * CAUSTIC_LIGHT;
        const uint4 tail = light.Load4(at + 24);
        if (abs(dot(p - a, nt)) > asfloat(tail.w) * len) continue;
        const float2 w = q.CandidateTriangleBarycentrics();
        const float3 onPlane = asfloat(light.Load3(at)) * (1.0 - w.x - w.y) + asfloat(light.Load3(at + 12)) * w.x + asfloat(tail.xyz) * w.y;
        const float3 l = unpack_normal(light.Load(at + 40));
        // Across the beam: over the cosine the light landed at (a twentieth at least: a triangle it only grazed).
        const float3 across = onPlane / max(dot(nt, l) / len, 0.05);
        const float cosine = dot(shading, l);
        sum += across * albedo * (cosine > 0.0 ? cosine * float3(1.0, 1.0, 1.0) : -cosine * through * tint);
        if (alpha > 0.0 && dot(n, l) > 0.0) sum += across * PI * ggx_specular(shading, v, l, f0, alpha);
    }
    return sum * reference_radiance();
}

// (Until PT-64 a ray's hit that lay on screen took HDRP's color and normal from its G-buffer there, hdrp_material_at: the same
// thing looked one way in a reflection while on screen and another once it left. A hit's material is its own now.)

#include "pt_water_path.hlsli"
// A path that starts inside the scene's water or air, for shade_path's water path (Hit.water): from the hit's position on.
static const uint WATER_HERE = 0x7FFFFFFFu;
// The distance from the water's surface to the first thing its last path met (the denoisers' hit distance at a water pixel).
static float g_waterGuide = HIT_DISTANCE_MISS;

// Radiance leaving a path vertex hit from direction dir, following the path diffusely for more bounces and ending on the
// radiance cache (feed: this path adds to it). Its emission as path_emission weighs it for the vertex each ray left
// (g_pathFrom and g_pathGathers for the first).
float3 shade_path(Hit hit, float3 dir, uint bounces, inout Rng rng, bool feed) {
    float3 throughput = 1.0, radiance = 0.0;
    float3 from = g_pathFrom; bool gathers = g_pathGathers;
    // ON TRIAL (lamp_around): the ray that met the vertex at hand, for the weighing of a lamp's light there: the caller's
    // for the first (pending; none unless it set one), then each bounce's own.
    NearRay arrive = g_nearPending;
    g_nearPending.alpha = 0.0;
    // The ray cone the caller traced with, given back after the bounces' wider ones.
    const float coneWidth = g_coneWidth, coneSpread = g_coneSpread;
    for (uint bounce = 0; bounce < 4; ++bounce) {
        if (hit.water != 0) break; // water: its own path, after the loop
        float travel; float3 interfaces = 0.0;
        // The vertex's material, read once (hit_is_glass): through glass, the material of what the walk ends on. Water behind
        // the glass has none to read: the walk ends on it, and its path is the water's own.
        SurfaceMaterial m = (SurfaceMaterial)0;
        bool beyond = false, escaped = false; // beyond: on the surface a walk through glass ended on
        [loop] while (hit.water == 0) {
            const bool glass = !beyond && hit_is_glass(hit);
            m = hit_material(hit);
            if (!glass) break;
            g_walkGathers = gathers; // the vertex the ray left samples the lamps and the sun itself (pt_dielectric)
            if (!dielectric_follow(hit, dir, m, throughput, rng, travel, interfaces)) { escaped = true; break; }
            beyond = true;
        }
        if (escaped) { radiance += interfaces + throughput * sky_radiance(dir); break; }
        radiance += interfaces;
        if (hit.water != 0) break; // water behind the glass
        float dist = length(hit.position - g.cameraPos.xyz);
        float offset = 0.01 + dist * 0.001;
        const float3 emitted = path_emission(hit, m.emissive, from, gathers);
        // A subsurface material (wax, skin): the vertex moves to where a walk inside it comes out, and sends on what the walk
        // carries (subsurface_vertex): its direct light, the cache's and the next bounce are the exit's.
        // ON TRIAL (lamp_around): a plain vertex's lamp light is weighed against the on-purpose way (direct_light, armed).
        const bool plain = arrive.alpha != 0.0 && !beyond && !(m.through > 0.0) && !cloth_stops(hit);
        subsurface_vertex(hit, m, offset, rng);
        if (plain) g_nearArmed = arrive;
        radiance += throughput * (emitted + direct_diffuse(hit.position, hit.normal, m.normal, m.reflectance, offset, rng, m.through, m.tint));
        arrive.alpha = 0.0;
        if (bounce + 1 >= bounces) {
            if (cache_on()) {
                radiance += throughput * m.reflectance * m.occlusion * cache_light(hit.position, hit.normal, rng);
                if (feed) cache_add(hit.position, hit.normal, finite_radiance(cache_sample(hit, offset, rng)), rng);
            }
            break;
        }
        throughput *= m.reflectance * m.occlusion;
        if (bounce >= 1) {
            float p = saturate(max(throughput.r, max(throughput.g, throughput.b)));
            if (rng.next() >= p) break;
            throughput /= max(p, 1e-3);
        }
        from = hit.position; gathers = true;
        // ON TRIAL (lamp_around; off: nothing here runs): what stands by a lamp, found on purpose for this vertex; its own
        // ray below then counts the rest of it at what it meets (arrive).
        if (lamp_around() > 0.0) {
            arrive = near_cosine(hit.position, hit.normal, hit.normal);
            float3 nearDir, nearPoint, nearNormal;
            radiance += throughput * finite_radiance(lamp_around_light(arrive, offset, -1.0, false, false, rng, nearDir, nearPoint, nearNormal));
        }
        float3 next = cosine_sample(hit.normal, rng.next2());
        Hit h2;
        float3 tint; // of the thin cloth the ray passed (trace_bounce)
        ray_cone(hit.footprint, DIFFUSE_SPREAD);
        const bool met = trace_bounce(hit.position + hit.normal * offset, next, g.params.y, h2, tint);
        if (g_clothMet > 0) arrive.alpha = 0.0; // passed thin cloth: left to chance
        throughput *= tint;
        if (!met) { radiance += throughput * sky_radiance(next); break; }
        hit = h2; dir = next;
    }
    // One place for the water's path, for water met at once, water reached by a bounce and a path that starts inside it
    // (WATER_HERE: from hit.position, in whatever it lies in): two copies of the medium's whole integrator in every caller
    // took the driver minutes to make a pass. From just before the surface, which the path then meets as its first event.
#ifdef PT_SPEED_MEASURE
    if (hit.water != 0 && !speed_measure(SPEED_M_NO_SPECIAL) && !speed_measure3(SPEED_M3_NO_WATER)) {
        if (hit.water == WATER_HERE) ++g_scWaterHere; // (the counters: pt_common speed_count_flush)
#else
    if (hit.water != 0) {
#endif
        const bool here = hit.water == WATER_HERE;
        float guide;
        // (With the width of the ray that met the water, the hit's ray cone, for the path's own: pt_water_path.hlsli.)
        radiance += throughput * water_path(here ? hit.position : hit.position - dir * (4.0 * water_epsilon(hit.position)), dir,
                                            here ? -1 : int(hit.water) - 1, hit.front, here ? 0.0 : hit.footprint, rng, guide);
        g_waterGuide = guide;
    }
    ray_cone(coneWidth, coneSpread);
    return radiance;
}
float3 shade_path(Hit hit, float3 dir, uint bounces, inout Rng rng) { return shade_path(hit, dir, bounces, rng, false); }

// What a reflection shows where its ray met a surface (PT-46, the owner: "we need our tracer to be able to perfectly render the
// world in a mirror"): the light the surface sends back along the ray, lit as the camera's own pixel of it is. Its diffuse color
// (a metal has none) and its specular lobe, with Reflection Boost as pt_prepare gives a pixel the camera sees (not a leaf's, a
// blade's or thin cloth's), take the sun's and the lights' samples (direct_light: its highlights); its bounced light is the
// radiance cache's, times its diffuse color and its mask map's occlusion, with Indirect Color as a camera pixel's bounced light
// takes it (pt_gi_spatial); what its specular lobe reflects of its surroundings is, for a smooth metal, one more reflection ray
// (a reflection in a reflection, shaded as a path vertex), else the cache's light times the lobe's directional albedo, as
// Reflections on Glossy Surfaces take it (pt_specular). Hair keeps no highlight here (the camera's are Kajiya-Kay's along its
// strands, pt_hair). Until 2026-10-07 a reflection showed a path vertex's light (shade_path): the base color as diffuse light
// (a metal's specular color too), no highlight, the bounced light without Indirect Color. A lit red wall in a perfect mirror
// (tgpt_gpu_test) came out at 0.86 of its brightness seen directly, its green and blue at 0.70 and 0.61 of the wall's against
// its red: too dark and too colored, its highlights and reflections (the light's color, not the wall's) missing. Now 0.98,
// 0.99 and 0.98 (at Indirect Color 1).
static const float SEEN_MIRROR_ROUGHNESS = 0.6; // a lobe smoother than this, of a metal, traces its own reflection
// For the reflections' filter (PT-80, pack_seen): the surface shade_seen shaded last (behind glass: the one its walk ended
// on), its shading normal and its reflectance (its diffuse color plus what its specular lobe reflects, as it lit it). The
// reflectance 0: none (the walk left the glass into the sky, or the water's own path took over).
static float3 g_seenNormal = 0.0, g_seenReflectance = 0.0;
float3 shade_seen(Hit hit, float3 dir, inout Rng rng, bool feed) {
    g_seenNormal = 0.0; g_seenReflectance = 0.0;
    // ON TRIAL (lamp_around): the ray that met this surface, where its pass set one (pt_specular's lobe ray); none otherwise.
    const NearRay arrive = g_nearPending;
    g_nearPending.alpha = 0.0;
    float3 opticalWeight = 1.0, interfaces = 0.0; float travel;
    // Glass is followed to what lies behind it. Water (met at once, behind glass or in the mirror below) keeps its own path,
    // shade_path's continuation: one call of it here for all three, so the medium's integrator is not made three times.
    // What is not water has its material read once (hit_is_glass): through glass, the material of what the walk ends on.
    SurfaceMaterial m = (SurfaceMaterial)0;
    bool beyond = false, escaped = false; // beyond: on the surface a walk through glass ended on
    [loop] while (hit.water == 0) {
        m = hit_material(hit); // read once (hit_is_glass)
        if (beyond || !hit_is_glass(hit)) break;
        if (!dielectric_follow(hit, dir, m, opticalWeight, rng, travel, interfaces)) { escaped = true; break; }
        beyond = true;
    }
    if (escaped) return interfaces + opticalWeight * sky_radiance(dir);
    Hit continuation = hit;
    float3 continuationDir = dir, continuationWeight = 1.0;
    bool follow = hit.water != 0;
    const float coneWidth = g_coneWidth, coneSpread = g_coneSpread;
    const float3 from = g_pathFrom; const bool gathers = g_pathGathers;
    float3 result = 0.0;
    // A smooth metal's own reflection ray below: HDRP's fog over its way too, as the camera's pixel of the metal has it over
    // its reflection ray (pt_specular: fogged). Without it a steel breastplate in a foggy cave, which shows the fog's light
    // and little else, was 4 to 5 times darker in a mirror than seen directly (PT-88, the owner: "pure black").
    bool secondWay = false; float3 secondFrom = 0.0; float secondDist = 0.0;
    if (!follow) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    const uint flags = materials[hit.material].flags;
    const float3 v = -dir;
    float roughness = m.roughness; float3 f0 = m.f0;
    if (!(m.through > 0.0) && (flags & MATERIAL_EYE) == 0) reflection_boost(roughness, f0, g.cameraPos.w);
    const bool strands = (flags & (MATERIAL_STRANDS | MATERIAL_HAIR_LAYER)) != 0;
    const float alpha = strands ? 0.0 : roughness_alpha(roughness);
    const float dist = length(hit.position - g.cameraPos.xyz);
    const float offset = (flags & MATERIAL_EYE) != 0 ? optical_epsilon(hit.position) : 0.01 + dist * 0.001;
    float3 radiance = path_emission(hit, m.emissive, g_pathFrom, g_pathGathers);
    // A subsurface material seen in a reflection: its highlight where the ray met it, its diffuse light where a walk inside it
    // comes out (subsurface_vertex), as the camera's own pixel of it has them (pt_walk).
    Hit leaves = hit; SurfaceMaterial inside = m; float exitOffset = offset;
    // ON TRIAL (lamp_around): a plain surface's diffuse lamp light is weighed against the on-purpose way (direct_light, armed
    // for the call that takes the diffuse light); its highlight stays whole.
    const bool plain = arrive.alpha != 0.0 && !beyond && !(m.through > 0.0) && !cloth_stops(hit);
    const bool walked = subsurface_vertex(leaves, inside, exitOffset, rng);
    if (plain && !walked) g_nearArmed = arrive;
    if (walked) {
        radiance += direct_light(hit.position, hit.normal, m.normal, 0.0, f0, alpha, v, offset, rng, 0.0);
        if (plain) g_nearArmed = arrive;
        radiance += direct_diffuse(leaves.position, leaves.normal, inside.normal, inside.albedo, exitOffset, rng, 0.0);
    } else radiance += direct_light(hit.position, hit.normal, m.normal, m.albedo, f0, alpha, v, offset, rng, m.through, m.tint);
#ifdef PT_PSR_CAPTURE
    float3 psrDiffuse = g_psrDirectDiffuse, psrSpecular = g_psrDirectSpecular;
#endif
    const float nv = saturate(dot(m.normal, v));
    const float3 lobe = strands || nv <= 0.0 ? 0.0 : specular_albedo(f0, roughness, nv);
    const bool mirrored = roughness < SEEN_MIRROR_ROUGHNESS && max(f0.r, max(f0.g, f0.b)) > 0.2 && any(lobe > 0.0);
    // A mirror seen in a mirror shows a picture of its own, which no guide of this surface describes: no reflectance, and the
    // filter leaves the pixel's samples to its history alone (seen_picture).
    g_seenNormal = m.normal; g_seenReflectance = mirrored && roughness < SEEN_PICTURE_ROUGHNESS ? 0.0 : max(m.albedo + lobe, 1e-4);
#ifdef PT_PSR_CAPTURE
    // PT-127: what a still pure mirror shows is handed to NRD as the surface it is, where that is an ordinary one: not
    // behind glass, no skin or wax a walk lights, no hair, nothing light passes through, and no pure mirror again (a chain
    // of mirrors is not followed yet: such a pixel stays the first mirror's own, as before).
    const bool psrEnd = g_psrSlot != INVALID && !beyond && !walked && !strands && !(m.through > 0.0) && !(m.subsurface > 0.0) &&
        !(mirrored && roughness * 255.0 < 0.5 && all(m.albedo == 0.0));
#endif
    if (cache_on()) {
        const float3 around = cache_light(hit.position, hit.normal, rng);
        if (walked) radiance += inside.albedo * m.occlusion * indirect_color(cache_light(leaves.position, leaves.normal, rng));
        else radiance += m.albedo * m.occlusion * indirect_color(around);
        if (!mirrored) radiance += lobe * around;
#ifdef PT_PSR_CAPTURE
        psrDiffuse += m.albedo * m.occlusion * indirect_color(around);
        if (!mirrored) psrSpecular += lobe * around;
        // (Before the cache's own rays below: the two parts go into the record here and cross no ray.)
        if (psrEnd) psr_capture(m.normal, roughness, m.albedo, f0, psrDiffuse, psrSpecular);
#endif
        if (feed) cache_add(hit.position, hit.normal, finite_radiance(cache_sample(hit, offset, rng)), rng);
    }
#ifdef PT_PSR_CAPTURE
    else if (psrEnd) psr_capture(m.normal, roughness, m.albedo, f0, psrDiffuse, psrSpecular);
#endif
    if (mirrored) {
        // Drawn from the lobe's own width, a mirror's too (reflection_alpha; the highlights above keep roughness_alpha's floor).
        const float rayAlpha = reflection_alpha(roughness);
        const float3 l = above_surface(ggx_sample(m.normal, v, rayAlpha, rng.next2()), hit.normal);
        const float nl = dot(m.normal, l);
        if (nl > 0.0) {
            const float3 h = normalize(v + l);
            const float3 weight = fresnel_schlick(f0, saturate(dot(v, h))) * smith_g1(nl, rayAlpha * rayAlpha);
            ray_cone(hit.footprint, coneSpread + rayAlpha);
            Hit next;
            secondFrom = hit.position + hit.normal * offset;
            const bool met = trace_from_surface(secondFrom, l, 0.0, g.params.y, 0.0, next);
            g_pathFrom = hit.position; g_pathGathers = false;
            if (met) {
                continuation = next; continuationDir = l; continuationWeight = weight; follow = true;
                secondWay = true; secondDist = length(next.position - secondFrom);
            } else {
                const float3 open = weight * fogged(sky_radiance(l), secondFrom, l, HIT_DISTANCE_MISS, rng);
                radiance += open;
#ifdef PT_PSR_CAPTURE
                if (g_psrSlot != INVALID) psr_capture_reflection(open, HIT_DISTANCE_MISS);
#endif
            }
        }
    }
    result = radiance;
    }
    if (follow) {
        float3 beyondIt = shade_path(continuation, continuationDir, 1u, rng);
        if (secondWay) beyondIt = fogged(beyondIt, secondFrom, continuationDir, secondDist, rng);
        result += continuationWeight * beyondIt;
#ifdef PT_PSR_CAPTURE
        // (A record of this pixel in the captured state: the end's own reflection ray, a glossy metal's. Water's and a
        // pure mirror's wrote none.)
        if (secondWay && g_psrSlot != INVALID) psr_capture_reflection(continuationWeight * beyondIt, secondDist);
#endif
    }
    g_pathFrom = from; g_pathGathers = gathers;
    ray_cone(coneWidth, coneSpread);
    return interfaces + opticalWeight * result;
}

#include "pt_medium.hlsli" // the fog as a medium: its light, its cache cells, traced_fog_on_way (2026-10-11)
