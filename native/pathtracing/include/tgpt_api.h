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
#define TGPT_ABI 0x00012700u

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
// The tracer's own first hit against HDRP's G-buffer, pixel by pixel (PT-59): dim green the same surface read the same;
// yellow another colour, blue another normal, cyan another roughness; grey the same surface where HDRP drew forward (nothing
// to compare); orange the tracer's hit nearer than HDRP's surface, red farther; magenta the tracer met nothing; purple it
// met something where HDRP has sky; half white on top where its own motion is not HDRP's. Its frames use the tracer's own
// camera rays whatever TGPT_OPTION_GBUFFER_SURFACES says. The numbers: TGPT_GetYardstick.
#define TGPT_VIEW_YARDSTICK 11u
#define TGPT_VIEW_COUNT 12u

// TGPT_Frame.flags
#define TGPT_FRAME_RESET 1u        // camera cut, first frame, resolution change: no history
#define TGPT_FRAME_GUIDES 2u       // write DLSS Ray Reconstruction's guide buffers in render event 2 (TGPT_GetGuides)
// ON TRIAL (PT-109, docs/agents/megageo-1.md; no size or ABI change, 0 as every caller left these bits): NVIDIA RTX Mega
// Geometry, the plugin's hidden "Mega Geometry trial", in the flags' bits 2 to 5 (no option bit: all 32 are taken).
// 0: the plain acceleration structures, as before. 1, 2, 3: the frame's top level partitioned (mega_geometry.hpp), in
// cells of 64, 128 or 256 m, where the tracer was built with CMake's TGPT_MEGA_GEOMETRY and the driver reports it;
// 4, 5, 6 (docs/agents/megageo-4.md): each cell's small plants in a group of their own; 5 builds those groups from clusters
// stamped from templates where the driver reports clusters, with their micromaps as NVAPI's own arrays where it reports
// those (docs/agents/megageo-5.md); 6 leaves their micromaps out. 7 (megageo-5): NVIDIA's layout, every plant its own bottom
// level of its clusters (with micromaps) and its own instance, in the partitioned top level of 128 m cells; 8: 7 without
// micromaps. Elsewhere, and with any other number, nothing changes, and the log says so.
#define TGPT_FRAME_MEGA_SHIFT 2u
#define TGPT_FRAME_MEGA_MASK 15u
// FOR MEASURING (PT-110, docs/agents/omm-1.md; no size or ABI change, 0 as every caller left these bits): the plugin's
// hidden "Micromaps trial", in the flags' bits 6 to 13 (no option bit: all 32 are taken). 0: everything as it ships.
// A number each, added up; EVERY ONE CHANGES THE PICTURE (1, 2, 4) OR ITS SPEED (8, 16) ON PURPOSE, for one step of one
// run, to bound what a kind of cut-out costs (NVIDIA's own advice: trace once with the cut-outs solid). Never a default.
//   1  the plants in the wind's pose: their cut-outs solid to every ray but the camera's own (leaves are whole cards)
//   2  the characters: their cut-outs solid the same way (hair is whole cards)
//   4  everything else with a cut-out (still meshes, the things the game moves, cloth in the wind's pose): solid the same
//      way; where a mesh has micromaps its bare pieces stay bare and only its unknown pieces turn solid
//   8  no instance traces with its micromaps (the shader's alpha test decides alone: the same picture, another speed)
//  16  the submeshes of characters and of plants in the wind's pose that are no cut-out go to the shader's test with
//      their cut-outs, as they did until 2026-10-10 (the same picture, another speed: what marking them solid brought)
// And ON TRIAL, off (0) until the game has answered (docs/agents/omm-1.md, M1):
//  32  the plants in the wind's pose trace with their meshes' micromaps: a group's structure links each member mesh's
//      micromap array, at its build and at every refit to the wind (the same picture: a micromap settles only what the
//      alpha test would decide; another speed)
//  64  with 32, for finding a cause: such a group is built anew at every pose in place of refitted (slow)
#define TGPT_FRAME_OMM_SHIFT 6u
#define TGPT_FRAME_OMM_MASK 255u
// PT-112 (A), ON TRIAL with the plugin's hidden "Game passes while tracing" 32 (HDRP draws no opaque object): render event 2
// writes the traced frame's own depth (reversed-Z, HDRP's projection with the frame's sub-pixel shift) and motion vectors
// (HDRP's kind) into HDRP's depth buffer and motion vectors (TGPT_Frame.depth and .motion), for what reads them after the
// composite: HDRP's fog and transparents, the upscaler, depth of field, motion blur, frame generation; and Fog Adaptation
// takes that depth too. No size or ABI change: 0 as every caller left the bit.
#define TGPT_FRAME_OWN_DEPTH (1u << 14)
// The game's fog, traced (PT-112 step B, the rule since 2026-10-11; docs/agents/fog-2.md; the plugin leaves HDRP's own
// opaque fog pass out with it): the game's height fog and fog boxes along each camera ray, lit by the sun, the sky, the
// lamps and what glows with traced shadows, laid over the picture by the composite (pt_compose), the traced light's gain
// unfaded by HDRP's fog. Its light is held in cells of a grid that follows the camera, as RTX Remix builds its fog
// (PT-118: pt_fog_froxel, pt_fog_integrate, pt_fog_tail; the rule since 2026-10-12).
#define TGPT_FRAME_TRACED_FOG (1u << 15)
// With it: the light the fog scatters more than once (the mist's cells of the radiance cache; without: the light
// scattered once alone, for tests).
#define TGPT_FRAME_FOG_WALK (1u << 16)
// (PT-37) (Bit 19: free since 2026-10-12. It chose the fog's cells against the per-pixel route, pt_fog.hlsl,
// which went.) (Bits 25 to 29: free since 2026-10-12. With 24 they were the fog grid's trial number, 24 and
// 25: the walk as the first trial had it and RTX Remix's reuse and filter, which showed the same picture as
// the plain cells in every scene for more time; and the number that left a part of the grid's fog out while
// its numbers were logged, 26 to 29: PT-118, PT-133.) (Bit 21: free since 2026-10-11. It was the trial of
// NRD's history confidence, which is the rule whenever NRD runs.) (Bit 20 is free: PT-110's micromaps for
// characters, on trial there, gave no speed in the game and came out, 2026-10-11.) NVIDIA NRD as its README
// sets it up (PT-127): all the light a surface gives, the diffuse light (the direct and the bounced light
// and the caustics, demodulated) and the specular light (the reflections and the lights' highlights,
// demodulated), with their hit distances, the shading normal and 2.5D motion (pt_nrd_prep.hlsl,
// nrd_denoiser.cpp). Glass and water keep the reflections' own filter (NRD is for opaque surfaces). The
// denoiser, ON TRIAL (no size or ABI change, 0 as every caller left these bits), the plugin's hidden "NRD
// trial" in the flags' bits 17 and 18 (no option bit: all 32 are taken): 0 or 1 RELAX (NVIDIA: "designed for
// RTXDI"), 2 REBLUR, which sizes its filter by the hit distances. (Until 2026-10-11 0 was RELAX on the
// bounced light alone, in its directional form, with the tracer's own filters on the rest: gone.)
#define TGPT_FRAME_NRD_SHIFT 17u
#define TGPT_FRAME_NRD_MASK 3u
#define TGPT_OMM_TRIAL_PLANTS_SOLID 1u
#define TGPT_OMM_TRIAL_CHARACTERS_SOLID 2u
#define TGPT_OMM_TRIAL_STILL_SOLID 4u
#define TGPT_OMM_TRIAL_NO_MAPS 8u
#define TGPT_OMM_TRIAL_PLAIN_TESTED 16u
// (32: since round 20 the plants in the wind's pose link their meshes' micromaps by default (M1, the town 65.2 -> 64.6 ms of
// the tracer, run 20261011-035325; the forest 121 -> 115 frozen); 32 leaves them without, as before, for measuring.)
#define TGPT_OMM_TRIAL_NO_PLANT_MAPS 32u
#define TGPT_OMM_TRIAL_PLANT_REBUILD 64u
// The micromaps of leaves and grass (a cut-out that lets light through and is no thin cloth) are baked at level 6 (4,096
// pieces a triangle; the rest's at 4) since round 20 (PT-110's M7; run 20261011-055446, the world running: the forest 82.8
// -> 75.5 ms, the town 64.5 -> 62.2, the frozen pictures the same, +130 MB in the forest and +156 in the town). FOR
// MEASURING, from the hidden "Speed trial" (tracer.cpp): 128 bakes them at level 4 as before, 256 at level 5. A mesh baked
// at another level than the frame asks is baked again.
#define TGPT_OMM_TRIAL_LEAVES_4 128u
#define TGPT_OMM_TRIAL_LEAVES_5 256u
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
#define TGPT_OPTION_NO_CLAMPS 4096u        // nothing since PT-50 took the radiance clamps and the 25 cm floor out (the bit stays taken)
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
// (Option bit 4194304 was HDRP's shading shares until they were retired; nothing set it since.)
// ON TRIAL (PT-81, docs/agents/water-9.md; the plugin's hidden "Caustics light keeps its direction"; off: the picture as
// before): under water the sun's and the lamps' light, which is the caustics' there, comes from the direction it came from:
// a surface answers it by the cosine with its own normal (its normal map shows) and with its highlight, as it answers a
// straight light (pt_common caustic_lit). Off, that light is diffuse alone, by the landed triangle's own plane.
#define TGPT_OPTION_CAUSTICS_LIT 4194304u
// For the capture's comparison of what boils (the F8 capture; no row sets them): the bounced light is the pixel's own rays'
// mean alone (ReSTIR GI's reuse, which shares one sample between neighbours and frames, left out of the image), and the
// direct light goes without ReSTIR DI's temporal and spatial reuse (each pixel's own light samples alone).
#define TGPT_OPTION_GI_OWN_RAYS 524288u
#define TGPT_OPTION_DI_NO_REUSE 1048576u
// (8388608u: not taken since 2026-10-11. Fog Adaptation's until that morning, then the hidden "NRD keeps detail (trial)":
// RELAX with 3 a-trous passes and a luminance edge of 1, which changed nothing the harness could see, run 20261011-141211.)
// No caustics: light through glass is tinted along a straight line (a closed glass object's colour, no focusing), as before,
// and the water's caustics (TGPT_SetWaterCaustics) are left out.
#define TGPT_OPTION_NO_CAUSTICS 16777216u
// For testing (PT-04, the hidden "Smooth round surfaces" off): round things shaded by their flat faces as before. Glass and
// props the tracer draws from its own mesh by its triangles' normals, not the mesh's smooth ones; the direct light's fade
// towards each face's terminator on every surface, not on skin alone; the built-in denoiser stopping at every face's edge;
// a translucent pixel's shadow rays starting on the light's side of every face turned from the light.
#define TGPT_OPTION_FACETED 33554432u
// The route before PT-59: each pixel's surface from HDRP's G-buffer (the tracer's own camera rays for hair alone). Without it
// every pixel's surface is what its own camera ray finds in the traced scene; HDRP's G-buffer is only compared with it.
#define TGPT_OPTION_GBUFFER_SURFACES 2147483648u
// For testing (PT-46): rays read the atlas tiles as before even where a material names the game's own textures (no normal
// maps, no mask map's occlusion, hair and eyes from their baked tiles).
#define TGPT_OPTION_ATLAS_TEXTURES 67108864u
// For testing (PT-46, mirrors not sharp; the hidden "Reflections read the top mip"): what a reflection ray meets is read at
// its texture's full size (mip 0), whatever the ray's width: against the reflection's own ray cone, it shows whether the
// mip a reflection reads is what keeps a mirror soft.
#define TGPT_OPTION_REFLECTION_TOP_MIP 134217728u
// For testing (PT-40, PT-43): no walk inside subsurface materials (wax, skin): they are lit as plain surfaces, where the light
// falls on them, on screen and where paths meet them. For comparing the two; HDRP's screen-space scattering is gone either way.
#define TGPT_OPTION_NO_SUBSURFACE 268435456u
// For testing (PT-52): the ground stays dry where HDRP's waves have been (no wet sand or rock: pt_wet.hlsl's store is not
// kept and nothing reads it), as before.
#define TGPT_OPTION_DRY_GROUND 536870912u
// (PT-92, docs/agents/char-normals.md: the normal NRD is handed as the guide of a character's pixel is its mesh's smooth
// normal there wherever the character brought its normals, TGPT_SetSkinned; no option bit: it was 4194304 while on trial.)
// ON TRIAL (PT-81, docs/agents/water-9.md; the plugin's hidden "Caustics land split light"; off: the picture as before): a
// triangle of the water's caustics whose three corners' rays met different surfaces (a small thing, an edge, a round stone)
// is not left out with its light. Each corner's third of its light lands on the plane of what that corner met
// (pt_caustics.hlsl emit_part): what is written is what the triangles carry.
#define TGPT_OPTION_CAUSTICS_SPLIT 1073741824u
// PT-114, ON TRIAL (docs/agents/caustgrid.md; the plugin's hidden "Caustics grid trial", 0 to 3; bits of TGPT_Frame.flags,
// 0 as every caller left them: everything as before). The caustics' stage (pt_caustics.hlsl) cost 4 to 7 ms of every
// outdoor frame, most of it where none of its light landed; measured, the cost was the lights' pick, in which the first
// of a tile's 256 threads joined their shares alone, twice, while 255 waited. Three steps, each leaving what is written
// the same, triangle for triangle:
// bit 19 (the setting's 1): the tiles the pass would run are listed on the CPU (a slot with water, not in the finer
//   level's square, on the slot's extent), a group runs for each listed tile where one ran for every tile of every level
//   and slot, and THE LIGHTS' PICK HAS A PASS OF ITS OWN, a thread a tile, which does the same sums and draws in the same
//   order: the same lights, the same numbers.
// bit 25 with it (the setting's 2): only the tiles with a triangle of their slot's level water under them are listed
//   (where there is none the tile's rays straight down meet no water, so it has no corner and writes nothing: a river's
//   extent is a rectangle round its meshes, mostly dry land), and a corner that only cells of the finer level's square
//   share does nothing (no triangle is made from it).
// bit 26 with both (the setting's 3): a light of a tile whose rays on from every corner cannot land in the camera's
//   view (where alone a landed triangle is written) is not traced there, shadow rays and all.
#define TGPT_FRAME_CAUSTICS_GRID (1u << 19)
#define TGPT_FRAME_CAUSTICS_GRID_WATER (1u << 25)
#define TGPT_FRAME_CAUSTICS_GRID_VIEW (1u << 26)
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
// (5 was Fog Adaptation's event, TGPT_SetFog, until 2026-10-11.)
#define TGPT_EVENT_MARK 8          // data: Unity's frame count times 128 plus a mark (0 the frame's Update, 1 HDRP's rendering begun,
                                   // as the context's first command, 2 the frame's LateUpdate, 3 HDRP's rendering begun, queued at
                                   // once; measure-waits only: 4 to 8 HDRP's injection points BeforeRendering,
                                   // AfterOpaqueDepthAndNormal, BeforeTransparent, BeforePostProcess, AfterPostProcess; 16 and up
                                   // after a step of Unity's frame): the render thread's clock when it reaches that point of the
                                   // frame, and the game's device's (PT-107, PT-112)

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
// Material bit 32 is retired (HDRP shading shares).
// A dielectric interface, for every radiance ray. IOR/absorption and the material
// finish control transport. Shadow connections still use the caustic/tint path.
// Requires ALPHA_TEST so traversal sees each interface.
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
// Glass HDRP draws as a transparent surface (a jar, a window, a potion; with TGPT_MATERIAL_GLASS and ALPHA_TEST, cutoff 0):
// the tracer draws it while tracing (HDRP's own passes for it are hidden), in front of HDRP's surface where a camera ray
// that meets such glass alone finds it (pt_prepare), seen through and refracted, tinted by its color as far as its alpha
// covers. Its instances carry their own mask bit for that ray.
#define TGPT_MATERIAL_SEE_THROUGH 2048u
// Hair strands (with TGPT_MATERIAL_ALPHA_TEST, and TGPT_MATERIAL_ALPHA_TILE for their coverage): a ray other than a camera
// ray stops at a texel by chance, as much of it as the strands cover, so a layer of hair lets part of the light through
// between its strands. The cutoff is not used. Such a mesh gets no micromaps.
#define TGPT_MATERIAL_STRANDS 4096u
// Moved by the wind as HDRP draws it (its shader bends the vertices: grass, leaves, a banner, laundry), where the traced
// mesh stands still: near a surface HDRP drew, the two need not agree, so rays leaving such a surface pass an instance whose
// every material has it within 20 cm (a ray from a blade met its own still copy beside it, and the ground the drawn grass
// swayed away from lay in the still grass's shadow: black spots swaying with the grass).
#define TGPT_MATERIAL_SWAYS 8192u
// An eye (with TGPT_MATERIAL_CAMERA_ONLY and ALPHA_TEST, cutoff 0): traced by camera rays only and shaded by the tracer as a
// wet surface, its normal HDRP's own where HDRP drew it (the traced mesh has only its faces'). Camera-only surfaces without
// it (HDRP's Unlit decals) reflect nothing.
#define TGPT_MATERIAL_EYE 16384u
// Hair with its highlights (with TGPT_MATERIAL_STRANDS): HDRP's Kajiya-Kay hair from the material's hair fields and its
// hairLobeTile, where camera rays find it; without it, strands reflect as any surface does.
#define TGPT_MATERIAL_HAIR_LOBES 32768u
// Hair drawn by the tracer as a layer over HDRP's surface, as the game draws it up close (with TGPT_MATERIAL_STRANDS; the
// plugin hides its HDRP passes while tracing, so HDRP's depth and GBuffer hold the skin, the scalp or the wall behind it):
// a camera ray of its own finds its cards in front of HDRP's surface, each as much of the pixel as the game's alpha test and
// blend let through (its texture's alpha at the texture's own size, remapped by TGPT_Material.hairAlpha; none below
// TGPT_HairColor.alphaClip, the whole pixel from alphaPrepass on), lights them and lays them over the surface's light
// (pt_hair.hlsl). Other camera rays pass through it; the rest meet it as strands.
#define TGPT_MATERIAL_HAIR_LAYER 65536u
// A glowing surface a kept light stands for (a crystal whose light the game placed beside it): as a light's own surface found
// by the light's radius, it lights nothing itself (its light is the light's), and reflections and the camera still see it glow.
#define TGPT_MATERIAL_OWN_SURFACE 131072u
// Set by the tracer, never by the caller: some of the material's shared textures (colorTexture, normalTexture,
// maskTexture) are open and in the tracer's heap this frame, and rays read them at a hit (PT-46).
#define TGPT_MATERIAL_NO_DECALS 33554432u // receiver shader disables DBuffer decals
#define TGPT_MATERIAL_SHARED 262144u
// The hero's first-person body (PT-46: the plugin sets it on the skin of the body the game loads under its first-person rig).
// The game loads no head in first person: that body's mesh has the face and the front of the neck cut out for the camera,
// and what is left of the head (the back of the skull, an open shell facing outwards) HDRP never draws from in front, its
// skin being one-sided. Every ray that shades what it meets passes this material's back faces, at any distance (a mirror
// shows the body without a head, as the game's own view from the mirror's side does), unless the material is drawn
// two-sided (TGPT_MATERIAL_DOUBLE_SIDED); its front faces are met, and shadow rays meet both sides (the hero's shadow keeps
// its head).
#define TGPT_MATERIAL_FIRST_PERSON 1048576u
// Light scatters inside it (HDRP's subsurface scattering: wax, skin, teeth; PT-40): TGPT_Material.subsurface holds its
// diffusion profile's shape. Where the camera's own ray (pt_walk), a path or a reflection meets it, the light it sends on is
// what comes out where a random walk inside the traced mesh leaves it (pt_common subsurface_walk). Set where the game's
// own shader scatters light under the material's surface (its subsurface feature is on: not for naming a diffusion
// profile, which clothes, armour and the ground do too), and taken as often as TGPT_Material.subsurfaceMask says. For
// things with an inside: not with TRANSLUCENT, THIN_CLOTH, STRANDS or GLASS (a sheet has none), and with
// TGPT_MATERIAL_DOUBLE_SIDED only where the piece of the mesh the ray met closes a volume (scene.cpp mark_closed: a
// candle's wax drawn from both sides walks, its wick's card and a sheet do not).
#define TGPT_MATERIAL_SUBSURFACE 2097152u
// TGPT_Material.textureFlags: the mask map's channels (as TGPT_RegisterMaskTexture's: metallic in bits 0-1, smoothness in
// bits 2-3, 0 red to 3 alpha), and whether its green is ambient occlusion (HDRP Lit's: its bounced light only).
#define TGPT_TEXTURE_MASK_OCCLUSION 16u
// PT-64. The normal map's strength as Shader Graph's Normal Strength node applies it (the game's graphs: xy times the
// strength, z toward 1 as far as the strength is under 1); without it HDRP Lit's way (z from the map, then xy times it).
#define TGPT_TEXTURE_GRAPH_NORMAL 32u
// A back face of a two-sided surface turns its whole tangent-space normal around (HDRP's Double-Sided "Flip" mode); without
// it the normal map's bumps keep their side and the surface normal alone turns (its "Mirror" mode, HDRP's default).
#define TGPT_TEXTURE_FLIP_BACK 64u
// The emissive map's red alone scales the emission (the game's grass and leaves), not its color.
#define TGPT_TEXTURE_EMISSIVE_RED 128u
// The mask map's occlusion through the material's own range (HDRP Lit's _AORemapMin and _AORemapMax): bits 12-19 its low
// end, bits 20-27 its high end (0-255 for 0-1), read when TGPT_TEXTURE_OCCLUSION_REMAP is set.
#define TGPT_TEXTURE_OCCLUSION_REMAP 256u
#define TGPT_TEXTURE_OCCLUSION_LOW_SHIFT 12u
#define TGPT_TEXTURE_OCCLUSION_HIGH_SHIFT 20u
// TGPT_Instance.flags
#define TGPT_INSTANCE_ALPHA_TEST 1u   // some material of the instance is alpha-tested: rays test its texture
#define TGPT_INSTANCE_HIDDEN 2u       // kept in the list but not traced
#define TGPT_INSTANCE_CHARACTER 8u    // set by the tracer on the characters it traces (TGPT_SetSkinned), never by the caller
#define TGPT_INSTANCE_STAND_IN 4u     // a coarse stand-in for geometry drawn otherwise (a shadow-only mesh): rays from
                                      // a drawn surface pass through it close by, whichever way it faces
