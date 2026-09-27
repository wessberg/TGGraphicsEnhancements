// SPDX-License-Identifier: MIT
// TG Path Tracing native ABI, shared with the BepInEx plugin (NativeTracer.cs).
//
// The plugin describes the scene (meshes, textures, materials, instances, lights, the sky) from the main
// thread and hands over one frame per gameplay camera: HDRP's GBuffer, depth and motion vectors right
// after HDRP rendered them. Render event 1 copies those to the bridge's D3D12 device and path traces
// there while HDRP goes on with shadows and lighting; render event 2, later in the same frame, waits for
// the result on the GPU and puts it in place of HDRP's lighting on deferred surfaces.
#pragma once
#include <stdint.h>
#ifdef _WIN32
#define TGPT_EXPORT extern "C" __declspec(dllexport)
#define TGPT_CALL __cdecl
#else
#define TGPT_EXPORT extern "C"
#define TGPT_CALL
#endif
#define TGPT_ABI 0x00010F00u

// TGPT_Frame.debugView: what the composite writes instead of the lit image
#define TGPT_VIEW_NONE 0u
#define TGPT_VIEW_ALBEDO 1u        // HDRP's diffuse color, as the tracer reads it
#define TGPT_VIEW_NORMALS 2u       // HDRP's normals, as the tracer reads them
#define TGPT_VIEW_COVERAGE 3u      // camera rays against the traced scene: each instance its own color, black where the scene has nothing
#define TGPT_VIEW_MATERIALS 4u     // the traced scene's own material colors along camera rays
#define TGPT_VIEW_INDIRECT 5u      // denoised indirect light on a white surface
#define TGPT_VIEW_INDIRECT_RAW 6u  // the same before denoising
#define TGPT_VIEW_DIRECT 7u        // path-traced direct light on a white surface
#define TGPT_VIEW_SURFACES 8u      // how HDRP draws each pixel (its material type, or shaded by HDRP itself)
#define TGPT_VIEW_SPECULAR 9u      // the traced reflections and specular light alone
#define TGPT_VIEW_EMISSIVE 10u     // camera rays: emissive surfaces green where sampled as lights, blue where a light's own
                                   // (TGPT_OPTION_EMISSIVE_LIGHTS), red where only rays that meet them see their light
#define TGPT_VIEW_COUNT 11u

