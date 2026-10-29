# Characters shaded smooth: their skinned normals reach the tracer (PT-92)

Worker report. Copy: `wt\normals`. Build: `%TEMP%\tgb\normals`.

**State (2026-10-09 08:12): steps 1 to 4 are built. Built and source-checked; NOT run on a GPU and NOT seen in the
game.** The switch is `[Advanced] Character normals` (0 off, the default; 1 smooth hits; 2 and NRD's guide). The one
run that answers it: `.\tools\harness\Run-PtHarness.ps1 -Scenario metal,night -Recipe char-normals`. What is open is
at the end ("Where it stands").

## 1. Where the data is (read in the game's own code and in ours)

- The plugin does not fill the positions' texture. The game's skinning (Kandra, `SkinningManager`, a compute pass at
  `beginContextRendering`) writes every character's posed vertices into ONE buffer, `OutputVerticesBuffer`, in world
  space. `CharacterCapture.CaptureCharacters` hands that buffer's pointer to the native side (`TGPT_SetSkinned`); the
  native D3D11 pass `TGPT_SKIN` (`d3d11.hlsl`) copies each character's positions into a shared texture, and the D3D12
  pass `pt_skin.hlsl` moves them into the character's own position buffer.
- A vertex there is Kandra's `CompressedVertex`, 20 bytes: `float3 position` (bytes 0-11), then
  `uint2 normalAndTangent`: the SKINNED NORMAL at byte 12 and the skinned tangent at byte 16. Each is a unit vector
  as an octahedral map, its two coordinates half floats in 0..1 (x in the low 16 bits, y in the high). The tangent's
  sign is not skinned: it lies with the texture coordinates in `MeshManager._additionalVerticesDataBuffer`
  (`AdditionalVertexData`: a half2 uv, then `float tangentW`), which the plugin already reads once a mesh for the uvs.
- So the game's skinning leaves the smooth normal right beside the position the tracer already copies. Until now the
  copy pass skipped it (`SkinVertex { float3 position; uint rest[2]; }`: `rest` was never read).
- The tracer already has everything after that: a mesh record's `normals` (4 bytes a vertex, octahedral 16:16,
  `pt_common.hlsli unpack_normal`) gives `Hit.rounded` in `fetch_hit` for EVERY ray; `hit_frame` lays the normal map
  on `Hit.rounded` with the triangle's own texture directions (dP/du, dP/dv); `write_traced` (the camera's hit) takes
  the normal-mapped normal, or `Hit.rounded` where the material has no shared normal map. A character's mesh record
  just had `normals = INVALID`.

Answer to the brief's open question (is a character's normal map laid on its surface at the camera's hit?): yes, it
is read (`hit_material` -> `material_normal` -> `hit_frame`), but in a frame built on `Hit.rounded`, which for a
character was the flat face normal. So the hammered relief was there, laid on flat faces; what wipes it in the
picture is the facets under it and the denoiser's per-face guide, not a missing read.

### What was chosen

The game's own skinned normal, copied with the position in the SAME two passes:

1. `d3d11.hlsl TGPT_SKIN`: the texel's alpha (until now 1.0, read by nothing) takes the game's packed normal word.
   No new texture, no new pass, no new dispatch.
