# PT-45 / PT-46 phase 2: the tracer draws with the game's own textures

Agent report, 2026-10-06 night, finished 2026-10-07 by a second agent (the first stopped on a usage limit; see
"Finished after the stop"). Built and tested offline in a copy of the tree. Nothing ran in the game, nothing was
deployed or committed. Phase 1 (the hook and the registry): `docs/agents/share-1.md`.

## In three lines

1. A ray that hits a surface now reads the game's own color map, normal map and mask map, at the right sharpness for how
   far and how widely the ray has travelled, with the same kind of filtering the game uses. The 64x64 atlas tile is only
   the fallback now.
2. Hair and eyes read their own maps too (RealHair's color maths, the strands' flow, the eye's sclera and iris), with the
   512 detail tiles and the 64x64 lobe tiles as the fallback.
3. Phase 1's three findings from the game are fixed: the hook goes in at launch whatever the Mode row says, only
   textures of 64 KB or more are shared, and a texture that mip streaming replaces is registered again.

## Words used here

- **Handle**: what a material names a shared texture by: its number in the tracer's table plus a serial, so a number
  that was given to another texture since can never read that one.
- **Ray cone**: a ray's width as it travels. A camera ray starts one pixel wide; a mirror keeps it narrow; a diffuse
  bounce makes it wide. Its width where it hits decides which mip (how blurred a copy) of the texture is read.
- **Mip**: one of the smaller, pre-blurred copies a texture keeps of itself.
- **Anisotropic filtering**: reading a texture seen at a slant as a stretched footprint, not a blurry square: what the
  game's own sampler does.

## What changed

1. **Materials name the game's textures.** `TGPT_Material` grew from 128 to 160 bytes: the handles of its color, normal
   and mask maps (`colorTexture`, `normalTexture`, `maskTexture`; -1 or 0 none), the normal map's strength
   (`normalScale`) and `textureFlags` (the mask map's channels, as `TGPT_RegisterMaskTexture` takes them, and
   `TGPT_TEXTURE_MASK_OCCLUSION`: its green is HDRP Lit's ambient occlusion). All three are read at the material's
   `uvScaleOffset`, as HDRP Lit reads its main maps (and the plugin's Shader Graph tilings already set it). The plugin
   (`SceneCapture.cs`, `TextureShare.cs`) fills them: the base color map it already tiles, the normal map (HDRP Lit only
   with `_NORMALMAP` on and in tangent space; its strength from `_NormalScale`, `_NormalScale0`, `_BumpScale` or
   `_NormalStrength`), the mask map where `MaskLayouts` knows its shader. A mask map now counts even when the atlas has
   no room left for its tile.
2. **Handles, checked every upload.** `TGPT_SharedTexture` grew from 88 to 96 bytes: `handle` = the number in the low 14
   bits, the registration's serial (17 bits, never 0) above. Before the material buffer goes to the GPU, the scene turns
   each handle into the number (or -1) with `share::resolve`, again whenever a texture was published or died
   (`share::generation`). A number that is dead, given to another texture since, or not yet copied into the tracer's heap
   reads as -1: the atlas tile. The GPU copy gets `TGPT_MATERIAL_SHARED` (material bit 131072) where any is left.
3. **A dead number is never read.** A death no longer frees the texture after the frames recorded so far, but after the
   first frame recorded after the registry learned of it (the first `publish` gives it that frame's fence value): that
   frame and every later one resolved the handle to -1. Phase 1's rule freed it while a frame recorded after the death
   could still name it. The water pass reads the frame's material buffer and signals before the next frame: covered.
4. **Ray cones.** Each ray carries a width (`g_coneWidth`, `g_coneSpread`, `ray_cone`): a camera ray one pixel's angle
   (`pixel_angle`), a reflection (pt_specular, the water's) the pixel's width at the mirror widening by the pixel's angle
   plus the lobe's roughness, a diffuse bounce (pt_gi, `shade_path`, `cache_sample`) its vertex's width widening by half a
   radian. `fetch_hit` turns the width at the hit into the footprint's two axes on the triangle (along the ray's slant,
   stretched by it, and across), and those into texture-coordinate gradients through the triangle's own UV map; it also
   keeps dP/du and dP/dv (the triangle's tangent directions).
5. **Reading.** `material_texture`, `material_finish`, `material_normal` (pt_common.hlsli): `SampleGrad` with a new static
   sampler, anisotropic 16x wrapping (`s4`; `s5` clamped for the eye), times the upscaler's mip bias as the game samples.
   The color goes through the same maths as before (base color times the map, vertex color, HDRP Lit's metallic split);
   the mask map's metallic and smoothness through `maskRemap`'s ends texel by texel, its green as occlusion on the bounced
   light alone (the cache's and the next bounce's: HDRP Lit takes AO off indirect diffuse only). The normal map: Unity's
   RG-or-AG packing times the strength, in the frame of the mesh's smooth normal and the triangle's dP/du, its bitangent
   along dP/dv (a mirrored UV island or a negative tiling flips it), kept on the ray's side. `SurfaceMaterial` gained
   `normal` and `occlusion`; `direct_diffuse` takes the shading normal for its cosines and the triangle's for which side
   a ray leaves from (an opaque surface takes no light its triangle faces away from). Every ray that reads a material at a
   hit goes through `hit_material`: reflections, path vertices, the radiance cache's rays, the water's reflections, what
   pt_prepare and pt_hair take for surfaces HDRP draws forward, and see-through glass (`see_through_tint`). The alpha test
   still reads the alpha atlas (its micromaps are baked from it), and emission still reads its tile (the light sampling of
   emissive triangles reads the same).
6. **Hair and eyes from their own maps.** The bakes (`bake_hair`, `bake_eyes`, `bake_hair_lobes`) also write a record per
   detail tile (and per lobe tile) of a new buffer (`GpuScene::set_map_record`, `FrameConstants.textures.y`): the maps'
   handles (found by pointer: `share::handle_of`), which maps the material has, and the game shader's values (the bakes'
   own constants). Resolved like the materials. `material_texture` then computes RealHair's color (`hair_color`) or the
   eye's (`eye_color`) at the hit from the maps at full size, with the bakes' maths for one texel, and `hair_lobe(tile,
   Hit)` the strands' flow, smoothness and specular map. A record any of whose maps has no number reads as none: the tile.
7. **Phase 1's findings from the game** (runs 20261006-232203, -232601):
   - The hook goes in with the game's device whenever path tracing can run on this GPU, whatever the Mode row says
     (`read_launch_mode` is gone). 485 textures were "made before the hook" with Mode Off at launch.
   - Only textures of **64 KB or more with their mips** (and slices) are shared (`texture_bytes`, `kMinBytes`): a 128x128
     BC1 (8 KB) is no longer shared at eight times its memory. `TGPT_SHARE_SMALL` now means that.
   - **Mip streaming.** The plugin watches each texture a material reads (`TextureShare.Refresh`, 256 a scan): where its
     loaded mip or update count changed, it reads the native pointer again; a new D3D11 object is registered and its
     handle written into every material that reads it, which are sent again. The old one's death frees its number. The log
     line counts them ("N swapped for a new texture by mip streaming"). Hair's and eyes' maps are not watched (their bakes
     take the pointer once).

| File | What |
|---|---|
| `native/pathtracing/include/tgpt_api.h` | ABI 0x00011800; `TGPT_Material` 160 B; `TGPT_SharedTexture.handle` (96 B); `TGPT_MATERIAL_SHARED`; `TGPT_TEXTURE_MASK_OCCLUSION`; `TGPT_OPTION_ATLAS_TEXTURES` |
| `native/pathtracing/src/texture_share.cpp`, `.hpp` | handles, `resolve`, `generation`, `handle_of`, deaths freed after the next frame, `texture_bytes`, the hook at launch |
| `native/pathtracing/src/scene.cpp`, `.hpp` | materials resolved before upload; the map records' buffer |
| `native/pathtracing/src/tracer.cpp` | `FrameConstants.textures` (6128 B); the bakes write the map records |
| `native/pathtracing/src/extension.cpp` | `handle` -1 by default; no Mode read |
| `native/pathtracing/shaders/pt_root.hlsli` | static samplers s4 (anisotropic, wrap), s5 (anisotropic, clamp) |
| `native/pathtracing/shaders/pt_common.hlsli` | the material record, ray cones, `fetch_hit`'s footprint, the readers, hair and eye maths, `direct_diffuse`'s shading normal, `shade_path` |
| `pt_specular.hlsl`, `pt_gi.hlsl`, `pt_water.hlsl`, `pt_prepare.hlsl`, `pt_hair.hlsl` | the cones; `hair_lobe(tile, hit)` |
| `src/TGPathTracing/NativeTracer.cs`, `TextureShare.cs`, `SceneCapture.cs` | the managed mirror, handles, normal maps, the streaming watch |
| `tests/pathtracing_gpu_test.cpp` | phase 1's sizes for the 64 KB rule, a 128x128 BC1 not shared; the mirror checks below |
| `tests/source_checks.py` | ABI 1.18, sizes, `test_path_tracing_shares_the_games_textures` extended |
| `CHANGELOG.md`, `docs/PROJECT-CONTEXT.md` 9.1 | the entry; the ABI and sizes |

## ABI

- **TGPT_ABI 0x00011800** (was phase 1's 0x00011700). Added: `TGPT_Material` +32 bytes (160: `colorTexture`,
  `normalTexture`, `maskTexture`, `normalScale`, `textureFlags`, 3 reserved), `TGPT_SharedTexture` +8 bytes (96:
  `handle`, `reserved`), `TGPT_MATERIAL_SHARED` (131072, set by the tracer only), `TGPT_TEXTURE_MASK_OCCLUSION` (16),
  `TGPT_OPTION_ATLAS_TEXTURES` (67108864, for testing: the claimed option bit). If another branch took 1.18 first, the
  merge takes the next number in `tgpt_api.h`, `NativeTracer.cs`, `test_path_tracing_abi` and PROJECT-CONTEXT 9.1.
- **FrameConstants** 6112 -> 6128 bytes: one `uint4 textures` at the end (x the shared range's start, y the map records'
  SRV). Source checks count 34 rows.
- Merge notes: `hit_material`, `shade_path`, `cache_sample` and `direct_diffuse` changed in place (other agents who touch
  them: keep `m.normal` for the cosines and `m.occlusion` on the bounced light). `Hit` gained `dpdu`, `dpdv`,
  `footprint`, `uvGradX`, `uvGradY`; `SurfaceMaterial` gained `normal`, `occlusion`.

## Offline numbers

`tgpt_gpu_test` on the final build: all checks pass (757 ok, 0 failed, exit 0: run 3 at 00:01, and again after the
`scene.cpp` fix below, same numbers).
`python tests/source_checks.py`: 81 tests, OK. The plugin and the harness plugin compile (`dotnet build -c Release` to a
scratch folder, 0 errors).

The new mirror check (a 1024x1024 BC7 panel of 128 by 64 light and dark cells, seen in a perfect mirror, 149212 pixels):

| What | Number |
|---|---|
| Agreement of the mirror with the cells as a pinhole sees them (1 = a perfect copy) | **0.713** shared, 0.001 with the 64x64 atlas tile (grey) |
| Mean light of the panel | 0.2915 shared, 0.2905 the tile: same colours, same light |
| A material with no number, and one whose texture the game freed, against the atlas picture | off by 1.5e-4 to 1.7e-4 of its light (the noise of two runs): they read their tile |
| The panel tiled 16 times (cells a sixth of a pixel): relative spread | 0.190, the same as the grey tile's 0.190 (the lamp's falloff alone): no aliasing; mean 0.2936 against 0.2915 |
| A normal map tilted toward a lamp at the side, flat, tilted away | 0.2506, 0.1435, 0.0575 (the atlas, no normal maps: 0.1435) |
| Stage times, shared against atlas (90 frames each) | within noise of each other on this small scene (bounced light 0.67 against 0.85 ms, reflections 0.24 against 0.25 ms); the in-game F8 report is the number that counts |

Mutation checks (each one broken on purpose, the test run, the file put back):

| Broken | What failed |
|---|---|
| The color map never read (`noshared`) | "a mirror shows the game's own texture sharp" |
| No footprint, every hit at the top mip (`nocone`) | "a texture far finer than a pixel is read at its small mips" |
| Handles read whatever their texture's state and serial (`noresolve`) | "a material without a number, or whose texture died, reads its atlas tile", the normal-map check, and four checks of surfaces drawn forward (zeroed handles then read texture 0) |
| The tangent turned round (`tangent`) | "the game's normal map tilts the shading at a hit" |
| Phase 1's size rule, texels instead of bytes (`texels`) | "a texture under 64 KB is not shared" |
| Six source-check mutations (materials not resolved, Mode read again, streaming not watched, the size rule, a death freed at once, no anisotropic sampler) | each fails `test_path_tracing_shares_the_games_textures` |

## Finished after the stop (2026-10-07)

The first agent stopped on a usage limit at about 00:05 with everything built and tested; its last note was about a line
whose backslashes had been eaten. No such line is left in the tree: every changed file was checked (each `printf`'s
`\n`, the source checks' escapes, the root signature's line ends), each file keeps its base's line endings (CRLF or
LF, unmixed), and the build, the GPU test and the source checks pass. One fix on review: `scene.cpp` read `materialsDirty_ || !materialBuffer_.buffer` again after the
new upload had made the buffer, so a first upload with the list not marked dirty would have skipped the micromaps'
material signatures; the condition is now taken once, before the upload (`materialsChanged`).

## What the next harness runs should show

Build, install and deploy this copy (TGPathTracing.Native.dll and the plugin; ABI 1.18, so both at once), then:

1. **`-Scenario mirror-tavern,mirror-day`** with the `mirror` recipe, read through `tools/harness/mirror.py`
   (`mirror.md`, `mirror.jpg`). Against the first run (fine detail 0.37 of the reference with Ray Reconstruction, 0.58
   with no denoiser; colours 0.98 against 0.75 in the tavern): **detail toward 1** (the door's planks no longer in
   squares; the iron bands' rivets and the wood grain there), **colour saturation toward the reference's** (a guess, not
   measured: the shared texture is decoded exactly as the game's sampler decodes it, where the atlas bake stored a
   linear texture's bytes as if sRGB and decoded them again; if saturation stays at 0.98, its cause is elsewhere), and bumps in the door
   and the walls from their normal maps. Brightness may move a little: the mask map's occlusion now darkens bounced light
   where the game's materials have it. If detail rises but stays well under 1, compare `mirror.md`'s no-denoiser number
   (the texture's own sharpness) with the RR one (what the denoiser keeps).
2. **The log** (`BepInEx/LogOutput.log`): "Shared textures (PT-45): A of the traced scene's B textures opened ...",
   now with "**made before the hook**" near 0 even when Mode is Off at launch, "**under 64 KB with their mips**" among
   the reasons, and "**N swapped for a new texture by mip streaming**" growing as the scene's textures stream in. In
   `TGPathTracing.Native.log`: "made shareable from the game's device on" at launch whatever the Mode row.
3. **`-Scenario hair-eyebrows`** with `views`: the crown's fine strands (hair's colour now from RealHair's maps at their
   own size, its highlights' flow from the flow map at the hit), and the eyes' irises sharper in a reflection. If hair
   looks as before, check that its maps have numbers (they are registered by the plugin; hair maps under 64 KB or made
   before the hook keep the tiles).
4. **Cost**: the F8 report's stage times (reflections, bounced light) against the last run's. The offline numbers
   below say a few percent at most; the game's textures are larger and more varied (cache misses), so the in-game
   number is the one that counts.
5. **Failures to look for**: black or magenta patches in reflections or bounced light (a number read after its texture
   died: should not happen, the frame-after rule is there for it); the game itself drawing differently (the hook now
   runs from launch with Mode Off too: compare a launch with path tracing off against the last build's); textures that
   stay blurred where `mipstreaming.txt` says they loaded (the watch should catch the swap within a few scans).

## Risks

- **Video memory, counted twice.** The probe saw Windows count a shared texture once per device while D3D12 holds it
  (the memory itself is there once). Phase 1's runs opened 490 MB and 1019 MB in two scenes, and `textures.txt` says the
  scene's 1979 loaded textures take 3388 MB as the game holds them. On an **8 GB card** Windows' budget for the game is
  about 7 GB; the game's textures (3.4 GB) and its render targets (well over 1 GB at 1440p with HDRP) already take most
  of it, so 1 to 3.4 GB more counted against the same budget can push it over: Windows then moves textures to system
  memory, which shows as stutter or blurry textures, path tracing on or off while they are open. Nothing caps this yet.
  Next step if a player or the harness on a small card shows it: open only what rays meet (by distance from the camera,
  nearest first) up to a share of `QueryVideoMemoryInfo`'s budget, and let go of textures no material within reach names.
- **Cost.** A shared read is a real texture fetch (anisotropic, BC-compressed) per hit, where the atlas was one load from
  a small texture that stays in cache; normal maps add a fetch and the mask map another. Diffuse bounces read small mips
  (cheap); mirrors and smooth reflections read large ones.
- **Cards where sharing fails** (AMD, Intel, Windows' software adapter: another agent tests them): everything without a
  number keeps its atlas tile and detail tiles, as before. The hook going in at launch whatever the Mode row means a
  driver bug in the flag would now show with path tracing never turned on; the hook's fallback makes the texture as the
  game asked if the driver refuses.
- **Hair's and eyes' maps are not watched for mip streaming** (their bakes take the pointer once): a hair map streamed
  in after its bake keeps its tile. Character textures are usually not streamed; the log's reasons would show.
- **The ray cone is an approximation**: no curvature (a convex mirror's reflection would want a wider cone), one spread
  for all diffuse bounces. Too narrow reads finer mips than needed (sparkle); too wide blurs. The 16x-tiled check shows
  no aliasing; the mirror check shows the cells resolved.
- **Mip bias**: the game's upscaler bias is applied to every shared read (camera rays and reflections alike), as the
  game's own sampler applies it on screen.
- **The water pass** reads the frame's materials and signals before the next frame records: it is inside the
  frame-after rule.