// TGPT_Frame.flags
#define TGPT_FRAME_RESET 1u        // camera cut, first frame, resolution change: no history
#define TGPT_FRAME_GUIDES 2u       // write DLSS Ray Reconstruction's guide buffers in render event 2 (TGPT_GetGuides)
// TGPT_Frame.options
#define TGPT_OPTION_NO_SPATIAL 1u  // ReSTIR without its spatial reuse
#define TGPT_OPTION_NO_TEMPORAL 2u // ReSTIR without its temporal reuse
#define TGPT_OPTION_NO_DENOISER 4u
#define TGPT_OPTION_NO_SPECULAR 8u // without path-traced reflections
// DLSS Ray Reconstruction denoises: the traced light is composited as traced (no denoiser, nearest-texel
// reprojection) and ReSTIR's temporal reuse is permuted, so neighbouring samples stay uncorrelated.
#define TGPT_OPTION_RAY_RECONSTRUCTION 16u
// Tests and troubleshooting: instances trace without opacity micromaps (the alpha test alone decides).
#define TGPT_OPTION_NO_MICROMAPS 32u
// AMD FSR Ray Regeneration denoises instead of the built-in denoiser, where the GPU runs it
// (TGPT_FEATURE_RAY_REGENERATION); ReSTIR's temporal reuse is permuted, as for Ray Reconstruction.
#define TGPT_OPTION_RAY_REGENERATION 64u
// Rays from the G-buffer's surfaces stop at near back faces and stand-ins instead of passing through (a debugging aid:
// the capture compares the image with and without it).
#define TGPT_OPTION_NO_PASS_THROUGH 128u
// Direct light's reuse with visibility (RTXDI's initial visibility and its ray-traced bias correction): fewer bright
// dots inside strong lights' shadows and less error, but lamps' shadow edges several times wider (off by default).
#define TGPT_OPTION_DI_VISIBILITY 256u
// Spatial reuse's result feeds the next frame's temporal reuse, as NVIDIA's RTXDI sample does (off by default: reuse
// is kept decorrelated for the denoisers, which assume independent samples).
#define TGPT_OPTION_SPATIAL_FEEDBACK 512u
// NVIDIA's Real-Time Denoisers (NRD, RELAX) denoise instead of the built-in denoiser, where this build has them and they
// started on the device (TGPT_FEATURE_NRD): the fallback for GPUs without Ray Reconstruction or Ray Regeneration.
#define TGPT_OPTION_NRD 1024u
// For testing (the harness's energy experiment; the plugin never sets them): each undoes one of the changes the tracer
// makes to what it estimates, so an honest reference and the causes of a bias can be measured.
#define TGPT_OPTION_NO_BOILING 2048u       // ReSTIR's boiling filters off (direct and bounced light)
#define TGPT_OPTION_NO_CLAMPS 4096u        // no radiance clamps, and no 25 cm floor on a light's distance at path vertices
#define TGPT_OPTION_EXACT_JACOBIAN 8192u   // ReSTIR GI's reconnection Jacobian neither rejected nor clamped
#define TGPT_OPTION_OFFSET_RECEIVER 16384u // RTXDI's surface position is where the surface's rays start (off it, along its normal)
// The Boiling Filter row off: ReSTIR GI's boiling filter off (the direct light's stays). It drops a bounced-light sample far
// brighter than its neighbours', which stops rare bright ones from spreading as blotches but also throws real light away
// next to strong lights (the harness's light 10 cm from a wall: 21 % of the room's bounced light).
#define TGPT_OPTION_NO_GI_BOILING 32768u
// The Radiance Cache row off: paths end where the Bounces setting ends them, and a reflection shows the direct light of
// what it reflects alone. With it on, a world-space cache of the bounced light arriving at surfaces (pt_common.hlsli)
// ends them instead, filled by the paths themselves, so light keeps bouncing past the last traced bounce.
#define TGPT_OPTION_NO_RADIANCE_CACHE 65536u
// Reflections on Glossy Surfaces: a surface rougher than pt_specular's GLOSSY_ROUGHNESS reflects the bounced light the radiance
// cache holds where it is (times its lobe's directional albedo) instead of a traced ray. Needs the radiance cache.
#define TGPT_OPTION_GLOSSY_REFLECTIONS 262144u
// ReSTIR GI's reuse without its biases in dark crevices (the Crevice Light row, off by default): a neighbour or the
// previous pixel counts for a sample only if it sees it (RTXDI's ray-traced bias correction), a light path's history is
// kept whatever its age, and the boiling filter brings an outlier down to its group's bound instead of dropping it.
// Under a canvas over sunlit sand, against an honest reference: its mouth within 1 % and deeper in 3 to 9 % short (14 %
// short and 9 % too bright without), for 15 to 30 % more error after denoising in the harness's room.
#define TGPT_OPTION_GI_VISIBILITY 2097152u
// The Emissive Lights row: the traced scene's emissive triangles are sampled as lights (ReSTIR DI at the camera's surfaces,
// the light sampling at every path vertex), within a triangle budget, the brightest and largest first. A path that meets
// one counts only the part of its light the sampling leaves (beyond its reach), so the light converges to the same image
// with far less noise. Off: only rays that happen to meet an emissive surface see its light.
#define TGPT_OPTION_EMISSIVE_LIGHTS 131072u
// What HDRP shades itself (hair, eyes: the pixels the tracer leaves to HDRP) keeps HDRP's light as it is. Without it the
// composite darkens HDRP's color by the traced shadows of the lights HDRP casts none for (TGPT_LIGHT_HDRP_SHADOWS, the sun's
// w), measured against the bounced light arriving there from the radiance cache (none without it).
#define TGPT_OPTION_NO_HDRP_SHADOWS 4194304u
// For the capture's comparison of what boils (the F8 capture; no row sets them): the bounced light is the pixel's own rays'
// mean alone (ReSTIR GI's reuse, which shares one sample between neighbours and frames, left out of the image), and the
// direct light goes without ReSTIR DI's temporal and spatial reuse (each pixel's own light samples alone).
#define TGPT_OPTION_GI_OWN_RAYS 524288u
#define TGPT_OPTION_DI_NO_REUSE 1048576u
// TGPT_Status::features. DXR12: the Shader Model 6.9 passes run (DirectX Raytracing 1.2), and alpha-tested
// meshes get opacity micromaps. SER: GI and reflections run as ray generation shaders whose threads
// Shader Execution Reordering regroups by the material they hit (only where the GPU reorders).
#define TGPT_FEATURE_DXR12 1u
#define TGPT_FEATURE_SER 2u
// RAY_REGENERATION: AMD FSR Ray Regeneration runs on this GPU (a Radeon RX 9000 or newer).
// RAY_REGENERATION_ACTIVE: it denoised the last traced frame.
#define TGPT_FEATURE_RAY_REGENERATION 4u
#define TGPT_FEATURE_RAY_REGENERATION_ACTIVE 8u
// NRD: NVIDIA's Real-Time Denoisers (RELAX) are in this build and started on the device. NRD_ACTIVE: they denoised the
// last traced frame.
#define TGPT_FEATURE_NRD 16u
#define TGPT_FEATURE_NRD_ACTIVE 32u

