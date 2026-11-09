# TG Path Tracing in detail

The recipes, decodes and measurements behind `docs/PROJECT-CONTEXT.md` section 9 (read that first; it keeps the short
version and the DEAD ENDS). The game's shader code is read to learn its math and never kept in the repository; HDRP's and
Cycles' sources are read only.

Contents: Surfaces (9.7): a game shader's math, hair, eyes, see-through glass, caustics through glass and under water,
sunlight reflected off water. ReSTIR (9.5): research and measurements. The harness (9.11): its switches.

## Reading a game shader's math

- Reading a game shader's math (R3): Shader Graphs sit in `resources.assets` (find "TG/Character/..."; after the
  fallback name "Hidden/Shader Graph/FallbackError": platforms (1: d3d11 = 4), offsets, compressed and decompressed
  lengths, then the blob; LZ4 block chunks, `lz4.block.decompress`). DXBC containers inside, no RDEF: disassemble with
  d3dcompiler_47's `D3DDisassemble` (ctypes); UnityPerMaterial is the small CB (CB3), laid out in property order with
  HLSL packing (a Vector3 lets the next float into its w); textures in property order from t18. Tools:
  `tools/research/unity_shader_blob.py <name> <dir>`, then `tools/research/dxbc_split.py` in that dir.

## Hair

### Its color (R3; 512x512 in R4)

- Hair is the tracer's: RealHair's color from its compiled shader, baked into a 512x512 detail tile that materials of
  the same maps and values share, while the pool of 128 has one for hair, else 64x64 (`TGPT_RegisterHairColor`, d3d11 `TGPT_HAIR`): base = lerp(1, diffuse map, _BaseColorStrength); toward
  lerp(_RootColor, _EndColor, m) by lerp(_RootColorStrength, _EndColorStrength, m) x _GlobalStrength, m = root map
  green (inverted by _InvertRootMap); highlight A/B = tent(ID map green; distribution x, y, z: smoothstep up to y,
  down to z) x strength x (1 - overlapEnd x lerp(root, 1 - root, overlapInvert)), saturate(x _HighlightBlend), lerp to
  their colors; x _DiffuseStrength; lerp(c, c x blend map green, _BlendStrength); AO: lerp(c, c x lerp(1, mask green,
  _AOStrength), _AOOccludeAll); x _DiffuseColor. Mask map alpha is smoothness: lerp(min, max, pow(a, power)) (the highlights' lobe tile
  holds it per texel, R4). The bartender: _DiffuseColor 0.70 grey, auburn root/end: the old
  _DiffuseColor-only color was grey. Recolors (CharacterHairColor.ApplyTo, compared per frame) re-bake the tile.
  Camera rays stop at MATERIAL_STRANDS whatever the alpha; lit through from behind; no Reflection Boost.

### Its highlights (R4)

- The game's RealHair is HDRP's Hair master stack, Kajiya-Kay (its compiled forward pass decoded, CB3 and t18-t26):
  strand = normalize(fx T + fy' B + fz N) in the mesh's tangent frame, the flow unpacked as a normal map (x = R x A,
  y = G, each 2v - 1, z = sqrt(1 - min(x^2 + y^2, 1)); fy' = fy + _FlowMapFlipGreen x (1 - 2 fy), on the unpacked value);
  shift1 = lerp(_SpecularShiftMin, _SpecularShiftMax, ID map), shift2 = shift1 + _SecondarySpecularShift; primary
  smoothness lerp(_SmoothnessMin, _SmoothnessMax, pow(saturate(mask alpha), _SmoothnessPower)), secondary
  _SecondarySmoothness; tint1 = specular map x _SpecularTint x _SpecularMultiplier, tint2 the same x
  _SecondarySpecularMultiplier. The flow, mask, specular and ID maps are read at the mesh's own coordinates (only the
  diffuse map at its tiling).
- HDRP's Kajiya-Kay (Hair.hlsl, read only): t = normalize(T + N x shift); D = saturate(t.H + 1) x (n + 2) / 2 pi x
  (1 - (t.H)^2)^(n/2); n = clamp(2 / r^2 - 2) with r = (1 - smoothness)^2 (the primary's smoothness at most 0.99);
  specR = 0.25 x F_Schlick(0.0465, L.H) x (tint1 D1 + tint2 D2) x N.L, none where the geometric normal faces away. Its
  environment: GGX's FGD at 0.0465 with the second lobe's perceptual roughness x 0.4, times tint2 (the game's hair
  reflects almost nothing: 0.075 x 0.47 x 0.05). The rim transmission (specT) is left out: hair keeps the tracer's
  thin-cloth light from behind.
- The game's hair (FirstPass_Mat_H_m001, Mat_Beard_Long_FirstPass, from the asset bundles on 2026-09-28): the flow maps
  lie along -v within 3 degrees (the beard's within 10), the mask alpha is nearly constant (0.36: smoothness about
  0.76), the ID map's shift range is 0.05 (the tracer takes its middle), the specular map is the diffuse map (luminance
  0.47); multipliers 0.075 (hair), 0.15 (beard), 0.5 (brows); secondary smoothness 0.8 (0 on brows).
- In the tracer: `TGPT_MATERIAL_HAIR_LOBES` 32768 with 48 bytes of hair fields on TGPT_Material (shifts, smoothness
  ends, strengths, tint) and a 64x64 lobe tile (`TGPT_RegisterHairLobes`, d3d11 `TGPT_HAIR_LOBES`: RG the flow as 0.5 +
  0.5 x, B pow(mask alpha, power), A the specular map's luminance; read raw by `hair_lobe`). pt_prepare builds the strand
  from the hit triangle's dP/du and dP/dv (`uv_frame`: T along u, B = cross(N, T) signed toward dP/dv) and writes it with
  the material index into the hair texture (R32G32_UINT, `FrameConstants.hair`); mat.y's RGB holds (specular luminance,
  smoothness share) with bit 64; the roughness byte is the second lobe's roughness x 0.4, f0 0.0465. load_surface reads
  the hair texture for this frame's surface only (a previous frame's gets the f0 alone). `hair_specular` shades the sun
  (pt_di_spatial) and every RTXDI sample (rtxdi_shade); pt_specular tints the reflections by tint2. The exponent stops at
  2000 (HAIR_MAX_EXPONENT: HDRP's 2e8 at smoothness 0.99 is a line rays never meet; wider, the normalization keeps its
  light). The recolor's _SpecularTint is followed (SceneCapture HairState).
- Harness: a sunlit quad of hair turned 25 degrees off the mirror angle about one axis: its strands along that axis give
  0.01787 against HDRP's formula's 0.01788, the other way 18.8x fainter (a GGX lobe: 1.0x; the frame swapped: reversed).
  Not yet: the ID map's per-texel shift, the rim transmission, a specular albedo guide for Ray Reconstruction on hair.

### Held opaque while tracing (R3; cut at 0.5 in R4)

- Hair, eyebrows and lashes are held opaque while tracing (`CharacterCapture.HoldOpaque`): a Harmony prefix on
  `HairController.Refresh(transparent, blendshapes)` forces false and keeps what the game asked (restored on stop);
  eyebrows/lashes (names Brow/Lash, transparent, _AlphaCutoffEnable 1) set _SurfaceType 0 + HDMaterial.ValidateMaterial
  + KandraRenderer.MaterialsTransparencyChanged. Transparent band: BodyFeatures.RefreshDistanceBand, band < 2.
