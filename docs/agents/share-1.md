# PT-45 phase 1: the game's textures opened by the tracer, with no copy

Agent report, 2026-10-06 night. Built and tested offline in a copy of the tree. Nothing ran in the game, nothing was
deployed or committed. The probe behind it: `docs/agents/texture-share.md`.

## In three lines

1. The game's own textures can now be opened by the tracer's Direct3D 12 device with no copy. A hook gives each
   texture the game makes for shaders to read the sharing flag as it is born, and a registry opens it and gives it a
   number.
2. Nothing draws with them yet. Rays still read the 64x64 atlas tiles. Phase 2 moves the shaders over (plan at the end).
3. Offline: BC1, BC3, BC5 and BC7 textures with all their mips read back through the tracer's view bit for bit the same
   as Direct3D 11 reads them. The whole GPU test passes (681 checks) with the hook on.

## Words used here

- **Hook**: our function put in front of one of Direct3D 11's, so we see (and can change) every call to it.
- **Sharing flag**: a bit set when a texture is born that lets another device open it. We use the old one
  (`D3D11_RESOURCE_MISC_SHARED`); the new one (NT handles) refuses BC4 to BC7 and cube maps.
- **View**: a small record that tells the GPU how to read a texture (its format, which mips). Shaders find views by
  number in a table, the **descriptor heap**.
- **Death notice**: a tiny object we hang on a game texture. Direct3D 11 lets go of it when the texture dies, and that
  tells us to let go of our side.
- **Fence**: a counter the GPU raises when it finishes a frame; we free something only once the GPU is past it.

## What was built

1. **The hook** (`native/pathtracing/src/texture_share.cpp`). It sits on Direct3D 11's own `CreateTexture2D` (vtable slot
   5) and `CreateTexture2D1` (slot 54, which does not pass through slot 5), through the bridge's MinHook registry.
   Those functions belong to d3d11.dll and every device in the process shares them, so a wrapper such as ReShade
   (RenoDX rides on ReShade) forwards Unity's calls into the hook. A texture gets the flag only if it is made for
   shaders to read and nothing else: usage DEFAULT or IMMUTABLE, bind flags exactly SHADER_RESOURCE, no CPU access, one
   sample, no misc flags but TEXTURECUBE, larger than 64x64 (either side). Nothing else changes in its description.
   If the driver refuses the flag, the hook makes the texture again exactly as the game asked: **the game never sees a
   failure**. That texture is tagged so the registry can say why it has no number. The hook takes no lock, logs
   nothing and touches no D3D12: it runs on Unity's loading threads.
2. **When the hook goes in.** Decision: with the game's device (inside its swapchain creation, before any level loads),
   when path tracing can run on this GPU, **unless the Mode row was Off at launch**. The native side reads
   `BepInEx/config/com.wessberg.tgpathtracing.cfg` at the bridge's bootstrap, before the game has a device. With Mode
   Off at launch the hook waits, and goes in when path tracing registers its first texture (the player turned it on);
   textures loaded before that keep their atlas tiles. So a player who never uses path tracing pays nothing. It is
   **never taken out**: turning path tracing off at runtime leaves it in, because a loading thread could be inside it,
   and the flag alone costs only the 64 KB rounding of textures just over 64x64. Nothing is opened while path tracing
   is off, so nothing is counted twice.
3. **The registry** (same file). Given a texture (`Texture.GetNativeTexturePtr`), if it carries the flag: its old-style
   handle (`IDXGIResource::GetSharedHandle`, a number, nothing to close), opened on the shared D3D12 device, a view over
   all its mips in a CPU table of the registry's own, and a number (16384 of them). Typeless formats are read as their
   plain variant. The render thread copies new views into a fixed range of the tracer's descriptor heap before each
   frame (`share::publish`; the range is taken first thing when the heap is made, so the number is the heap index).
   The registry holds **no reference to the D3D11 texture**, only its address, so its death notice comes. The notice
   (`SetPrivateDataInterface`) is taken in on the next registration, every `TGPT_ReadStatus` (four times a second,
   tracing or not) and every frame; the D3D12 side goes once the GPU has finished every frame recorded until then. A
   texture registered again after a clear gets a new serial, so an old notice can't drop it. `TGPT_ClearScene` lets go
   of everything. A texture made before the hook, refused, too small, an array or cube map, or not made only for
   shaders to read gets no number and a reason; the caller keeps today's atlas tile for it.