// Render events (Unity's IssuePluginEventAndData; the data is TGPT_Frame.sequence).
#define TGPT_EVENT_TRACE 1
#define TGPT_EVENT_COMPOSITE 2
#define TGPT_EVENT_SHADOW_DETAIL 3 // data: a TGPT_SetShadowDetail request
#define TGPT_EVENT_WATER 4         // data: the TGPT_Frame.sequence TGPT_SetWater named

// Vertex attribute formats (Unity's VertexAttributeFormat values).
#define TGPT_VF_FLOAT32 0u
#define TGPT_VF_FLOAT16 1u
#define TGPT_VF_UNORM8 2u
#define TGPT_VF_SNORM8 3u
#define TGPT_VF_UNORM16 4u
#define TGPT_VF_SNORM16 5u

// TGPT_Material.flags
#define TGPT_MATERIAL_DOUBLE_SIDED 1u
#define TGPT_MATERIAL_ALPHA_TEST 2u
#define TGPT_MATERIAL_VERTEX_COLOR 4u // the mesh's vertex colors are the albedo (terrain)
// alphaTile holds a TGPT_RegisterAlphaTexture tile: the alpha test (and its micromaps) read the texture's alpha
// at 256x256 there, not the albedo tile's 64x64, which blurs straw, rope nets and leaves into opaque blocks.
#define TGPT_MATERIAL_ALPHA_TILE 8u
// Seen by camera rays only (eyes, unlit decals: surfaces HDRP draws forward): every other ray passes through it, so it
// casts no shadow and stops no bounced light. Needs TGPT_MATERIAL_ALPHA_TEST too (its triangles are tested per ray).
#define TGPT_MATERIAL_CAMERA_ONLY 16u
// With TGPT_MATERIAL_CAMERA_ONLY: a camera ray looking for a surface HDRP draws forward stops at it and leaves the pixel to
// HDRP's own shading (the eyes: HDRP's eye shader refracts the iris under the cornea from textures of its own).
#define TGPT_MATERIAL_HDRP_SHADED 32u
// Glass (a bottle, a flask): a ray passes through it by chance, as bright as its color is (both crossings of a closed
// object together letting its luminance through), whether it carries a shadow, the sky's or bounced light; the rest meet
// it, as camera rays always do. Needs TGPT_MATERIAL_ALPHA_TEST (its triangles are tested per ray), with a cutoff of 0
// unless its texture cuts it out as well.
#define TGPT_MATERIAL_GLASS 64u
// maskTile holds a TGPT_RegisterMaskTexture tile (HDRP Lit's mask map, read through the base color map's tiling): metallic
// and smoothness texel by texel, each the lerp of its maskRemap ends by the texel's value. Without it metallic and
// smoothness are the material's constants.
#define TGPT_MATERIAL_MASK_TILE 1024u
// The emission is multiplied by the albedo (base color and texture), as HDRP's Albedo Affects Emissive has it.
#define TGPT_MATERIAL_EMISSIVE_ALBEDO 128u
// Thin and lit through from behind, as HDRP's transmission lights a leaf (grass, leaves): where a path meets it, the light
// on its far side comes through at the share the camera's translucent pixels take (pt_common.hlsli TRANSMISSION).
#define TGPT_MATERIAL_TRANSLUCENT 256u
// Thin cloth that hangs (a banner, laundry, a curtain, canvas): lit through from behind as the light passes through its weave,
// dimmer than a leaf (pt_common.hlsli CLOTH_TRANSMISSION), where the camera sees it and where paths meet it.
#define TGPT_MATERIAL_THIN_CLOTH 512u
// Hair strands (with TGPT_MATERIAL_ALPHA_TEST, and TGPT_MATERIAL_ALPHA_TILE for their coverage): a ray other than a camera
// ray stops at a texel by chance, as much of it as the strands cover, so a layer of hair lets part of the light through
// between its strands. The cutoff is not used. Such a mesh gets no micromaps.
#define TGPT_MATERIAL_STRANDS 4096u
// Moved by the wind as HDRP draws it (its shader bends the vertices: grass, leaves, a banner, laundry), where the traced
// mesh stands still: near a surface HDRP drew, the two need not agree, so rays leaving such a surface pass an instance whose
// every material has it within 20 cm (a ray from a blade met its own still copy beside it, and the ground the drawn grass
// swayed away from lay in the still grass's shadow: black spots swaying with the grass).
#define TGPT_MATERIAL_SWAYS 8192u
// TGPT_Instance.flags
#define TGPT_INSTANCE_ALPHA_TEST 1u   // some material of the instance is alpha-tested: rays test its texture
#define TGPT_INSTANCE_HIDDEN 2u       // kept in the list but not traced
#define TGPT_INSTANCE_CHARACTER 8u    // set by the tracer on the characters it traces (TGPT_SetSkinned), never by the caller
#define TGPT_INSTANCE_STAND_IN 4u     // a coarse stand-in for geometry drawn otherwise (a shadow-only mesh): rays from
                                      // a drawn surface pass through it close by, whichever way it faces