- Held opaque, HDRP cuts them by their opaque clip: RealHair's `_AlphaClip` (its depth pass: alpha = pow(saturate(a /
  _AlphaRemap), _AlphaPower), 1 at or above _DepthPrepass, kept where at least _AlphaClip), Kandra_Lit's `_AlphaCutoff`.
  The game's values are blending cuts (0.1 hair and brows, 0.067 lashes, 0.05 scalp masks) and turned every soft fringe
  solid: LEGO hair, black brow and lash bars, a black hairline (a scalp mask's faint edge is near black; 2026-09-28).
  The game's Refresh sets `_SurfaceType` on every rendering material, the scalp mask's too. While held the cuts are
  raised to 0.5 (`CharacterCapture.RaiseCut`: a postfix on Refresh, and in HoldLayersOpaque) and given back on stop: a
  brow keeps 27 % of its soft area (78 % at 0.1), the lashes 48 % (89 %), a head of hair 73 % (97 %); the game's own hair
  turns solid for its depth prepass at _DepthPrepass 0.5.

## Eyes (R3 decode, PT-71 camera surfaces in R6)

- Eyes are the tracer's: TG/Character/Eye/Eye decoded: sclera = map((uv-0.5) x _Sclera_Tiling + 0.5) x tint; iris uv =
  lerp((uv-0.5) x IT + 0.5, (uv-0.5) x (IT + 8 _Pupil_Size - 6) + 0.5, 1 - min(8|uv-0.5|, 1)); base = lerp(sclera, iris
  x tint', iris alpha) (tint' toward (0.0935, 0.118, 0.170) by saturate((_Iris_Emission_Intensity - 1) / 9)); limbal
  ring r = |2(irisUv - 0.5)|: ring = (0.1 + 0.9 max(1 - r, 0) / size)^intensity where r > 1 - size, else 1; overlay
  (b <= 0.5 ? 2 b ring : 1 - 2(1 - b)(1 - ring)) by iris alpha. One shared sampler: CLAMP (a repeating iris tiled the
  sclera: harness). Left out: its screen-blended fake cubemap, the iris glow e^(emission - 3), SSS. Baked per kind of
  eye (key from its values) into a 512x512 detail tile (`TGPT_RegisterEyeColor`); this copy is
  still a materials-migration gap. `TGPT_MATERIAL_EYE` now stops all rays. Its material's
  `_Sclera_Smoothness` replaces the fixed 0.9. Cornea is classified as clear glass, uses its own
  IOR where present (fallback 1.376), and is traced above the iris. HDRP's transparent draw is
  suppressed to avoid drawing it twice. Skinned smooth normals remain a moving-geometry gap.

## Glass camera surfaces (PT-71, R6)

- One `pt_dielectric.hlsli` walk is shared by camera, reflection and bounce paths. It uses
  exact dielectric Fresnel, GGX visible normals, Snell refraction/TIR and absorption over
  each real segment. No pane count or guessed wall thickness; compensated roulette terminates
  paths without truncating their expected light. Medium lookup uses closed geometry without
  a fixed stack. Nested closed solids are supported; arbitrary overlapping/open meshes need
  material/volume authoring rules. Material IOR and absorption use `_Ior`, `_TransmittanceColor`
  and `_AbsorptionDistance`; missing values are logged as fallbacks. Missing absorption stays
  clear until the material decode supplies it; no per-interface colour filter stands in for it.
- The first glass interface supplies depth, motion, normals, albedo and its material roughness
  to the traced frame/denoiser inputs. Glass's radiance is entirely specular. The old glass-only
  camera prepass, 64-sheet walk, 3 cm guessed walls, 1 m exit search, 15 m eligibility, forced
  roughness and HDRP blending are removed. `GlassFinish` still classifies glass and suppresses
  duplicate HDRP transparent draws. Mixed glass/opaque labels and lids need an actual material
  decode (PT-71 item 2); roughness no longer guesses that boundary.
- Shadow connections below still use the older caustic solver. Do not confuse its limitations
  with the new radiance walk, or claim glass caustics complete. Raw checks and remaining gaps:
  `docs/agents/glass-eyes.md`.

## Caustics through glass (R3)

- Caustics through glass (R3, the Caustics row): MNEE in `caustic_through` (pt_common.hlsli), called by the sun's and
  RTXDI's final shadow rays when they met glass twice. Seed: the straight ray's two glass crossings of ONE instance
  (a third crossing or two objects: the straight tint). Vertex setup, constraints (generalized half-vector, eta by
  the smooth normal's side), block-tridiagonal Newton and the transfer-matrix geometry term follow Cycles' mnee.h
  (Apache-2.0, read only; its directional phi uses dot(wo, s) without dividing by sin theta: we divide). Differences:
  a vertex whose step is nothing stays (Cycles fails it, which fails every flat pane under a directional light: the
  first vertex needn't move); fail only if neither moves. Result against the straight light: sun T x G x cos0'/cos;
  lamp T x G x d^2 x cos0'/cos; T = both Fresnel transmissions x sqrt(color) per crossing; clamp 16; no solution -> 0
  (physically dark: a cube's hard edges bend 84 % of the sun's light away from its straight shadow). Verified in numpy
  (`tools/research/mnee_check.py`, `mnee_mesh.py`): slab G = 1 exactly; analytic and 32x64 sphere 98-99 % solved,
  0.68-0.75 of the light back (Fresnel, missed fold solutions); the harness ball matches (0.67, peak x13.5, ring
  x0.06). Meshes now bring vertex normals (MeshRecord's last word; world by cofactors; oct 16:16). Not yet: hollow
  glass (four crossings), path vertices' NEE. Which the game's glass is (`tools/research/glass_mesh_crossings.py`,
  69 glass-named meshes): single bottles are closed shells crossed twice (whisky, mead, wine, Bottle_01 to 06,
  Potion_01, Huge_Jar: caustics solved); Prop_Jars_01*, drinking glasses, the wine glass's cup and Potion_02 are
  hollow (4 or more: the straight tint, close to right for thin empty walls). The F8 report counts it in game too.

## Caustics under water (R3)

- Caustics under water (R3, the Caustics row; `docs/CAUSTICS-RESEARCH.md` Step 1). HDRP's pattern as data, the light
  the tracer's. Where HDRP makes it: every frame, per surface with `caustics` on, a 256x256 grid refracted straight down
  onto a plane 8 m below into an R16 texture with mips (`WaterSurface.GetCausticsBuffer(out regionSize)`); HDRP's water
  multiplies the refracted camera colour by it, the underwater fog everything under water, the volumetric light shafts
  their light. HDRP's mapping (`UnderWaterUtilities.hlsl` EvaluateSimulationCaustics, read only): coord = worldToWater x
  (absolute position / causticsTilingFactor) / (regionSize / 2) + 0.5 (so it repeats every regionSize / 2 m, though the
  texture was rendered over regionSize); uv xz, xy, zy weighted by the normal's triplanar weights .yzx (|n| - 0.2,
  cubed, normalized); weight = saturate(depth / causticsPlaneBlendDistance); mip bias (1 - weight) x maxLOD (2, 3, 4
  for 256, 512, 1024); 1 + dot x weight x intensity. worldToWater is identity for custom meshes (IsCustomMesh: Custom
  with renderers), else inverse(TRS(position, rotation)). Ours: water instances (`TGPT_INSTANCE_WATER`, slot in bits
  8-9; custom surfaces' MeshRenderers with transform.localToWorldMatrix as HDRP draws them, a unit quad x lossyScale xz
  for quads, a 2 x radius quad at sea height under the camera for IsInfinite: OceanSeaLake + Infinite) under mask 0x08
  alone; every other query asks MASK_ALL 0xF7 (MASK_STILL 0xF5; kMaskStill 0xF1). `pt_compose` water_caustic: a
  forced-opaque MASK_WATER ray from the lit point towards the sun (straight up without one), up to 3 tries past steep
  crossings (|n.y| < 0.5: waterfalls), none at all above every slot's highest point (its `top`: dry land), slot from
  the instance record, HDRP's map folded into one affine map of the
  origin-relative position in double (whole repeats dropped), mip = log2(footprint in texels) + HDRP's blur (a compute
  shader has no derivatives), sampler s3 (wrap all ways, new in the root signature); it multiplies `sun[p]` (direct:
  sun and lamps) after its filter, in the final image and the Direct view. Texture copies: CopyResource on D3D11 into
  per-parity NT-shared R16 textures with the source's mip count, before the producer fence. HDRP's own off while
  tracing (WaterCaustics.Hold): a prefix/finalizer on WaterSystem.UpdatePerSurfaceConstantBuffer zeroes
  causticsIntensity for that call (the texture keeps rendering: EvaluateWaterCaustics needs only `caustics`; the
  underwater CB reads the intensity elsewhere, so the light shafts keep it), and one on
  SkyManager.RenderOpaqueAtmosphericScattering sets the underwater surface's `caustics` false while HDRP records the
  opaque fog pass (keyword SUPPORT_WATER instead of SUPPORT_WATER_CAUSTICS). The game's River and Ocean shaders never
  read the pattern from above (research 2.3); the tracer lights all surfaces with caustics on (a default, section 7).
  Harness: stripes under a water quad (pattern + half-shifted pattern = 4 on every floor pixel under it, 0 change
  elsewhere, the shade's change = the lamp's direct change x albedo, the water blocks no ray). Risks: an infinite sea
  inside a dungeon (Sagremor Castle) covers the whole plane (HDRP's water excluders are not read): a sunlit floor below
  its level would take the pattern; Ray Reconstruction with the moving pattern is unseen.

## Sunlight reflected off water (R3)

- Sunlight reflected off water (R3, research Step 2a; pt_compose water_reflected_sun): for a surface with dot(n, m) > 0,
  m = the sun's direction mirrored in level water (down), a MASK_WATER ray along m (bounded by the slots' lowest water
  point: a water-only ray passes through the ground), steep water skipped; Fresnel 0.02 + 0.98 (1 - sunDir.y)^5
  (water, n 1.33); HDRP's pattern at the water point (xz uv, mip log2 of the sun's disc over the distance in texels:
  mean about 1, so the total is the plain mirror's); shadow rays receiver-to-water (transmittance_from_surface) and
  water-to-sun (transmittance); sun x F x pattern x cos / pi, diffuse only, into the stored diffuse after the filter.
  HDRP's pattern texture is written without blending (each texel the last refracted triangle's area ratio, 0 in gaps),
  so its mean is about 1, not exactly. Both water rays stop at the water's span (top and bottom per slot).

## ReSTIR: research and measurements (from section 9.5)

- Published practice (2026-09-26 research): RTXDI DI 20 (permutation, boiling 0.2), GI 8 + age 30; RTX Remix DI 4, GI
  30 frames in its RR preset; course notes 5-30; Solari 8. Randomized GI temporal lookup (Remix up to 80 px), relative
  boiling filter, trace final DI visibility, don't demodulate for RR, albedo < 0.5/255 -> 0, sky albedo 0.5, >= 32
  jitter phases (Remix 64), unjittered motion. Sources: `docs/SOURCES.md`.
- Honest energy reference (`TGPT_ENERGY=1`): plain path tracing with `NO_BOILING | NO_CLAMPS` (+ EXACT_JACOBIAN,
  OFFSET_RECEIVER for attribution), 8 rays x 256 frames, SE over 8 batches. 24 torches: ~0.99. One light 10 cm from a
  wall: 0.794 as shipped (21 % dark), 1.013 without the GI boiling filter (strength 0.2 drops samples > 41x the
  neighbours' average). Hence the Boiling Filter row (default On; the owner can't tell On from Off).
- Crevices (canvas 40 cm over sand, a 12 cm slit; `TGPT_CREVICES=1`): ReSTIR is 14 % short at the mouth (RTXDI's
  normalization) and 9 % over deeper in (30-frame age restarts). Crevice Light (GI ray-traced bias correction, no age
  limit, clamp instead of drop) fixes it for +15-30 % denoised error: default Off. Deep sand gets ~1/150 of sunlit
  sand; 2 bounces hold 53-72 % of it; with the radiance cache 0.97-0.98 of an honest 4-bounce.
- Boiling measures: 8x8-block error lag-1 correlation (0.34 / 0.17 / 0.08 at 1 / 8 / 16 rays). The F8 capture's
  "Boiling" line: 8x8-block frame-to-frame change as traced, GI from 16 own rays only (`TGPT_OPTION_GI_OWN_RAYS`), DI
  without reuse (`TGPT_OPTION_DI_NO_REUSE`), neither, GI history 4, and "as traced again, settled": compare with the
  settled one (the first pair follows the report and read 0.48 against a settled 0.17 once). [crosshair] is the spot.

## The harness's switches (from section 9.11)

- Switches: `TGPT_ENERGY=1`, `TGPT_CREVICES=1`, `TGPT_TEST_ADAPTER=1002|10DE|8086`, `TGPT_TEST_DEBUG_LAYER=<folder with
  the staged Agility's d3d12SDKLayers.dll>` (+ `TGPT_TEST_GPU_VALIDATION=1`). The repo's Agility-SDK-1.619.6 holds only
  D3D12Core.dll: take `build/native/bin/x64` from nuget.org's `Microsoft.Direct3D.D3D12` 1.619.6 package (a zip; its
  D3D12Core.dll hash matches the staged one) into a scratch folder, never the repo. Run 2026-09-28 07:10: 0 errors,
  0 warnings over the whole tracer test;
  `TGPT_TEST_STAGE=<name>` for a second concurrent run (copy the exe too). Sky off needs skyIntensity 1e-6 (0 = 1).
  `TGPT_TEST_SURFACES_ONLY=1` stops after the round-things checks (PT-04's 12-sided cylinder), some 20 s in.

## What draws what (from section 2)

- Static world: Awaken's Drake ECS renderer (entities with `DrakeMeshMaterialComponent` + `LocalToWorld`; mesh and
  material from `DrakeRendererManager.Instance.LoadingManager.MeshLoadingData[i].loadingHandle`; LOD by LODRange x
  2 tan(fov/2) / lodBias; visible ranges are SQUARED distances; per-chunk `RenderFilterSettings`).