#define TGPT_INSTANCE_SWAYS 32u      // set by the tracer where every material of the instance sways (TGPT_MATERIAL_SWAYS)
// HDRP's water as it lies level (a surface's own mesh, or a quad where HDRP draws one). Bits 8 and 9 name the surface's slot
// in TGPT_SetWaterCaustics (TGPT_INSTANCE_WATER_SLOT). Its materials are never read. The tracer makes the water's surface from
// it: the mesh's triangles cut finer near the camera and moved every frame as the slot's wave data moves them, in the
// acceleration structure, where every ray meets them (reflection, refraction and absorption are the path's). The level mesh
// itself is met by the caustics' own query alone (a ray mask of its own), where they launch their light.
#define TGPT_INSTANCE_WATER 64u
#define TGPT_INSTANCE_WATER_SLOT(flags) (((flags)>>8)&3u)
// TGPT_Light.flags, above its cookie's 16 bits: HDRP shadows this light itself (it renders a shadow map for it), so what HDRP
// shades itself also uses this shadow-map classification.
#define TGPT_LIGHT_HDRP_SHADOWS 0x10000u
// HDRP's light doesn't affect specular (HDAdditionalLightData.affectSpecular off): it makes no highlight, on metal or anything
// glossy, in the game, and none where the tracer shades its light either (its diffuse light as ever).
#define TGPT_LIGHT_NO_SPECULAR 0x20000u
// The light stands where what emits it is (the plugin moved it to its flame's centre): nothing of its prop is around it, so
// its shadow rays are tested up to its own size and a centimetre (pt_common light_own_reach), not the 5 cm past which a light
// inside its lamp's mesh is not blocked by that mesh. A candle's own wax under its flame then shadows the wax's sides.
#define TGPT_LIGHT_AT_EMITTER 0x40000u
// The light falls off with distance alone, as a real light does, with no window at its range (PT-50): its range then says
// only how far the light grid lists it as a near light; past that the far lists sample it (pt_common.hlsli light_lists).
// Without the flag a light keeps HDRP's window at its range (an effect's light, which the game makes a glow on the effect).
#define TGPT_LIGHT_NO_RANGE 0x80000u
// ON TRIAL (TGPT_Frame.jitter's w; docs/agents/flame-specks.md): bits 20-31 of a point or spot light's flags, the radius
// HDRP gave it in millimetres (1-4095; 0: not given), read only by the trial's switches at 1. The light's own radius stays
// its emitter's size.
#define TGPT_LIGHT_HDRP_RADIUS_SHIFT 20u

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
    uint32_t submeshCount, reserved; // reserved bit 0: retain float vertex colours for decal shaders
    const TGPT_Submesh* submeshes;
    // The vertex normals (normalStream 0xFFFFFFFF or normalDimension below 3: none), which the tracer interpolates where the
    // glass's light is bent (caustics): the traced surface is otherwise its triangles'.
    uint32_t normalStream, normalOffset, normalFormat, normalDimension;
    // PT-64: the mesh's fourth set of texture coordinates (Unity's TexCoord3; uv2Stream 0xFFFFFFFF or uv2Dimension below 2:
    // none), which the game's trunk shader lays its bark by (TGPT_PROGRAM_TRUNK).
    uint32_t uv2Stream, uv2Offset, uv2Format, uv2Dimension;
    uint32_t tangentStream, tangentOffset, tangentFormat, tangentDimension; // optional float4 tangent, mesh decals
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
    // Hair's highlights (HDRP's Kajiya-Kay hair, which the game's RealHair is), with TGPT_MATERIAL_HAIR_LOBES: hairLobeTile a
    // TGPT_RegisterHairLobes result (the strands' direction, smoothness and specular map texel by texel), and two lobes around
    // the strand, shifted along the normal by hairShift and hairSecondaryShift, the first as smooth as hairSmoothnessMin to
    // hairSmoothnessMax by its texel, the second hairSecondarySmoothness, tinted by hairSpecularTint times the specular map
    // and each lobe's strength.
    int32_t hairLobeTile;
    float hairShift, hairSecondaryShift, hairSecondarySmoothness;
    float hairSmoothnessMin, hairSmoothnessMax, hairStrength, hairSecondaryStrength;
    float hairSpecularTint[3];
    // With TGPT_MATERIAL_HAIR_LAYER: how RealHair turns its texture's alpha into the hair's, pow(saturate(alpha / _AlphaRemap),
    // _AlphaPower), as two IEEE halves: _AlphaPower in the low 16 bits, _AlphaRemap in the high. A half of 0 counts as 1 (the
    // texture's alpha as it is). Was a reserved float of 0 until 2026-10-06: same size and place.
    uint32_t hairAlpha;
    // PT-46: the game's own textures, read at a hit at the ray's footprint (TGPT_SharedTexture.handle, -1 none): the base
    // color map (in place of albedoTile's), the normal map (Unity's packing, red-green or alpha-green, times normalScale) and
    // the mask map (by textureFlags' channels, through maskRemap's ends, in place of maskTile's). All three at uvScaleOffset,
    // as HDRP Lit reads its main maps. A handle whose texture died or is not in the tracer's heap yet reads as -1, and so
    // does 0 (a zeroed material): the atlas tiles serve then, as before.
    int32_t colorTexture, normalTexture, maskTexture;
    float normalScale;
    uint32_t textureFlags;
    // PT-40, light inside translucent materials (was textureReserved[3], zeros: same size and place). With
    // TGPT_MATERIAL_SUBSURFACE: Burley's shape parameter of the material's diffusion profile per color channel, per metre of
    // the world (1 / its scattering distance: HDRP's shapeParam x 1000 x the profile's world scale), the medium of the walk
    // inside it. With TGPT_MATERIAL_TRANSLUCENT (a leaf, a blade): the share of the light from behind that a sheet of it lets
    // through per channel, HDRP's transmittance of its profile at its thickness times its transmission mask (tint / 4 x
    // (e^-St + 3 e^-St/3) x mask), where the camera's own ray or a path meets it; all 0: not known, and the tracer's grey
    // half serves (pt_common TRANSMISSION).
    float subsurface[3];
    // PT-64: what the game's shader shows of the material at a hit. program: which of the game's shaders computes it
    // (TGPT_PROGRAM_*; 0: HDRP Lit's way, from the fields above alone), programRow: its record's first row in
    // TGPT_SetMaterialPrograms' rows. A program whose textures are not all open reads as 0 (the fields above serve).
    uint32_t program, programRow;
    // The emissive map itself (TGPT_SharedTexture.handle, -1 or 0 none), read at the hit in place of emissiveTile's 64x64 copy.
    int32_t emissiveTexture;
    // Glass (TGPT_MATERIAL_GLASS): the material's own index of refraction (HDRP Lit's _Ior), which its light bends by where
    // the tracer sees through it and where it throws a caustic; 0: the material names none (ordinary glass's 1.5 serves).
    float ior;
    float absorption[3]; // Beer-Lambert extinction, inverse metres
    uint32_t surfaceModel; // 0 lit, 1 unlit (emission only)
    // PT-40, with TGPT_MATERIAL_SUBSURFACE: the game's subsurface mask for the material, 0 to 1 (HDRP Lit's
    // _SubsurfaceMask; what the game's own shader writes for it): the share of the surface's diffuse light that scatters
    // under it. A path vertex or a camera pixel on it takes the walk that often and is a plain surface the rest; 0: never
    // (the game's terrain names a profile and has a mask of 0). A material whose shader computes the mask per point
    // (TGPT_PROGRAM_SKIN) has it from there once its program is open, and this number until then.
    // (Was the first of three reserved words, 0: same size and place. A zeroed material does not walk.)
    float subsurfaceMask;
    uint32_t opticalReserved[2];
};
// TGPT_Material.program: the game's shaders whose GBuffer pass the tracer computes at a hit, each from its compiled code
// (docs/agents/materials.md has what each one does and its record's rows).
#define TGPT_PROGRAM_LIT 0u           // HDRP Lit, and the game's graphs that read as it does (Kandra_Lit)
#define TGPT_PROGRAM_DETAIL 1u        // HDRP Lit with its detail map
#define TGPT_PROGRAM_ARCHITECTURE 2u  // TG/Architecture/KF_Architecture
#define TGPT_PROGRAM_CLIFF 3u         // TG/Environment/Cliffs/Cliff
#define TGPT_PROGRAM_GRASS 4u         // TG/Vegetation/Vegetation_Grass
#define TGPT_PROGRAM_LEAVES 5u        // TG/Vegetation/Vegetation_Leaves
#define TGPT_PROGRAM_TRUNK 6u         // TG/Vegetation/Vegetation_Trunk
#define TGPT_PROGRAM_TERRAIN 7u       // TG/Environment/MeshTerrain
#define TGPT_PROGRAM_TRIPLANAR 8u     // TG/Architecture/TriplanarArchitecture2
#define TGPT_PROGRAM_SKIN 9u          // RealSkin: head/body, region masks, layered normals and tattoos
#define TGPT_PROGRAM_COUNT 10u
// A program's record (TGPT_SetMaterialPrograms): rows of four 32-bit words. Its first row: the program (TGPT_PROGRAM_*), how
// many textures it names, how many rows of values follow them, and a bit for each texture the material has (one it has
// and the tracer has not open yet holds the whole program back). Then its textures' handles, four to a row
// (TGPT_SharedTexture.handle; -1: the material has none, and the shader's own default serves, white, black or flat), then
// its values as floats, four to a row.
#define TGPT_PROGRAM_MAX_TEXTURES 32u
#define TGPT_PROGRAM_MAX_ROWS 65536u
// RealHair's color (the game's TG/Character/RealHair, as its compiled shader computes it), for TGPT_RegisterHairColor. Per
// texel: its diffuse map (at diffuseTilingOffset, the Shader Graph's own tiling and offset) as far as baseColorStrength (from
// white), toward the gradient from rootColor to endColor along the root map's green (inverted as far as invertRootMap), as
// far as rootColorStrength to endColorStrength along it, times globalStrength; two highlight bands where the ID map's green
// lies within one (highlightA/B xyz: its start, peak and end, smoothsteps), as strong as their strength times highlightBlend,
// faded toward the strands' end by w (the root map inverted as far as their overlap invert); then times diffuseStrength, the
// blend map's green (as far as blendStrength), the mask map's green as occlusion (aoStrength, as far as aoOccludeAll), and
// diffuseColor. Colors linear; a map of 0 reads as white. The diffuse map's alpha is also kept at its own size for hair drawn as
// a layer (TGPT_MATERIAL_HAIR_LAYER), with RealHair's alpha test on its remapped alpha (TGPT_Material.hairAlpha): alphaClip
// (_AlphaClip: less is no hair) and alphaPrepass (_DepthPrepass: at least as much covers the pixel and hides the hair behind
// it, as the game's depth prepass and forward pass do; 0: no prepass).
struct TGPT_HairColor {
    uint32_t size, abi;
    uint64_t diffuseMap, rootMap, idMap, maskMap, blendMap; // ID3D11Texture2D*
    float diffuseTilingOffset[4];
    float diffuseColor[4], rootColor[4], endColor[4], highlightAColor[4], highlightBColor[4];
    float highlightA[4], highlightB[4];
    float baseColorStrength, globalStrength, rootColorStrength, endColorStrength;
    float invertRootMap, highlightBlend, highlightAStrength, highlightBStrength;
    float highlightAOverlapInvert, highlightBOverlapInvert, diffuseStrength, blendStrength;
    float aoStrength, aoOccludeAll, alphaClip, alphaPrepass;
};
// RealHair's strands for its highlights, for TGPT_RegisterHairLobes, per texel of its mesh's own coordinates (the game's
// shader samples these three there): the flow map's direction in the mesh's tangent frame, unpacked as the shader unpacks it
// (red times alpha, and green, each to -1..1; green flipped as far as flowFlipGreen); the mask map's alpha to smoothnessPower
// (how far the primary lobe is from hairSmoothnessMin to Max); the specular map's luminance. Missing, the flow runs along -v
// (root to tip, as the game's hair cards lie), the mask is full and the specular map white.
struct TGPT_HairLobes {
    uint32_t size, abi;
    uint64_t flowMap, maskMap, specularMap; // ID3D11Texture2D*
    float smoothnessPower, flowFlipGreen, reserved[2];
};
// The eye's color (the game's TG/Character/Eye/Eye, as its compiled shader computes it), for TGPT_RegisterEyeColor. Per
// texel of the eye's mesh coordinates: its sclera map (tiled by scleraTiling about the centre) times scleraTint; over it by
// the iris map's alpha, the iris map (tiled by irisTiling, magnified toward the centre by pupilSize as the shader's pupil
// does) times irisTint, through the shader's overlay with its limbal ring (the outer ringSize of the iris darkening to the
// power of ringIntensity; 1 elsewhere, which doubles a dark iris as the game's does). emissionIntensity pulls the iris tint
// toward the shader's own blue above 1. Colors linear; a map of 0 is left out. Its fake cubemap reflection is left out: the
// tracer reflects the scene.
struct TGPT_EyeColor {
    uint32_t size, abi;
    uint64_t scleraMap, irisMap;        // ID3D11Texture2D*
    float scleraTint[4], irisTint[4];
    float scleraTiling, irisTiling, pupilSize, ringSize;
    float ringIntensity, emissionIntensity, reserved[2];
};
// PT-63: decals are material changes at a ray's hit, never geometry that hides its receiver.
// Ordered as HDRP draws them (ascending draw order, material set order, projector handle index).
// All texture fields are TGPT_SharedTexture handles; no atlas copies. Bounds and transforms are world-space.
// flags: colour 1, normal 2, metal 4, AO 8, smoothness 16, emission 32; normal/mask opacity from mask blue 64/128;
// affects transparent receivers 256, layer filtering disabled 512, mesh 1024, cull back/front 2048/4096.
// kind selects a decoded shader; reserved[] for a mesh: mesh handle, geometry index, native-only TLAS descriptor.
struct TGPT_Decal {
    float worldToDecal[12], normalToWorld[12];
    float boundsMin[3]; uint32_t flags;
    float boundsMax[3]; uint32_t layerMask;
    float baseColor[4], emissiveBlend[4], uvScaleOffset[4];
    float remapMin[4], remapMax[4]; // metallic, AO, smoothness; w: mask blue scale
    int32_t textures[4]; // base, normal, mask/step, emission (-1 shader default; -2 white, -3 black, -4 flat bump)
    float fade[4]; // fade factor (including distance), angle scale, angle bias, normal strength
    uint32_t kind, reserved[3];
    float parameters[16]; // decoded shader-specific values; zero for HDRP/Decal
    float graph[24][4];                 // widest shipped decal constant block (384 bytes)
    int32_t graphTextures[8];           // shader property bindings; current programs use at most five
    float graphWorld[12];               // actual draw transform, also used by mesh decals
    float graphEnv[4], graphEnv2[4], graphViewZ[4]; // time, exposure, globals, camera view axis
    uint32_t samplers[12]; float samplerBias[12]; // base/normal/mask/emission, then the graph bindings
};
static_assert(sizeof(TGPT_Decal)==928, "decal ABI");
// Replaces the complete list; count zero clears it. No overlap count or draw-distance limit of the tracer's own.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetDecals(const TGPT_Decal* decals, uint32_t count);