#define TGPT_INSTANCE_MOVING 16u      // moved since the plugin's last scene update (a carried lantern, a held weapon, a door):
                                      // its emission is never sampled as a light, which would stay where the update left it
#define TGPT_INSTANCE_SWAYS 32u       // set by the tracer where every material of the instance sways (TGPT_MATERIAL_SWAYS)
// TGPT_Light.flags, above its cookie's 16 bits: HDRP shadows this light itself (it renders a shadow map for it), so what HDRP
// shades itself takes no traced shadow from it as well (TGPT_OPTION_NO_HDRP_SHADOWS).
#define TGPT_LIGHT_HDRP_SHADOWS 0x10000u
// HDRP's light doesn't affect specular (HDAdditionalLightData.affectSpecular off): it makes no highlight, on metal or anything
// glossy, in the game, and none where the tracer shades its light either (its diffuse light as ever).
#define TGPT_LIGHT_NO_SPECULAR 0x20000u

#pragma pack(push,4)
struct TGPT_Submesh { uint32_t indexStart, indexCount, baseVertex, topology; }; // topology: Unity MeshTopology, 0 triangles
// A mesh on the game's device (Mesh.GetNativeVertexBufferPtr / GetNativeIndexBufferPtr). The buffers
// are referenced when registered and read back once; the mesh may change or go away afterwards.
struct TGPT_MeshDesc {
    uint32_t size, abi;
    uint64_t key;                       // the plugin's identity for the mesh (Unity instance ID), for logs
    uint64_t vertexBuffers[4];          // ID3D11Buffer* per vertex stream, 0 when unused
    uint32_t strides[4];
    uint32_t positionStream, positionOffset, positionFormat, positionDimension;
    uint32_t uvStream, uvOffset, uvFormat, uvDimension;  // uvStream 0xFFFFFFFF: no texture coordinates
    uint32_t colorStream, colorOffset, colorFormat, colorDimension; // colorStream 0xFFFFFFFF: none
    uint64_t indexBuffer;               // ID3D11Buffer*
    uint32_t indexFormat;               // 16 or 32
    uint32_t vertexCount;
    uint32_t submeshCount, reserved;
    const TGPT_Submesh* submeshes;
};
// A mesh made on the CPU (terrain): world-space-ready object positions, optional UVs and RGBA8 colors.
struct TGPT_CpuMeshDesc {
    uint32_t size, abi;
    uint64_t key;
    const float* positions;             // xyz per vertex
    const float* uvs;                   // xy per vertex, or null
    const uint32_t* colors;             // RGBA8 per vertex (linear), or null
    const uint32_t* indices;            // triangle list
    uint32_t vertexCount, indexCount;
};
struct TGPT_Material {
    float baseColor[4];                 // linear RGBA; alpha multiplies the texture's for alpha testing
    float uvScaleOffset[4];             // the base color map's tiling (xy) and offset (zw)
    float emissive[3];                  // linear radiance, nits (HDRP's emissive color x intensity, its exposure weight applied)
    float metallic;
    int32_t albedoTile, emissiveTile;   // TGPT_RegisterTexture results, -1 none
    float smoothness, alphaCutoff;
    uint32_t flags;                     // TGPT_MATERIAL_*
    int32_t alphaTile;                  // with TGPT_MATERIAL_ALPHA_TILE: a TGPT_RegisterAlphaTexture result
    int32_t maskTile;                   // with TGPT_MATERIAL_MASK_TILE: a TGPT_RegisterMaskTexture result
    uint32_t maskRemap;                 // metallic's low and high end, then smoothness's, 8 bits each (0-255 for 0-1), low first
};
struct TGPT_Instance {
    float transform[12];                // object to world, row-major 3x4
    int32_t mesh;                       // TGPT_RegisterMesh / TGPT_RegisterCpuMesh result
    uint32_t materialBase;              // index in the material index list of submesh 0's material
    uint32_t flags;                     // TGPT_INSTANCE_*
    uint32_t mask;                      // ray mask, 0xFF for everything
};
// A character this frame (the game's GPU-skinned renderer): where its vertices are in the skinning output that
// TGPT_SetSkinned names, and which registered skinned mesh holds its triangles. Its bottom-level acceleration
// structure is rebuilt from those vertices every frame, so it shadows itself and the world as it moves.
#define TGPT_MAX_SKINNED 64u
#define TGPT_SKINNED_MATERIALS 8u
struct TGPT_SkinnedInstance {
    float transform[12];                // skinned space to world, row-major 3x4 (identity when the skinning writes world space)
    int32_t mesh;                       // TGPT_RegisterSkinnedMesh result
    uint32_t firstVertex;               // the character's first vertex in the skinning output
    uint32_t vertexCount;               // its vertices there: the mesh's vertex count
    uint32_t submeshCount;              // up to TGPT_SKINNED_MATERIALS: the registered mesh's submeshes (more are not registered)
    uint32_t materials[TGPT_SKINNED_MATERIALS]; // material per submesh (TGPT_SetMaterials indices)
};
struct TGPT_Light {
    float position[3]; uint32_t type;   // world; 0 point, 1 spot, 2 directional
    float color[3]; float range;        // linear color x intensity in HDRP's units (candela, lux), dimmer applied
    float direction[3]; float radius;   // where a spot or directional light points (world); emitter radius
    float angleScale, angleOffset;      // spot cone as HDRP computes it
    uint32_t id, flags;                 // id: the light's identity, 0 for an empty slot: a slot holding the same id in
                                        // consecutive frames is the same light for ReSTIR's reuse across frames;
                                        // flags: the low 16 bits 1 + its cookie (a TGPT_RegisterCookie result), 0 without
                                        // one; TGPT_LIGHT_HDRP_SHADOWS
    float up[3];                        // the light's up (world): with direction, the orientation of its cookie
    float cookieScale;                  // a spot's cookie across its cone: 1 / tan(half its angle), as HDRP projects it
};
struct TGPT_Frame {
    uint32_t size, abi, sequence, flags;
    uint64_t gbuffer0, normal, gbuffer2, gbuffer3, depth, motion, exposure; // ID3D11Resource*, HDRP's
    uint32_t renderWidth, renderHeight, debugView;
    float invViewProj[16];              // HDRP's mainViewConstants.invViewProjMatrix: camera-relative, jittered
    float viewProj[16];                 // mainViewConstants.viewProjMatrix
    float prevViewProj[16];             // mainViewConstants.prevViewProjMatrix (without jitter), which takes positions relative
                                        // to this frame's camera: HDRP moves it by the camera's move
    float cameraPosition[4];            // world; w unused
    float prevCameraPosition[4];        // world; w unused
    float sunDirection[4];              // towards the sun, world, normalized; w: angular radius in radians
    float sunColor[4];                  // linear color x illuminance (lux); w: 1 when there is a sun, 2 when HDRP shadows it
                                        // itself (its cascades), as TGPT_LIGHT_HDRP_SHADOWS marks a light
    // giHistory: ReSTIR GI's temporal history in frames (NVIDIA RTXDI's maxHistoryLength); 0: its default of 8.
    uint32_t frameIndex, bounces, spatialSamples, giHistory;
    uint32_t options, raysPerPixel;     // raysPerPixel: indirect-light rays from each pixel a frame (1-16)
    float reflectionBoost;              // 0 as the game made them, 1 every surface nearly a mirror
    // The traced light's own exposure, on top of HDRP's (which stays the game's, for everything HDRP draws itself): the
    // composite multiplies the traced light by it. 0 means 1.
    float tracedGain;
    float maxRayDistance, skyIntensity;
    float indirectColor;  // how much color the bounced light keeps at the same brightness: 1 as traced, 0 grey, up to 4
    uint32_t diHistory;   // ReSTIR DI's temporal history in frames (NVIDIA RTXDI's maxHistoryLength); 0: 20
    uint32_t thickProfiles; // bit i: HDRP's diffusion profile i transmits as a thick object (skin): lit as a solid surface, not through like a leaf
    // For AMD FSR Ray Regeneration: the camera-relative world-to-view matrix (mainViewConstants.viewMatrix),
    // the projection without jitter (nonJitteredProjMatrix, GPU form), and the jitter (HDCamera.taaJitter,
    // render pixels; zw unused).
    float view[16];
    float nonJitteredProj[16];
    float jitter[4];
    // HDRP's height fog as its Fog volume sets it for the camera, which HDRP lays over the composite: its extinction per
    // metre (1 / its attenuation distance; 0 without fog), its base height (world) and 1 / its scale height (the density
    // falls off as e^-((h - base) / H) above the base); w: the player's Exposure as a factor, the part of tracedGain that
    // isn't automatic (0 means 1). The automatic part fades as the fog hides a surface.
    float fog[4];
    // x: the share of the game's ambient light HDRP leaves the surfaces it shades itself (hair, eyes), as much of the sky's
    // light as reaches the camera (HdrpPasses), 1 without; the tracer gives them its bounced light for the rest. yzw unused.
    float forward[4];
};
struct TGPT_Status {
    uint32_t size, abi;
    uint32_t deviceReady, supported;    // supported: inline ray tracing and Shader Model 6.6 on the shared device
    uint32_t meshesRegistered, meshesReady, meshesFailed, meshesQueued;
    uint32_t instances, instancesTraced, materials, textures;
    uint32_t lights, disabled;          // disabled: turned off for the session after errors
    uint64_t framesTraced, framesFailed;
    uint64_t triangles, geometryBytes, accelerationBytes;
    float gpuMs, frameMs;               // GPU time of the last traced frame; render-thread time of event 1
    int32_t lastError; uint32_t renderWidth, renderHeight;
    uint32_t composited;                // traced light applied to HDRP's color buffer (event 2), wraps
    float d3d11Ms, flushMs, waitMs, recordMs; // event 1's render-thread time by part, last frame
    uint32_t features;                  // TGPT_FEATURE_*: what the passes run with on this GPU
    uint32_t micromapMeshes;            // meshes whose alpha-tested submeshes have opacity micromaps
    uint32_t micromapInstances;         // instances that traced with them last frame
    uint32_t micromapsWaiting;          // meshes waiting for their textures before theirs are made
    // The sky the camera sees against the open sky's along the same rays (pt_probe.hlsl): 1 in the open, 0 in a closed
    // room; negative until measured. And the sky's light it sees, linear RGB in HDRP's units.
    float cameraLight, cameraRadiance[3];
    // The traced light as shown, for its gain (pt_meter.hlsl): log2 of the median and the 98th percentile of its
    // luminance at HDRP's exposure without the gain, centre-weighted over every other pixel the tracer lights, and the
    // weight they come from (0 until measured).
    float tracedLog2Mid, tracedLog2High;
    uint32_t tracedSamples;
    // The surface under the screen's centre (pt_probe.hlsl): 1 something traced there, 2 glass the tracer sees through,
    // 4 a GBuffer surface, 8 one drawn forward; bits 8 to 15 HDRP's roughness there, 16 to 23 the tracer's (0 to 255).
    uint32_t pickSurface;
    // What the traced scene has under the screen's centre (pt_probe.hlsl, along the camera ray): its instance (the
    // TGPT_SetScene index; past those, characters), material (a TGPT_SetMaterials index) and distance; pickFound 0 if nothing.
    uint32_t pickInstance, pickMaterial; float pickDistance; uint32_t pickFound;
    // Traced reflections on water (TGPT_SetWater, render event 4), both wrapping: frames whose water surfaces the tracer
    // traced reflections for, and those whose HDRP reflections took the previous one's in.
    uint32_t waterTraced, waterApplied;
};
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGPT_Submesh)==16, "TG Path Tracing submesh ABI");
static_assert(sizeof(TGPT_MeshDesc)==144, "TG Path Tracing mesh ABI");
static_assert(sizeof(TGPT_CpuMeshDesc)==56, "TG Path Tracing CPU mesh ABI");
static_assert(sizeof(TGPT_Material)==80, "TG Path Tracing material ABI");
static_assert(sizeof(TGPT_Instance)==64, "TG Path Tracing instance ABI");
static_assert(sizeof(TGPT_Light)==80, "TG Path Tracing light ABI");
static_assert(sizeof(TGPT_Frame)==568, "TG Path Tracing frame ABI");
static_assert(sizeof(TGPT_Status)==208, "TG Path Tracing status ABI");
static_assert(sizeof(TGPT_SkinnedInstance)==96, "TG Path Tracing skinned instance ABI");
#endif