- Castle sets: Awaken's Medusa BRG renderer (`MedusaRendererManager` per scene; EditorAccess structs are public:
  Renderers, TransformsCount, BrgRenderer.Xs/Ys/Zs/Radii/LodDistanceSq/TransformIndices/BasePath). Matrices in
  `matrices.medusa` (PackedMatrix, 48 B) inside `medusa.arch`, read with AsyncReadManager, not System.IO.
- Castle walls, arches, battlements: Entities Graphics entities WITHOUT Drake, meshes and materials registered at
  runtime (`World.GetExistingSystemManaged<EntitiesGraphicsSystem>().GetMesh/GetMaterial`; `IsRuntimeMaterial` is
  internal, use `Material >= 0`). They use MaterialMeshInfo RANGES into the game's own shared component
  `Unity.Entities.Graphics.RendererRange { MaterialMeshInfo* }`; no RenderMeshArray.
- Shadow-only cluster meshes `CM_*_Cluster_*` (GameObjects): coarser copies of the castle the game casts shadows with.
- Vegetation: Leshy BRG renderer in TG.Main (`LeshyManager`, a cells catalog, `Matrices.bin`, PrefabRuntime) for grass,
  plants, bushes, ivy and trees; Vegetation Studio Pro trees and large objects (`LoadedVegetationCellList`). Grass has
  no colliders (a raycast probe can't hit it).
- Characters: TG's Kandra GPU skinning. All registered renderers skin at `beginContextRendering` into one
  `OutputVerticesBuffer` (structured, 20 B CompressedVertex, 2.75 M capacity, never recreated, WORLD space). UVs sit in
  `MeshManager._additionalVerticesDataBuffer` (half2 at sharedStartVertex). `KandraTrisCuller` culls body triangles under
  clothes (`rendererData.RenderingMesh`). Kandra draws min(materials, submeshes) submeshes.
- FoA keeps `QualitySettings.lodBias` equal to the camera's HDRP LOD bias (LodBiasWatcher).


## The F8 capture's contents (from section 9.10)

- F8 (DebugCapture.cs, hold still ~24 s; F8 also triggers TGFG's emergency off and AvalonAtlas) writes
  `BepInEx/TGPathTracing/Captures/<time>/`: report.txt (exposure, EV,
  gain and what it metered, every light within 40 m with slot, colour, dimmers, keep/leave-out reason and "no
  specular", the probe, diffusion profiles, tonemapper, GPU time by stage, the Boiling line, the pick under the
  crosshair with HDRP's and the tracer's roughness and "glass seen through" (and, R3, how many times the camera ray
  crosses the first glass object along it: 2 closed, caustics solved; 4 hollow), each Drake mesh's material and whether rays
  see it, "Characters along the crosshair") and JPGs (final, each debug view, sky off,
  pass-through off, the game's own with tracing off, a 2-frame difference, the boiling variants:
  GI own rays, DI no reuse, neither, GI history 4, settled, and (R3) without the reflections, 01i).
  JPGs are CPU box downscales of raw bytes (before 2026-09-27 ~10:47 they were sRGB-encoded twice: washed out).

## Archived PROJECT-CONTEXT section 9 notes (moved 2026-10-12 from base20i)

These are verbatim records of earlier designs, measurements and decisions, not today's implementation.
The dates below identify the work described. Some source paragraphs mixed several rounds; their old "now",
"rule", "pending" and "end of R20" refer to those checkpoints, not the latest round-20 state at 01:25 on 10-12.
Use PROJECT-CONTEXT section 9 for current ABI sizes, flags, stage order, defaults and validation state.
In particular, the G-buffer starting route, old skin blur, HDRP water shading/patterns and bounced-light-only
NRD are retired designs or cleanup gaps, not accepted ways to render today. NR shipped on 10-09; early NR
release-blocker and HDR-validation notes below are historical. No SDK source has been added here.

### ABI timeline and frame handoff (R3-R20, 2026-09-28 to 10-11)

- The third bridge extension: `native/pathtracing` (TGPathTracing.Native.dll), `src/TGPathTracing` (net472 only),
  `src/TGPathTracing.Autoload`. GUID `com.wessberg.tgpathtracing`. Mono only (`Runtimes=@('Mono')` in
  `tools/Package.Common.ps1`). ABI 0x00012300 (R6: 2026-10-08 02:58: the skin program (TGPT_PROGRAM_SKIN, 10 programs), a submesh's closed mark
  (GeometryRecord.flags: glass has an inside only where its mesh closes a volume); 0x00012200 R6: 2026-10-08 00:50: decals seen by rays (TGPT_SetDecals, TGPT_Decal 320 B, instances carry their
  layer), the tracer's memory accounted and released (TGPT_ReadDiagnostics, TGPT_ReleaseMesh), compiled passes cached;
  0x00012100 R6: 2026-10-07 22:45: scattering by a walk, the wet ground, material programs, moved things each
  frame, glass and eyes as surfaces; exports removed: TGPT_SetDiffusionProfiles, TGPT_SetSkinTransmission,
  TGPT_SetShadowDetail; TGPT_Material 192 B;
  0x00012000 R6: 2026-10-07 18:44: grass, leaves and trunks in the game's wind, PT-63: TGPT_SetPlantWind, TGPT_SetWindNoise;
  0x00011F00 R6: (2026-10-07 17:00: the tracer's own camera rays for every pixel, PT-59: TGPT_GetYardstick, the yardstick view);
  0x00011E00 R6: 0x00011E00 (2026-10-07 14:15: real traced caustics, PT-14: TGPT_WaterCaustics carries HDRP's waves, 192 B);
  0x00011D00 R6: 0x00011D00 (2026-10-07 12:50: the limits job, PT-50: TGPT_LIGHT_NO_RANGE, TGPT_MAX_SKINNED 256);
  0x00011C00 R6: PT-48, the tracer draws HDRP's water: TGPT_SetWaterShade, TGPT_WaterShade 568 B, event 6;
  0x00011B00 R6: PT-37, the game's fog along reflected rays: TGPT_Frame 600 B, fogColor and fogMip;
  0x00011A00 R6: PT-45 phase 3, `TGPT_SwapSharedTexture`, the start-up
  self-test and the memory cap; 0x00011900 R6: PT-49, `TGPT_SetClothWind`, TGPT_ClothWind 48 B: the game's wind on
  cloth; 0x00011800 R6: PT-46, rays read the game's own textures: TGPT_Material 160 B with their handles,
  TGPT_SharedTexture 96 B with `handle`; 0x00011700 R6: `TGPT_RegisterSharedTexture`, PT-45's shared textures;
  0x00011600 R6: the detail tiles' pool, `TGPT_ReleaseHairColor`; 0x00011500 R6:
  the profiles' reflectance; 0x00011400 R4: the hair layer; 0x00011300 R4: the start's status, hair's highlights): TGPT_Frame 568 B,
  TGPT_Status 216 B, TGPT_Light 80 B, TGPT_Material 160 B (hair fields, then the shared textures), TGPT_MeshDesc 160 B, TGPT_HairColor
  240 B, TGPT_HairLobes 48 B, TGPT_EyeColor 88 B, TGPT_WaterCaustics 88 B, TGPT_Guides 64 B; FrameConstants 6128 B
  (`tracer.cpp` static_assert; two strand rows for each of the 128 detail tiles, and the profiles' reflectance). TGPT_Material's
  last float is `hairAlpha` since R6 (RealHair's _AlphaPower and _AlphaRemap as two halves; 0 reads as 1).
- The passes are made on threads of their own from device_ready (`start_passes`: the D3D12 pipelines several at once,
  the D3D11 shaders compiled there too), `TGPT_Status.starting` until done; the plugin hands frames over only after
  (`Session.WantsFrames`). The driver compiles every pass after a new build or driver: 13-22 s in round 3, which the
  first traced frame waited for on the render thread (the game frozen at its start). Warm cache: 0.5 s.
  `NativeTracer.Validate` checks the managed sizes at start: a size changed on one side only stops the plugin.
- HDRP hook: a Harmony prefix on the opaque `HDRenderPipeline.RenderSSR` (right after the GBuffer; the transparent call
  returns first) records a CustomPass through `ExecuteInternal` with GBuffer0-3, depth + stencil, motion vectors and
  exposure. The composite is a global CustomPassVolume at AfterOpaqueAndSky, before fog.
- PT-112 (A), the rule since R20 (`GamePasses.cs`): while tracing HDRP draws no opaque object and no decal for the
  gameplay camera (its opaque draw lists left empty: `CreateOpaqueRendererListDesc`, both overloads; Decals off in
  `AggregateFrameSettings`); the composite writes the frame's depth and motion into HDRP's depth buffer, motion vectors
  and depth pyramid (`TGPT_FRAME_OWN_DEPTH`). HDRP's G-buffer is drawn only for the Yardstick view, the old route and the
  hidden "HDRP draws under the traced picture". The traced decals follow the game's own decal setting (recorded before
  the cut). HDRP still culls, renders shadow maps (its fog's shafts), its fog, transparents, post-processing.
- Native: Shader Model 6.6 inline ray tracing (DXR 1.1), bindless `ResourceDescriptorHeap`, root signature in
  `pt_root.hlsli`, DXC at build time (`-WX`). With DXR 1.2 (raytracing tier 1.2 + SM 6.9 via the shipped Agility
  1.619): SM 6.9 builds of every pass, opacity micromaps (`pt_omm_bake.hlsl`), Shader Execution Reordering raygen state
  objects when D3D12_OPTIONS22 says it actually reorders (feature 65, not in the 26100 SDK header). No d3d12.lib: only
  the bridge's device.
- The same frame since round 6 (`docs/agents/edges.md`): frame N's composite waits on the GPU for frame N's own trace
  (exact sub-pixel matching; no reprojection). The old "one frame late" bought no overlap anyway: the two devices take
  turns on this card (9.13). Inputs double-buffered by parity; async compute queue.
- While tracing: `IndirectLightingController.indirectDiffuseLightingMultiplier` = 0 always (GBuffer3 carries baked
  indirect as "emission"); HDRP's AO, contact shadows and SSR off; HDRP distortion off (its resampling of undenoised
  light made candle air boil); the game's Shadows, Contact Shadows, Ambient Occlusion, Reflections and Subsurface
  Scattering rows greyed with the reason (ReplacedRows.cs wraps `_isInteractable` / `_tooltipConstructor`). Every
  Volume override uses `Add<T>()` and sets only the named parameters. To scale a game value: a second global volume
  with value 0 and weight 1 - factor (HDRP lerps: game x factor).

### Scene capture, LOD and alpha-reader history (R2-R20, through 2026-10-11)

- LOD (R6, PT-61, `WorldRules.cs`, in `wt/world` until merged): the traced level is the drawn one, by each renderer's own
  rule read from the game's code: Entities Graphics and Drake `scale x distance to the LOD reference point` in
  `[MinDist, MaxDist)` with `scale = 2 tan(fov/2) / lodBias`, levels finer than `maximumLODLevel` never, the one at it from 0;
  Medusa the first level whose squared distance is above; Leshy plain metres, no bias; Unity LOD groups culled past the
  last level. HDRP sets `QualitySettings.lodBias` only for the length of its culling (`TryCull`), from the camera's frame
  settings: read `HDCamera.frameSettings.GetResolvedLODBias(asset)`. A Drake entity is drawn while it has
  `MaterialMeshInfo` (the game's `DrakeStateJob` loads it by its visible range against the scaled distance to the
  reference point, the far end x `ExtraDrawDistance`): read that, never the range. The old LOD stays until the new one
  is ready ("darkens as I walk up" was a LOD switch). Round 2's "finest within 30 m" is gone.
- Characters (CharacterCapture.cs): copies of Kandra's output buffer each frame for renderers within 100 m (nearest
  first, at most 64); BLAS refit every frame and rebuilt every 4th, staggered (64 x 8192 triangles: 0.8 ms rebuilt
  every frame, ~0.3 ms staggered). Character instances were FORCE_OPAQUE, so hair and alpha parts must be dropped at
  registration or per-material tested.
- Rays start where HDRP drew the surface, which can sit centimetres inside the traced mesh. Pass-through: a ray whose
  first hit is the inside of a mesh within 3 x near_back (~0.45 m + 3 % of distance) passes out. Stand-ins
  (`TGPT_INSTANCE_STAND_IN`) are skipped within that reach whatever their facing. `occluded_from_surface`: closest
  hit within 3 x nearBack, any-hit beyond.
- Wind: `TGPT_MATERIAL_SWAYS` gives an instance mask 0x02 (still ones 0xFD). Rays from the G-buffer query all masks
  first, then still-only below 20 cm, then all only if the hit is `TGPT_INSTANCE_SWAYS` within reach. Only for what is
  not posed: cloth is posed per instance (`TGPT_SetClothWind`), grass, leaves and trunks per 16 m cell of the world (R6,
  PT-63, `TGPT_SetPlantWind`, `PlantGroup` in `scene.hpp`: one merged mesh and one BLAS a cell, refitted a frame). The three
  vegetation shaders' vertex maths and how it was read (serialized buffer layouts and parameter blobs give the names the
  DXBC lacks): `docs/agents/world.md` 4.1. Leaves' and trunks' bend fades along the camera's view: the pose needs the camera.
- Alpha: 256x256 alpha tiles in their own atlas, coverage-preserving (each texel holds how much passes the cutoff over
  16 samples of the finer mip), filtered bilinearly. Every alpha tile stops non-camera rays by chance, as much as it
  covers (strands, straw). Opacity micromaps settle only coverage 0 and 1. Hair (`TGPT_MATERIAL_STRANDS`) gets none.
  Colour tiles are 64x64 in a shared atlas; mask maps share it (up to 3/4).
- PT-110 M3 (2026-10-11, the rule): one glass submesh no longer takes the solid marks from a whole character (the
  tavern: 109,800 triangles). `caustic_clear` forces non-opaque candidates, so a solved caustic's segments still
  skip their whole source instance. M4, micromaps for the characters themselves, was tried and TAKEN OUT the same
  day (`docs/agents/ommout.md`; DEAD ENDS). What it taught: hair's camera alpha is read through dynamic mips AND an
  anisotropic footprint, not a top-mip bilinear sample, so a hole at mip 0 does not prove a piece bare; the other
  rays read the alpha tile; a piece may be settled only where every reader agrees; covered HAIR_LAYER must stay
  unknown while the probe's camera lookup passes it (STRANDS without HAIR_LAYER too). Never narrow that to mip 0 or
  change the camera's filtering for a better count. A posed structure's map links must be named again at every
  build and refit; a change of them or of the geometry flags needs a build.

### Light selection and HDRP-era overrides (R2-R6, 2026-09-27 to 10-08)

- Traced by attachment (SceneLights.IsPlacedLight): a light inside a source prop (Fire, Candle, Candela, Brazier,
  Lamp, Lantern, Torch, VFX, Magic, Portal... in it or 5-6 parents) is traced whatever its name; HeroLight, Volumetric
  and Fake always stay out; fills outside source props stay out. CLO's light is kept (and holds its slot while fading
  in). Skipped: affectDiffuse off, lights off the default light layer. Inventories are logged once per session.
- Reach is physical: as far as the light still gives 50 lux (sqrt(I/50), at most 40 m), not HDRP's range; no HDRP
  distance fade. Effect-only lights (VFX/Magic/Portal with no flame or lamp word) keep HDRP's range.
- Size: HDRP's shape radius (>= 2 cm) is the disc; flames capped at 0.3 m (fires 1 m); a new disc point every frame.
  Cookies are traced (atlas 1024^2, 64 px octahedral tiles). `TGPT_LIGHT_HDRP_SHADOWS`: HDRP renders a shadow map for
  it. `TGPT_LIGHT_NO_SPECULAR`: HDRP's affectSpecular is off, so no traced highlight either.
- Left-out lights while tracing: HDRP `lightDimmer` 0; fog via `affectsVolumetric = original && Fill Light Fog`. Kept
  lights: HDRP lightDimmer x traced gain (so what HDRP still shades matches). One `originals` record per light,
  restored together when tracing stops.
- CLO's light is scaled by `HdrpPasses.AutomaticGainInverse` (at gain x3.1 its spot was 3x the game's).
- Light grid: CPU-built, 8 m cells, 48x20x48 around the camera snapped to 32 m, uncapped CSR lists, rebuilt on a hash
  change. Entries hold (index, cumulative weight), weight = luminance / max(squared distance to the cell box, 1 m^2);
  systematic CDF candidates in pt_di and direct_diffuse, invPdf = 1/p_i. Tavern: 11 lights a cell on average, 43 max.
- Emissive surfaces are lights too (Emissive Lights row): clusters of 32 triangles, a 16,384-triangle budget
  (brightest and largest kept). A glowing surface within a kept light's radius + 25 cm is that light's own surface and
  lights nothing. Moving instances (`TGPT_INSTANCE_MOVING`), characters and stand-ins aren't sampled. Traced emission
  applies HDRP's exposure weight (at 0 the light is colour / exposure, ~10,000x at EV 13) and Albedo Affects Emissive;
  single-sided surfaces glow from the front only.

### Exposure, retired Shadow Detail and fog-grid trials (through 2026-10-11 afternoon)

- HDRP keeps the game's exposure (no Exposure override). Only the traced light is brightened: `pt_meter.hlsl`
  (histogram of every other traced pixel, centre-weighted, log2 -16..+8, 64 bins, before gain) -> p50/p90/p98 ->
  `HdrpPasses.UpdateTracedGain` -> `frame.tracedGain` -> compose multiplies.
- Rules now (`HdrpPasses.AutomaticGain`): brighten = max(0, min((mid tone -3.06 - p50) x Dark Adaptation, 0.5 - p98)),
  so the automatic gain never darkens and the brightest fiftieth stays under ~1.4x white; at most (game EV - darkest
  scene EV 7) stops, and 12; 1.5 EV/s up, 3 EV/s down; frozen in debug views and during F8; the Exposure row adds +-3.
- The "seen" meter: a 128x72 copy of HDRP's image before post, read back every 4th frame; Dark Adaptation's mid =
  max(traced p50, seen p50 - gain), so fog counts as bright.
- Fog (since 2026-10-11): the TRACER's (`pt_fog.hlsl`, `pt_medium.hlsli`: height fog + the game's local fog boxes, mirrors
  through the medium, lamps by equiangular sampling, multiple scattering from the mist's cache cells). The traced light
  takes its whole gain; HDRP's fog, `traced_gain_at`'s fading and Fog Adaptation (PT-48) are gone. HDRP's lighting under
  the traced picture is off (GamePasses: AO, contact shadows, SSR, SSGI, screen-space shadows, SSS, shadow maps, its
  volumetric grid, deferred lighting, camera motion vectors). Open: the sky's haze, fog-only lights, boxes' shadows.
- PT-118 worker `fogfroxel` (2026-10-11): first Remix-style cache trial, hidden `Traced fog: froxel cache`, native
  frame flag 19, own passes only; no FrameConstants row/ABI layout change. /16 x /16 x 48, squared view depth,
  20 m cache and traced tail. Reproject unjittered centres, rescale stored radiance. First tavern run reduces noise;
  excessive cost (timing invalid). Part (b): separate tail queries from the slice loop, constant-density maths,
  two uint4 reservoir grids with corrected temporal proposals (fresh visibility). Clean tavern integration 9.84-10.54 ms:
  seven boxes checked repeatedly in every slice. Cull each ray's box misses once and share grid reads per 8x8 group;
  monitored small GPU case 9.73 -> 0.55 ms, same raw mean. Keep the exact per-ray medium: Remix's historical depth-prefix
  assumes homogeneous fog. Historical complete integrator/filter read at Remix 0.4; current-main bodies still absent.
  Part (b) stability samples, Gaussian filter and retained-excess histories built/GPU-checked; filter 0.08 ms in the small case.
  Directional history and game acceptance pending. `tavern-room@fog-froxel-b`; report `docs/agents/fog-froxel.md`.
- Camera light (`pt_probe.hlsl`): 256 sky-only rays from the camera against the open sky -> `cameraLight`, eased
  0.5 s; it scales HDRP's forward ambient, `reflectionLightingMultiplier` and `Fog.globalLightProbeDimmer`. Interiors
  (no sun, or an Interior* scene): sky 1e-6 and cameraLight 0.01, so no sky leaks through wall seams.
- Shadow Detail row: a D3D11 curve on HDRP's image after the denoiser and upscaler, before post (postfix on
  `BeforeCustomPostProcessPass`, event 3). Below log2 0.12 it lifts s x min(3, 0.4 h(d)) EV; s = row x activation;
  activation = smoothstep(0, 2, auto gain EV) x smoothstep(4.5, 6.5, p98 - p50), eased 1 s, 0 after cuts.
- RR's pre-exposure is HDRP's value clamped 0.2-2, as HDRP's DLSS pass does (the real ~1e-4 would overflow RR's FP16
  history). RR guide 3.7: exposure inputs are unsupported.