struct TGPT_Instance {
    float transform[12];                // object to world, row-major 3x4
    int32_t mesh;                       // TGPT_RegisterMesh / TGPT_RegisterCpuMesh result
    uint32_t materialBase;              // index in the material index list of submesh 0's material
    uint32_t flags;                     // TGPT_INSTANCE_*
    uint32_t mask;                      // ray mask, 0xFF for everything
    uint32_t renderingLayerMask;        // decal receiver layers, as HDRP renders the instance
};
// A thing the game moves, where HDRP draws it in the frame about to be handed over (TGPT_SetMoving; PT-71: the weapon in a
// hand, a door, a carried lantern, a turning windmill): the first 68 bytes as a TGPT_Instance (materialBase into
// TGPT_SetMoving's own material index list), then where it was a frame before, for its own motion (the same as transform
// for a thing that stood still or is new). Traced in that frame's top level as an instance of its mesh: no mesh is rebuilt.
struct TGPT_Moving {
    float transform[12];                // object to world this frame, row-major 3x4
    int32_t mesh;
    uint32_t materialBase;
    uint32_t flags;                     // TGPT_INSTANCE_* (HIDDEN: not traced)
    uint32_t mask;
    uint32_t renderingLayerMask;        // decal receiver layers, preserved beside previous motion
    float previous[12];                 // object to world the frame before, row-major 3x4
};
// A character this frame (the game's GPU-skinned renderer): where its vertices are in the skinning output that
// TGPT_SetSkinned names, and which registered skinned mesh holds its triangles. Its bottom-level acceleration
// structure is rebuilt from those vertices every frame, so it shadows itself and the world as it moves.
#define TGPT_MAX_SKINNED 256u
#define TGPT_SKINNED_MATERIALS 8u
struct TGPT_SkinnedInstance {
    float transform[12];                // skinned space to world, row-major 3x4 (identity when the skinning writes world space)
    int32_t mesh;                       // TGPT_RegisterSkinnedMesh result
    uint32_t firstVertex;               // the character's first vertex in the skinning output
    uint32_t vertexCount;               // its vertices there: the mesh's vertex count
    uint32_t submeshCount;              // up to TGPT_SKINNED_MATERIALS: the registered mesh's submeshes (more are not registered)
    uint32_t materials[TGPT_SKINNED_MATERIALS]; // material per submesh (TGPT_SetMaterials indices)
    uint32_t renderingLayerMask;        // Kandra renderer filtering settings
};
// The wind on a cloth the game sways in its vertex shader (TG/Clothes/Cloth_Wind: a canopy, a banner, laundry on a line), as
// that shader moves it: the tracer moves its traced copy's vertices the same way every frame (pt_sway.hlsl), so the camera's
// pixels, its shadows and its reflections all see the pose HDRP draws. The material's values, read from its properties:
//   uv    = TGPT_WIND_POSITION_UV ? the object-space position's xy : the mesh's first texture coordinates
//   n     = Shader Graph's Simple Noise (deterministic, three octaves) of uv + time x speed at noiseScale
//   push  = uv.y x wind x (direction.x, 0, direction.y), in world space, into the object's space
//   mask  = TGPT_WIND_VERTEX_PAINT ? 2 x the vertex color's rgb : 1
//   moved = position + intensity x n x (position + push) x mask
#define TGPT_MAX_CLOTH_WIND 256u
// The two switches as the compiled shader uses them, by their place in its UnityPerMaterial (the properties' order: the
// 8th float4's x is _UseWorldSpace, its y _UseVertexPaint): their names say the other way round, the game moves as written.
#define TGPT_WIND_VERTEX_PAINT 1u       // the vertex colors mask the sway (the material's _UseWorldSpace)
#define TGPT_WIND_POSITION_UV 2u        // the noise follows the object-space position, not the texture coordinates (_UseVertexPaint)
struct TGPT_ClothWind {
    uint32_t material;                  // TGPT_SetMaterials index
    float speed, wind, noiseScale;      // _Speed, _Wind, _NoiseScale
    float intensity[3]; uint32_t flags; // _Intensity; TGPT_WIND_*
    float direction[2]; float reserved[2]; // _Direction's x and y
};
// The game's wind on its grass and leaves (PT-63), as their vertex shaders move them
// (TG/Vegetation/Vegetation_Grass and Vegetation_Leaves, read from their compiled code: docs/agents/world.md
// 4.1). The tracer keeps every instance with such a material in the pose HDRP draws it in that frame
// (pt_sway.hlsl), in groups of the instances of one cell of the world, each group one BLAS refitted every
// frame; no count or distance leaves one still. grass: gust = max of two readings of the noise map over the
// world, scrolled by speed x time and turned by the direction; moved = position + saturate(saturate(6 y)
// gust) x the object's W2O x ((cos A, 0, sin A) (k^4 - k^2)), k = 1 + 0.0001 speed strength y, A = 450 -
// direction degrees leaves: the position turned about the wind's axis by noise x speed x strength degrees,
// weighed by its height ((y + pivotOffsetY) / bendHeight), fading with the distance along the camera's view
// from fade[0] to fade[1]
#define TGPT_MAX_PLANT_WIND 4096u
#define TGPT_PLANT_GRASS 1u
#define TGPT_PLANT_LEAVES 2u
#define TGPT_PLANT_BEND 1u              // TGPT_PlantWind.flags: leaves' _WindBend is on (off: they stand still)
#define TGPT_NOISE_CLAMP_U 1u           // TGPT_SetWindNoise flags: the map's wrap mode is clamp (else repeat), across and up
#define TGPT_NOISE_CLAMP_V 2u
#define TGPT_NOISE_POINT 4u             // its filter is point (else bilinear)
struct TGPT_PlantWind {
    uint32_t material;                  // TGPT_SetMaterials index
    uint32_t kind;                      // TGPT_PLANT_GRASS or TGPT_PLANT_LEAVES
    uint32_t noise;                     // its noise map's key (TGPT_SetWindNoise); 0: none, read as 1 (Unity's white)
    uint32_t flags;                     // TGPT_PLANT_*
    float strength;                     // grass: _Wind_Strength; leaves: _WindBendStrength
    float bendHeight;                   // leaves: _WindBendHeight
    float pivotOffsetY;                 // leaves: _WindBendPivotOffset's y
    float fade[2];                      // leaves: _WindCameraFade
    float reserved[3];
};
// What every plant's wind shares in a frame: the shader time (Time.time), the game's _VegetationWindSpeed and
// _VegetationWindDirection (degrees), and the camera's forward direction (leaves' fade; its position is the frame's).
struct TGPT_PlantWindFrame {
    float time, speed, direction, reserved0;
    float forward[3]; float reserved1;
};
// The rain's wetness (PT-108, PT-104, ON TRIAL: docs/agents/rain-1.md), as the game's own pass makes it (its
// ScreenSpaceWetness, which rewrites the roughness HDRP lights its G-buffer with and nothing else: no normal, no colour).
// The tracer makes the same roughness at every hit, for every kind of ray (pt_common rain_roughness), from the game's own
// numbers of that frame:
//   noise    the game's noise picture laid over the world, a tile per 10 m of x and z (its red channel)
//   soaked   how smooth a soaked surface is: `soaked` at the camera, fading over 100 m to 0.75 x noise
//   puddle   smoothstep(5 x (1.2 x moisture - noise)), on what faces up (normal.y to the fifth)
//   wet      clamp(clamp(puddle + moisture) - 0.33 x screenNoise)
//   smoothness' = smoothness + wet x (soaked - smoothness)
// Compiled into the passes only with CMake's TGPT_RAIN_WETNESS (OFF: no pass holds a line of it and the record does
// nothing). NOT BUILT YET: the game's shelter (dry under a roof: its top-down height array); every surface is under
// the open sky until it is.
struct TGPT_Rain {
    uint32_t size, abi;
    float moisture;     // the game's moisture (PrecipitationController, 0 to 1) while its wetness pass is on; 0: dry
    float soaked;       // the smoothness of a soaked surface at the camera, the game's shader's own: 0.9 (0.33 with _SNOWMAP)
    float screenNoise;  // the game reads its noise once more over the SCREEN (the screen shows a tenth of it): the mean
                        // of that part, which stands for it in the world (a mirror shows what the camera sees)
    int32_t noise;      // the game's noise texture: its TGPT_RegisterSharedTexture handle, -1 while it has none
    float reserved[10]; // 0
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
    float invViewProj[16];              // HDRP's mainViewConstants.invViewProjMatrix: camera-relative, WITH the frame's sub-pixel
                                        // shift (jitter): a pixel's centre unprojects to the point HDRP drew its depth at, and the
                                        // tracer's camera rays pass through it. Read it while HDRP records its opaque passes: once
                                        // it has recorded its upscaler or TAA, HDCamera holds the matrices without the shift (PT-84)
    float viewProj[16];                 // mainViewConstants.viewProjMatrix, with the same shift
    float prevViewProj[16];             // mainViewConstants.prevViewProjMatrix (without jitter), which takes positions relative
                                        // to this frame's camera: HDRP moves it by the camera's move
    float cameraPosition[4];            // world; w, ON TRIAL (no size or ABI change, 0 as every caller left it), a whole
                                        // number. Its low 4 bits: glass's switches (PT-13, PT-35, docs/agents/glass-3.md;
                                        // pt_common glass_trial): 1 the view through see-through glass crosses the mesh's
                                        // smooth face, 2 its paint only on the faces the game draws, 4 glass has an inside
                                        // where its piece of the mesh closes a volume, 8 light through glass counted twice
                                        // again. Times 16: the water's (PT-42, PT-81, PT-94, docs/agents/water-10.md;
                                        // pt_water_surface water_trials): 1 shore foam, 2 lamps light the water's body,
                                        // 4 its light without the view
    float prevCameraPosition[4];        // world; w, ON TRIAL (no size or ABI change, 0 as every caller left it), a whole
                                        // number that carries two things. Its bits 0 to 2 (PT-95, PT-19,
                                        // docs/agents/nrd-relief.md): the reflections' own filter (pt_atrous, which
                                        // serves with NRD too): 1 two pixels exchange a reflection only as far as their
                                        // normals gather the same light (by the lobe), 2 and only as far as they reflect
                                        // alike, 4 its first pass is not fed back into the history. The rest, divided
                                        // by 8 (PT-99, docs/agents/speed-1.md), a whole number of 16 bits: speed that
                                        // leaves the picture as it is (pt_common speed_trial). 1: a shadow ray asks
                                        // which water it starts in only under the water's highest point. 2, 4: not
                                        // taken. 8, for finding causes: the bounced light's and the reflections' passes
                                        // as plain compute passes (no Shader Execution Reordering). 16 and up: a
                                        // measuring build's switches (CMake's TGPT_SPEED_MEASURE; nothing elsewhere).
                                        // 0: everything as before
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
    // The hold on what is already bright (PT-73 e, pt_common highlight_hold): W, the brightness (radiance times HDRP's
    // exposure) at and above which a pixel takes none of the automatic gain. 0: the built-in (pt_common HIGHLIGHT_KEPT, 1.0); 1e6: no hold, one
    // gain for every pixel as before 2026-10-08. (frameReserved: 0; keeps the record a multiple of 8 bytes.)
    // frameReserved, ON TRIAL (PT-40, docs/agents/flame-near.md; no size or ABI change, 0 as every caller left it): the
    // highlights' shoulder, a whole number: the knee in 64ths in bits 0-11 (0: the hold's W), the most the traced light
    // rolls off towards in 64ths in bits 12-23 (0: off). pt_common highlight_shoulder.
    float highlightKept, frameReserved;
    float maxRayDistance, skyIntensity;
    float indirectColor;  // how much color the bounced light keeps at the same brightness: 1 as traced, 0 grey, up to 4
    uint32_t diHistory;   // ReSTIR DI's temporal history in frames (NVIDIA RTXDI's maxHistoryLength); 0: 20
    uint32_t thickProfiles; // bit i: HDRP's diffusion profile i transmits as a thick object (skin): lit as a solid surface, not through like a leaf
    // For AMD FSR Ray Regeneration: the camera-relative world-to-view matrix (mainViewConstants.viewMatrix),
    // the projection without jitter (nonJitteredProjMatrix, GPU form), and the jitter (HDCamera.taaJitter,
    // render pixels). jitter's z, ON TRIAL (PT-85; no size or ABI change, 0 as every caller left it): 1, the tracer's own
    // histories of the frame before (direct light, reflections, bounced light) are fetched where the pixel's point was in
    // THAT frame's pixel grid, the two frames' shifts' difference added to the motion; 2, ReSTIR's reservoirs as well,
    // for finding causes; 0, all by the motion alone as before.
    // jitter's w, ON TRIAL (PT-85 and PT-40, docs/agents/flame-specks.md; no size or ABI change, 0 as every caller left it):
    // why lamps as small as their flames make specks. A whole number: bits 0-7, A: no surface takes more of a lamp's light
    // than it would this many centimetres from it (0 off; a diagnostic); bits 8-9, B: the ball a reflection ray on glass or
    // water meets a lamp as; bits 10-11, C: the disc its highlight is evaluated over; bits 12-13, D: the disc its shadow ray
    // aims at. B, C, D: 0 the lamp's own radius (as before), 1 HDRP's (TGPT_LIGHT_HDRP_RADIUS_SHIFT), 2 five times its own.
    // Bits 14-21 (docs/agents/flame-near.md): what stands within that many centimetres of a lamp is found on purpose by
    // the rays that gather light (pt_common lamp_around), weighed against the chance hit; 0 off.
    float view[16];
    float nonJitteredProj[16];
    float jitter[4];
    // HDRP's height fog as its Fog volume sets it for the camera, which HDRP lays over the composite: its extinction per
    // metre (1 / its attenuation distance; 0 without fog), its base height (world) and 1 / its scale height (the density
    // falls off as e^-((h - base) / H) above the base); w: the player's Exposure as a factor, the part of tracedGain that
    // isn't automatic (0 means 1).
    float fog[4];
    // x: the share of the game's ambient light HDRP leaves the surfaces it shades itself (hair, eyes), as much of the sky's
    // light as reaches the camera (HdrpPasses), 1 without; the tracer gives them its bounced light for the rest. y: the mip
    // bias of an upscaler (log2 of the render width over the output width, 0 without one), which hair drawn as a layer reads
    // its strands' alpha with, as the game's textures are read. z: HDRP's maximum fog distance in metres (its Fog volume's:
    // where HDRP laid its fog over the sky; read by nothing since 2026-10-11, the traced fog takes fogColor's w). w: the game's _GlobalSaturationAdjustment (PT-64: a global its own shaders add 1 to and scale every surface's
    // color's distance from its grey by; 0 as a rule), which the tracer's programs for those shaders apply as they do.
    float forward[4];
    // HDRP's fog color, for the fog along reflected and refracted rays (PT-37, PT-46: what a ray meets beyond a mirror or
    // through glass fades into the fog as the camera would see it fade from there). rgb: what the fog scatters of the
    // light that reaches it, HDRP's _HeightFogBaseScattering over its extinction (the Fog volume's albedo while its
    // volumetric fog is on, 1 without), times the fog's color (its tint in the Sky colour mode, its constant color
    // otherwise); w: 1 in the Sky colour mode, where the color is that times HDRP's sky in the ray's direction, read at a
    // blurrier mip the nearer the fog (fogMip); 0 for a constant color. All 0: no fog color sent.
    float fogColor[4];
    // HDRP's mip fog (its Fog volume's Mip Fog Near, Far and Max Mip): the sky's mip the fog's color is read at, by the
    // distance, as GetFogColor computes it. w, ON TRIAL (PT-93, docs/agents/grass-boil.md; no size or ABI change, 0 as every
    // caller left it), nothing of the fog's: 1, a light's shadow ray takes the product of what the blades of grass and the
    // leaves in its way let through, in place of a coin at each (pt_common leaf_product_on); 0, the coin as before.
    float fogMip[4];
    // PT-112 step B (round 20, ABI 0x00012700): the fog as a medium for the traced fog (pt_medium.hlsli): rgb what it scatters
    // of the light that reaches it (the Fog volume's albedo, as HDRP's volumetric fog takes it; white without its volumetric
    // fog), w its anisotropy (HDRP's Cornette-Shanks phase function's). All 0: not sent (the medium then takes fogColor's rgb,
    // isotropic).
    float fogMedium[4];
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
    // Reserved counters from the removed separate water passes. Kept at zero for the status ABI.
    uint32_t waterTraced, waterApplied;
    // starting: 1 while the passes are being made, on a thread of their own from the moment the shared device exists (the
    // driver can take minutes after a new version or driver; diagnostics report progress); frames handed over until
    // then go untraced, and the plugin waits for it before it hands HDRP's frames over. startSeconds: how long it took.
    uint32_t starting; float startSeconds;
    // PT-98, what the native side still owes of the traced world (the plugin's notice and the harness's wait count them
    // with the plugin's own queues; all 0 when it owes nothing). Appended on 2026-10-09: the record grew from 216 to 232
    // bytes at its end, TGPT_ABI as it was. TGPT_ReadStatus still fills a record of TGPT_STATUS_SIZE_BEFORE_ARRIVAL bytes
    // (a plugin from before), and the plugin asks an older native side with that size when the whole record is refused.
    uint32_t tilesToBake;       // colour, mask, alpha, hair, eye and cookie tiles asked for and not baked yet
    uint32_t instancesWaiting;  // listed instances left out of the last traced frame: their mesh is not built yet
    uint32_t plantsWaiting;     // plants traced where they stand still until their group's turn to be made (they sway after)
    uint32_t arrivalReserved;   // 0
};
#define TGPT_STATUS_SIZE_BEFORE_ARRIVAL 216u
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGPT_Submesh)==16, "TG Path Tracing submesh ABI");
static_assert(sizeof(TGPT_MeshDesc)==192, "TG Path Tracing mesh ABI");
static_assert(sizeof(TGPT_CpuMeshDesc)==56, "TG Path Tracing CPU mesh ABI");
static_assert(sizeof(TGPT_Material)==204, "TG Path Tracing material ABI");
static_assert(sizeof(TGPT_HairLobes)==48, "TG Path Tracing hair lobes ABI");
static_assert(sizeof(TGPT_Instance)==68, "TG Path Tracing instance ABI");
static_assert(sizeof(TGPT_Moving)==116, "TG Path Tracing moving instance ABI");
static_assert(sizeof(TGPT_Light)==80, "TG Path Tracing light ABI");
static_assert(sizeof(TGPT_Frame)==624, "TG Path Tracing frame ABI");
static_assert(sizeof(TGPT_Status)==232, "TG Path Tracing status ABI");
static_assert(sizeof(TGPT_SkinnedInstance)==100, "TG Path Tracing skinned instance ABI");
static_assert(sizeof(TGPT_ClothWind)==48, "TG Path Tracing cloth wind ABI");
static_assert(sizeof(TGPT_PlantWind)==48 && sizeof(TGPT_PlantWindFrame)==32, "TG Path Tracing plant wind ABI");
static_assert(sizeof(TGPT_Rain)==64, "TG Path Tracing rain ABI");
#endif