TGPT_EXPORT uint32_t TGPT_CALL TGPT_GetAbiVersion();
TGPT_EXPORT int32_t TGPT_CALL TGPT_ReadStatus(TGPT_Status* status);
// Why path tracing can't run on this GPU (UTF-8), empty while it can or before the device exists.
// Returns the full length.
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetUnsupportedReason(char* buffer, uint32_t size);
// What each stage of a frame costs the GPU, averaged over about the last second (UTF-8, "scene 1.20 ms, prepare 0.31 ms,
// ..."), for the F8 report; empty until measured. Returns the full length.
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetStageTimes(char* buffer, uint32_t size);
// Scene, main thread. Mesh and texture results are handles (> 0 for meshes, >= 0 tiles for textures),
// negative on refusal. Meshes are read back on the render thread over the following frames.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterMesh(const TGPT_MeshDesc* mesh);
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterCpuMesh(const TGPT_CpuMeshDesc* mesh);
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterTexture(uint64_t key, uint64_t texture);
// A character's triangles (a CPU mesh: positions may be null, the skinning output replaces them every frame;
// submeshes as index ranges in order). Returns a mesh handle for TGPT_SkinnedInstance.mesh.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterSkinnedMesh(const TGPT_CpuMeshDesc* mesh, const uint32_t* submeshStarts, uint32_t submeshCount);
// This frame's characters, taken by the next TGPT_SubmitFrame: the game's skinning output (ID3D11Buffer*, a
// structured or raw buffer whose vertices begin with a float3 position; stride in bytes) and the characters in it,
// at most TGPT_MAX_SKINNED. A count of 0 traces none.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSkinned(uint64_t buffer, uint32_t stride, const TGPT_SkinnedInstance* instances, uint32_t count);
// An alpha-tested material's texture (ID3D11Texture2D*): its alpha at 256x256 in the alpha atlas, for the alpha
// test. The tile, or <0: -3 when the alpha atlas is full (the albedo tile's alpha serves then).
// cutoff: the alpha test's (over the material's base alpha): the tile holds how much of each texel passes it (its
// coverage), which TGPT_Material.alphaCutoff is then compared with (0.5: half the texel covered).
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterAlphaTexture(uint64_t key, uint64_t texture, float cutoff);
// A mask map (ID3D11Texture2D*, its bytes read as they are: a linear texture) for TGPT_Material.maskTile: a 64x64 tile of the
// color atlas, TGPT_RegisterTexture's, under keys of its own. channels: which of the texture's channels (0 red to 3 alpha)
// holds metallic (bits 0-1) and which smoothness (bits 2-3); HDRP Lit's are red and alpha. The tile, or <0: -3 once three
// quarters of the atlas are taken (the rest stays for color).
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterMaskTexture(uint64_t key, uint64_t texture, uint32_t channels);
// A light's cookie (HDRP's mask on its intensity by direction): texture is its ID3D11Texture2D*, a cubemap for a point
// light (baked as an octahedral map) or a 2D texture for a spot. Returns its index for TGPT_Light.flags (baked a few
// frames later; until then the light is unmasked), or < 0.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterCookie(uint64_t key, uint64_t texture);
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetMaterials(uint32_t first, const TGPT_Material* materials, uint32_t count);
// The whole instance list and the flattened per-instance, per-submesh material indices.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetScene(const TGPT_Instance* instances, uint32_t count, const uint32_t* materialIndices, uint32_t materialIndexCount);
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetLights(const TGPT_Light* lights, uint32_t count);
// HDRP's sky reflection (a cubemap or cubemap array, ID3D11Resource*); resampled every few frames.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSky(uint64_t texture);
// Drops the scene: meshes, textures, materials and instances (a new game or a crash recovery).
TGPT_EXPORT int32_t TGPT_CALL TGPT_ClearScene();
// Frame, main thread while HDRP records it: the request render event 1 carries out, then the
// destination render event 2 writes into (HDRP's camera color buffer).
TGPT_EXPORT int32_t TGPT_CALL TGPT_SubmitFrame(const TGPT_Frame* frame);
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetComposite(uint32_t sequence, uint64_t color);
TGPT_EXPORT void* TGPT_CALL TGPT_GetRenderEvent();
// Shadow Detail (the player's row): HDRP's finished HDR image, after the denoisers and the upscaler and before its
// post-processing and tonemapper, with its dark parts lifted by a bounded curve below a mid tone (d3d11.hlsl
// TGPT_SHADOW_DETAIL). color: the camera's color texture there (ID3D11Resource*); strength 0 to 1. Render event 3 with the
// same request number applies it, in place. 0 success; <0 for a strength of 0 (nothing to do) or no texture.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetShadowDetail(uint32_t request, uint64_t color, float strength);
// Traced reflections on water (HDRP's water system: lakes, rivers, the sea) and the other transparent surfaces HDRP gives
// screen-space reflections (liquids): HDRP's transparent SSR step finds what they reflect in the last frame's image, and
// leaves the rest of each reflection (what is off screen or hidden) to reflection probes and the sky. Render event 4 with
// the frame's sequence, issued right after that step, fills the rest from the traced scene and keeps HDRP's own where it
// found it. reflections: that step's result, HDRP's SSR texture (R16G16B16A16_FLOAT with an unordered access view: the
// reflected light pre-exposed with the previous frame's exposure, premultiplied by its weight in alpha); depth: HDRP's
// depth-stencil buffer then, holding the water's depth and, on every surface that takes these reflections, stencil bit 8
// (HDRP's TraceReflectionRay); normal: its normal buffer, holding the water's normals and roughness. The event traces this
// frame's surfaces for the next frame's event, a frame late as the composite applies its light. reflectionMultiplier:
// HDRP's reflection lighting multiplier (IndirectLightingController) for the frame, which HDRP applies to a surface's
// whole reflection: what the event writes is divided by it. 0 success.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetWater(uint32_t sequence, uint64_t reflections, uint64_t depth, uint64_t normal, float reflectionMultiplier);
// Light scattered under skin: for each of HDRP's diffusion profiles by its GBuffer index (up to 16), four floats: Burley's
// shape parameter per color channel (1 / the scattering distance, per metre of the world) and the filter radius in metres
// (0: none). The surfaces whose GBuffer holds transmission through a thick profile (TGPT_Frame::thickProfiles) have their
// traced diffuse light spread as their profile sets (pt_sss.hlsl). count 0 turns it off. 0 success.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetDiffusionProfiles(const float* profiles, uint32_t count);
// Light through thin skin (an ear, a nostril, fingers lit from behind): for each of HDRP's diffusion profiles by its GBuffer
// index (up to 16), eight floats as HDRP's shader constants hold them: the transmission tint times 0.25 (rgb) and the
// thickness remap's start (mm), then Burley's shape per channel (1 / the scattering distance, per mm) and the remap's span
// (mm). A surface whose GBuffer holds transmission through a thick profile (TGPT_Frame::thickProfiles) takes HDRP's
// transmittance of its thickness (Lit's FillMaterialTransmission) and is lit from behind as HDRP's thin-object mode lights
// it, where the traced geometry lets the light through. count 0 (or a tint of 0) lets none through. 0 success.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSkinTransmission(const float* profiles, uint32_t count);
// DLSS Ray Reconstruction's guides, which render event 2 writes for frames with TGPT_FRAME_GUIDES, as
// textures on the game's device (ID3D11Resource*, exactly width x height): linear diffuse albedo, specular
// albedo, and world-space normals with linear (perceptual) roughness in alpha. The textures stay valid
// for at least eight frames after they are replaced (a render size change). 0 success; the pointers are
// 0 before the first frame with guides.
#pragma pack(push,4)
struct TGPT_Guides {
    uint32_t size, abi;
    uint32_t width, height;
    uint64_t diffuseAlbedo, specularAlbedo, normalRoughness;
    uint32_t frameIndex;    // TGPT_Frame.frameIndex of the last frame whose guides were written
    uint32_t extrasFrameIndex; // the frame whose composite wrote the two below (they are written after the others)
    // Written by the composite, as it applies the traced light: the traced reflections' hit distance (world units,
    // R16_FLOAT), and the frame's color right after it, before HDRP draws transparent surfaces and particles
    // (R16G16B16A16_FLOAT): Ray Reconstruction's specular hit distance and color before transparency.
    uint64_t specularHitDistance, colorBeforeTransparency;
};
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGPT_Guides)==64, "TG Path Tracing guides ABI");
#endif
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetGuides(TGPT_Guides* guides);