4. **The plumbing to prove it in game.** `TGPT_RegisterSharedTexture(texture, info)` returns the number or the reason
   (`TGPT_SHARE_*`), and fills `TGPT_SharedTexture`: the texture's D3D11 size, mips, format, usage and flags, its video
   memory once opened, and the registry's totals. The plugin (`src/TGPathTracing/TextureShare.cs`) calls it beside
   today's atlas registration for each traced material's colour (`Tile`, `DetailTile`), mask (`MaskTile`) and normal
   map (`_NormalMap`, `_BumpMap`, ...; the atlas never held these), and for characters' maps (hair, eyes:
   `NativeTexture`). It logs a line per traced scene (below).
5. **The harness step for mip streaming** (`src/TGPathTracing.Harness/MipStreaming.cs`, step field `mipStreaming`): for
   up to N streamed textures without their top mip loaded, the native pointer and D3D11's own width, mips and misc
   flags as found; then with every mip requested; then with only the smallest three; then given back to Unity. It
   writes `mipstreaming.txt` with "NEW OBJECT" or "same object" per change and a summary line. Added to the mirror
   recipe's step `5-mirror-game` (`"mipStreaming": 12`, after its frames); `tools/harness/mirror.py` copies the summary
   into `mirror.md`.

## Files changed