TGPT_EXPORT uint32_t TGPT_CALL TGPT_GetAbiVersion();
// Allocated resource bytes (shared textures counted once), sampled on the render thread.
// Driver-private heaps and opaque SDK allocations are not available through this API.
struct TGPT_Diagnostics {
    uint32_t size, abi, passesDone, passesTotal, cacheHits, reserved;
    uint64_t meshGeometry, acceleration, scratch, plants, cloth, skinned, records, sharedTextures, atlases, targets, histories, caustics, wetStore, yardstick, radianceCache, transfer, denoiser, retired, total;
};
static_assert(sizeof(TGPT_Diagnostics)==176, "diagnostic ABI");
TGPT_EXPORT int32_t TGPT_CALL TGPT_ReadDiagnostics(TGPT_Diagnostics* diagnostics);

TGPT_EXPORT int32_t TGPT_CALL TGPT_ReadStatus(TGPT_Status* status);
// No new instances may name a released mesh. Existing GPU frames retire before its allocation returns to the pool.
TGPT_EXPORT int32_t TGPT_CALL TGPT_ReleaseMesh(int32_t handle);
// Why path tracing can't run on this GPU (UTF-8), empty while it can or before the device exists.
// Returns the full length.
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetUnsupportedReason(char* buffer, uint32_t size);
// What each stage of a frame costs the GPU, averaged over about the last second (UTF-8, "scene 1.20 ms, prepare 0.31 ms,
// ..."), for the F8 report; empty until measured. Returns the full length.
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetStageTimes(char* buffer, uint32_t size);
// The yardstick's numbers (PT-59): the tracer's own first hit against HDRP's G-buffer over one traced frame, counted about
// twice a second while the tracer's own camera rays run (or TGPT_VIEW_YARDSTICK is shown). where: pixels by where the two
// surfaces are: 0 both sky, 1 the same surface (within 1 cm and 1 %), 2 the tracer's hit nearer, 3 farther, 4 the tracer met
// nothing where HDRP drew, 5 the tracer met a surface where HDRP has sky. Of the pixels on the same surface: those whose
// normal, colour or roughness differ (more than 25 degrees, 0.1 in a channel, 0.2), and those HDRP drew forward (nothing to
// compare). motion: pixels whose own motion is more than a pixel from HDRP's motion vector. kinds: the same by the kind
// of instance hit (0 still, 1 a character, 2 swaying in the wind, 3 moved by the plugin, 4 a hair card). materials: the
// materials (TGPT_SetMaterials indices) with the most differing pixels, most first, each with how its pixels differ (PT-64):
// place (the tracer's hit nearer, farther or over sky: geometry, not the material), and of those on the same surface another
// colour, normal or roughness (a pixel may count in several). 0 success; frameIndex 0: none counted yet.
#define TGPT_YARDSTICK_MATERIALS 16u
struct TGPT_Yardstick {
    uint32_t size, abi;
    uint32_t frameIndex, pixels;
    uint32_t where[6];
    uint32_t normal, colour, roughness, forward, motion;
    uint32_t kinds[5][6];
    uint32_t materialCount;
    struct { uint32_t material, pixels, differing, place, colour, normal, roughness; } materials[TGPT_YARDSTICK_MATERIALS];
};
#ifdef __cplusplus
static_assert(sizeof(TGPT_Yardstick)==632, "TG Path Tracing yardstick ABI");
#endif
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetYardstick(TGPT_Yardstick* yardstick);
// PT-40, who takes the scattering walk, counted with the yardstick's numbers of the same traced frame (frameIndex; 0: none
// counted yet): onSubsurface, the pixels whose own camera ray stopped on a material light walks inside
// (TGPT_MATERIAL_SUBSURFACE and its profile's shape); walked, those of them whose ray took the walk in that frame (by the
// mask at the hit, and where a material drawn from both sides closes a volume). Any pointer may be null. 0 success.
TGPT_EXPORT int32_t TGPT_CALL TGPT_GetWalkPixels(uint32_t* frameIndex, uint32_t* onSubsurface, uint32_t* walked);
// Scene, main thread. Mesh and texture results are handles (> 0 for meshes, >= 0 tiles for textures),
// negative on refusal. Meshes are read back on the render thread over the following frames.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterMesh(const TGPT_MeshDesc* mesh);
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterCpuMesh(const TGPT_CpuMeshDesc* mesh);
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterTexture(uint64_t key, uint64_t texture);
// A texture the camera sees from the atlas itself, on a surface HDRP's GBuffer doesn't hold (see-through glass:
// TGPT_MATERIAL_SEE_THROUGH), for TGPT_Material.albedoTile: a 512x512 detail tile, filtered bilinearly, where
// TGPT_RegisterTexture's 64x64 tile, point-sampled, showed a jar's stains as blocks. Its number follows the 64x64 tiles'
// (4096 and up), and every ray reads it as it reads those. The tile, or <0: -3 once the detail tiles are taken (128 of them,
// a pool shared with the eyes and hair, which leaves the last 16 to these; TGPT_RegisterTexture's tile serves then).
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterDetailTexture(uint64_t key, uint64_t texture);
// A character's triangles (a CPU mesh: positions may be null, the skinning output replaces them every frame;
// submeshes as index ranges in order). Returns a mesh handle for TGPT_SkinnedInstance.mesh.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterSkinnedMesh(const TGPT_CpuMeshDesc* mesh, const uint32_t* submeshStarts, uint32_t submeshCount);
// This frame's characters, taken by the next TGPT_SubmitFrame: the game's skinning output (ID3D11Buffer*, a
// structured or raw buffer whose vertices begin with a float3 position; stride in bytes) and the characters in it,
// at most TGPT_MAX_SKINNED. A count of 0 traces none.
// stride's bits 16-23, ON TRIAL (PT-92; no size or ABI change, 0 as every caller left it): the byte in a vertex at which
// the skinning left its skinned NORMAL (12 or more, a multiple of 4, inside the stride; 0: none, characters are shaded by
// their flat triangles as before): a unit vector in the positions' space as an octahedral map in one 32-bit word, its two
// coordinates half floats from 0 to 1, x in the low 16 bits, y in the high (the game's Kandra CompressedVertex). The
// characters' hits then read their smooth normal from it (pt_common Hit.rounded), as a mesh's with normals of its own.
// Bits 0-15 are the stride.
#define TGPT_SKINNED_STRIDE_MASK 0xFFFFu
#define TGPT_SKINNED_NORMAL_SHIFT 16u
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSkinned(uint64_t buffer, uint32_t stride, const TGPT_SkinnedInstance* instances, uint32_t count);
// The game's shader time (Time.time, HDRP's _TimeParameters.x) for the next TGPT_SubmitFrame, and the wind of every material
// the game sways as cloth (TGPT_ClothWind, at most TGPT_MAX_CLOTH_WIND): kept until the next call. Instances with such a
// material near the camera are traced in the pose HDRP draws them in that frame; a count of 0 sways none.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetClothWind(float time, const TGPT_ClothWind* winds, uint32_t count);
// The game's wind on its grass and leaves for the next TGPT_SubmitFrame (TGPT_PlantWindFrame) and the wind of every such
// material (TGPT_PlantWind, at most TGPT_MAX_PLANT_WIND): kept until the next call. Every instance with such a material is
// traced in the pose HDRP draws it in that frame; a count of 0 sways none.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetPlantWind(const TGPT_PlantWindFrame* frame, const TGPT_PlantWind* winds, uint32_t count);
// PT-112: the game's local fog volumes (HDRP's LocalVolumetricFog, its "Local Volumetric Fog" boxes: mist patches, a
// blizzard's, rain's), part of the medium the traced fog is (pt_medium.hlsli). Every enabled one in the world, in the order
// HDRP voxelizes them (its priority), whatever its distance or the Fog volume's Depth Extent (HDRP leaves out the far ones
// and the ones past its own count; the tracer leaves out none). One record a box, as HDRP's voxelization takes it
// (ShaderPassVoxelize.hlsl, the default Fog Volume graph, VolumeRendering.hlsl ComputeVolumeFadeFactor): at a point x,
//   box  = ((x - center) . right, (x - center) . up, (x - center) . (right x up)) / extents, inside where every |.| <= 1
//   uvw  = (box * 0.5 + 0.5) * tiling + scroll           mask = the mask texture there (A8: (1, 1, 1, a)), none: 1
//   fade = the faces' (min(1, (1 - n) * rcpPosFaceFade), min(1, n * rcpNegFaceFade), n = box * 0.5 + 0.5, all six
//          multiplied; ^2.2 when exponential; 1 - it when inverted) times min(1, max(0, endTimesRcpDistFadeLen -
//          |x - camera| * rcpDistFadeLen))
//   extinction = mask.a * extinction * fade,   scattering = min(1, mask.rgb * albedo * that extinction), per channel
// added to the height fog's (HDRP's Additive blend: every one of the game's). Positions in the world, metres.
#define TGPT_FOG_BOX_EXPONENTIAL 1u   // the faces fade exponentially (HDRP's falloff mode Exponential)
#define TGPT_FOG_BOX_INVERT 2u        // the fade is inverted (HDRP's invertFade)
#define TGPT_FOG_BOX_ALPHA_ONLY 4u    // the mask is an alpha texture: its alpha alone, white colour (HDRP's _AlphaOnlyTexture)
#define TGPT_FOG_BOX_CLAMP 8u         // the mask clamps at its edges (its wrap mode); else it repeats
#define TGPT_FOG_BOX_POINT 16u        // the mask is read point-filtered (its filter mode); else linearly
#pragma pack(push,4)
struct TGPT_FogBox {
    float center[3]; float extinction;            // 1 / the mean free path (HDRP: at least 0.05 m)
    float right[3]; float rcpDistFadeLen;          // the box's unit axes (HDRP's OrientedBBox); the camera-distance fade
    float up[3]; float endTimesRcpDistFadeLen;
    float extents[3]; uint32_t flags;              // half its size along right, up and right x up; TGPT_FOG_BOX_*
    float albedo[3]; int32_t mask;                 // linear; the mask's TGPT_SharedTexture handle (a 3D texture; -1: none)
    float rcpPosFaceFade[3]; float maskMost;       // maskMost: the most the mask's alpha holds (1 for a unorm texture)
    float rcpNegFaceFade[3]; float reserved0;
    float tiling[3]; float reserved1;
    float scroll[3]; float reserved2;              // the mask's offset now: its scrolling speed times the game's shader time
};
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGPT_FogBox)==144, "TG Path Tracing fog box ABI");
#endif
// The fog boxes for the frames submitted from now on (their mask's scroll moves with time: sent every frame), kept until
// the next call; 0 boxes: none. Records with numbers that are not finite are left out. Main thread.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetFogBoxes(const TGPT_FogBox* boxes, uint32_t count);
// The rain's wetness for the next TGPT_SubmitFrame (TGPT_Rain), kept until the next call; a null pointer or a moisture of 0:
// dry. ON TRIAL (PT-108): the plugin's hidden "Rain wetness" sends it, nothing else does.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetRain(const TGPT_Rain* rain);
// PT-112, the sky (docs/agents/sky-1.md, sky-2.md), ON TRIAL behind the plugin's hidden "Sky trial": the sky the camera and the
// rays see, made by the tracer from what HDRP's Physically Based Sky, its Cloud Layer and the directional lights that interact
// with the sky are for the gameplay camera this frame (its blended volume stack). The model is the published one HDRP 17
// implements (Bruneton and Neyret 2008, Hillaire 2020: air, aerosols and an ozone layer over a sphere, single scattering
// marched, multiple scattering from Hillaire's transfer table), written anew: tracer.cpp makes its tables when the atmosphere
// changes, bakes the clouds when HDRP would (its baking hash), and draws the sky map every frame (pt_sky.hlsl); the camera's
// rays that meet nothing are drawn per pixel (pt_compose, pt_sky.hlsli). Every length in metres, every direction in the world.
#define TGPT_SKY_HDRP 0u          // mode: HDRP's sky as before; nothing else of the record is read
#define TGPT_SKY_MEASURE 1u       // mode: the plugin leaves HDRP's sky passes out; the camera's misses drawn from the map rays read now
#define TGPT_SKY_OWN 2u           // mode: the tracer's own sky (tables, the clouds' bake, the map every frame, the misses per pixel)
#define TGPT_SKY_HDRP_PIXELS 1u   // flags: HDRP draws the sky the camera sees this frame (the game's star layer at night): the
                                  // misses keep HDRP's colour, the rays read the tracer's map