2. `pt_skin.hlsl`: decodes it (the game's packing), re-packs it as the tracer's own (`pack_normal`, 16 bits a
   coordinate: finer than the half floats it came from) into a normals buffer of the character's own, 4 bytes a vertex.
3. The character's mesh record names that buffer as `normals`. From there on a character is a mesh with normals like
   any other: `fetch_hit` -> `Hit.rounded` for camera rays, reflections, bounces, shadow tests alike.

No tangent is copied: the tracer builds the normal map's frame from the smooth normal and the triangle's own texture
directions, as it does for every static mesh (which also bring no tangents for this). Noted under "Not done".

### What was rejected, and why

- **Normals worked out from the skinned triangles (area-weighted over a vertex's neighbours).** The game splits a
  vertex at every texture seam and hard edge: the two copies share a position but not their triangles, so normals made
  from neighbours break along every seam (a line down a sleeve, around a helmet), where the game's own normals are
  equal on both copies. It also costs a pass over the triangles and a scatter-add per frame, and it is not what the
  game shades with (blend shapes, artists' weighted normals).
- **Skinning the rest-pose normals ourselves** (bones and weights into the tracer): the game has done exactly this a
  moment before; doing it again is rendering work twice, and its bones, weights and blend shapes would all have to
  be carried over.
- **HDRP's normal buffer / a draw of the character**: screen-space (nothing for a reflection or a bounce to read) and
  forbidden by the rulings.
- **A second shared texture for the normals** (R32_UINT, 8 MB a parity): works, costs 16 MB and nothing is gained over
  the free alpha channel.
- **A plain uint bit pattern in the float alpha**: a float channel is not promised to keep every bit pattern (the
  smallest ones are denormals). The word is stored with bit 30 set, which no valid packed normal has (a half float of
  0..1 is at most 0x3C00): every valid word is then an ordinary float, and the D3D12 side clears the bit again.
  Lossless, and safe against any float clean-up on the way.

### Checked offline, against the game's own code (no GPU, no game)

`scratchpad\normals-tools\packcheck` loads the game's `Awaken.Kandra.dll` (read only) and runs the game's own
encoder over 200,000 directions:

- `CompressedVertex` is 20 bytes, `normalAndTangent` at byte 12.
- The tracer's decode (`pt_skin.hlsl skinned_normal`, the same lines in C# as `CharacterCapture.DecodeSkinnedNormal`)
  gives the same direction as the game's own decode for every one of them (within 1e-5).
- Against the true direction the worst error is 0.12 degrees: that is the game's own half floats. After the tracer's
  re-pack (16 bits a coordinate) it is still 0.12 degrees: nothing is lost on our side.
- No packed word has bit 30 or 31 set, and with bit 30 set every one is an ordinary float (the trick of the alpha
  channel holds).

Not checked offline: that the game's COMPUTE shader writes the buffer with the same packing as its C# type (the
shader is in the game's asset bundles, not read). The plugin checks the C# packing at run time (below); the game's
picture answers the rest: the recipe has a Normals view for it.

## 2. The trial (step 2 of the brief)

**The switch:** `[Advanced] Character normals` (hidden, a whole number, default 0).

| value | what it does |
|---|---|
| 0 | as before: characters reach the tracer as positions, every triangle is shaded flat |
| 1 | the game's skinned normals come with the positions; a character's hit takes its smooth normal (`Hit.rounded`), its normal map laid on that, for camera rays, reflections and bounces |
| 2 | as 1, and NRD is handed the mesh's smooth normal as its guide (step 3, below) |

It is read every frame: one harness run can show it off and on.

**How it travels.** No ABI size changes, no new export:

- Plugin to native: `TGPT_SetSkinned`'s `stride` carries, in its bits 16-23, the byte at which a vertex holds its
  skinned normal (12 for Kandra; 0: none). The plugin takes that byte from the game's own type at run time
  (`UnsafeUtility.GetFieldOffset` of `CompressedVertex.normalAndTangent`, the size against the buffer's stride) and
  checks the packing once against the game's own encoder with six directions. If a game update moves the field or
  packs it another way, it logs once and sends 0: characters fall back to flat triangles, nothing breaks.
- Value 2: option bit `TGPT_OPTION_SMOOTH_GUIDE` = 4194304 (bit 22: it held the HDRP shading shares until those
  went, and was marked retired; the coordinator assigned it on 2026-10-09 after a clash, since 1073741824 is the
  water worker's `TGPT_OPTION_CAUSTICS_SPLIT`). The header's "retired" note is reworded. No source check pinned
  the bit as retired; nothing in the native code or the shaders read it.

**What the native side does with it** (`copy_skinned`, `skinned_positions`, `pt_skin.hlsl`): see "What was chosen".
A character's mesh record names its normals only in the frames they were written, so switching back to 0 gives the
old picture in the next frame.

**In the shaders** (`pt_common.hlsli`), all without an option bit: with no normals in a character's record
everything is as before.

- `fetch_hit`: nothing new for the smooth normal itself (a character's record now has `normals`). Added: a
  character's HAIR CARDS keep their flat face (`MATERIAL_STRANDS`, or `MATERIAL_HAIR_LAYER` without
  `MATERIAL_CAMERA_ONLY`): the brief says to leave hair as it is. A scalp or beard shell (camera-only) is a
  surface painted on the skin and takes the smooth normal like the skin around it.
- `hit_material`: a character's hit whose material laid no normal map on it took the flat triangle's normal for
  the rays after the camera's (the camera's own hit already took `Hit.rounded` in `write_traced`). It now takes
  the smooth one there too, so a mirror shows a character as round as the camera sees it. Characters only: the same
  fallback for static meshes is left as it is (noted under "Not done").

**Logs to look for** (they say the path is live):

- `BepInEx/LogOutput.log`: `Characters: the game's skinned normals are taken with their positions (byte 12 of its
  20-byte vertices; its packing read back to within 0.00xx): characters are shaded smooth (on trial)`. Or a warning
  that says why not.
- `TGPathTracing.Native.log`: `Characters: skinning output structured, 20 bytes a vertex, N vertices, its skinned
  normals at byte 12 (characters are shaded smooth)`. With the switch at 0 the line ends `positions alone
  (characters are shaded by their flat triangles)`. One line each time the layout changes.
- The Yardstick line: the count of pixels whose normal differs from the game's by more than 25 degrees should fall
  for characters.

**The recipe** (in the copy's `tests/ingame/scenarios.json`, added by `scratchpad\normals-tools\add_recipe.py`):

```
.\tools\harness\Run-PtHarness.ps1 -Scenario metal,night -Recipe char-normals
```

Steps: `1-game` (16 frames), `2-flat-nrd` (switch 0, NRD, 16), `3-smooth-nrd` (switch 1, 16), `4-smooth-guide-nrd`
(switch 2, 16), then the Normals debug view with the switch on and off (4 frames each: is the data right?), then off.
Crops to look at: `metal` x 1540 y 440, 460 x 640 (the helmet); `night` x 1440 y 1000, 640 x 860 (the cloak).

## 3. The denoisers' guides (step 3 of the brief)

| denoiser | what it was handed for a character's pixel | what it is handed now |
|---|---|---|
| Ray Reconstruction (and Ray Regeneration) | the pixel's shading normal `s.normal` (`pt_compose traced_guides`, `pt_rrg_guides`): the normal map laid on the FLAT face | the same `s.normal`, which is now the normal map laid on the SMOOTH normal. Nothing to change: it follows from the switch at 1 |
| NRD (RELAX, bounced light only) | the triangle's own normal `s.geoNormal`, for every surface (`pt_nrd_prep.hlsl`) | at 2: the mesh's smooth normal at the pixel, WITHOUT the normal map; at 0 and 1 as before |

Why NRD does not get the normal map: that was measured (the header of `pt_nrd_prep.hlsl`, and the source check
`test_path_tracing_round_surfaces`: "normal maps made its history fail", three times the flicker). RELAX tests its
history against the surface's plane; per-pixel bumps break that test. The smooth normal is a plane that bends with the
mesh and has no bumps in it: it is what that measurement asked for, minus the facets. The normal map's shading still
comes back the way it did (`bump_share`), now taken against the same smooth plane, so the pair still cancels.

How it is built:

- `pt_primary.hlsli write_traced`: with the option, a CHARACTER's plain surface's two words in the hair texture
  (0, 0 until now; read by no pass for such a pixel) take `pack_normal(hit.rounded)` and the mark `SMOOTH_PLANE`
  (0x40000000), where the hit has a smooth normal other than its face's. No new texture.
- `pt_nrd_prep.hlsl guide_plane`: reads it back; the four places that used `s.geoNormal` as RELAX's plane (the guide,
  and `bump_share` in modes 1 and 2) use it. Without the option, and for everything that is not a character's plain
  surface (the world, hair, glass, water), `s.geoNormal` as before.
- Characters only, on purpose. A round jar's guide is per-face too (PT-04's note "NRD keeps the per-face guide"), and
  the same two lines would serve it: drop the character test in `write_traced`. I did not, because it would also hand
  NRD the bent vertex normals of leaves and grass (their meshes' normals point away from the card on purpose), which
  nobody has looked at in a forest, and the trial is about characters. Worth its own short run once this one is in.
- `geoNormal` itself is untouched: shadow rays and ReSTIR still use the true triangle.

**Not compiled by my build:** `pt_nrd_prep.hlsl` is only built with NRD on, and a worker builds with NRD off. I
compiled it by hand with the build's own DXC line (see "Builds"). It has not run on a GPU: the coordinator's merged
build is its first run.

## 4. What it costs

Memory:

| what | bytes |
|---|---|
| a vertex | 4 (the packed normal, in a buffer of the character's own beside its two position buffers of 12 each) |
| a character renderer | 4 x its vertices, in a buffer of its own: D3D12 gives a buffer at least 64 KB, so 64 KB up to 16,384 vertices |
| the busiest scene of run `20261009-071155` (98 renderers; its "skinned" total is 92 MB, 64 MB of that the two shared textures, 25 MB the position buffers) | about 4 to 7 MB more (a sixth of the 25 MB, rounded up per renderer). Not measured: read `skinned=` in `TGPathTracing.Native.log` with the build in the game |
| the shared textures between D3D11 and D3D12 | 0: the normal rides in the alpha that was a constant 1 |
| NRD's smooth guide (switch at 2) | 0: two words of the hair texture that were 0 for such a pixel |
| the frame's constants, the ABI's structs | 0 |

The normals buffers exist whatever the switch says (they are made with the mesh). If the trial is dropped they go
with it; if it is kept they are needed.

Time a frame (not measured: no GPU run before the game's answer, as the brief orders; the run's own GPU time for
steps 2 and 3 is the number):

- No new pass, no new dispatch. The D3D11 copy reads one more word a vertex (it read the 20-byte struct already)
  and ORs one bit. `pt_skin.hlsl` decodes and stores 4 more bytes a vertex: for the roughly 1 million vertices of
  the busiest scene, 4 MB written a frame.
- A hit on a character: three 4-byte loads and an interpolation (what a static mesh's hit already does), plus one
  read of the material's flags for the hair test. `hit_material` reads the instance's flags only where the hit had a
  smooth normal and no normal map.
- The first frame with the switch on compiles one more variant of the D3D11 copy pass (FXC, a small shader, once).
- Compile time of the passes: not changed in any way I could see (the full build took as long as before).

## 5. Noted, not built

- **The helmet's chrome against the game's dark iron** is its material (PT-21), not its normals. With smooth normals
  a mirror-like helmet will be a smooth mirror: more plainly wrong in colour, no longer in bands.
- **Hair cards** keep their flat faces on purpose (`fetch_hit`). `pt_primary.hlsli hair_normal`'s comment ("the
  tracer has no vertex normals for characters") stays true for hair by that choice; reword it if the trial is kept.
- **The skinned tangent is not taken.** It lies at byte 16 of the same vertex (same packing), its sign in
  `AdditionalVertexData.tangentW`, which the plugin already reads back beside the uvs. The tracer lays every normal
  map in a frame built from the smooth normal and the TRIANGLE's own texture directions (`hit_frame`), for static
  meshes too. The game uses the mesh's tangent. On a coarse, strongly bent mesh the two differ a little from triangle
  to triangle. A job for all meshes at once (a `hit_frame` that prefers the mesh's tangent), 4 more bytes a vertex.
- **A static mesh's hit without a normal map still takes its flat face for the rays after the camera's**
  (`hit_material`: `material_normal(..., h.normal)`). I changed that for characters only, to keep the trial about
  characters. The same one-line change for every mesh would make a mirror show a round jar round.
- **The shadow line still follows the triangles.** A smooth normal can face a light its own triangle is turned from;
  the shadow ray from that triangle is blocked by the mesh itself, so the edge of a shadow on a round thing steps
  along the triangles. That is so for every smooth-shaded mesh in the tracer since PT-04. On a character's mesh the
  triangles are small; if it shows in the game it wants its own entry.

Workarounds met (the brief: list them, removed or not):

- `terminator_softening` (`pt_common.hlsli`): Chiang's fade, for skin only. `Surface.skin` is set only by the route
  that starts from HDRP's G-buffer (`pt_prepare`, colour alpha a quarter); `write_traced` never sets it. With the
  tracer's own camera rays it runs for nothing but the hidden `Smooth round surfaces` off. Not removed: it belongs
  to the G-buffer route's removal (PT-60), not to this job.
- `pt_prepare.hlsl`'s G-buffer route takes `facing_normal(hit.rounded, -dir)` for forward-drawn things: it gets the
  smooth normal for characters for free; nothing to do.
- The stride's bits 16-23 and the alpha channel's bit 30 are the trial's own shortcuts. If the trial is kept:
  `TGPT_SetSkinned` gets a parameter of its own for the normal's place (an ABI change, both sides and the pinned
  sizes), the switch and its `0` path go, `SMOOTH_PLANE` moves beside `HAIR_STRAND` in `pt_common.hlsli` (it is
  defined twice now, in `pt_primary.hlsli` and `pt_nrd_prep.hlsl`, to spare a 12-minute recompile of every pass),
  and the warm-up (`tracer.cpp kSkinStructured20`) compiles the variant with the normal.

## 6. Builds, commands

All in the worker's own folders; nothing installed, nothing deployed, the game never started, no GPU test run.

- Native: `%TEMP%\tgb\normals` (NMake Release, `-DTGPT_WITH_NRD=OFF`), script
  `scratchpad\normals-tools\normals-build.cmd` (configure, `Build-PtShaders.ps1 -Throttle 8`, the bridge and the
  tracer). The full shader build: 40 headers, 0 failed, 798 s (`%TEMP%\tgb\normals-b2-shaders.log`); the native build
  links (`normals-b2-build.log`, no compiler warning).
- Two edits came after that full build: `pt_primary.hlsli` (step 3's lines) and the NUMBER of `OPTION_SMOOTH_GUIDE`
  in `pt_common.hlsli` (the bit's move). CMake lists both files as dependencies of every pass, but only
  `pt_prepare.hlsl` includes `pt_primary.hlsli`, and only `pt_primary.hlsli` and `pt_nrd_prep.hlsl` read that option
  (`scratchpad\normals-tools\prepare-only.ps1` checks both before it does anything). So that script compiled
  `pt_prepare.h` and `pt_prepare_dxr12.h` with the build's own DXC lines, marked the other 38 headers as up to date
  (their text did not change), and relinked (`normals-p3-build.log`, no compiler warning). The final
  `TGPathTracing.Native.dll`: SHA-256 starts `BEC1B0BFCE18F40A`; the plugin's `TGPathTracing.dll`: `8EB5DC45F4C7E87C`.
  The coordinator's own build compiles everything anyway: that is the full compile of the final text.
- `pt_nrd_prep.hlsl` (not in a build with NRD off): `scratchpad\normals-tools\nrd-prep-compile.cmd`, the build's own
  DXC line with `-WX`, exit 0. Reads NRD's shader headers from the SDK folder, writes only `%TEMP%\tgb\normals-nrd`.
- `d3d11.hlsl` is compiled by the game at its start (FXC, `cs_5_0`), not by the build:
  `scratchpad\normals-tools\fxc-skin.ps1` compiles its `TGPT_SKIN` pass in three layouts (structured 20 bytes without
  and with the normal, raw with the normal): all exit 0. The compiled pass with the normal is four instructions: load
  the vertex, OR bit 30 into the fourth word, store.
- Plugin: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472
  -o %TEMP%\tgb\normals-plugin`: 0 warnings, 0 errors. (It leaves `obj` folders under `src` in the copy.)
- Source checks: `python tests/source_checks.py`: 100 of 100 (99 and the new
  `test_path_tracing_character_normals_on_trial`, added by `scratchpad\normals-tools\add_source_check.py`).
  `python tests/harness_analyze_test.py`: exit 0.
- Two source checks pin lines this job is near: `test_path_tracing_round_surfaces` (`fetch_hit`'s lines, and NRD's
  guide line) and `test_path_tracing_nrd` (`bump_share`'s lines). All of those lines stand verbatim: the code was
  shaped around them (in `pt_nrd_prep.hlsl` the surface's `geoNormal` is replaced by `guide_plane` once, at the top of
  each mode, so every line that used it reads as before).
- The packing check against the game's own encoder: `scratchpad\normals-tools\packcheck` (a small .NET program; it
  loads the game's `Awaken.Kandra.dll` from the game folder, read only).

Files changed (the whole list; `merge_copy.py --dry` shows the same):

```
native/pathtracing/include/tgpt_api.h          the stride's bits 16-23 (two defines), TGPT_OPTION_SMOOTH_GUIDE
native/pathtracing/src/extension.cpp           TGPT_SetSkinned checks the stride's two parts
native/pathtracing/src/tracer.cpp              copy_skinned: the copy pass with the normal; the unpack hands pt_skin the normals' buffer
native/pathtracing/src/scene.hpp, scene.cpp    a skinned mesh's normals buffer; skinned_positions names it in the mesh record
native/pathtracing/shaders/d3d11.hlsl          TGPT_SKIN: the packed normal into the texel's alpha (SKIN_NORMAL)
native/pathtracing/shaders/pt_skin.hlsl        decodes it, stores it as the tracer packs normals
native/pathtracing/shaders/pt_common.hlsli     fetch_hit (hair cards stay flat), hit_material (a character without a normal map), OPTION_SMOOTH_GUIDE
native/pathtracing/shaders/pt_primary.hlsli    write_traced: a character's smooth normal for NRD's guide (switch at 2)
native/pathtracing/shaders/pt_nrd_prep.hlsl    guide_plane (switch at 2)
src/TGPathTracing/Settings.cs                  the hidden "Character normals"; the option bit at 2
src/TGPathTracing/NativeTracer.cs              OptionSmoothGuide, SkinnedNormalShift
src/TGPathTracing/CharacterCapture.cs          SkinnedNormalAt: the normal's place from the game's own type, its packing checked
tests/ingame/scenarios.json                    the recipe char-normals
tests/source_checks.py                         one check
docs/agents/char-normals.md                    this report
```

No pinned size moves: `TGPT_SkinnedInstance` 100, `TGPT_Frame` 608, the frame constants, the mesh record (48 bytes:
its `normals` field was there, empty for characters). No ABI number changes.

## 7. Where it stands

Four claims, kept apart:

- **Built:** yes (above).
- **Passes the harness:** not run. No GPU test of any kind was run: the brief puts the game first.
- **Seen in game:** no. That is the coordinator's one run.
- **The owner says it's fixed:** no.

What the run should show, and what each outcome means:

1. `LogOutput.log` has the line "the game's skinned normals are taken with their positions (byte 12 of its 20-byte
   vertices ...)" and the native log "its skinned normals at byte 12". If instead a warning: the game's type or packing
   is not what was read; the picture stays flat, nothing else changes.
2. The Normals view with the switch on (`5-smooth-normals-view`) shows characters with normals that vary smoothly
   over a helmet and a cloak, and the same view with it off (`6-flat-normals-view`) shows flat triangles. If the
   on-view shows noise or wrong colours on characters only: the game's compute shader packs its output another way
   than its C# type (the one thing not checkable offline). Then the fix is in `pt_skin.hlsl skinned_normal` alone.
3. `3-smooth-nrd` against `2-flat-nrd`, the helmet and the cloak crops: the direct light and the reflections should be
   smooth; the bounced light may still show the triangles' edges (NRD's guide is still per-face at 1).
4. `4-smooth-guide-nrd`: the edges in the bounced light should be gone too. If the picture boils or flickers more on
   characters at 2 than at 1: NRD does not like the smooth plane; keep 1 and drop 2 (two lines in `pt_primary.hlsli`,
   `guide_plane` in `pt_nrd_prep.hlsl`, the option bit).
5. The Yardstick line: fewer pixels with the normal more than 25 degrees from the game's, for characters.

After the game has said yes (the brief's order), still to do, by a fresh worker:

- One small offline check in its own `TGPT_TEST_CHAR_NORMALS_ONLY` mode: the existing skinned box of
  `tests/pathtracing_gpu_test.cpp` (the "Characters" block, a 20-byte vertex with `packed[2]`) made a coarse cylinder
  with packed normals, `SetSkinned(..., sizeof(SkinVertex) | (12u << 16), ...)`: the Normals view is the cylinder's
  own normal within a degree with the normal, one colour a face without; a mirror beside it shows the same.
- The clean-up listed under "Workarounds met": a parameter of its own for the normal's place, the switch gone,
  `SMOOTH_PLANE` in one place, the warm-up's variant, the comments that say characters bring no normals
  (`fetch_hit`, `hair_normal`, `docs/PROJECT-CONTEXT.md` section 9 "Round things", `CharacterCapture.cs`'s header).
- The measured cost: `skinned=` in the native log off and on, the GPU time of steps 2, 3 and 4.
