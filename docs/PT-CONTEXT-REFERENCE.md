# Path tracing context reference

Moved from PROJECT-CONTEXT during PT-71 item 6 to retain its 700-line budget.
AGENTS.md and current headers outrank older values here.

### 9.5 ReSTIR on RTXDI 3.1
- DI: the tracer's light-grid candidates go into RTXDI reservoirs; temporal reuse (history 4 since 2026-09-28, RTX
  Remix's: RTXDI's 20 boiled on a cage's thin bars, F8 change 0.40 at 20, 0.14 at 4, 0.11 without reuse; hidden "Direct
  light history"), Basic spatial reuse, permutation sampling with a new random number every frame. The final
  shadow ray is always traced; spatial results never feed the next frame. Own boiling filter (`pt_boil.hlsli`,
  weighs W x targetPdf, excludes the sample itself once >= 4 others are lit). discountNaiveSamples 0. Previous-frame
  lights + light instance IDs for `RAB_TranslateLightIndex`.
- GI: history 8 + maxReservoirAge 30 (RTXDI's defaults; hidden "Bounced light history"); temporal reuse with
  permutation, a search around the reprojected pixel and the reconnection Jacobian (rejected beyond [1/10, 10],
  clamped to [1/3, 3]); NaN check on unpack; RTXDI's fallback sampling off. With N rays a pixel its own mean is
  blended (N-1)/(N-1+16) (30 % at 8, 48 % at 16).
- Decorrelation for the denoisers (RR guide 3.5, AMD denoising.md 5.1.2): permutation sampling; none on complex
  surfaces (a neighbour's normal more than 35 degrees off: strands, slats).
- Published practice, the honest energy reference (one light 10 cm from a wall: 0.794 as shipped, 1.013 without the GI
  boiling filter), the crevices study and the boiling measures: `docs/PT-DETAILS.md`.

### 9.8 Radiance cache
- Own SHaRC-like design, no SDK code: a world hash grid of 2^20 slots (buckets of 8; keys, sums, light = 36 MB);
  cells 0.1 m within 5 m of the camera, doubling with each doubling of distance; a side per dominant normal axis; it
  stores cosine-mean incoming indirect light (E/pi). A path's last vertex adds throughput x albedo x cache light
  (jittered lookup). Fed by 1/4 of the 8x8 tiles each frame (whole tiles), the pixels' first rays and an extension ray
  at the last vertex; 512 samples a cell a frame, per-channel cap 2000; EMA >= 1/16; ages out after 240 frames.
  Cleared on first use, re-enable and scene clears, never on `TGPT_FRAME_RESET`. Furnace: 0.994 with 1 bounce.

### 9.10 F8 capture, debug views, logs
- F8 (DebugCapture.cs, hold still ~24 s; F8 also triggers TGFG's emergency off and AvalonAtlas) writes
  `BepInEx/TGPathTracing/Captures/<time>/`: report.txt (exposure, lights, the probe, GPU time by stage, the Boiling line,
  the crosshair's pick, the characters along it) and JPGs (final, each debug view, the variants). Everything it holds:
  `docs/PT-DETAILS.md`. JPGs are CPU box downscales of raw bytes.
- Debug views (the last, Yardstick, R6: the own ray's hit against HDRP's G-buffer; colours in `docs/agents/hair-6.md`):
  None, Albedo, Normals, Coverage, Materials, Indirect, Indirect raw, Direct, Surfaces (cyan: forward,
  traced; magenta: HDRP-shaded; orange: solid transmission, i.e. skin), Specular, Emissive (green sampled, blue a
  light's own surface, red rays only).
- Logs: BepInEx's `Path tracing hook 10s:` and `Path tracing:` lines; the native `Path tracing 10s:` line; GPU time by
  stage (9 stages + water, `TGPT_GetStageTimes`). Other logs: README "Diagnostics". The native log's start: `Path tracing
  passes made in N s (the D3D12 ones, the D3D11 ones)` and `Path tracing ready ... in N s; GPU progress markers on`.
- A driver reset (the game freezes; Windows' System log: nvlddmkm 153 "Error occurred on GPUID", 14 "GPU recovery
  action ... PF FLR", "TDR occurred"; 13 lines name an MMU fault when it is one) leaves `The GPU stopped (the driver reset
  it): ... D3D12 device <reason>, D3D11 device <reason>; the GPU finished stage X of frame N, and stopped in stage Y` in
  the native log (`report_gpu_stop`: WriteBufferImmediate markers after each stage into a readback buffer). The first,
  2026-09-28 09:16, came before the markers: its cause is unknown.

### 9.11 The offline harness (`tests/pathtracing_gpu_test.cpp`, ctest `tgpt_gpu_test`)
- The in-game harness (the real game, saves by name, the game's own picture as reference, RR and NRD, frame rates):
  `docs/PT-HARNESS.md`. Built 2026-10-06; no scenario had run to its end in game when that round closed.
- Boots the real bridge and extension, rasterizes a synthetic HDRP-like GBuffer, traces and composites through the same
  render events; PNGs flipped upright in `build/native/tgpt-images`. Motion from unjittered view-projections; the
  previous one x T(cam - prev), as HDRP. `objects[]` only draw the GBuffer; traced-only objects go through SetScene.
- Switches (energy, crevices, adapter, debug layer, GPU-based validation, a second concurrent run): `docs/PT-DETAILS.md`.
  Sky off needs skyIntensity 1e-6 (0 = 1). It waits for TGPT_Status.starting to clear before its first frame (R4).
- Its frame 0 runs without the cache (so A/B runs stay identical); checks that sum darkened pixels run cache-off;
  single-frame errors vary with the seed (average 4 frames). Debug views are a frame late: run each twice. It never runs
  Ray Reconstruction (`tgup_gpu_smoke_with_pt` does). Harness meshes carry normals since R3, so glass cubes get
  caustics: the old tint/refraction tests run with TGPT_OPTION_NO_CAUSTICS. A test object's light path must be clear of
  the traced-only leaves (a caustic under the checkered leaf lost half its light: the bent paths cross other texels).
  A/B pairs meant to differ only by one change run the same frame indices (the water test: 0, 7001-7039): with the
  same seeds, what differs is what the change made (the water alone changed the image 0.00000).

### 9.12 Bits (check before adding one; parallel agents get theirs assigned)
- `TGPT_OPTION_*`: NO_SPATIAL 1, NO_TEMPORAL 2, NO_DENOISER 4, NO_SPECULAR 8, RAY_RECONSTRUCTION 16, NO_MICROMAPS 32,
  RAY_REGENERATION 64, NO_PASS_THROUGH 128, DI_VISIBILITY 256, SPATIAL_FEEDBACK 512, NRD 1024, NO_BOILING 2048,
  NO_CLAMPS 4096, EXACT_JACOBIAN 8192, OFFSET_RECEIVER 16384, NO_GI_BOILING 32768, NO_RADIANCE_CACHE 65536,
  EMISSIVE_LIGHTS 131072, GLOSSY_REFLECTIONS 262144, GI_OWN_RAYS 524288, DI_NO_REUSE 1048576, GI_VISIBILITY 2097152,
  retired 4194304, FOG_ADAPTS 8388608, NO_CAUSTICS 16777216, FACETED 33554432, GBUFFER_SURFACES 2147483648 (R6). Option bits are assigned; ask the coordinator before using any.
- `TGPT_MATERIAL_*`: DOUBLE_SIDED 1, ALPHA_TEST 2, VERTEX_COLOR 4, ALPHA_TILE 8, CAMERA_ONLY 16, retired 32, GLASS
  64, EMISSIVE_ALBEDO 128, TRANSLUCENT 256, THIN_CLOTH 512, MASK_TILE 1024, SEE_THROUGH 2048, STRANDS 4096, SWAYS 8192,
  EYE 16384, HAIR_LOBES 32768 (R4), HAIR_LAYER 65536 (R4). Consult the current header before assigning bits. Instance masks: sways 0x02, see-through 0x04, water 0x08 (R3), still 0xF1; ray masks
  MASK_ALL 0xF7, MASK_STILL 0xF5, MASK_WATER 0x08 (a source check bans any other in a ray query). Render events: 1
  trace, 2 composite, 3 retired, 4 water, 5 fog (R3). Static samplers: s0 linear clamp, s1 linear wrap U/W clamp
  V (the sky), s2 point clamp, s3 linear wrap (R3, the water's pattern).
  Atlas tiles: 64x64 0-4095, detail 512x512 4096-4223 (128, 1 MB each, a pool: hair of the same maps and values shares one,
  hair gives its back when its material is gone (`TGPT_ReleaseHairColor`; the plugin: destroyed or not traced for 600 frames)
  or colored anew, and hair leaves the last 16 to see-through glass and the eyes; `runtime.hpp`, `take_detail_tile`); color tile keys: textures (instance ids), mask maps
  bit 63, hair colors bit 62, eyes bits 61-62, hair lobes bit 61 alone. pt_prepare's mat.y flags: 1 complex, 2 glass,
  4 thin cloth or hair, 8 eye, 16 thin skin, 32 retired, 64 hair with its highlights (R4), 128 a leaf (R6: its
  reflectance in red, its ambient and specular occlusion in green and blue); none free. Skin is the color byte's alpha
  0.25 (R6; forward is 1). `TGPT_Frame.forward`: x HDRP's ambient kept, y the mip bias, z the max fog distance (R6),
  w free.
- `TGPT_INSTANCE_*`: ALPHA_TEST 1, HIDDEN 2, STAND_IN 4, CHARACTER 8 (tracer-set), MOVING 16, SWAYS 32 (tracer-set),
  WATER 64 (R3; its TGPT_SetWaterCaustics slot in bits 8-9).
  `TGPT_LIGHT_*` above the cookie's 16 bits: HDRP_SHADOWS 0x10000, NO_SPECULAR 0x20000. Frame flags: RESET 1, GUIDES 2.


### 9.4 Exposure and the traced gain
- HDRP keeps the game's exposure (no Exposure override). Only the traced light is brightened: `pt_meter.hlsl`
  (histogram of every other traced pixel, centre-weighted, log2 -16..+8, 64 bins, before gain) -> p50/p90/p98 ->
  `HdrpPasses.UpdateTracedGain` -> `frame.tracedGain` -> compose multiplies.
- Rules now (`HdrpPasses.AutomaticGain`): brighten = max(0, min((mid tone -3.06 - p50) x Dark Adaptation, 0.5 - p98)),
  so the automatic gain never darkens and the brightest fiftieth stays under ~1.4x white; at most (game EV - darkest
  scene EV 7) stops, and 12; 1.5 EV/s up, 3 EV/s down; frozen in debug views and during F8; the Exposure row adds +-3.
- The "seen" meter: a 128x72 copy of HDRP's image before post, read back every 4th frame; Dark Adaptation's mid =
  max(traced p50, seen p50 - gain), so fog counts as bright.
- Fog: `traced_gain_at` fades the AUTOMATIC gain by HDRP height-fog transmittance (H = layer x 0.144765) to 1, with Fog
  Adaptation to the fog's gain; Exposure stays. Fog Adaptation lifts the fog at most to the traced light it veils (PT-48:
  event 5's median stops of fog over traced light; `fog_automatic`, FrameConstants `textures.z`; `docs/agents/lake.md`).
- Radiance reference: signals are stored relative to max(sun lux, brightest light at 1 m)/pi, eased (FP16 then holds
  HDRP's physical units); the indoor clamp uses the brightest light at 25 cm. Clamps vs the reference
  (`pt_restir.hlsli`): LIMIT_INDIRECT 4, LIMIT_REFLECTION 8, LIMIT_DIRECT 48.
- Camera light (`pt_probe.hlsl`): 256 sky-only rays from the camera against the open sky -> `cameraLight`, eased
  0.5 s; it scales HDRP's forward ambient, `reflectionLightingMultiplier` and `Fog.globalLightProbeDimmer`. Interiors
  (no sun, or an Interior* scene): sky 1e-6 and cameraLight 0.01, so no sky leaks through wall seams.
- PT-71 item 6 removes Shadow Detail's image curve, row and render event 3, plus the separate
  HDRP shading-share pass. Read exposure/gain after integration; no appearance verdict yet.
- RR's pre-exposure is HDRP's value clamped 0.2-2, as HDRP's DLSS pass does (the real ~1e-4 would overflow RR's FP16
  history). RR guide 3.7: exposure inputs are unsupported.