### RTXDI parameters and measurements (R2-R6, 2026-09-27 to 10-08)

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

### Denoiser migration and Neural Rendering validation (R11-R20, through 2026-10-11)

- Choices {None, NRD, Ray Regeneration, Ray Reconstruction}; best RR > RRG > NRD. NRD's option is set for every
  choice but None, so NRD serves while RR waits for DLSS upscaling or RRG failed. The tracer's own SVGF-style filters
  serve glass and water under NRD (NRD is for opaque surfaces), and take over if NRD fails. A refused RR frame gives the
  tracer's denoiser back for 0.5 s with fresh history.
- RR runs in TGBU on shared D3D12 (D3D11 NGX DLSSD crashed). NVIDIA's Default preset, which is F in SDK 310.9.16 (the
  owner, 2026-10-11: "Use NVIDIA's Default, if that is currently preset F. If it's not, use preset F"): recheck on a new DLL.
  PT-127 worker: RGB specular in output C.rgb, hit distance in A (+6 bytes/pixel); exported distance R16, `ReadAs::Depth`.
  Material diffuse, no floor/emission tint; sky 0.5. Required distance survives missing/refused optional transparency.
  RR's own optimal query sets exact HDRP dimensions before targets/jitter. Game validation pending; details and removed
  workarounds in `docs/agents/vendor-rr.md`. Hair guides, highlight clamp, glass distance remain separate findings.
  Watch starts -1000 (int.MinValue overflowed); TGPT warns after 5 s of guided frames without RR. Glass: diffuse 0,
  specular covers transmission, roughness 0.03, hit distance behind. Guides' RGB precision is unchanged at export.
