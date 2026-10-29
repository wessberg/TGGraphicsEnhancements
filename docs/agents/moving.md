# Moving agent: things the game moves are traced where they are this frame (PT-71 item 4)

Copy: `<scratch>/wt/moving` (base `wt/base39`, ABI 0x00012000). 2026-10-07. Nothing here ran in the game: every "in game" line is
what the coordinator's run must show (section 6). `<scratch>/moving-work/` holds the scripts, the decompiled `Awaken.ECS.dll`
(`ecs/`), the logs and the mutants named below.

## 0. State

| Part | State | Proof |
|---|---|---|
| 1 Every moved thing at its place, this frame; what appears or disappears the same frame | built | GPU checks (section 5): GPU_STATE; rules test 11 mutants killed; plugin compiles; **not seen in game** |
| 2 Each moved thing's previous transform, its own motion | built | the GPU check against HDRP-style object motion vectors: GPU_STATE |
| 3 The first-person view | read from the game's code: one camera, one projection (section 3) | nothing to build; the in-game yardstick line checks it |
| 4 `TGPT_INSTANCE_MOVING` | deleted (API, plugin, shaders, tests, docs) | source checks: 91 pass, the new one's 5 mutants killed |

What the coordinator should know first:

1. **ABI 0x00012100 in this copy** (main is 0x00012000): one new export `TGPT_SetMoving`, one new struct `TGPT_Moving` (112 B:
   a `TGPT_Instance`'s 64 bytes then the previous transform). `TGPT_INSTANCE_MOVING` (16) is gone. No struct that existed
   changed; no option bit, material bit or `TGPT_Frame` field taken. Inside the tracer: `InstanceRecord.pad` is now
   `previous` (every record writes `kInvalid` there but the moving things'), and `FrameConstants.primary.w` (was unused)
   is the moving things' previous-transform rows. `TGPT_Status` and `TGPT_Yardstick` unchanged (the yardstick's kind 3,
   "moving", now means "placed this frame").
2. **A frame is now traced with the scene list it was submitted with.** `TGPT_SetScene` makes a new `SceneList` each call,
   `TGPT_SubmitFrame` hands the newest to the frame's slot, and the render thread takes the slot's (it took whatever was
   newest when it traced, which a scan between submit and trace could swap under the frame). The moving things' "stand in
   for" indices name that list, so it had to be exact. `Engine::instances/materialIndices/instancesDirty` are gone.
3. **Every GameObject mesh renderer that is not statically batched is now placed every frame and never listed by the
   scan** (the harness scenes trace 0 to 22 of them), and so are Drake's movable entities (no `Static` tag). The scan lists
   everything else; an entity it listed that moves (a windmill's blades, a bird, an elevator's chain: the game tags those
   `Static` too) is found the frame it moves and placed every frame from then on.
4. Their glow is met by paths and not sampled as a light (as characters' and posed plants'): noise, not light. Section 8.

## 1. What moves in this game, read from its code

Decompiled with `ilspycmd` (`Awaken.ECS.dll` into `<scratch>/moving-work/ecs`; `TG.Main.dll` was already in `<scratch>/tgmain`).

- **Drake entities the game made movable.** `DrakeRendererManager.Register`: a renderer that is not static gets no `Static`
  tag and a `LinkedTransformComponent` (`DrakeRendererEntitiesManager.CreateArchetype`); `LinkedTransformSystem`
  (PresentationSystemGroup, before Entities Graphics draws) copies its GameObject's `Transform.localToWorldMatrix` into its
  `LocalToWorld` every frame in a job. The game makes things movable through `IWithUnityRepresentation.Options.movable`
  (`DrakeMeshRenderer.SetUnityRepresentation` -> `SetStatic(!movable)`): the weapon in a hand (`ItemEquip`, the hero's and
  `NpcWeaponsHandler`'s), placed items (`ItemPlaceLocation`), instruments, cutscene props, lock picks, plants in plots,
  dug-out things, pooled instances.
- **Hiding and showing** a weapon is `gameObject.SetActive` (`CharacterHandBase.HideWeapon`), which `LinkedEntityLifetime`
  turns into `EntityManager.SetEnabled`: the entities leave every query (and come back) the same frame. Destroying the
  GameObject destroys them.
- **Drake entities tagged `Static` that move all the same**: `RotateTransformSystem` turns every entity with a
  `RotateTransformComponent` every frame (its baking step sets `gameObject.isStatic = true`); the flocks' birds
  (`FlockEntity`/`FlockGroup` register them `movable = false`; `SyncDrakeEntitiesTransformsSystem` writes their
  `LocalToWorld` through a lookup); the elevator's chains (`MoveChainsJob`). So the `Static` tag does not mean "still".
  Every system of the game that writes `LocalToWorld` was looked for (`ComponentLookup<LocalToWorld>`, read-write type
  handles): these four, and nothing in `TG.Main.dll`.
- **Characters, their clothes, the first-person body**: Kandra's GPU skinning, posed every frame already (`CharacterCapture`).
- **GameObject mesh renderers**: rare in this game (0 to 22 traced in the harness scenes); any script may move them.
- Still by construction: Medusa's and Leshy's baked matrices, Vegetation Studio's, terrain, water (the world agent's).

## 2. What was built

- **Plugin** (`SceneCapture.Follow`, called in `HdrpPasses` right after `CharacterCapture.Capture` and before
  `TGPT_SubmitFrame`, so what is placed is what HDRP draws in that frame):
  - GameObject mesh renderers that are not statically batched: the scan keeps each one it finds (its mesh, its materials,
    the LOD groups); every frame each is read again: destroyed, disabled, inactive, forced off, on a layer the camera leaves
    out, not the drawn level of its LOD group (`SkipUndrawnLevels`, the scan's rule, now shared), and its matrix.
  - ECS: `CaptureDrake` and `CaptureEntities` take a `follow` flag and the same rules (layer, shadow-only, loaded for
    drawing, the drawn level of detail): the scan takes every chunk but those placed every frame, `Follow` takes those.
    A chunk is placed every frame when it is Drake's and has no `Static` tag, or when it was found moving
    (`MovingRules.PlacedEveryFrame`). Found moving: each frame, every listed chunk whose `LocalToWorld` some system wrote
    since the last frame (`ArchetypeChunk.DidChange` against `EntityManager.GlobalSystemVersion`) is compared value by
    value with what the scan listed (`MovedSinceListed`); the first entity that differs puts the chunk (by
    `SequenceNumber`) in `movingChunks`. Its listed instances stand in for nothing from then on (`StandInFor`: sent as
    `replaced`), and the next scan leaves the chunk out. A chunk the game merely rewrites with the same numbers is not
    found moving (`MovingRules.SameTransform` is exact: a windmill turns by less than a thousandth of a radian a frame).
  - Each placed thing has an identity (`MovingRules.EntityIdentity`: the lowest entity of its mesh-material pairs;
    `RendererIdentity`: its renderer, as a negative number no entity has), and `MovingRules.Previous` keeps, by identity,
    the transform it had the frame before; a thing that appears, or was hidden for a frame, starts without motion.
  - A placed thing's glow keeps its "kept light's own surface" mark by its identity (`MoverSurfaceKey`), and the scan
    hands its glows to `SceneLights` with the still ones (`Glows`).
  - New materials a placed thing brings are uploaded in the same call; a frame whose materials could not be uploaded
    places nothing rather than things with materials the tracer does not have.
  - The log (`Summary`, every 10 s): "moving: N things placed this frame where HDRP draws them (C moved since the frame
    before; R GameObject renderers, the rest entities; K listed chunks found moving, F so far), S of the scan's instances
    stood in for, L renderers the camera does not draw; the oldest traced transform of a moving thing 0 frames old; X ms
    this frame, at most Y ms". The yardstick's line (also every 10 s) gains "things the game moves P % of the screen, of
    their pixels the same surface as drawn A %, nearer B %, farther C %".
- **Native** (`TGPT_SetMoving`, `extension.cpp`; `GpuScene::set_lists`, `record`, `scene.cpp`): the moving list rides in
  the frame's slot with the scene list. Each moving thing is an instance of its mesh's own BLAS (or its micromapped one,
  where its materials match) in the TLAS, which is built every frame anyway: no mesh is built or refitted for it. Its
  material indices follow the scene list's in the frame's index list. The scene instances it stands in for keep their
  record (so every record keeps its number, which the emitters' table goes by) and lose their place in the TLAS; their
  glow leaves the emitters (rebuilt when that set of glowing instances changes).
- **Motion** (`pt_primary.hlsli hit_motion`): a moving thing's hit point is read in its mesh's own space (the triangle's
  positions at the hit's barycentrics) and taken to the world by the transform it had a frame before (its row in
  `primary.w`); through the previous view-projection that gives its motion as HDRP's motion vectors give it.
- **The yardstick's kind 3** ("moving") is now the instances with a previous-transform row.

## 3. The first-person view

How the game draws the hero's hands, weapon and shield, read from `TG.Main.dll`:

- **One camera.** No second camera, camera stack, custom pass or shader of the game's draws them; the main camera draws
  them in its own G-buffer pass with the world. The field of view is the Cinemachine virtual camera's
  (`VHeroController.SetVirtualCameraFoV`, from `HeroFoV.FoV`: the FOV setting times sprint, dash, slide, bow zoom, the
  spyglass and slow time), which is the main camera's, for hands and world alike. `resources.assets` holds no
  first-person field-of-view or near-plane value (searched: only animation names hold "FPP"). The near plane is the
  camera's.
- **The hands and body** are the first-person body (`maleHeroBodyFPP`/`femaleHeroBodyFPP`), instantiated under
  `fppParent` (at the camera's height, `_defaultFPPHeadZPosition` ahead; `HeroCamera` turns it with the camera). It is a
  Kandra renderer: posed every frame by `CharacterCapture` already, as the game skins it (world space).
- **The weapon and shield** are `CharacterHandBase` prefabs equipped by `ItemEquip` with `movable = true`: Drake entities
  with `LinkedTransformComponent`s that follow the hand's bones every frame (plus Kandra parts where a weapon has them).
  Until now they were traced where the last scan found them (up to 2 s old, swinging the other way); now they are placed
  every frame.
- So the camera rays already show them where the game does: the rays use HDRP's own view-projection for every pixel and
  start at its near plane, and the geometry is where the game has it. Nothing was built for this part.
- What would prove it in game: the yardstick's line in first person with a weapon drawn (section 6): its pixels "the same
  surface as drawn" as the world's are. If the game ever pulled the hands toward the camera (some games do, against walls)
  the yardstick's "nearer/farther" share at the hands would show it.

## 4. What it costs

- **Native, offline** (RTX 5080, the GPU shared with other agents' tests and the owner's game, so the numbers are rough):
  GPU_COST
- **Plugin**: the bookkeeping a placed thing costs (`<scratch>/moving-work/bench`, .NET Framework's JIT, the previous
  transform, the 112-byte record, its indices, the listed-entity lookup, the still chunks' test): 0.11 to 0.12 ms a frame
  for 2,000 things, 1.2 to 1.3 ms for 20,000. The game's Mono is slower (about twice), and on top come the ECS reads (a
  chunk's arrays, about 50 ns a chunk for the still world's chunks: their `Static` tag, the set of moving chunks, one
  change-version test) and, for GameObject renderers, about six Unity calls each (enabled, active, layer, shadow mode,
  matrix). The still world costs a chunk test, not an entity test: 13,000 listed entities in some 300 chunks are a few
  tens of microseconds. The log line's "X ms this frame, at most Y ms" is the true number in game.
- 2,000 and 20,000 are far past what the game moves at once: weapons (a few per character in view), items, doors,
  windmills, a flock. The vista's scan holds 22 GameObject renderers.

## 5. Tests

- **GPU** (`tests/pathtracing_gpu_test.cpp`, the block `movingChecks`, run after the warm-up and alone with
  `TGPT_TEST_MOVING_ONLY=1`; also at the end of the full run). The test's own raster pass now writes an object's own
  motion vectors (a previous world matrix per object, the world matrix when it stood still), as HDRP does for a moving
  entity (`unity_MatrixPreviousM`). With the tracer's own camera rays and no denoiser, against the yardstick:
  1. A red box HDRP draws 2.3 m from where the scan's list holds it; `TGPT_SetMoving` places it where it is drawn and names
     the list's copy: its pixels are the moving kind's and "the same surface as drawn", and the list's copy adds no
     "nearer" pixels where it was.
  2. The box 10 cm on in a frame: its pixels' own motion is the drawn motion vectors' (both from its previous transform).
  3. Hidden (a sheathed weapon): neither it nor the list's copy is traced.
  4. The cost of 2,000 and 20,000 things placed every frame (printed, judged only for every one being traced).
  And the emissive view's check 7 now places its "moving" quad through `TGPT_SetMoving` (met, not sampled: red).
  GPU_RESULTS
- **GPU mutants** (`<scratch>/moving-work/build_mutants.py`, `moving-mutants.ps1`; each run alone and stopped at its first
  FAIL): GPU_MUTANTS
- **Rules** (`tests/moving_rules_tests.cs`, `tests/MovingRules.Tests.csproj`): 23 expectations, each worked out by hand from
  the game's code. Mutation run (`<scratch>/moving-work/mutate_rules.py`, log `mutate_rules.log`): 11 mutants, all
  killed at their own FAIL line (a tolerance hiding small moves, the translation not compared, Drake's movable entities
  listed, every entity without `Static` placed, entities found moving still listed, an entity's version left out of its
  identity, renderers in the entities' range, a new thing moving from nothing, the first frame's transform kept, a thing
  hidden for a frame keeping its old transform, an instance stood in for twice).
- **Source checks**: 91 pass. New: `test_path_tracing_moving_things_traced_where_they_are`. Pins moved with the code in
  `test_path_tracing_abi` (0x00012100), `_emissive_lights` (the flag gone; the emitters skip what a moving thing stands in
  for), `_entities_graphics`, `_exposure`, `_passes_made_while_the_game_starts` (micromaps' material pointer),
  `_world_is_what_the_game_draws`. The new check's mutation run (`<scratch>/moving-work/mutate_checks.py`): 5 mutants
  killed (the moving things placed after the frame is handed over, the flag back, a moving thing's motion the camera's
  alone, the scan listing GameObject renderers again, a frame traced with the newest list rather than its own).
- **Plugin**: `dotnet build src/TGPathTracing -c Release -o %TEMP%\tgb\moving-plugin`: 0 errors, 0 warnings.
- **Native**: `TGPathTracing.Native` and `tgpt_gpu_test` build (`%TEMP%\tgb\moving-b`, Ninja, `-DTGPT_WITH_NRD=OFF`).

## 6. The one in-game scene that proves it

**First person with the weapon drawn, swinging, in a place with a still world around** (any save: the coordinator's
`tavern` or `vista` scenario is fine). Recipe: load it, path tracing on, Denoising None, draw the weapon, swing it a few
times and walk; then sheathe it; wait for two "Path tracing:" report lines (10 s apart).

The log must show:
- the plugin's report line (`Path tracing: ... | scene: ...`): "moving: N things placed this frame where HDRP draws them
  (C moved since the frame before; ...)" with N at least the weapon's renderers while it is drawn and C > 0 while it
  swings; "the oldest traced transform of a moving thing 0 frames old"; "X ms this frame, at most Y ms" (the cost);
- the native status line: "moving: N placed this frame, H hidden, S of the scene's instances stood in for";
- the yardstick line (`Yardstick 10s`): "things the game moves P % of the screen, of their pixels the same surface as
  drawn A %" with A as high as the still world's "the same surface" share, and the screen's "its own motion more than a
  pixel from the game's" share no higher while swinging than standing still.
- After sheathing: N drops by the weapon's renderers the same report.
The picture (Denoising None): the blade's shadow and reflection on the floor follow the blade, not a blade 2 s behind.

## 7. Files

- New: `src/TGPathTracing/MovingRules.cs`, `tests/moving_rules_tests.cs`, `tests/MovingRules.Tests.csproj`, this report.
- Managed: `SceneCapture.cs` (the follow pass, the scan leaving placed things out, the listed entities, `PlaceMesh`,
  `Place`, `SkipUndrawnLevels`, glow keys, the summary, the yardstick's moving line; `Moved`/`placed` deleted),
  `NativeTracer.cs` (`NativeMoving`, `SetMoving`, the ABI, `InstanceMoving` deleted), `HdrpPasses.cs` (one call),
  `Session.cs` (one call after the report: the "at most" resets).
- Native: `include/tgpt_api.h`, `src/runtime.hpp` (`SceneList`, `MovingList`, the slot's lists), `src/extension.cpp`
  (`TGPT_SetMoving`, the lists into the slot), `src/scene.hpp`/`scene.cpp` (`set_lists`, the replaced set, the moving
  instances and their previous rows, the micromap helpers taking the instance's material list), `src/tracer.cpp` (the
  slot's lists, `primary.w`, the status line), `shaders/pt_common.hlsli` (`InstanceRecord.previous`, `INSTANCE_MOVING`
  gone), `shaders/pt_primary.hlsli` (`hit_motion`, the yardstick's kind).
- Tests and docs: `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`, `docs/TESTING.md`, `docs/PT-REQUESTS.md`
  (PT-71 item 4), `docs/PROJECT-CONTEXT.md` (two lines), `CHANGELOG.md`.

## 8. Gaps, risks and ideas

1. **A moving thing's glow is met by paths, not sampled as a light** (as characters' and posed plants'): the same light,
   more noise on what it lights. Sampling it needs its emitter records rebuilt where it is each frame (the emitters are
   keyed by transform and rebuilt only when the scene changes). An idea: give moving glows emitter slots of their own,
   rewritten every frame from their transform, outside the cached build.
2. **New GameObject mesh renderers** that appear between two scans (a script instantiating a prop) are found by the next
   scan (0.5 to 2 s), not the same frame: Unity has no cheap way to list new renderers each frame. The game spawns its
   movable things through Drake (found the same frame); the harness scenes trace 0 to 22 GameObject renderers.
3. **Entities Graphics' own (non-Drake) entities that appear or disappear between scans** wait for the scan too (the
   castle's, which nothing in the game moves). Drake's static world's loading and unloading as the camera moves is the
   world agent's level-of-detail part (the scan, 0.5 s after 1 m).
4. **A moving cloth or plant** is placed with its mesh's own BLAS, not posed by the wind (`pt_sway` works on the scene
   list's instances); a plant the scan listed that is found moving is still grouped at its listed place by `plan_plants`.
   Neither is known to exist in the game.
5. **The previous transform is per identity**: a Drake renderer whose lowest entity changes (another level of detail drawn)
   starts that frame without motion (one frame of the camera's motion alone).
6. Not measured in game: whether `LocalToWorld` at HDRP's render time is the one Entities Graphics drew with (it should be:
   `LinkedTransformSystem` runs before `EntitiesGraphicsSystem` in PresentationSystemGroup). The yardstick line says.
7. Ideas: read GameObject renderers' matrices with a `TransformAccessArray` job when a scene has thousands; send only the
   things that changed (the native side would then keep the rest; today each frame's list is whole, which is what makes a
   dropped frame harmless).

## 9. Workarounds met that exist only because the tracer started from HDRP's picture (or from a scan)

| # | Workaround | Where | State |
|---|---|---|---|
| 1 | Renderers that moved since the last scan flagged "moving", so their glow was not sampled where the scan left it | `tgpt_api.h TGPT_INSTANCE_MOVING`; `NativeTracer.cs InstanceMoving`; `SceneCapture.cs Moved`, `placed`; `scene.cpp build_emitters`; `pt_common.hlsli INSTANCE_MOVING`; `pt_primary.hlsli` (the yardstick's kind); the GPU test's flagged quad; three source checks; PROJECT-CONTEXT | **Removed**: what moves is placed where it is. |
| 2 | A hit's motion taken as still in the world for anything but a character | `pt_primary.hlsli hit_motion` | **Removed** for what the game moves (its previous transform). Still so for what the wind sways (the world agent's gap 4). |
| 3 | A frame traced with whatever scene list was newest when the render thread got to it | `scene.cpp intake`, `Engine::instancesDirty` | **Removed**: the frame's own list. |
| 4 | Shadow-only meshes as stand-ins while Entities Graphics cannot be read (`standIns`) | `SceneCapture.cs`, `TGPT_INSTANCE_STAND_IN` | **Not removed** (the world agent's #7); the follow pass keeps the same rule for the GameObject renderers it places. |
| 5 | The first-person body passed by camera rays within 30 cm of the camera (`MATERIAL_FIRST_PERSON`, `FIRST_PERSON_HEAD`) | `pt_primary.hlsli primary_walk`, `pt_common.hlsli` | **Not removed** (the mirror agent's; its report says the constant goes, base39 still has it). Likely dead with the camera rays' back-face rule (the game's first-person body is one-sided): the yardstick at the hands in first person decides. |
| 6 | The switch back to the old route (`OwnCameraRays`, `TGPT_OPTION_GBUFFER_SURFACES`, `primary_pixel`) | `Settings.cs`, `HdrpPasses.cs`, `pt_primary.hlsli` | **Not removed** (the camera-ray agent's; it goes once nothing needs it). |
| 7 | The distrust of swaying things (`MASK_SWAYS`, `SWAY_REACH`, `TGPT_INSTANCE_SWAYS`) | `pt_common.hlsli`, `scene.cpp` | **Not removed** (the world agent's #4: cloth past 4,096 copies). Moving things get the same mask rule as listed ones. |
| 8 | Rays from HDRP's surfaces passing out of a mesh close by (`near_back`, `TGPT_OPTION_NO_PASS_THROUGH`) | `pt_common.hlsli` and the passes | **Not removed** (the camera-ray agent's). |