#define TGPT_SKY_CLOUDS 2u        // flags: a cloud layer
#define TGPT_SKY_CLOUDS_UPPER 4u  // flags: its map covers the upper hemisphere only
#define TGPT_SKY_SHOW_MAP 8u      // flags, for tests (the plugin never sets it): the misses show the map the rays read, no discs
#define TGPT_SKY_MAX_BODIES 4u    // the game has two (its Sun and Moon); the plugin says so in its log when more are found
#define TGPT_SKY_BODY_LIGHTS 1u   // a body's flags: it lights the atmosphere (HDRP: its intensity is not 0)
#define TGPT_SKY_BODY_REFLECTS 2u // its disc is lit by a sun (HDRP's shading source Reflect Sun Light or Manual): a moon
#pragma pack(push,4)
struct TGPT_SkyBody {
    float direction[3]; float angularRadius;   // towards it (normalized); half its angular diameter, radians (0: no disc)
    float color[3]; uint32_t flags;            // HDRP's EvaluateLightColor (lux, linear); TGPT_SKY_BODY_*
    float surface[3]; float flareSize;         // its disc: radiance (emissive: color x tint / the disc's solid angle) or, a moon,
                                               // its sun's light x its tint; the flare's size past the disc, radians
    float flare[3]; float flareFalloff;        // the flare's radiance at the disc's edge (HDRP's flareColor, made as surface is)
    float sunTravel[3]; float earthshine;      // a moon: the way its sun's light travels; HDRP's earthshine x 0.01
    float right[3]; int32_t surfaceTexture;    // its surface texture's axes and the texture (TGPT_RegisterSharedTexture handle; -1 none)
    float up[3]; float distance;               // its distance (HDRP's: it stands beyond the atmosphere's top in any case)
};
struct TGPT_SkyLayer {
    float opacities[4];                        // the cloud map's channels' weights (HDRP's opacity R, G, B, A)
    float rotation, steps, sigma, altitude;    // turns (degrees / 360); lighting steps (0: unlit); thickness as HDRP's sigma; height
    float light[3]; int32_t map;               // tint x exposure x HDRP's sun (its color x dimmer); the map (shared texture handle)
    float scroll[2]; float scrolled; uint32_t distortion; // the wind's way (x, z); how far it moved the layer so far; 0 static, 1 procedural, 2 flowmap
    float ambientDimmer; int32_t flowmap; uint32_t on; float reserved; // the flowmap (shared handle; -1 none); 1 when the layer is drawn
};
struct TGPT_Sky {
    uint32_t size, abi, mode, flags;           // TGPT_SKY_HDRP, _MEASURE or _OWN; TGPT_SKY_HDRP_PIXELS, _CLOUDS, _CLOUDS_UPPER
    float planetCenter[3]; float planetRadius; // HDRP's planet (VisualEnvironment)
    float airExtinction[3]; float airScaleHeight;   // per metre at the ground (HDRP's GetAirExtinctionCoefficient); its scale height
    float airScattering[3]; float atmosphereDepth;  // per metre at the ground; from the ground to the atmosphere's top (GetMaximumAltitude)
    float aerosolScattering[3]; float aerosolExtinction;
    float ozoneExtinction[3]; float aerosolScaleHeight;
    float groundAlbedo[3]; float aerosolAnisotropy; // HDRP's ground tint
    float horizonTint[3]; float ozoneStart;    // the ozone layer's lowest height
    float zenithTint[3]; float ozoneWidth;
    float colorSaturation, alphaSaturation, alphaMultiplier, horizonZenithShift;
    float intensity; uint32_t cloudsHash, bodyCount, reserved0; // HDRP's sky intensity (exposure x multiplier); the clouds' baking
                                               // hash (HDRP's CloudLayer.GetBakingHashCode: a new bake when it changes); bodies used
    TGPT_SkyBody bodies[TGPT_SKY_MAX_BODIES];
    float hdrpSun[3]; float cloudOpacity;      // towards HDRP's main light (its shadow caster), which bakes and lights the clouds
    TGPT_SkyLayer layers[2];
    uint32_t cloudWidth, cloudHeight, reserved1[2]; // the baked clouds' size: HDRP's resolution (the height half of it for the upper hemisphere)
};
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGPT_SkyBody)==112 && sizeof(TGPT_SkyLayer)==80 && sizeof(TGPT_Sky)==816, "TG Path Tracing sky ABI");
#endif
// The sky for the frames submitted from now on (TGPT_Sky), kept until the next call; a null pointer: TGPT_SKY_HDRP. Numbers
// that are not finite make the record TGPT_SKY_HDRP. Main thread. 0 success.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSkyModel(const TGPT_Sky* sky);
// A wind's noise map (grass: the red channel of the shader's own gust noise; leaves: the alpha of the material's
// _WindMaskMap), read once from the game's texture at its own size: width x height bytes, the row at v = 0 first.
// key: any number but 0 the plugin names it by (TGPT_PlantWind.noise); a key sent again replaces its map. Kept until
// TGPT_ClearScene. Up to 4096 x 4096.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetWindNoise(uint32_t key, uint32_t width, uint32_t height, uint32_t flags, const uint8_t* texels);
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
// A hair material's color (TGPT_HairColor) baked into a tile of the color atlas, for TGPT_Material.albedoTile with a
// white base color and the tile read at the mesh's own coordinates (uvScaleOffset 1, 1, 0, 0): a 512x512 detail tile (4096
// and up) while the pool has one for hair, else a 64x64 tile, which the key then keeps. Hair materials of the same maps
// and values (a character's clone each) share one detail tile. Called again with the same key (the game colored the hair
// since): the tile it returns holds the new colors, and may be another one than before (a tile it shared stays as it was
// for the others; the caller puts the returned tile into the material). The tile, or <0: -1 a wrong size or ABI, -3 the
// atlas is full.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterHairColor(uint64_t key, const TGPT_HairColor* hair);
// A hair material is gone (its character left, its material was destroyed): its key lets go of its tile, and a detail tile
// no key reads any more goes back to the pool. 1 when a detail tile came free, 0 otherwise. The material must not be drawn
// with that tile again: registered anew, it gets one.
TGPT_EXPORT int32_t TGPT_CALL TGPT_ReleaseHairColor(uint64_t key);
// RealHair's strands (TGPT_HairLobes) into a 64x64 tile of their own, for a hair material's hairLobeTile: its highlights' direction,
// smoothness and specular map, baked over the next frames. Returns the tile, negative on refusal; the same key again bakes it again.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterHairLobes(uint64_t key, const TGPT_HairLobes* lobes);
// An eye's color (TGPT_EyeColor) baked into a 512x512 detail tile (a 64x64 tile once those are taken), for
// TGPT_Material.albedoTile with a white base color at the mesh's own coordinates. The same key again returns its tile
// without baking it again. The tile, or <0: -1 a wrong size or ABI, -3 the atlas is full.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterEyeColor(uint64_t key, const TGPT_EyeColor* eye);
// A light's cookie (HDRP's mask on its intensity by direction): texture is its ID3D11Texture2D*, a cubemap for a point
// light (baked as an octahedral map) or a 2D texture for a spot. Returns its index for TGPT_Light.flags (baked a few
// frames later; until then the light is unmasked), or < 0.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterCookie(uint64_t key, uint64_t texture);
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetMaterials(uint32_t first, const TGPT_Material* materials, uint32_t count);
// PT-64: the records of the materials whose own shader the tracer computes at a hit (TGPT_Material.program, programRow):
// rows of four 32-bit words from row first on, each record as TGPT_PROGRAM_* describes it. The whole list is kept; rows
// past the end are added, rows before it replaced (a texture made anew: its handle written again). count 0 with first 0
// forgets them all. 0 success, -1 past TGPT_PROGRAM_MAX_ROWS.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetMaterialPrograms(uint32_t first, const uint32_t* rows, uint32_t count);
// The whole instance list and the flattened per-instance, per-submesh material indices. Each frame is traced with the list
// that was the newest when it was submitted (TGPT_SubmitFrame), which is what TGPT_SetMoving's replaced indices name.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetScene(const TGPT_Instance* instances, uint32_t count, const uint32_t* materialIndices, uint32_t materialIndexCount);
// The things the game moves, for the next TGPT_SubmitFrame (kept until the next call, as TGPT_SetSkinned's characters): each
// where it is in that frame and where it was a frame before (TGPT_Moving), their submeshes' materials (materialIndices, as
// TGPT_SetScene's), and the TGPT_SetScene instances they stand in for (replaced: indices into that list, which that frame
// does not trace). A count of 0 places nothing and replaces nothing.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetMoving(const TGPT_Moving* things, uint32_t count, const uint32_t* materialIndices, uint32_t materialIndexCount,
                                             const uint32_t* replaced, uint32_t replacedCount);
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetLights(const TGPT_Light* lights, uint32_t count);
// HDRP's sky reflection (a cubemap or cubemap array, ID3D11Resource*); resampled every few frames.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetSky(uint64_t texture);
// Drops the scene: meshes, textures, materials and instances (a new game or a crash recovery).
TGPT_EXPORT int32_t TGPT_CALL TGPT_ClearScene();
// Frame, main thread while HDRP records it: the request render event 1 carries out, then the
// destination render event 2 writes into (HDRP's camera color buffer).
TGPT_EXPORT int32_t TGPT_CALL TGPT_SubmitFrame(const TGPT_Frame* frame);
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetComposite(uint32_t sequence, uint64_t color);
// PT-112 (A), on trial (TGPT_FRAME_OWN_DEPTH): HDRP's depth pyramid (ID3D11Resource*; its first level at the top left), a
// copy of the depth HDRP makes after its opaque passes and its fog and transparents read: render event 2 writes the frame's
// own depth into its first level too. 0 success.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetDepthPyramid(uint32_t sequence, uint64_t pyramid);
TGPT_EXPORT void* TGPT_CALL TGPT_GetRenderEvent();
// PT-107 (R19), logging only: the game's main thread on the CPU's clock (QueryPerformanceCounter values) for the frame with
// this sequence: its Update, HDRP's rendering begun, the capture pass's start and end, HDRP's rendering done, its LateUpdate
// (count 6); with
// Unity's frame count, which render event 8 carries. The native 10-second line puts them against the frame before's trace.
// 0 success.
TGPT_EXPORT int32_t TGPT_CALL TGPT_MainClock(uint32_t sequence, uint32_t frameCount, const int64_t* clocks, uint32_t count);
// (Until PT-71 item 5, TGPT_SetWater and render event 4 filled HDRP's transparent SSR texture with traced reflections for its
// water, and TGPT_SetWaterShade with render event 6 shaded HDRP's water pixels. The water is a surface of the traced scene now.)

