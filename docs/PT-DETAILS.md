# Path Tracing integration reference

Source map and supporting evidence for [PROJECT-CONTEXT.md](PROJECT-CONTEXT.md) and [PT-STATE.md](PT-STATE.md).
Detailed shader decodes, measured alternatives and historical implementation notes remain in the local
archive under this same path; [DEVELOPMENT.md](DEVELOPMENT.md) describes access.

## Source map

| Concern | Starting points |
|---|---|
| Managed capture and ABI | src/TGPathTracing/SceneCapture.cs, NativeTracer.cs, native/pathtracing/include/tgpt_api.h |
| Game renderer families / LOD | WorldRules.cs and per-family capture classes |
| Materials / original textures | MaterialPrograms.cs, TextureShare.cs, pt_common.hlsli |
| Camera rays | pt_prepare.hlsl, pt_primary.hlsli |
| Light identity and candidates | SceneLights.cs, lamp_tree.hpp, light_grid.hpp |
| Tracing and timed stages | native/pathtracing/src/tracer.cpp |
| ReSTIR | pt_di.hlsl, pt_gi_reuse.hlsl and RTXDI adapters |
| Skin / dielectric walks | pt_walk.hlsli, pt_dielectric.hlsli |
| Water / caustics | scene_water.cpp, pt_water_mesh.hlsl, pt_water_sun.hlsli, pt_caustics.hlsl |
| Fog | pt_medium.hlsli, pt_fog_froxel.hlsl, pt_fog_integrate.hlsl |
| Denoiser / mirrors | nrd_denoiser.cpp, pt_nrd_prep.hlsl, pt_psr_record.hlsli |
| HDRP handoff | GamePasses.cs, depth/motion publication, d3d11.hlsl |

Read actual headers before allocating ABI fields, material/instance bits or frame flags. Older tables retain
retired trials and conflicting meanings. Material bits, instance flags and ray masks are different domains.

## Evidence to recover before extending a design

- Archived psr-2: pure-mirror virtual guides, glass/water options and RR proposal boundaries.
- vendor-rr / review-vendor-astra / audit-4: vendor input/settings audit and outstanding findings.
- world / moving / materials: renderer-family capture, shader properties, wind and motion.
- fog-froxel / fogrule2 / fog-3: fog reference equations, grid design, no-sun compiler issue and fixture corrections.
- megageo-2 through megageo-6: cluster/refit/CPU allocation measurements. megageo-7 itself is missing; use R20 summary.
- caps / stale / fixt: capacity limits, stable record identity and mutation coverage.
- water-4 through water-11 / caustgrid: displaced geometry, transport, stage ordering and merged grid cost.
- inspection-20261008: 123 scene findings, linked by PT issue IDs.

These archived reports are evidence, not current defaults. Read PT-STATE first and verify actual source.
