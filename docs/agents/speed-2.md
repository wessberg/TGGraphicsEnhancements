# Speed 2: the scene stage's 9 to 14 ms, and the opacity micromaps that were gone (PT-08, PT-73 c)

State at 2026-10-08 10:03 (the worker stopped here: usage limit). **Code final and built; stopped before the last
clean whole run had ended.**

- BUILT: `%TEMP%\tgb\spd2-build` (NRD off). The DLL is `%TEMP%\tgb\spd2-dll-f.dll` (= `-g`, `-h`: the same file,
  SHA-256 starts A1AD812602A3); the final test exe is `%TEMP%\tgb\spd2-test-h.exe`. Source checks: 95 pass.
- PASSED ON THE GPU: the new micromap checks alone (0 FAIL, not one pixel differs with micromaps on); the memory
  clear check alone; the whole test once with this DLL (`out\full-f.txt`: 0 failed frames, 6 FAIL: the base's 4,
  one time limit on a shared GPU, and the micromap count 4 where the check said 3, corrected in the test after).
- NOT DONE: a whole run with the final test exe on a quiet GPU. The second whole run (`out\full-h.txt`, 09:34) is
  **VOID**: its native log has "Path tracing frame failed: Interop fence woke before its value 0x887A0006" (frames
  at 284 to 292 ms, other workers' whole runs on the GPU); I stopped it at line 1031. Its 20 FAIL lines are not the
  code's (the first whole run of the same DLL passed every one of those checks).
- **A third whole run was started at 10:04 as a script of its own and finishes by itself:**
  `scratchpad\spd2\run.ps1 -Name full2 -Tags h`; its log is `scratchpad\spd2\out\full2-h.txt`, its pictures
  `%TEMP%\tgb\spd2-images-full2-h`, its native log `%TEMP%\tgb\spd2-stage-full2-h\TGPathTracing\TGPathTracing.Native.log`.
  It waits by itself for the game and the harness to be gone. Read it with
  `python scratchpad\spd2\compare.py scratchpad\spd2\out\full-base.txt scratchpad\spd2\out\full2-h.txt`: expect the
  base's 4 FAIL lines (slab colour, glass index, green glass, grey glass) and no other; a "frame failed" or
  0x887A0006 in it means void again. Its "characters' acceleration structures" line is the one clean number still
  missing (section 4.2 has it only from a shared GPU: between 0.08 and 1.5 ms against the base's 9.34).
- NOT ANSWERED: whether the 13 pictures of 130 that differ from the base run's by up to 7 levels of 255 (section
  4.3) differ because of the frames the test now runs before them or because of the DLL. They look like noise, not
  like shapes. The check that settles it is in 4.3.
- For the merge: main moved to ABI 0x00012600 this morning (TGPT_Frame 608 bytes, FrameConstants 7920, FogConstants
  128; the coordinator settles it). This job changes no export, struct or option bit, so it has nothing to add
  there. It does read `g.textures.x`, `g.settings2.y`, `g.scene0`, `g.scene1.z`, `g.extra.x` in `pt_omm_bake.hlsl`
  as before.
Copy: `scratchpad\wt\spd2` (base `wt\base52`, the main tree of 2026-10-08 07:50, ABI 0x00012500, unchanged: no export,
no struct of the API, no option bit). Builds: `%TEMP%\tgb\spd2-*`. Scripts and raw outputs: `scratchpad\spd2\`
(`build.cmd`, `run.ps1`, `scene_lines.py`, `out\`).

## The short version

- **The scene stage** was slow because the memory job made every acceleration-structure update of a frame wait for
  the one before it. Each character, each plant group in the wind, each swaying cloth and each new mesh was built
  over the same piece of scratch memory, with a "wait until all writes are done" barrier after each. 59 to 82
  characters a frame then ran one after another instead of side by side: about 0.15 ms each.
  **Worse in real play than the harness shows:** the harness freezes the world, so the plants are not re-posed there.
  With the wind's clock running, every plant group in use (287 to 442 outdoors) is refitted every frame, one after
  another too: the GPU test's 41 groups took 3.4 ms (0.65 ms before), so the forest's 287 would be over 20 ms more.
- The same change also made the three scratch buffers be thrown away and made again every frame (section 1.3).
- **The micromaps** were gone because the light and flicker job switched them off on purpose for every mesh whose
  material reads the game's own texture: the old micromaps were baked from the small copies (the 64x64 and 256x256
  tiles), and a ray's alpha test now reads the game's texture. In the game every texture is the game's own, so no
  mesh got one. In the offline test the leaf materials read tiles, so the check there never saw it.
- Fixed: builds side by side again, with scratch sized for one frame's builds (a refit asks for a refit's scratch,
  which is far less than a build's). Micromaps are baked from the game's own texture, exactly what the alpha test
  reads, and baked again when that texture is replaced.

## 1. The scene stage

### 1.1 What is in it
`tracer.cpp`: from `mark(0)` to `mark(1)`. The characters' skinned positions unpacked, `GpuScene::record`
(`scene.cpp`), `record_omm`, `record_emitters`.

### 1.2 The cause (code)
`scene.cpp`, all four from the memory job ("shared scratch for sequential BLAS builds"):

| Where | What is built a frame | Before (base45) | Base52 |
|---|---|---|---|
| `record`, characters | every traced character's BLAS, refitted (rebuilt every 4th frame) | each at its own offset of `skinScratch_` (the sum of all), one barrier at the end | all at offset 0 (the largest one's size), `barriers.uav(nullptr); flush` after each |
| `pose_plants` | every plant group whose pose changed (all of them while the wind's clock runs) | own offsets, one barrier | offset 0, a barrier after each |
| `sway` | every swaying cloth copy | own offsets, one barrier | offset 0, a barrier after each |
| `build_blas` | new static meshes (while a scene loads, up to 48 a frame) | own offsets | offset 0, a barrier after each |

A barrier with no resource named makes the GPU finish every unordered-access write before the next command. A BLAS
refit is several small compute jobs; side by side the GPU runs many characters' jobs at once, one after another it
runs each alone. (The water's surface units, `scene_water.cpp`, were written side by side and are not affected.)

### 1.3 A second fault in the same lines: the buffers were made anew every frame
`if(skinScratch_.size<needed || skinScratch_.size>max(needed*4, 1 MB)) make_buffer(max(needed*3/2, 8 MB))`.
With "needed" now the largest single character (well under 2 MB), the 8 MB minimum is always more than four times
it: the test is true every frame, and an 8 MB buffer is created and the old one retired every frame. The same for
the plants (8 MB) and the cloth (4 MB). That is CPU time on the render thread (the status lines' `record` went from
2.5 to 3.7 to 3.9 ms a frame in the vista between the two runs; not all of it need be this).

### 1.4 The measurements that show it (offline, already in the coordinator's logs)
The whole GPU test has a figure for exactly this: "characters' acceleration structures: 128 of 8192 triangles (1M),
posed every frame", and one for plants: "a field of 19881 tufts of grass ... in the wind ... the scene's stage".
From `scratchpad\gpu-test*.txt`:

| Run | 128 characters cost | 41 plant groups in the wind, scene stage |
|---|---|---|
| 2026-10-06 16:31 to 2026-10-07 10:38 (20 runs) | 0.32 to 0.33 ms | |
| 2026-10-07 12:45 to 18:59 (5 runs) | 0.47 to 0.49 ms | |
| 2026-10-08 00:06 (main, a shared GPU) | 1.51 ms | 0.65 ms |
| 2026-10-08 04:23 (main, after the memory merge) | **9.89 ms** | **3.37 ms** |

0.077 ms a character there (8192 triangles each) and 0.08 ms a plant group; the game's characters are 10 to 12
thousand triangles each and cost about 0.15 ms each.

In the game (run `20261008-034922`, the "Path tracing 10s" lines; `scratchpad\spd2\scene_lines.py <log>`):

| Scene | characters (triangles) | plant groups | cloth copies | scene stage |
|---|---|---|---|---|
| tavern | 59 (704k) | 0 | 2 | 8.9 to 9.2 ms |
| forest | 67 (807k) | 287 | 25 | 9.6 to 9.9 ms |
| lake | 69 (629k) | 442 | 30 | 9.5 to 9.6 ms |
| vista | 82 (861k) | 297 | 12 | 12.5 to 14.1 ms |

What the scratch memory was in that run ("PT memory bytes: ... scratch="): 13 MB in the tavern, 40 to 42 MB in the
forest, lake and vista (all scratch buffers together).

### 1.5 The fix
`scene.cpp`: `scratch_place`, `GpuScene::fit_scratch`, and the four loops.

- Every build of a stage gets a piece of the stage's scratch buffer to itself; one barrier after the last.
- **Each build is asked for what it needs this frame**: a refit takes the driver's `UpdateScratchDataSizeInBytes`, a
  build its `ScratchDataSizeInBytes` (new fields `Mesh::skinUpdateBytes`, `PlantGroup::updateBytes`,
  `SwayCopy::updateBytes`). Before the memory job every character had room for a full build every frame.
- **The buffer holds one frame's builds, up to 32 MB** (`kScratchWindow`), or its largest single build if that is
  more. A stage that needs more than the window builds in waves: when the buffer is full, one barrier, and the next
  build starts at the front again. So memory is bounded whatever the scene holds, and a wave is still dozens of
  builds running at once.
- A buffer is made a quarter larger than the frame that needed it, and it only goes when it is four times larger
  than anything asked of it in the last 256 frames (checked every 256 frames, not every frame).
- When a scratch buffer is made, the native log says so, with its size: "Acceleration structure scratch for
  characters: N MB (this frame's builds need M MB side by side, the largest K MB)". That is how the game run will
  show what the choice costs there.

What each choice costs. Measured on the RTX 5080 with the GPU test's 128 characters (1 M triangles) and its field
of grass (41 groups, 477,144 triangles); the scratch sizes are what the driver asks for:

| Choice | Scratch memory | Scene stage, 128 characters | 41 plant groups in the wind |
|---|---|---|---|
| One piece for all, a barrier after each (base52) | 8 + 8 + 4 MB (the buffers' minimums; the largest single build is 0.03 MB for a character, 0.97 MB for a plant group), made anew every frame | **9.34 ms** | **3.68 ms** |
| Room for a full build of everything, always (before the memory job) | the sum of all builds x 1.5, 8 MB at least (`base45`, the characters' lines): 8 MB for the characters; by the same rule about 26 MB for these plants | 0.32 to 0.49 ms | 0.65 ms |
| A frame's builds side by side, each at what it needs, a 32 MB window (this job) | **2.5 MB for the characters** (all 128 built in one frame need 2.0 MB), 21.6 MB for the plants' first builds (17.3 MB), which goes after 256 frames of refits that need less | see section 4.2 | **0.70 ms** |

So the memory job's one-piece scratch saved nothing here (the buffers' own minimums were larger than everything
the characters need side by side) and cost 9 ms a frame. In the game, expect about 2 MB for 60 to 80 characters,
and the plants' buffer at up to 32 MB while groups are being built, less after (the log line says).

## 2. The micromaps

### 2.1 The cause (code)
`scene.cpp` `want_omm` (and `omm_matches`) in base52:
`if((gpuMaterials_[index].flags&TGPT_MATERIAL_SHARED) && gpuMaterials_[index].colorTexture>=0) { mesh.omm=None; return; }`
for ANY submesh of the mesh. `docs/agents/light-flicker.md` point 1 says why: the bake (`pt_omm_bake.hlsl`) read
the atlas tile or the alpha tile, and a ray's alpha test (`pt_common.hlsli` `alpha_opaque`, `material_alpha`) now
reads the game's texture for such a material: `baseColor.a * texture.SampleGrad(aniso wrap, st, 0, 0).a >= alphaCutoff`.

### 2.2 Why the offline check passed
Its leaf, card and cloth materials have atlas tiles and no shared colour texture, so the rule above never applied.
The 01:50 failure ("2 meshes, 3 expected") was a race in the test itself: the cut-out fixture of the light job runs
right after the warm-up and gives the card mesh a material with a shared texture. If the card's micromaps were not
baked yet by then (a busy GPU), the rule above set the card to "none" for good, and the later check counted 2.

### 2.3 The fix: baked from exactly what the alpha test reads
`pt_omm_bake.hlsl` (rewritten around the old tile path, which is unchanged), `scene.cpp` `record_omm`.

- For a cut-out that reads the game's own texture (the shader's own `shared_cutout(m)`, the same call the alpha
  test makes, on the same material record of the same frame) the bake reads **that texture's top mip**, with the
  material's own tiling, the base colour's alpha and the material's own cutoff.
- The alpha test takes the top mip filtered between the 4 texels around a hit. A filtered value lies between the
  least and the most of those 4. So a micro-triangle is "covered" only when every texel a hit in it can reach is
  above the cutoff, "bare" only when every one is below, and "ask the test" otherwise. Two safety margins: a texel
  within 1/1024 of the cutoff counts as "ask the test" (the sampler rounds its weights), and the box of texels a hit
  can reach is 1/64 texel wider than the arithmetic says (the sampler rounds positions to 1/256 texel), plus the
  float's own rounding far out on a tiled texture.
- **The bounds.** With the game's textures a micro-triangle covers many texels (a leaf card on a 2048x2048 texture:
  hundreds), where on the 64x64 and 256x256 copies it covered a few. Read texel by texel with the old limit of 64
  texels, nearly every micro-triangle would have been left "ask the test" and the micromaps would do nothing. So the
  bake first makes, for each texture its cut-outs read, the least and the most alpha of every 4x4 texels of the top
  mip, then of every 2x2 of those, up to one cell for the whole texture (the same pass, two more steps:
  `OMM_BOUNDS_FIRST`, `OMM_BOUNDS_NEXT`). A micro-triangle is then settled by at most 64 cells of the level that
  fits it. It reads a few more texels than its own (a whole cell at each edge), which can only make it say "ask the
  test" more often, never give a wrong answer. A fully opaque "cut-out" texture is settled by its one top cell.
  The bounds live for the frame of the bake only (a 4096x4096 texture: 5.6 MB) and are not a copy a ray reads.
  A texture whose sides are not powers of two is read texel by texel alone (64 at most).
- Budget a frame: 262,144 triangles as before, and at most 64 M texels of textures read for bounds (four 4096x4096).

### 2.4 Kept right when things change (`scene.cpp` `omm_instance`, `omm_stale`, `release_omm`, the signatures)
- A material's signature (what its micromaps depend on) now includes the handle of the texture its cut-out reads. A
  handle names one texture object for its life, and mip streaming makes a new object (`TextureShare.cs`), so
  micromaps baked from one texture are never used with another.
- The signatures are made again when the textures' numbers are looked up again (`share::generation`) and when the
  frame switches between the game's textures and the tiles (`TGPT_OPTION_ATLAS_TEXTURES`; new
  `GpuScene::sharedTextures`, set by `tracer.cpp` beside `ommAllowed`).
- **New: micromaps are made again.** In base52 a mesh's micromaps were made once, and when their material changed
  the instance went without them for good. With streaming replacing textures as the camera moves, they would have
  died off one by one. Now, when the materials they were made for have changed, or no instance has traced with them
  for 8 frames (`kOmmUnusedFrames`), the old micromaps and their BLAS are retired (after the GPU is past the frame)
  and new ones are baked for the materials in use. Never for an instance that cannot have micromaps, so they are not
  lost to one.
- A mesh is no longer set to "none" by the first instance that cannot have micromaps (a camera-only shell, glass,
  hair): it waits for one that can.
- Hair drawn as a layer (`TGPT_MATERIAL_HAIR_LAYER`) is now left without micromaps, like hair strands, camera-only
  shells and glass (`omm_possible`): section 6 says why.
- The budget (`ommInputBytes_`, 256 MB of input) now gets a mesh's share back when the mesh is retired, when its
  micromaps are released and when its bake fails. In base52 it only grew.

### 2.5 What they cost, how long the bake takes, and the scene clear (numbers to come)
- Memory: in the run of 2026-10-07 22:46 the vista had 456 meshes, 786,689 triangles, 151 MB (the micromap arrays
  and the second BLAS each mesh gets; the mesh's first BLAS stays too). The new ones are the same structures, so the
  same order of size for the same meshes.
- The scene clear frees them: the arrays and the second BLASes are in the acceleration arena, which `clear()`
  releases whole (`accelerationArena.release_all`), with `ommScratch_`, the queue and the budget count. A retired
  mesh's are retired with it (`retire_meshes`).
- The bakes' own scratch (`ommScratch_`: 16 MB at least, as large as the largest frame's bakes needed) used to stay
  until the scene was cleared. It now goes as soon as no mesh waits for a bake.

## 3. What else got slower between 22:46 and 03:49, and is not a change of the picture

Read from the two runs' status lines; the forest is the brief's own comparison (lines 5170 and 5178 against 5969,
5979, 6801, 6814 of the two logs).

| Forest, ms | 22:46 | 03:49 | Change | What it is |
|---|---|---|---|---|
| scene | 0.66 to 0.74 | 9.6 to 9.9 | **+9.1** | section 1: fixed |
| prepare (the camera's own rays) | 8.0 to 8.8 | 9.0 to 9.3 | +0.5 to 1.0 | not measured apart; see below |
| bounced light | 38 to 42 | 38.6 to 41.7 | 0 | |
| direct light | 11.1 to 11.2 | 12.4 | +1.2 | not measured apart; see below |
| reflections | 21.9 to 22.4 | 25.8 to 27.1 | +3.5 to 4.7 | not measured apart; see below |
| whole frame | 93 to 96 | 110 to 117 | +17 to 21 | |
| render thread: `flush` | 0.37 (vista) | 2.9 to 3.2 (vista) | **+2.6** | named, not this job's: below |
| render thread: `record` | 2.5 (vista) | 3.7 to 3.9 (vista) | +1.2 to 1.4 | section 1.3 is part of it |

- **Prepare, direct light and reflections (about +6 ms together in the forest).** Three things changed for the rays
  between the two runs, and the logs cannot tell them apart:
  1. the micromaps went (this job brings them back: whatever these three stages lose again in the next run is theirs);
  2. a cut-out's alpha test now reads the game's own full-size texture (one filtered read of a 2048x2048 or larger
     compressed texture where it read four texels of a 256x256 tile): that is the light job's change of the picture
     (grass blades as the game draws them), and its cost stays;
  3. decals are now looked up at every hit (the decals job): a change of the picture too.
  **Know this before expecting much from the micromaps outdoors:** plants in the wind's pose (10,931 instances in 297
  groups at the vista, 6,453 in 287 in the forest) are traced through their group's own BLAS, which has no micromaps
  and never had ("they are made for a still mesh"). The 456 meshes that had micromaps at 22:46 served 461 instances:
  the still cut-outs (fences, far trees, props, straw). Section 7 has the idea that would give the plants micromaps.
- **`flush`, +2.6 ms of the render thread a frame.** It is the time of `Flush()` on the game's D3D11 context in
  `trace_event` (`tracer.cpp`, after the frame's inputs are copied). Nothing in this job touches it and I did not
  find its cause; it is in every scene of the 03:49 and 07:50 runs where it was 0.3 to 0.4 before (the tavern: 0.25
  to 0.44 before, 0.33 to 1.2 after; the forest and vista: 2.4 to 3.9 after). Named for whoever takes it: the frame's
  D3D11 work queued before that call grew (the decal capture and the light job's harness counters are the plugin-side
  changes of that night).
- **`record`**: the three scratch buffers made anew every frame (section 1.3) are in it, and the decals' records.
  The GPU test's field of grass shows the first: 0.73 ms standing still, 3.68 ms in the wind on base52.
- **Not slower:** bounced light, the spatial passes, caustics, composite, the water's reflections.

## 4. Tests

### 4.1 The base (base52 as it is), the whole GPU test once: `scratchpad\spd2\out\full-base.txt`
08:10 to 08:30, two other GPU tests running at its start and none at its end; 27,913 frames traced, 0 failed, last
error 0: a valid run. 1427 ok, **5 FAIL, all of the base**:

```
FAIL: coloured or white bulk slab retains its diffuse colour under even white light within 3.5 percent
FAIL: glass bends light by its material's own index of refraction (another index lays the light elsewhere)
FAIL: light through green glass reaches path vertices green (bounced light)
FAIL: every mesh with alpha-tested instances has opacity micromaps          (2 meshes, 3 expected: section 2.2)
FAIL: grey glass lets light through as bright as its color (its shadow about 0.64 of a solid one's)
```

Its figures for the scene stage:
- 128 characters: **9.34 ms** for them (27.63 ms a frame with them, 18.30 without; tenth quantile of 120 frames).
- The field of grass, 41 groups: standing still 0.51 ms; **in the wind 3.68 ms** of the GPU, and the render thread's
  recording 0.73 ms still, **3.68 ms** in the wind.

### 4.2 This job's build (`spd2-dll-f.dll`, SHA-256 starts A1AD812602A3)

**The new micromap checks alone** (`TGPT_TEST_MICROMAPS_ONLY=1`, `out\omm-f.txt`, 08:41 to 08:45, two other GPU
tests running at its start): **passed, 0 FAIL.**

```
cut-outs of the game's own texture: micromaps on 5 meshes in all, 0 waiting, 3 instances tracing with them
micromaps off against on, largest difference of any pixel (lit frames and the camera's view), and the cut-outs' pixels seen:
  discs 0.00000 (39746 pixels; instances with micromaps 0 off, 3 on); cutoff 0.3 0.00000 (44395; 3 after the change);
  the other texture 0.00000 (66870; 3 after the change)
a bake of 125,000 triangles from the game's own texture: micro-triangles 2 texels wide, the slowest frame 9.9 ms of the GPU
  (a frame after it 7.9 ms; traces with micromaps: 1); 16 texels wide, 10.0 ms (8.4 ms; 1)
```

- The three cut-outs get micromaps from the shared texture and trace with them (on base52: none).
- Off against on: not one pixel differs, in 16 lit frames (the sun's shadows through the cut-outs, one of them a
  leaf that lets light through) and in the camera's own view, for the first cutoff, after the cutoff changed, and
  after the texture was replaced by another object. Picture: `%TEMP%\tgb\spd2-images-omm-f\74-cutout-discs-on.png`
  (three cards of discs over a floor, their shadows under them).
- **The bake costs about 2 ms of one frame for 125,000 triangles** (9.9 against 7.9 ms; 1.6 ms through the bounds).
  A frame's budget is 262,144 triangles: about 4 ms in the frames after a scene loads. The vista's 786,689
  triangles of 2026-10-07: three or four frames, some 12 ms of GPU time in all.

**The memory job's clear check alone** (`TGPT_TEST_MEMORY_ONLY=1`): the micromaps' arrays and BLASes go with the
scene clear (`out\mem-g.txt`: "memory clear bytes: ... geometry 16777216 -> 0, acceleration 17825792 -> 0, records
1507328 -> 0, plants 0 -> 0, scratch 1048576 -> 0", with micromaps on 5 meshes before it). That mode had one FAIL
of the base's own (`out\mem-base.txt`, base52's DLL and test: "unused mesh retired while the other meshes stay
ready"): its count was pinned at 3 from before the decals' three meshes were registered. Corrected in the test to
"one fewer than before" ("meshes ready: 7 before the cube's release, 6 after"); with that the mode passes, 69 ok.

**The whole GPU test, first time** (`out\full-f.txt`, 08:55 to 09:29; **not alone: 2 other GPU tests at its start,
3 at its end**, so its times are worth little): 28,182 frames traced, 0 failed, last error 0: a valid run.
1436 ok, 6 FAIL:

```
FAIL: coloured or white bulk slab retains its diffuse colour under even white light within 3.5 percent      (the base's)
FAIL: glass bends light by its material's own index of refraction (another index lays the light elsewhere)  (the base's)
FAIL: light through green glass reaches path vertices green (bounced light)                                 (the base's)
FAIL: grey glass lets light through as bright as its color (its shadow about 0.64 of a solid one's)         (the base's)
FAIL: the caustics and their lookup stay affordable where 25 lamps light a lake (under 10 ms here; ...)     (a time, on a shared GPU)
FAIL: every mesh with alpha-tested instances has opacity micromaps                                          (4 meshes, the check said 3)
```

- The caustics check is a time limit: "the caustics stage 3.700 ms, with the composite 11.360 ms; with the option
  off 0.290 ms" against the base run's "1.790, 4.960, 0.140" on a GPU it had to itself then: everything twice as
  slow, the stage this job never touches and the empty stage alike. Three other tests shared the GPU.
- The micromap count: the check counted exactly 3 meshes (the quad, the card, the cube). Now there are 4: the
  cylinder too. Its first alpha-tested look is glass, which can have no micromaps; in base52 that set the mesh to
  "none" for good, and its later alpha-tested look (wax) traced without. That is the fault of section 6 fixed, so
  the check's number was corrected to 4 after this run, with the reason beside it. "Instances tracing with
  micromaps: 0 off, 2 on; image difference: mean 0.000000, largest 0.00000" as before.
- Its scene stage figures (on the shared GPU): 128 characters "-0.46 ms for them (1.50 by the medians, 0.08 by the
  fastest frames)" against the base's 9.34; the field of grass in the wind **0.70 ms** against the base's 3.68.
- Scratch, from this run's native log (the new line): characters, all 128 built in one frame: **2.0 MB side by
  side** (the largest single one 0.03 MB), a 2.5 MB buffer; the 41 plant groups' first builds 17.3 MB (the largest
  0.97 MB), a 21.6 MB buffer; a swaying cloth under 0.1 MB.

New checks in `tests/pathtracing_gpu_test.cpp` (alone: `TGPT_TEST_MICROMAPS_ONLY=1`): three cut-out meshes of their
own whose material reads a shared texture (as the game's do), with tiling and offset; they must get micromaps and
trace with them; the picture with micromaps off and on must be the same pixel for pixel (16 lit frames with sun
shadows through the cut-outs, and the camera's own view); then another cutoff, then another texture object in the
material (what mip streaming does): micromaps again, the same picture again. This check fails on base52 (no
micromaps for a shared texture).

**The whole GPU test, second time** (`out\full-h.txt`, the final test exe, started 09:34): **VOID.** Frames failed
("Interop fence woke before its value 0x887A0006" in its native log, GPU frames of 284 to 292 ms with other
workers' whole runs beside it). Stopped at line 1031. Its FAIL lines (the water's reflections, lamps off water,
hair, the yardstick and others) all passed in the first run of the same DLL.

**The whole GPU test, third time**: started 10:04, `out\full2-h.txt` (see the state at the top).

### 4.3 The picture: the same?
- The test's own picture checks give the same numbers in the base run and this job's (the swaying sheet's outline
  0.001 px, the canopy's shadow 0.0000 / 0.2519 / 0.0474, the red canopy 0.27; micromaps off against on 0.00000).
- The pictures the two runs saved (`%TEMP%\tgb\spd2-images-full-base` against `spd2-images-full-f`;
  `python scratchpad\spd2\pics.py <a> <b>`): **117 of 130 are byte for byte the same.** 13 differ: 13 and 14
  (micromaps off, on), 27, 29, 30, 37, 38, 60, 61, 62, 63, 65, 67. By how much: 8 of them by 1 level of 255 at most
  on under 1 % of the pixels; 29 by up to 7 levels on 2.6 %; the mirror pictures 60, 61, 62, 67 by up to 4 or 5
  levels on 7 to 8 %; mean difference 0.04 levels at most. Looked at (x60, `%TEMP%\tgb\spd2-diff\`): scattered
  speckles on the room's plain walls, nothing along a cut-out's edge or a shadow's.
- **Not settled:** the test now runs frames it did not run before (the new block, and 24 frames of the room before
  the old micromap check), and the room's radiance cache carries on from frame to frame, so later pictures may
  differ for that alone. Or it is the DLL (a mesh with micromaps is traced through its second BLAS, another tree
  over the same triangles). To tell: run the final test exe with the BASE DLL once
  (`run.ps1 -Name basedll -Tags base -Exe h`) and compare its pictures with `spd2-images-full2-h`: the same 13
  differing again means the DLL; all the same means the test's extra frames. And compare `spd2-images-full-f` with
  `spd2-images-full2-h` (the same DLL twice): differences there are the test's own run-to-run noise.

## 5. Files changed (against base52; `python tools/agents/merge_copy.py spd2 --base=base52 --root=<scratchpad> --dry`)

| File | Lines | What |
|---|---|---|
| `native/pathtracing/src/scene.cpp` | +241 -74 | `scratch_place`, `fit_scratch` and the four build loops (`build_blas` and the new meshes' scratch in `record`, `sway`, `pose_plants`, the characters in `record`); the scratch's shrink check after `++swayFrame_`; the signatures; `shared_cutout`, `omm_possible`, `want_omm`, `omm_matches`, `omm_stale`, `release_omm`, `omm_instance`; the two instance loops' one line each; `record_omm` (the bounds, the bake's constants); `retire_meshes` (the budget) |
| `native/pathtracing/src/scene.hpp` | +32 -7 | `Mesh::ommArrayVa`, `ommUsedFrame`, `skinUpdateBytes`; `SwayCopy::updateBytes`; `PlantGroup::updateBytes`; `kOmmUnusedFrames`; `GpuScene::sharedTextures` (public), the new private methods, three scratch peaks, `sharedLast_` |
| `native/pathtracing/src/tracer.cpp` | +3 -1 | `st.scene.sharedTextures=...` beside `ommAllowed`; a comment |
| `native/pathtracing/src/texture_share.cpp`, `.hpp` | +11 | `share::dimensions(number, width, height)` |
| `native/pathtracing/shaders/pt_omm_bake.hlsl` | +154 -14 | the bounds' two steps, `own_state`, the mesh by its record; the tile path as it was. **Only this pass is compiled again** (no shared header touched) |
| `tests/pathtracing_gpu_test.cpp` | +169 | the new block after the light job's cut-out fixture (`TGPT_TEST_MICROMAPS_ONLY`); 24 frames of the room before the old micromap check |
| `tests/source_checks.py` | +28 -7 | the five quoted lines that changed, and the new rules |
| `docs/agents/speed-2.md` | new | this report |

No export, no struct of `tgpt_api.h`, no option bit, no ABI number. The bake pass's eight constants changed their
meaning (the mesh's handle and the bounds where the two buffer views were): the pass and `scene.cpp` go together,
as they are built together. `CHANGELOG.md`, `README.md`, `docs/TESTING.md`: not edited (the coordinator files them;
section 8 has the text).

## 6. Workarounds and faults met

- **Removed:** micromaps switched off for every cut-out that reads the game's own texture (`want_omm`,
  `omm_matches`). It existed because the bake read the texture copies; the bake now reads what the test reads.
- **Removed:** one piece of scratch for all of a frame's builds with a barrier after each (the memory job's way of
  saving scratch). Replaced by scratch sized for a frame's builds.
- **Fixed, found on the way:**
  - the three scratch buffers were made anew every frame (section 1.3);
  - the micromap budget only grew (a retired mesh never gave its share back);
  - the first alpha-tested instance of a mesh that could NOT have micromaps (a camera-only shell, glass, hair) set
    the mesh to "none" for good, whatever instances came after;
  - hair drawn as a layer (`TGPT_MATERIAL_HAIR_LAYER`) could get micromaps, though `alpha_opaque` lets every
    camera ray but the tracer's own first pass it whole: a micromap's "covered" would have stopped those rays. In
    the game such materials are on characters, whose meshes get no micromaps anyway; excluded now for the rest.
  - the offline micromap check hung on which earlier check had used the card mesh last (section 2.2).
- **Still there, named:**
  - The micromap bake and the alpha test of the materials that still read tiles (a texture not shared, hair) are the
    texture copies' path, unchanged: the copies are PT-45's subject.
  - A mesh has micromaps for ONE set of materials. Instances of the same mesh with other materials trace without
    (as before 2026-10-08). Now at least the set follows what is in use.
  - A mesh with micromaps keeps two BLASes (its first one, and the one linked to the micromaps). Every instance
    uses the second once it exists, so the first could go: memory, not speed; not done.
  - Swaying cloth's copies and the plants' groups have no micromaps (section 7).
  - `kOmmBudget` (256 MB of micromap input for the scene) and `kOmmTrianglesPerFrame` are budgets of speed and
    memory: a mesh over the budget keeps the alpha test alone, the picture is the same.

## 7. What is left, and what would pay more

1. **Micromaps for the plants in the wind: the fast path where the rays are.** Outdoors nearly every alpha-tested
   triangle is in a plant group (5.1 M triangles at the vista), and a group's BLAS has no micromaps. A micromap
   lives in a triangle's texture coordinates, which the wind does not move, so nothing about a refit forbids it.
   A group's geometries are its members' submeshes, one each, their triangles in the member's own order
   (`PlantRangeGpu`), so each could link to its SOURCE mesh's micromap array and index buffer (DXR links an array a
   geometry): no micromap data per group, only the still meshes' as today. It needs: the source meshes' micromaps
   asked for from `plan_plants` (today only a still instance asks), their index buffers kept (4 bytes a triangle;
   they are dropped after the BLAS build now), the group's BLAS built with the linkage and
   `ALLOW_DISABLE_OMMS`, and a group made anew when a source's micromaps come or go. It is the plants job's code
   (`plan_plants`, `pose_plants`), a job of its own. Not started.
2. **The plants' refits are real work in real play.** Even side by side, 300 to 440 groups (5 M triangles) are
   refitted every frame the wind's clock runs. The harness never shows it (it freezes the world). Section 4 has the
   offline figure. If it is too much, the honest levers are the refit itself (the driver's cost) and how many
   triangles a group holds, not skipping groups.
3. **`flush` on the render thread** (section 3): +2.6 ms a frame, cause not found.
4. The whole frame's large numbers are the rays' (bounced light 38 to 42 ms and reflections 26 ms in the forest):
   the speed job's report (`docs/agents/speed.md`) and the who-walks job cover those.

## 8. For the changelog (the coordinator's to file)

> **TG Path Tracing: the scene stage back under a millisecond, and opacity micromaps back.** Since the memory work of
> 2026-10-08 every character, plant group and swaying cloth was refitted one after another over one piece of
> scratch memory, with a barrier after each: 9 to 14 ms of every frame with 59 to 82 characters (128 characters: 9.3
> ms in the GPU test, 0.3 to 0.5 before), and the scratch buffers were made anew every frame. A frame's builds are
> side by side again, each asking for what it needs (a refit far less than a build), in a buffer of at most 32 MB a
> stage. And opacity micromaps, switched off on 2026-10-08 for every cut-out that reads the game's own texture
> (every cut-out in the game), are baked from that texture now: the same texels, tiling and cutoff the alpha test
> reads, so a micromap only answers early what the test would answer. They are made anew when mip streaming
> replaces the texture. No picture change. (Numbers: `docs/agents/speed-2.md`.)