| File | What |
|---|---|
| `native/pathtracing/src/texture_share.hpp`, `.cpp` | new: the hook, the registry, the death notices, publish |
| `native/pathtracing/include/tgpt_api.h` | ABI 0x00011700; `TGPT_SharedTexture` (88 B); `TGPT_RegisterSharedTexture`; `TGPT_SHARE_*` |
| `native/pathtracing/src/extension.cpp` | bootstrap reads Mode; `d3d12_device_ready` puts the hook in; the export; `ReadStatus` collects; `ClearScene` clears (outside the engine lock) |
| `native/pathtracing/src/tracer.cpp` | `sharedBase` (heap range at creation); `share::publish` before each frame's intake; `share::heap_lost` in `release_all` |
| `native/pathtracing/src/gpu.hpp` | `DescriptorHeap::allocate_range` |
| `CMakeLists.txt` | `texture_share.cpp` in TGPathTracing.Native |
| `src/TGPathTracing/NativeTracer.cs` | `Tgpt.Abi` 0x00011700, `NativeSharedTexture`, the binding, `Tgpt.Share*` |
| `src/TGPathTracing/TextureShare.cs` | new: registration, counts, the log line |
| `src/TGPathTracing/SceneCapture.cs` | the calls (colour, detail, mask, normal, characters' maps), `Reset`, `Report` |
| `src/TGPathTracing.Harness/MipStreaming.cs`, `Request.cs`, `Runner.cs` | the mip-streaming step |
| `tests/ingame/scenarios.json`, `tools/harness/mirror.py` | the step in the mirror recipe, its summary in `mirror.md` |
| `tests/pathtracing_gpu_test.cpp` | `shared_texture_checks` (before the scene), `TGPT_TEST_SHARE_ONLY=1` |
| `tests/source_checks.py` | ABI 1.17 and the struct's size; `test_path_tracing_shares_the_games_textures` |
| `CHANGELOG.md`, `docs/PROJECT-CONTEXT.md` 9.1 | the entry; the ABI |

## ABI

- **TGPT_ABI 0x00011700** (was 0x00011600): one export, `TGPT_RegisterSharedTexture`, and one struct,
  `TGPT_SharedTexture` (88 bytes, mirrored as `NativeSharedTexture`, size checked in `Validate` and the source check).
  If another branch took 1.17 first, the merge takes the next number in all four places (`tgpt_api.h`,
  `NativeTracer.cs`, `test_path_tracing_abi`, PROJECT-CONTEXT 9.1).
- **No change** to `FrameConstants` (still 6112 B), `TGPT_Status`, `TGPT_Frame`, `TGPT_Material`, or any shader. No
  new option bit.
- Merge notes: `tracer.cpp` changes are three small hunks (the `State` field, `ensure_d3d12`, `release_all`, and one
  line in `trace_event` before `st.scene.intake`). `extension.cpp`: the class's two callbacks, `TGPT_ReadStatus`,
  `TGPT_ClearScene` (its `return 0` moved after `share::clear()`), and the new export before `TGPT_GetGuides`.

## Offline numbers

The GPU test (`tgpt_gpu_test`, RTX 5080), texture-sharing part, run first thing after the passes are made:

| texture (made through the hook, data at creation) | mips | number | flags after the hook | GPU memory | texels compared | differ |
|---|---|---|---|---|---|---|
| BC1 256x256, DEFAULT | 9 | 0 | 0x2 (shared) | 0.06 MB | 87,381 | 0 |
| BC3 sRGB 512x128, IMMUTABLE | 10 | 1 | 0x2 | 0.12 MB | 87,383 | 0 |
| BC5 128x128, DEFAULT | 8 | 2 | 0x2 | 0.06 MB | 21,845 | 0 |
| BC7 sRGB 1024x1024, DEFAULT, through CreateTexture2D1 | 11 | 3 | 0x2 | 1.38 MB | 1,398,101 | 0 |

- Each texel of each mip is loaded by the same compute shader through the tracer's view (a copy of the registry's
  descriptor, on the bridge's D3D12 device) and through D3D11's own view of the texture; the 16 bytes of each must be
  equal. The same texture registered twice gets the same number.
- The BC1 texture freed on the D3D11 side: open 4 -> 3, freed 0 -> 1, 1.62 -> 1.56 MB, and the D3D12 texture's
  reference count back to the test's own.
- Not shared: a 64x64 BC1 texture `TGPT_SHARE_SMALL` (no flag added), a 128x128 render target `TGPT_SHARE_FLAGS` (no
  flag), a 2-slice array `TGPT_SHARE_SHAPE` (shared by the hook, refused by the registry); the 64x64 one still gets its
  atlas tile. A cleared scene leaves 0 open.
- The whole test with the hook on: 681 checks, 0 failed, 15,697 frames traced, none failed (the scene's own leaf and
  other textures now carry the flag: their atlas bakes and HDRP-style drawing are unchanged).
- Source checks: 81, all pass. `tests/harness_analyze_test.py`: 6 pass. Plugin and harness plugin compile (0 warnings).
- Mutation checks, each built and run (`scratchpad/share-mutate-gpu.py`, `share-mutate-source.py`), all caught:
  - GPU test: the hook adds no flag (7 checks fail); CreateTexture2D1 not hooked (the BC7 texture's 2); the view
    holds the top mip only (the 4 "every mip" checks); death notices ignored (the freed texture's check); 64x64
    textures shared too (the "no number and why" check); a cleared scene keeps its textures (the clear's check). The
    clean build passes again after them.
  - Source check: the hook without the game's failure path; a lock in the hook; the NT-handle flag; no death notice;
    the hook always in (Mode not read); no normal maps registered; the old ABI number. 7 of 7.

## What the game's log should say

In `BepInEx/TGPathTracing/Native/TGPathTracing.Native.log`, right after "Shared D3D12 device: DirectX Raytracing tier":

    Texture sharing (PT-45): the game's textures made only for shaders to read, larger than 64x64, are made shareable from the game's device on (CreateTexture2D and CreateTexture2D1 hooked)

then, at most every 30 s when something changed:

    Texture sharing: the game made N textures through CreateTexture2D and M through CreateTexture2D1; S made shareable, R refused by the driver; O open on D3D12 (X MB), P opened and F freed by the game since the start

(With Mode Off at launch: "Texture sharing waits for path tracing: its Mode was Off at launch ..." at bootstrap, and
"... made shareable from now on (path tracing turned on; Mode was Off at launch)" when it is turned on.)

In `BepInEx/LogOutput.log`, after the scans, at most every 10 s while new textures come, and when the traced scene
starts over:

    Shared textures (PT-45): A of the traced scene's B textures opened by the tracer with no copy, X MB; the others: c 64x64 or smaller (the atlas holds them whole), d not made only for shaders to read, ... The tracer holds O (Y MB); P opened and F freed by the game since the start; the hook is in.

In the harness (`-Scenario mirror-tavern`): `5-mirror-game/mipstreaming.txt`, first line "N streamed textures: a
loaded their top mips (b as a new D3D11 object), c dropped them (d as a new object)", also in `mirror.md`.

**Good**: S grows into the thousands as the world loads, R is 0, most of B is opened, the reasons are mostly "64x64 or
smaller" and "not made only for shaders to read", the MB match `textures.txt`'s "as the game holds them" order of
magnitude, F rises as you travel, and the game looks and runs exactly as before (nothing reads the shared textures yet).

**Failed, and what it would mean:**
- "could not be installed", or "the hook is not in" with Mode On: the hook failed (another hook on the same function?).
- S stays near 0 and the reasons are mostly "made before the hook" with Mode On at launch: Unity makes its textures
  through a path the hook doesn't see, or before the swapchain.
- R large: this driver refuses the old flag for some format (the line lists them by DXGI number).
- "D3D12 could not open them": the old-style handle doesn't open in game.
- Mostly "not made only for shaders to read": the game's material textures carry other bind flags; the filter is too
  strict for them.
- F stays 0 while you travel far and O keeps growing: death notices don't come; video memory would leak.
- Black or missing textures in the game's own picture, a crash or hang at load: the flag broke something. Comparing a
  launch with Mode Off (no hook at all) tells.
- `mipstreaming.txt` saying "NEW OBJECT": the plugin must re-register a texture whose pointer changed (phase 2, below).

## Risks

- **Only NVIDIA measured** (RTX 5080). AMD and Intel may refuse the old flag for some formats, or refuse to open an old
  handle in D3D12. The hook's fallback keeps the game working, and the atlas stays for what isn't shared. The log line
  shows refusals by format.
- **Counted twice**: while open, Windows counts a texture once per device (real memory once). On a nearly full card
  this might push textures to system memory. Phase 1 opens every traced material's textures; if that matters, phase 2
  can open only what rays meet.
- **The 64 KB rounding** of textures just over 64x64 (a 128x128 BC1 with mips is 11 KB, shared it takes 64 KB): paid
  while the hook is in, path tracing on or off. `textures.txt` can count how many there are.
- **Mode On at launch but never traced**: the hook is in (the 64 KB rounding), though nothing is opened.
- **A death the registry learns late**: notices are taken in at the next registration, status read (4 per second) or
  frame. Until then D3D12 keeps that texture's memory. Fine at 0.25 s.
- **A frame recorded after a texture's death may still name it** (phase 2): today nothing reads the numbers. The
  release waits for the frames recorded before the death; phase 2 must stop referencing a dead number in the same
  frame start where the registry learns of the death (below).
- **The water pass** records on its own; its fence values aren't the registry's yet (phase 2, if water reads them).
- **Old handles are global numbers**: any process could open a game texture. Game textures aren't secret.
- The hook adds a call to `host().internal()` and two relaxed atomic counters per texture creation: negligible next to
  the creation itself (0.06 to 0.38 ms).

## Phase 2: the tracer reading these textures

The goal: rays read the game's real textures at a hit, sharp and at the right mip, instead of 64x64 atlas tiles. That
is what makes hair and reflections better. The mirror harness (`-Scenario mirror-tavern`, `mirror.md`) is the yardstick:
the traced mirror against the game's own picture of the same view.

1. **Materials name numbers.** Add to `TGPT_Material` (or a parallel array) the shared numbers of its colour, normal and
   mask textures, `-1` for none (then the atlas tile, as today). The plugin already has them (`TextureShare`); keep
   them per texture and write them into `NativeMaterial`. ABI bump; FrameConstants gets `sharedBase` (0 today, but pass
   it). `TGPT_Material` is 128 B; check its free fields first.
2. **A dead number must not be read.** The registry learns of a death at `publish`; it should return the numbers that
   died, and the tracer must rewrite the materials that name them (back to the atlas tile) before it records that frame;
   only then is releasing after the fence safe. Change `kill` so that a death's retire value is that of the first
   publish after it. If water reads them, the water event needs a `publish`-like retire bump too.
3. **When the data is there.** A texture's bytes are written by D3D11 at creation or later (`UpdateSubresource`, mips
   streamed in). D3D12 sees them after a fence D3D11 signalled after the write. The tracer already waits on the game's
   frame (the producer fence) before tracing, and the plugin registers textures on the main thread after Unity made
   them, so a texture registered before frame N's render event is safe to read in frame N. Mips written later (the
   streaming swap, if in place) are covered the same way.
4. **Mip streaming.** Settle it with the harness step first. A new D3D11 object per change (what I expect): watch each
   traced texture's `loadedMipmapLevel` and `updateCount` from C# (cheap), and when either changes, read
   `GetNativeTexturePtr` again and register the new pointer; the old texture's death notice frees its side. Same
   object: nothing to do. The tracer could also ask for the top mip of what mirrors and glossy reflections see
   (`requestedMipmapLevel`), within the game's own streaming budget.
5. **The mip at a hit** (ray cones). Carry a cone (spread angle and width) along each ray: from the camera's pixel
   angle, widened at each bounce by the surface's curvature and roughness. At a hit, the cone's width divided by the
   texel size in world units (from the triangle's UV and position areas) gives the level:
   `lod = log2(coneWidth * sqrt(texels(uv area) / world area))`, then `SampleLevel` with a trilinear sampler (add a
   static sampler to the root signature). Reflections in a smooth mirror get the top mips; a far, rough bounce gets
   small ones, which is also cheaper.
6. **Colour.** sRGB textures' views already decode to linear (they are typed sRGB). Multiply by the material's base
   colour and vertex colour as the atlas path does; alpha test from the same texel (the alpha atlas can then go).
7. **Normal maps.** Unity packs normal maps as "DXT5nm" (BC3 with X in alpha, Y in green) or BC5 (X red, Y green):
   `n.xy = t.ag * 2 - 1` (or `.rg`), `n.z = sqrt(1 - saturate(dot(n.xy, n.xy)))`, scaled by the material's normal
   scale, then into world space with the hit's tangent frame. That needs tangents per vertex (read with the mesh like
   the normals, or derived from UVs per triangle). This is what gives reflections and bounced light their small
   detail.
8. **Mask maps.** HDRP Lit: metallic in red, smoothness in alpha, with the material's remap ranges, as `MaskTile`'s bake
   does today, read per texel instead of baked.
9. **Hair and eyes.** Hair's maps (`NativeTexture`: diffuse, root, ID, mask, blend, flow, specular) are registered now;
   the hair layer and its highlights can read the strands at full size instead of the 512x512 detail tiles and the
   85 MB strand atlas, and the eyes' sclera and iris likewise.
10. **Cost.** Measure the frame time with and without (`TGPT_GetStageTimes`), first with colour only, then normals. Keep
    the atlas as the fallback for whatever has no number.
