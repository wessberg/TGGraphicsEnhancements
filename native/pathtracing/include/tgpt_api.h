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
#define TGPT_ABI 0x00010600u

// TGPT_Frame.debugView: what the composite writes instead of the lit image
#define TGPT_VIEW_NONE 0u
#define TGPT_VIEW_ALBEDO 1u        // HDRP's diffuse color, as the tracer reads it
#define TGPT_VIEW_NORMALS 2u       // HDRP's normals, as the tracer reads them
#define TGPT_VIEW_COVERAGE 3u      // camera rays against the traced scene: each instance its own color, black where the scene has nothing
#define TGPT_VIEW_MATERIALS 4u     // the traced scene's own material colors along camera rays
#define TGPT_VIEW_INDIRECT 5u      // denoised indirect light on a white surface
#define TGPT_VIEW_INDIRECT_RAW 6u  // the same before denoising
#define TGPT_VIEW_DIRECT 7u        // path-traced direct light on a white surface
#define TGPT_VIEW_COUNT 8u

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
// TGPT_Status::features. DXR12: the Shader Model 6.9 passes run (DirectX Raytracing 1.2), and alpha-tested
// meshes get opacity micromaps. SER: GI and reflections run as ray generation shaders whose threads
// Shader Execution Reordering regroups by the material they hit (only where the GPU reorders).
#define TGPT_FEATURE_DXR12 1u
#define TGPT_FEATURE_SER 2u
// RAY_REGENERATION: AMD FSR Ray Regeneration runs on this GPU (a Radeon RX 9000 or newer).
// RAY_REGENERATION_ACTIVE: it denoised the last traced frame.
#define TGPT_FEATURE_RAY_REGENERATION 4u
#define TGPT_FEATURE_RAY_REGENERATION_ACTIVE 8u

// Render events (Unity's IssuePluginEventAndData; the data is TGPT_Frame.sequence).
#define TGPT_EVENT_TRACE 1
#define TGPT_EVENT_COMPOSITE 2

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
// TGPT_Instance.flags
#define TGPT_INSTANCE_ALPHA_TEST 1u   // some material of the instance is alpha-tested: rays test its texture
#define TGPT_INSTANCE_HIDDEN 2u       // kept in the list but not traced
#define TGPT_INSTANCE_STAND_IN 4u     // a coarse stand-in for geometry drawn otherwise (a shadow-only mesh): rays from
                                      // a drawn surface pass through it close by, whichever way it faces

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
    float emissive[3];                  // linear radiance, nits
    float metallic;
    int32_t albedoTile, emissiveTile;   // TGPT_RegisterTexture results, -1 none
    float smoothness, alphaCutoff;
    uint32_t flags;                     // TGPT_MATERIAL_*
    int32_t alphaTile;                  // with TGPT_MATERIAL_ALPHA_TILE: a TGPT_RegisterAlphaTexture result
    uint32_t reserved[2];
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
    uint32_t id, flags;                 // id: the plugin's stable slot, for ReSTIR's reuse across frames
};
struct TGPT_Frame {
    uint32_t size, abi, sequence, flags;
    uint64_t gbuffer0, normal, gbuffer2, gbuffer3, depth, motion, exposure; // ID3D11Resource*, HDRP's
    uint32_t renderWidth, renderHeight, debugView;
    float invViewProj[16];              // HDRP's mainViewConstants.invViewProjMatrix: camera-relative, jittered
    float viewProj[16];                 // mainViewConstants.viewProjMatrix
    float prevViewProj[16];             // mainViewConstants.prevViewProjMatrix, relative to the previous camera position
    float cameraPosition[4];            // world; w unused
    float prevCameraPosition[4];        // world; w unused
    float sunDirection[4];              // towards the sun, world, normalized; w: angular radius in radians
    float sunColor[4];                  // linear color x illuminance (lux); w: 1 when there is a sun
    uint32_t frameIndex, bounces, spatialSamples, temporalCap;
    uint32_t options, raysPerPixel;     // raysPerPixel: indirect-light rays from each pixel a frame (1-8)
    float reflectionBoost;              // 0 as the game made them, 1 every surface nearly a mirror
    float clampExposure;                // HDRP's exposure averaged over ~10 s, which the radiance clamps are measured against (0: its current one)
    float maxRayDistance, skyIntensity;
    float indirectColor;  // how much color the bounced light keeps at the same brightness: 1 as traced, 0 grey, up to 4
    // For AMD FSR Ray Regeneration: the camera-relative world-to-view matrix (mainViewConstants.viewMatrix),
    // the projection without jitter (nonJitteredProjMatrix, GPU form), and the jitter (HDCamera.taaJitter,
    // render pixels; zw unused).
    float view[16];
    float nonJitteredProj[16];
    float jitter[4];
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
};
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGPT_Submesh)==16, "TG Path Tracing submesh ABI");
static_assert(sizeof(TGPT_MeshDesc)==144, "TG Path Tracing mesh ABI");
static_assert(sizeof(TGPT_CpuMeshDesc)==56, "TG Path Tracing CPU mesh ABI");
static_assert(sizeof(TGPT_Material)==80, "TG Path Tracing material ABI");
static_assert(sizeof(TGPT_Instance)==64, "TG Path Tracing instance ABI");
static_assert(sizeof(TGPT_Light)==64, "TG Path Tracing light ABI");
static_assert(sizeof(TGPT_Frame)==528, "TG Path Tracing frame ABI");
static_assert(sizeof(TGPT_Status)==152, "TG Path Tracing status ABI");
static_assert(sizeof(TGPT_SkinnedInstance)==96, "TG Path Tracing skinned instance ABI");
#endif

TGPT_EXPORT uint32_t TGPT_CALL TGPT_GetAbiVersion();
TGPT_EXPORT int32_t TGPT_CALL TGPT_ReadStatus(TGPT_Status* status);
// Why path tracing can't run on this GPU (UTF-8), empty while it can or before the device exists.
// Returns the full length.
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetUnsupportedReason(char* buffer, uint32_t size);
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
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterAlphaTexture(uint64_t key, uint64_t texture);
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
    uint32_t reserved;
};
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGPT_Guides)==48, "TG Path Tracing guides ABI");
#endif
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetGuides(TGPT_Guides* guides);