// The water (and its caustics: the Caustics row; none with TGPT_OPTION_NO_CAUSTICS): up to TGPT_MAX_WATER of HDRP's water surfaces with their
// real waves, as HDRP's water shader moves its surface (its SampleWaterSurface: each band's displacement, bilinear, and its
// surface gradient, bicubic, at the undisplaced point mapped by the band's scale and offset, times its amplitude and its
// fade with the distance from the camera; the horizontal move times HDRP's choppiness of 2.25). The tracer traces the sun's
// and the lamps' light onto those waves, reflects and refracts it there (index 1.333, Fresnel) and traces it on to what it
// meets; where it lands, its light per area is the caustic (pt_caustics.hlsl). A light's straight shadow ray that meets a
// slot's water is stopped at its surface: under water, the light is the caustic's or a refracted path's. With the row off
// (TGPT_OPTION_NO_CAUSTICS) nothing traces that light, and the ray passes the surface instead, the water taking its share
// over the ray's way through it: the straight light, unbent and without the surface's Fresnel loss, as glass's then.
// The same wave data moves the water's own surface (TGPT_INSTANCE_WATER).
// The wave buffers are copied every traced frame. The call replaces every slot (count 0 clears them all); a slot whose
// displacement is 0 holds still water (its instances still count). 0 success.
// The surface's own smoothness (HDRP's Smoothness of a water surface: its start value near the camera, its end value far
// out, and the range of distances it goes from the one to the other over) is in scattering[3], foamColor[3] and
// smoothnessFade where flags holds TGPT_WATER_SMOOTHNESS: the water mirrors and lets light through as a surface that
// smooth does (GGX facets round the wave's normal: soft glints, a blurred mirror image far out). Without the flag the
// surface is a clear mirror facet (smoothness 1).
#define TGPT_MAX_WATER 4u
#define TGPT_WATER_SMOOTHNESS 1u
struct TGPT_WaterCaustics {
    uint32_t size;             // sizeof(TGPT_WaterCaustics)
    uint32_t abi;              // TGPT_ABI
    uint64_t displacement;     // ID3D11Texture2D* on the game's device: HDRP's displacement buffer (a 2D array, a slice a band, four
                               // float channels: x height, yz the horizontal move before the choppiness), 0 for still water
    uint64_t normals;          // ID3D11Texture2D*: HDRP's additional data buffer (the same size and slices; xy each band's surface
                               // gradient in the surface's space), 0 for still water
    float worldToWater[12];    // row-major 3x4: world to the surface's own space, where HDRP samples its waves (identity for a mesh's)
    float bands[3][4];         // per band: HDRP's _BandN_ScaleOffset_AmplitudeMultiplier (1 / patch size, the offset subtracted
                               // from x and z, the amplitude), as HDRP set it for this frame
    float fades[3][2];         // per band: HDRP's _BandN_Fade (a x the distance + b, saturated, squared, smoothstepped)
    uint32_t bandCount;        // 1 to 3
    float extinction[3];       // HDRP's absorption per metre under this surface (WaterSurface.extinction), rgb
    float top;                 // the highest point of the surface's water instances in the world (y)
    float bottom;              // and their lowest
    float bounds[4];           // the water instances' extent in the world: x min, z min, x max, z max
    float maxDisplacement;     // how far HDRP's waves move the surface at most, metres (its _MaxWaveDisplacement)
    uint32_t flags;            // TGPT_WATER_SMOOTHNESS
    float scattering[4];      // homogeneous medium single-scattering albedo, RGB; [3] the smoothness near the camera (0 to 1)
    float foam[4];             // simulation threshold, intensity, perceptual roughness, texture tiling
    float foamMask[4];         // local xz scale, offset (pre-scaled)
    float foamFlow[4];         // flow direction xz, simulation time, current influence
    int32_t foamMaps[2];       // registered original texture handles: the foam texture, the simulation's foam mask
    float smoothnessFade[2];   // metres from the camera: where the smoothness starts to go from near to far, and over how far
    float foamColor[4];        // the surface's linear foam colour, RGB; [3] the smoothness far from the camera
};
static_assert(sizeof(TGPT_WaterCaustics)==288, "TG Path Tracing water caustics ABI");
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetWaterCaustics(const TGPT_WaterCaustics* surfaces, uint32_t count);
// (Until PT-58 two tables by HDRP's GBuffer index stood here, TGPT_SetDiffusionProfiles and TGPT_SetSkinTransmission: each
// diffusion profile's scattering distances and its thin-object transmission, for pixels whose surface was HDRP's G-buffer.
// The surface is the tracer's own camera ray's hit now, and the material it met says both: TGPT_Material.subsurface, the
// walk's medium with TGPT_MATERIAL_SUBSURFACE and a leaf's own share with TGPT_MATERIAL_TRANSLUCENT.)
// A mirror reflectance per diffusion profile by its GBuffer index (up to 16): HDRP's Fresnel0 of the profile's index of
// refraction (its transmissionTintAndFresnel0's w), which HDRP Lit gives every pixel of its transmission features through that
// profile in place of a dielectric's 0.04 (FillMaterialTransmission). The game's foliage profile, an index of 1.1, mirrors
// 0.0023: at 0.04 every wet blade of grass was a small mirror. Used for the thin profiles' pixels (grass, leaves; skin keeps its
// 0.04). 0 for a profile: a dielectric's 0.04. count 0 clears them all. 0 success.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SetProfileReflectance(const float* fresnel0, uint32_t count);
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
// PT-45: the game's own textures read by the tracer's D3D12 device with no copy (texture_share.cpp). From the game's device
// on, a hook adds the legacy sharing flag (D3D11_RESOURCE_MISC_SHARED) to every texture made only for shaders to read
// (usage DEFAULT or IMMUTABLE, bind flags SHADER_RESOURCE alone, no CPU access, one sample, 64 KB or more with its mips),
// whatever the Mode row says (a texture made before the hook can never be shared); 3D textures too (fog volumes' masks,
// PT-112), in a format the self-test proved as a 3D texture. Given such a texture (ID3D11Texture2D* or ID3D11Texture3D*,
// from Texture.GetNativeTexturePtr), TGPT_RegisterSharedTexture opens it on the D3D12 device,
// writes a view over all its mips and returns its number: the tracer's shaders find it in their descriptor heap at that
// number plus the range's start. The same texture again returns the same number. The game freeing the texture, or
// TGPT_ClearScene, gives the number back once the GPU is done with it. A texture with no number keeps its atlas tile.
// Returns the number (>= 0) or why it has none:
#define TGPT_SHARE_NOT_TEXTURE (-2)  // not an ID3D11Texture2D or ID3D11Texture3D
#define TGPT_SHARE_UNSUPPORTED (-6)  // path tracing can't run on this GPU
#define TGPT_SHARE_FLAGS (-21)       // not made only for shaders to read (a render target, written by shaders or the CPU, ...)
#define TGPT_SHARE_SHAPE (-22)       // an array or a cube map (shared, but the registry takes plain 2D and 3D textures only)
#define TGPT_SHARE_REFUSED (-23)     // the driver refused to share it (its format: the hook made it as the game asked)
#define TGPT_SHARE_BEFORE_HOOK (-24) // made without the flag: before the hook was in (or on another device)
#define TGPT_SHARE_OPEN_FAILED (-25) // D3D12 could not open it
#define TGPT_SHARE_FULL (-26)        // the registry's 16384 numbers are taken
// PT-45 phase 3: the start-up self-test on this card read its format back wrong (or could not run): never shared here
#define TGPT_SHARE_SELF_TEST (-27)
// the game's video memory count is near the budget Windows gives it: not opened now (its atlas tile serves; ask again later)
#define TGPT_SHARE_BUDGET (-28)
#pragma pack(push,4)
struct TGPT_SharedTexture {
    uint32_t size, abi;
    // The texture as D3D11 holds it now (its DXGI format, D3D11 usage and flags). arraySize: a 3D texture's depth.
    uint32_t width, height, mips, arraySize, format, usage, bindFlags, miscFlags;
    uint64_t bytes; // opened allocation, or the requested allocation estimate on refusal
    // The registry: textures open now, how many were opened and how many the game freed since the process started, and
    // the hook (0 not in, 1 in, 2 could not be installed, 3 waiting for its start-up self-test, 4 out: the self-test passed
    // no format on this card or could not run); the open ones' video memory.
    uint32_t open, opened, freed, hook;
    uint64_t openBytes;
    // For diagnostics and tests, while open: the D3D12 texture (ID3D12Resource*, borrowed) and its view (the registry's own
    // CPU descriptor, D3D12_CPU_DESCRIPTOR_HANDLE.ptr, which the tracer copies into its heap).
    uint64_t resource, view;
    // What a material names the texture by (TGPT_Material.colorTexture, ...): its number in the low 14 bits and its
    // registration's serial above, so that a number the registry gave to another texture since never reads that one.
    // -1 without a number.
    int32_t handle;
    // PT-45 phase 3: the start-up self-test, once it has ended (0 before): the formats that passed in bits 0-7, the formats
    // it tested in bits 8-15, its time in milliseconds in bits 16-31.
    uint32_t selfTest;
};
#pragma pack(pop)
#ifdef __cplusplus
static_assert(sizeof(TGPT_SharedTexture)==96, "TG Path Tracing shared texture ABI");
#endif
// info may be null. Main thread or any other; never the render thread's events.
TGPT_EXPORT int32_t TGPT_CALL TGPT_RegisterSharedTexture(uint64_t texture, TGPT_SharedTexture* info);
// PT-45 phase 3: mip streaming made a texture anew (Unity's Texture now has another ID3D11Texture2D*): every hair and eye map
// record baked from oldTexture (or from newTexture before it was registered) reads newTexture from the next frame on, as
// does a bake still waiting for its turn. Register newTexture first (TGPT_RegisterSharedTexture). oldTexture is never
// dereferenced (it may be gone). Main thread. 0, or -1 for a null newTexture.
TGPT_EXPORT int32_t TGPT_CALL TGPT_SwapSharedTexture(uint64_t oldTexture, uint64_t newTexture);