- NRD 4.17.3 exactly as its README sets it up (PT-127, 2026-10-11; the owner: "100% follow NVIDIA's and AMD's
  implementation guidelines"): RELAX DIFFUSE_SPECULAR (REBLUR on trial), ALL the light, demodulated by NRD_MaterialFactors,
  real hit distances, shading normal, 2.5D motion, history in seconds. DEAD END: rounds 2 to 20 gave RELAX the bounced light
  alone (a hit distance of 1, the triangle's normal) and filtered the rest with the tracer's own filter, because offline
  "full RELAX blurred sun shadows": the inputs were wrong, not RELAX; in game the gravel came out milky and the tavern
  blurred. A static library with DXIL embedded, its own root signature and a small heap on the tracer's list; its own
  radiance units (restart when the median moves). CMake refuses releases other than 4.17. Judge None against a denoiser
  only at the same traced gain (pt_meter meters 4 x 4 block means since 10-11: noise used to lower the median).
- PT-127, primary surface replacement (`docs/agents/psr-2.md`, `pt_psr_record.hlsli`): the rule under NRD with own
  camera rays, no flag. A still, flat, pure mirror's pixel (no roughness, no diffuse colour) is handed to NRD as the
  surface it shows (game: a mirror's detail under NRD 2.41 against 0.47). 64-byte records stamped with a frame serial,
  never cleared; `pt_specular` alone compiles the capture and carries one word across its rays. Glass and water have
  two ways on (reflected, seen through): each needs guides and a signal of its own; NVIDIA's two answers (half-resolution
  checkerboard layers; its sample's glass undenoised, lit from the denoised frame on screen) each break a ruling: the
  owner rules. A `_ONLY` mode placed late in `tgpt_gpu_test` runs every check above it: put one after the warm-up.
- AMD RRG (FidelityFX 2.4 signed DLLs, runtime query; untested): demodulated radiance, sqrt albedos, world oct normals.
  PT-127: fixed history scale, matching specular factors, reset after gaps; context creation on a retained-device CPU task,
  old GPU work fenced. `TGPT_RRG_DEBUG=1`: debug/validation flags. Previous-depth motion/direct-specular split remain
  open. Supported AMD hardware needed; `docs/agents/vendor-rr.md` has implementation, costs, checks and vendor references.
- Camera cuts reset every history: a new camera, HDRP's `resetPostProcessingHistory` rising edge, a > 60 degree turn,
  a > 20 m jump. The radiance cache survives cuts.
- TG Neural Rendering (R12): signed310.8 feature18/shared device; DLSS-only, starts Off. Native HDR gated; RenoDX HDR unvalidated. Model receives compressed sRGB LDR, original scale restored; no HDR-fidelity proof.
  Settled nativeStyle0/full/maskOff; Quality after, Performance before. UI now Default/A/B/C=0/1/2/3 (old A/B/C=0/1/2). Default is actual vendor0, never our Auto alias. Preset hints0..3 identical in probe; no extra row. RR forces After.
  NR-44,10-09: Ron close-up nr-face-20261007/03-after-A used today's Default/Quality/full/maskOff,
  4K DLSS Performance, vertical FOV40 and camera1.8m forward from Slot_94; PT/FG Off, SDR, CLO present.
  Four selectable styles do not establish four independent networks; historical labels need translating.
  Passes1-4 installed11:22, default1; per-pass strength/history, HDR once. GPU oracle+3 faults and tavern7 pass. Old/new120 synthetic frames identical, not proof against game regressions.
  NR-14 release blocker: owner rejects15:13 walking video, stairs/plants/shields too. RenoDX Upscaled/Present equally unstable. R16 final-output HDR probe with ReShade/RenoDX (no RenoDX DLSS5 add-on) captured FP16 scRGB after ReShade and found no invalid values or fallback pixels. Off/neutral match closely, full strength changes the intended image, but frozen full output still varies more than Off. Unity HDR flag is false when RenoDX owns the FP16 swapchain, so it is not sufficient to detect RenoDX HDR. No stability fix. NR-19/24/PT-72 open.
  NR-30/31/32, R17: owner approves a separate community adapter with its caller-name requirement, then rejects per-file allowlisting because patches change frequently. Signed DLLs keep normal NVIDIA SDK dispatch; modified/unsigned NR DLLs automatically use TGNeuralRendering.nvngx.dll. No new setting. Community acceptance checks x64, direct exports/API0x13 and runtime support, not pinned hashes or file versions. Hash is diagnostic and rechecked under a retained write/delete-denying file handle. Helper owns public parameters and feature handles, sharing the bridge device/NGX lock. No model/driver/Windows trust patches; helper bundled, model excluded.
  R17 corrects R16: NGX's own signature gate rejected the supplied model before loading; Available=0 was NOT hardware refusal or loading proof. Byte-identical caller-name control initialized/rendered. Main/helper passes SDR/HDR/control/resize/Off and shared RR32/NR30 on5080; signed route passes without helper. NR-35: owner switched to IL2CPP and authorized NR-only deployment, completed12:22. Eight files plus patched model hash-verified;80 other mod/config/loader files preserved. Managed build isolated with dotnet --artifacts-path build/nr/il2cpp-r17/managed and TargetFrameworks=net6.0; native files reused from validated R17. NR-36 follow-up: IL2CPP hook crash fixed (section7); official model selected,7 Mono leftovers backed up, startup and4800 NR evaluations observed. Owner says it worked well, then Options crashed (NR-37, section7). Child-array lifetime fix and
  NR-38 Local Tone under Advanced deployed13:07. Options repeat passes20 gameplay cycles13:27; owner says it works great. NR-39 normal FG/BU deployed13:36; two gameplay pause/options/apply/resume cycles pass13:38 with DLSS SR, FG2x/Reflex and NR in both placements (no errors). NR-40 Quality/Performance short labels and tooltip deployed. NR-41/42 normal zips packaged; NR-43 owner switched to Mono/BepInEx5, all3 Mono mods and official NR model deployed13:49, same gameplay smoke passed13:54, all6 zips verified/ready. Game closed, probe removed, configs/saves restored; Source105 passes. RTX20/30/40 remain untested. Details: DLSS5-COMPATIBILITY.md. Other NGX consumers keep their existing trust policy.
  NR-33:5080 cannot stand in for older GPU execution. Our NR path has no50-series/device-ID gate (own shaderscs_5_0, bridge feature11_0); mocks validate dispatch only. Spoofed names/capabilities or PTX JIT on5080 are not RTX20/30/40 proof. Use the existing standalone NR GPU probe on actual older cards; no game needed. Do not spend another5080 run claiming to validate other architectures.
  R11 guide walk161014:1.2m/s,256frames. Exact copies; camera/depth predicts motion within0.008..0.016 guide pixels median, edgep95<=0.027; outlines align. Reversed depth correct for this SR route, not PT/RR/animation proof. Graded161628 reduces variation but halves effect, no same-look stability win. Defaults stay. Native1A3C40026364/Mono75E05F513FF4 deployed16:10; Source95, five valid takes,117 saves/configs preserved.
  R12 isolated310.8: GlobalTone unset/0/.5/1 gives identical pixels. Removing motion raises translated-photo block changes0.383 to2.684, reset-each2.967; correct repeat exact. Motion also affects tone-only. Depth omission exact on planar fixture, not proof depth is always unused. These are input-consumption checks, no improvement to installed rendering. Owner likes current exposure and face close-ups. NR-26 checks input exposure without sacrificing that look.
  R12 exposure walk170616:384 valid frames,1.2m/s,1/.25/1. Quarter input reduces common-scale moving residual about10% but effect19%; held final image slightly worse. Model-space-only comparison would falsely suggest50% less drift because its brightness scale changed. Keep1. Run170215 rejected before capture by UI guard, not model evidence.117 saves/configs restored, game closed.
  R14 boundary182643: actual post-SR colour agrees with current-frame guides. Interior tracking median0.139..0.155px, p95<=0.496; wrong previous motion9.7..10.6px. Same-frame raw/SR alignment0.060..0.065px after recorded jitter. No scale/sign/delay correction justified. Stair outlier crops show multi-depth tracking ambiguity, not proof of guide failure. NR-only diagnostic pair deployed18:26 (nativeAACCD74648CC, Mono unchanged), Source95 passes,117 saves/configs restored, game closed. No lighting fix. FINAL owner ruling: no extra temporal filter on top of NR, including optional trials. Earlier permission superseded, none implemented.

  R15 validation: both builds, policy10 faults, GPU image/fallback/shared RR+NR/multipass pass. FoA0.6.63 reader/menu5 states pass195305. Hidden entries omitted from catalogue. Standalone195951, FG200317, native Video gate/resize/cut/reload201643 pass. DLSS/XeSS vendor output2x, FSR dispatches only (no output counter). Source96 passes. Quality4K tavern83.7/32.4/86.1fps Off/On/Off, +1.67GiB process VRAM. NR pair installed19:49 (83C681B8F630/DDBE0B1415E6), no rendering/default change. Matrix/limits: DLSS5-VALIDATION.md. Steam sync prompt resolved by owner.
  R15 final202304: DLAA and FSR/XeSS gates pass. NR-29 FSR FG slow even with NR Off:20274118.78/15.99/18.48fps Off/On/Off, no errors/contention. Broader FG issue, not proven with NR DLL absent. ReShade/normal runtime pending setup, PT waits for PT-72. All files/configs/saves restored, game closed, no owned test/lock.

### Surface migration and retired stand-ins (R3-R20, 2026-09-28 to 10-11)

- Metals: path vertices are diffuse-only, so `SurfaceMaterial.reflectance` = base colour (a metal's specular energy
  sent on as diffuse); pixels HDRP draws forward use the diffuse albedo. Mask maps per shader, remapped as HDRP does.
- GGX: `ggx_d = a2 / max(PI d^2, 1e-30)` (a +1e-7 capped peaks below roughness ~0.12). HDRP Unlit: f0 0, roughness 1.
  Schlick f90 = saturate(50 f0). A reflection drawn into the surface's own triangle is mirrored back out.
- Reflections row: Off / Glossy Surfaces (rougher than 0.6 uses the cache's light x split-sum albedo) / All Surfaces.
- Translucent (`TGPT_MATERIAL_TRANSLUCENT`: grass, leaves, HDRP Lit `_MaterialID` 5, HDRP transmission features 2/3):
  light from behind at pt_common's TRANSMISSION share; the shadow ray from the light's side only for these. Thin
  cloth (`TGPT_MATERIAL_THIN_CLOTH`, CLOTH_TRANSMISSION 0.3): the owner wants realistic see-through, not glow.
- Skin: a profile is solid when HDRP transmits it as a thick object OR its name says skin. `pt_sss.hlsl`: HDRP's
  Burley after compose, 24 samples on a tangent-plane disc projected to the screen (a screen disc under-weighted
  slanted surfaces), centre weight = CDF(half a pixel). Thin skin (ears, fingers): HDRP's transmittance of the GBuffer
  thickness, 15 degree wrap, the ray must leave within 3 cm. Terminator: Chiang softening on skin alone (mat.x alpha
  0.25; PT-04: on everything else it drew each face near the shadow line); opaque shadow rays start outside. Skin is
  exempt from Reflection Boost (the neck "rash": glossy skin, roughness 0.18, mirrored the chin).
- Round things (PT-04): what the tracer draws from its own mesh (see-through glass, forward props, a glass's far side) is
  shaded by the mesh's vertex normals (`Hit.rounded`; characters bring none); the built-in filter crosses faces where the
  smooth normal agrees; a translucent pixel's shadow ray starts on the light's side only where its shading normal turns
  away too (closed alpha-tested wax went black one face early). NRD keeps the per-face guide. The hidden "Smooth round
  surfaces" off is the old look (an A/B, option FACETED).
- Glass: `GlassFinish.cs` names (Bottle, Glass, Flask, Vial, Potion, Phial, Decanter, Carafe, Beaker, Jar; a NotGlass
  list) -> `TGPT_MATERIAL_GLASS`. Rays pass by chance, sqrt(luminance) per crossing; final shadow rays carry
  per-channel tint. Glossy texels (HDRP roughness < 0.5, < 15 m) are see-through: refract at IOR 1.5, find the far
  side within 1 m, shade behind with 1 + Bounces vertices. Its reflection ray meets lamp spheres and the sun's disc.
  The blackcurrant bottle is simply very dark glass.
- Water (`pt_water.hlsl`): GGX rays from HDRP's water depth and normals, 12-frame accumulation + 5x5 edge-stopped
  filter. The D3D11 fill replaces HDRP's SSR entirely (traced x 1/multiplier, weight 1), right after the transparent
  `RenderSSR` (postfix, event 4, `TGPT_SetWater`). The water's own shading is still HDRP's.
- **Own camera rays (PT-59, R6, `docs/agents/hair-6.md`):** each pixel's surface is the first thing its own ray stops at in
  the traced scene (`pt_primary.hlsli` primary_all, from pt_prepare); the record is the G-buffer route's (surf, nrm, mat), plus
  the hit's emission and its own motion (`FrameConstants.primary`; a character's by `MeshRecord.previous`). Texels are
  "forward" kind (light times colour), which a G-buffer pixel takes in the composite. HDRP's G-buffer is the yardstick
  (view Yardstick, `TGPT_GetYardstick`, a log line every 10 s) and the old route (`TGPT_OPTION_GBUFFER_SURFACES`, hidden
  "Own camera rays" off). Since R20 the frame's depth and motion are the tracer's (PT-112 A); the wind's sway has its
  own motion (a plant group's per-vertex move, `kMeshPreviousDelta`; a cloth's previous pose). Not read
  from a hit yet: skin's and leaves' own terms, decals, terrain layers; things the plugin moves are as old as its last scan.
- Forward pixels (the G-buffer route): pt_prepare traces a camera ray within 3 cm and 1 % of HDRP's depth (mat alpha =
  forward flag, 0.25 skin); hair without a strand block met more than 3 mm in front of that depth is passed (R4).
  `TGPT_MATERIAL_CAMERA_ONLY` (eyes, opaque HDRP Unlit decals < 60 m) is seen by camera rays only.
- Hair (R6): a surface of the picture, no layer. The pixel's ray stops at a card (`TGPT_MATERIAL_HAIR_LAYER`: its strands'
  alpha at the texture's size) with the chance its texel covers, a new draw every frame; lit about its strand (strand_cos)
  with HDRP's Kajiya-Kay highlights. Hidden from HDRP while tracing, as brows are (every pass but the shadow caster off,
  its cuts past 1); the depth and motion at a hair pixel are the tracer's own since R20 (PT-112 A). From R6 to R19 HDRP
  drew its depth at the game's alpha clip (`HoldLayerDepth`, gone) so that DLSS and RR saw the hair's, not the wall's
  behind it (PT-53: the hair slid with the wall). Colour tiles, lobes, eyes: `docs/PT-DETAILS.md`.
- See-through glass, caustics through glass and under water, sunlight reflected off water (R3): `docs/PT-DETAILS.md`
  and `docs/CAUSTICS-RESEARCH.md`. In short: see-through glass by GlassFinish names, drawn by the tracer in front of
  HDRP's surface and of the sky; MNEE through one closed glass object crossed twice (the game's single bottles; its jars
  and drinking glasses are hollow, and keep the straight tint); HDRP's water pattern on the direct light that crosses
  its water (instance mask 0x08), HDRP's own off while tracing; the sun's mirror image in level water on what faces it.

### Radiance-cache design and furnace measurement (R2-R6, through 2026-10-08)

- Own SHaRC-like design, no SDK code: a world hash grid of 2^20 slots (buckets of 8; keys, sums, light = 36 MB);
  cells 0.1 m within 5 m of the camera, doubling with each doubling of distance; a side per dominant normal axis; it
  stores cosine-mean incoming indirect light (E/pi). A path's last vertex adds throughput x albedo x cache light
  (jittered lookup). Fed by 1/4 of the 8x8 tiles each frame (whole tiles), the pixels' first rays and an extension ray
  at the last vertex; 512 samples a cell a frame, per-channel cap 2000; EMA >= 1/16; ages out after 240 frames.
  Cleared on first use, re-enable and scene clears, never on `TGPT_FRAME_RESET`. Furnace: 0.994 with 1 bounce.

### Capture and driver-reset diagnostics (R3-R6, through 2026-10-08)

- F8 (DebugCapture.cs, hold still ~24 s; F8 also triggers TGFG's emergency off and AvalonAtlas) writes
  `BepInEx/TGPathTracing/Captures/<time>/`: report.txt (exposure, lights, the probe, GPU time by stage, the Boiling line,
  the crosshair's pick, the characters along it) and JPGs (final, each debug view, the variants). Everything it holds:
  `docs/PT-DETAILS.md`. JPGs are CPU box downscales of raw bytes.
- Debug views (the last, Yardstick, R6: the own ray's hit against HDRP's G-buffer; colours in `docs/agents/hair-6.md`):
  None, Albedo, Normals, Coverage, Materials, Indirect, Indirect raw, Direct, Surfaces (cyan: forward,
  traced; magenta: HDRP-shaded; orange: solid transmission, i.e. skin), Specular, Emissive (green sampled, blue a
  light's own surface, red rays only).
- A driver reset (the game freezes; Windows' System log: nvlddmkm 153 "Error occurred on GPUID", 14 "GPU recovery
  action ... PF FLR", "TDR occurred"; 13 lines name an MMU fault when it is one) leaves `The GPU stopped (the driver reset
  it): ... D3D12 device <reason>, D3D11 device <reason>; the GPU finished stage X of frame N, and stopped in stage Y` in
  the native log (`report_gpu_stop`: WriteBufferImmediate markers after each stage into a readback buffer). The first,
  2026-09-28 09:16, came before the markers: its cause is unknown.

### Offline harness recipes and comparison pitfalls (R3-R6, through 2026-10-08)

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

### Performance investigations (R6-R20, through 2026-10-11 afternoon)

- The game's D3D11 work and our D3D12 work TAKE TURNS on this RTX 5080 (R19 `<scratch>\overlap`: one job on each device
  of a process runs in the sum of their times; two queues of one D3D12 device overlap). The composite waits for its
  own frame's trace (round 6 `edges`), so a traced frame = the trace + the game's whole frame + a rest. The native
  10-second line splits it ("game device a frame": inputs, the game to the composite, the wait, the composite, Fog
  Adaptation, the game after). HDRP's SSS, transmission, shadow maps, probes, fog lighting cost ~0 here (R19).
  R19 (round-19.md 7 to 10): a traced frame = the trace + the game's own work on the card (tavern ~12.6 ms, town ~20)
  + a 1 to 3 ms gap where Unity's render thread waits in `BufferD3D11::EndWrite` (`LockBufferForWrite`'s 4 MB staging
  copy, asked for in PreLateUpdate, likely Entities Graphics; not HDRP's rendering, not the driver's pre-rendered
  frames, not Reflex). Tools: the 10-second line's CPU clock against the trace; the file `measure-waits` in
  `BepInEx\TGPathTracing\Native` hooks the game's Map/GetData/Flush with callers (never for players); UnityPlayer's
  public symbols (symbolserver.unity3d.com; dbghelp loading the .pdb directly). Our decal capture: 13 ms main thread.
- The reflections' regrouping (R19): clean, the material's key wins or ties everywhere (beach 73.3 vs 75.5 with no
  hint, town 22.0 vs 30.5). With Sunshine on the card the sorted pass loses ~18 ms at the beach and no hint does not:
  "no hint wins at the water" (round 6) was that. The tracer picks per frame by measuring (`State::ReflectionsSort`).
- Shader Execution Reordering (PT-99, 2026-10-10, `docs/agents/rule-2.md`): never a regrouping call inside a loop of a
  big pass, nor work in the screen's order after one (RTXDI's reuse is `pt_gi_reuse`). What a thread carries across the
  call is the cost; the log's "driver's stack a thread" line measures it. Bounced light ms (tavern/town/beach):
  call in rays' loop, reuse inside 41.9/57.7/61.5 (2,592 B); once after ray 1 9.5/22.7/28.9 (0 B); plain 13.7/27.6/64.6.
  DXC copies a regrouping call into each path whose hint is another constant: one call, a computed hint. The
  reflections' pass wants the material's key (8 bits as good as 16). R20: the bounced light's one call had NO key
  (an empty hit object, 0 bits: nothing to sort by); keyed by its first ray's material (0 bytes still carried) the
  tavern 32.2 -> 30.6 ms, the forest 82.3 -> 78.1. A key costs where much is carried, and pays where little is.
  A local array indexed by a computed number, in a loop that holds a regrouping call, is kept WHOLE on the driver's
  stack whatever its DXIL lifetime (R19: `water_sun`'s `rows[2][33]`, 2,112 B, the water pass's whole stack). Count
  a pass's allocas (`= alloca [`) against the driver's figure before blaming the carried values.
- Speed numbers count only from runs with the game in front and no other program on the card (2026-10-10:
  minimized cost 2 to 9 %; the owner's streaming at 4 to 5 % of the 3D engines made the beach 35 to 50 % slower).
- Micromaps (R20, world running): plants have maps (M1) and leaves' covered pieces are opaque but to light rays (M2);
  what alpha tests still cost: the forest 19 ms of 83 (bounced light 8.8, reflections 7.2), the town 3.4 of plants and
  8 of other cut-outs. At level 4 59 % of the plants' pieces are unknown (a piece covers hundreds of texels): finer
  leaves (M7) on trial. Cluster structures (NVAPI) take micromaps too (`ALLOW_OMM`): Mega Geometry need not drop them.
- PT-109 megageo-6 (2026-10-11): NVAPI's explicit destination bound is per object for the WHOLE call's declared
  maxima, not each object's own counts. Mixing individually sized BLAS in a larger-max batch without GET_SIZES is
  outside the contract. Size classes and pre-submit checks fix this; driver-size readback confirms actual use.
  NVIDIA's animated-clusters sample uploads build arguments at initialization and reuses them; its per-frame GPU
  pass writes resulting BLAS addresses into instances. Our explicit route can reuse its arguments too. Never ask
  `allocation_bytes` for each cluster group every frame just to print a log occasionally: cache immutable sizes.
  The old town walk's extra 21-24 ms is in native CPU recording; GPU stages are unchanged. Forest TDR game proof
  still owed; report `docs/agents/megageo-6.md`. No shader, public ABI or option change in this checkpoint.
- Where a town's traced frame goes (R20, `round-20.md` 9): the game's own work 23.5 ms + the tracer 63 (bounced light
  20.6 with the radiance cache's feeding ray 5.6 of it, reflections 20.5 of which "glossy only" would save 8, direct
  6.4, the scene stage 6.2 with the plants' wind poses 4.7, caustics 4.8, camera rays 3.1). Interiors float above the
  map (Ron's Tavern ~470 m over the sea): the sea's caustics cost a room 3.9 ms for nothing (PT-114). End of R20 (leaves
  at level 6, the bounced light keyed, the decals' tree by place): the town's tracer 57, the forest's 74, the tavern's 30.
- Picture checks across runs mislead in the town (a soldier stands elsewhere, a lamp differs, plants hold another wind
  pose): compare frozen steps WITHIN one run, or look only where nothing moves (`round-20.md` 10: the mean frames'
  luma in 16-pixel blocks; within a run two equal builds differ by 0.2 to 0.4 levels on average).

### Legacy atlas tile allocation and keys (R4-R6, through 2026-10-08)

  Atlas tiles: 64x64 0-4095, detail 512x512 4096-4223 (128, 1 MB each, a pool: hair of the same maps and values shares one,
  hair gives its back when its material is gone (`TGPT_ReleaseHairColor`; the plugin: destroyed or not traced for 600 frames)
  or colored anew, and hair leaves the last 16 to see-through glass and the eyes; `runtime.hpp`, `take_detail_tile`); color tile keys: textures (instance ids), mask maps
  bit 63, hair colors bit 62, eyes bits 61-62, hair lobes bit 61 alone.
