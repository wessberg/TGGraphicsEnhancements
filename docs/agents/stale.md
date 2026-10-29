# stale: what else keeps a number or a view that can go stale (PT-125's kind of fault)

Worker "stale", 2026-10-12. Copy: `%TEMP%\tg-agents\wt\stale` (base `base20m`). Brief: `docs/agents/briefs/brief-stale.md`.
An audit BY READING. Nothing here was run on the GPU: Starfield was open the whole time (`-GpuCheck` exit 1).

## The answer in five lines

- One more fault of PT-125's kind can reset the GPU: a mesh decal's own top level keeps the ADDRESS of its mesh's
  bottom level, and that address moves when the mesh is compacted (F1). Only scenes with mesh decals; the harness's
  scenes have none, so it is not what reset the forest, and it is not PT-129 (that scene had no decals at all then).
- One more spoils the picture: the emitters' table is by this frame's record number and was kept across frames (F2):
  a glow's bounced light doubled or lost for up to 2 s after plants' groups are made.
- One slow leak: a mesh let go on a walk kept one or two descriptor slots for good (F3).
- All three are fixed in this copy: `native/pathtracing/src/scene.cpp` and `scene.hpp`, no shader. Type-checked with the
  main build's flags, and held by one new source check whose 8 mutants are all caught; not built into a DLL, not
  run, no GPU fixture.
- Everything else read is right, and is listed below with the reason. PT-129 is not explained by anything found here.

## State when stopped

(the newest line first)
- DONE as far as reading goes. F1, F2, F3 fixed in the copy; the four sources that include the changed header pass
  `cl /Zs` with the main build's flags and defines (`%TEMP%\tgb\stale-check\check.cmd scene.cpp scene_water.cpp
  tracer.cpp mega_geometry.cpp`: no error, no warning). One new source check, `test_kept_numbers_cannot_go_stale`
  (PT-125's fix, which had none, and F1 to F3), mutation-checked: 8 mutants, each puts an old line back, all 8 caught
  (`%TEMP%\tgb\stale-check\mutate.py`). `python tests/source_checks.py`: 118 tests OK.
  NOT done: a DLL build, any GPU run, the offline check for F1 (the card was the owner's).
- Read since the first note: the water (`scene_water.cpp` whole, `pt_water_mesh.hlsl`, `pt_waves.hlsli`,
  `pt_water_surface.hlsli`'s reads), the micromap bakes (`record_omm`), the light grid and its far lists, the shared
  textures' registry (`texture_share.cpp`), NRD's tables, every release site of `tracer.cpp`, `fog_froxel.inl`,
  `sky_model.inl`, the fog's reservoirs, the radiance cache, the caustics' room and its readers, the mirrors' records.
- First note. Read: `gpu.hpp` whole, `scene.hpp` whole, `scene.cpp` record / plan_plants / pose_plants / sway /
  build_emitters / upload_decals / clear / compact / retire, `pt_sway.hlsl`, `pt_rtxdi.hlsli`.

## Findings, ranked by harm

### F1 (can reset the GPU; only where a scene has MESH decals) A mesh decal's top level names a bottom level that has moved. FIXED

`scene.cpp` `upload_decals`, `compact_meshes`; `pt_common.hlsli` `apply_decals` (the ray query in `d.kind.w`'s structure).

- What is kept: each mesh decal has a top level of its own (`DecalMesh::tlas`, one instance), built in
  `upload_decals` with `instance.AccelerationStructure = mesh->blasVa`: the ADDRESS of its mesh's bottom level. It was
  built again only when `upload_decals` ran and found the instance changed, and `upload_decals` returns at once
  unless the plugin sent other decals (`TGPT_SetDecals` marks them only when the array differs), a mesh was converted
  in THIS frame, a texture came or went, or the origin moved (256 m steps).
- What moves under it: every still mesh's bottom level is built at full size in the builds' own chunks
  (`buildArena`) and, 2 or 3 frames later, copied compacted to ANOTHER address (`compact_meshes`); the old piece is
  given back at that frame's fence (`retire_structure`). Nothing marked the decals. (A mesh let go gives its
  structure back too, `retire_meshes`; the plugin never lets a decal's mesh go, `SceneCapture.DecalMeshHandle`, so
  that way is closed today, and the fix covers it all the same.)
- The order of events: frame N converts the decal's mesh M (the decals are looked at: the top level names M's
  full-size build at address A). Frame N+k: M is copied to B, A is retired. No mesh is converted in frame N+k (M was
  among the last to arrive, or arrived alone on a walk): `upload_decals` returns early, the top level still says A.
  Once the GPU is past frame N+k, A is free room. A new build laid over it comes in a frame that converts a mesh,
  which looks at the decals again, so that case mends itself. The one that does not: the chunk's last piece leaves
  (the scene has finished arriving), and 120 collects later (one or two a frame: a second or two at 60 frames a
  second) the whole chunk goes back to the driver (`Arena::collect`, `keepEmpty`). From then until the next thing
  that marks the decals, the top level points at memory that is no longer there.
- What the GPU then reads: every hit whose ray crosses that decal's box runs a ray query (`TraceRayInline`) in that
  top level. Its traversal follows the instance into unmapped memory: a fault or a walk with no end, both of which
  DXGI calls "hung". In every pass that shades a hit (the camera's, the bounced light's, the reflections').
  (With the test's switch `TGPT_TEST_MEMORY2_OFF=1`, one arena: the room is taken by any later structure and the
  query walks another mesh's tree or the middle of one.)
- How likely: only scenes with mesh decals (`DecalCapture.cs` sets flag 1024 for a `MeshRenderer` with a decal
  material; the game ships 15 such shaders). The harness's scenes have none: the long log's last 700 "Decal GPU
  inputs" lines all say "0 meshes". So it reset nothing we have measured. Where a scene has them, the window is
  open longest when the world has finished arriving and the camera stands still.
- THE FIX (`scene.cpp` 3834-3848, 3861, 3914; `scene.hpp` 754-758): each top level that an uploaded record names
  keeps its mesh's handle (`DecalMesh::mesh`; 0: no record names it), and `upload_decals` checks every frame, before
  its early way out, that the address the top level was built with is still that mesh's (and that the mesh is there
  and ready). Where it is not, the decals are looked at again in that same frame, after `compact_meshes` and before
  any pass: the top level is built again with the new address, or its record is switched off. A check at use, as
  PT-125's; a loop over the decals, one compare each. No frame is recorded with a top level that names a retired
  address past its fence.

### F2 (spoils the picture; no reset) The emitters' instance table was kept by a frame's record numbers. FIXED

`scene.cpp` `build_emitters` (`emitterTable_`), `record` (the instances' loop); `pt_common.hlsli` `emitter_record`,
`emission_weight`.

- What is kept: `emitterTable_[record]` says, for an instance by its RECORD NUMBER in the frame's instance buffer,
  where its glowing submeshes' light slots start. It was made only when the emitters are built anew (a mesh
  converted, the materials or the scene's list changed, a mesh let go, the glowing things the game moves changed).
- What moves under it: the instance records are laid out anew every frame, one for every listed instance that is
  traced. A plant in the wind has NO record of its own once its group is made (`grouped`), and has one again while
  its group waits to be made anew. Groups are made over many frames (24 a frame by the clock) after the list that
  brought them, and none of that marked the emitters. (The code knew the table is by record: a thing the game moves
  "keeps its record so every other record keeps its number". The plants' groups came later.)
- The order of events: a new scene list arrives (the plugin scans every 0.25 to 2 s; the emitters and the table are
  built, the new plants still ungrouped: each has a record). Over the next frames their groups are made: every still
  instance listed after a grouped plant moves down by one record for each. The table holds the old numbers until
  the next list.
- What the GPU then reads: a path that meets a glowing triangle asks the table at its instance's NEW number and
  gets another instance's entry. Mostly "no emitter": its emission counts whole although the light sampling counts
  it too, so the glow's light on what is near doubles. Or another emitter's slots, whose record may say "a kept
  light's own surface" (the glow counts for nothing) or have another reach. And a record past the still ones (a
  thing the game moves, a group, a character) can fall under the old count and get an entry that was never its own.
  All reads stay inside their buffers or read zeros past a view's end: no reset.
- How it shows: where plants and glowing meshes are in one scene (a torch-lit camp in the forest), the bounced
  light of the glow steps up or down when groups are made and steps back at the next scan: a flicker on a walk.
  Not measured; by reading.
- THE FIX (`scene.cpp` 3437-3465, 3556-3558, 3607; `scene.hpp` 818-824): what the table is made from is kept (each
  emitter's instance by its place in the scene's list, its submesh, its first slot, its mesh's submesh count:
  `emitterPlaces_`), with the frame's records the table was last laid out for (`emitterTableSources_`). Whenever a
  frame's records are not those, the table alone is laid out again for this frame's records and uploaded (4 bytes a
  record): the same emitters, the same slots, the same reservoirs' lights. An emitter whose instance has no record
  any more (it went into a group) is no emitter: the emitters are built anew, as on any change. The number kept is
  now one that cannot go stale (the instance's place in the list, which marks the emitters when it changes).

### F3 (no reset; ends path tracing for the session after a very long walk) A mesh let go kept two of its views. FIXED

`scene.cpp` `retire_meshes`.

- `upload_meshes` gives a mesh a view for its tangents and one for its decal colors (`tangentsSrv`,
  `decalColorsSrv`). `clear` gives them back; `retire_meshes` did not. The plugin lets a mesh go whenever a scan no
  longer sees it and registers it anew when it is seen again (`SceneCapture.cs` 312-321), so every such mesh on a
  walk cost one or two of the heap's 262,144 slots for the rest of the session. When they run out: "The path tracer
  ran out of descriptors", the frame fails while it is recorded, and path tracing stays off for the session.
- By the harness's logs a scene holds about 900 meshes and a few minutes of loads reach handle 6,300: at that rate
  the heap lasts many hours of walking. Slow, but it only ever grows.
- THE FIX (`scene.cpp` 4265-4268): the two views are given back with the rest.

## PT-129 (the hang in the reflections after a second scene): what the log says, and that none of this explains it

Read in `artifacts/harness/20261012-033350/TGPathTracing.Native.log`, lines 34688 to 34735 (the log runs on across
starts):
- The plugin cleared the scene FOUR times in four frames (traced frames 3709 to 3713, "scene=4" to "scene=7").
- The hang was in frame 3776: 63 frames after the last clear, about half a second after "The water's surface: 1602
  pieces ... made this frame", "4 of the game's fog boxes", and the characters' first builds.
- The second scene had NO decals then (no "Decal GPU inputs" line after the clear: the list was empty), and no
  plants ("plants=0"). So F1 cannot be it, and F2 and F3 cannot hang anything.
- By reading, a scene load leaves nothing behind that names the old scene: see "Where the code is right". nosun's
  loops (a fog box walked by a ray that is no number) stay the best lead.

## Where the code is right (looked at, with the reason)

The heap and the release queues
- The descriptor heap (`gpu.hpp`): a slot given back waits until the GPU has finished the frame being recorded when
  it was given back (`set_retire(consumer.value+1)` at the start of `trace_event`; `collect` only there and in the
  maintenance event, which retires at the last submitted frame's fence, and runs only while no frame is being
  recorded). So no slot gets a new owner within the frame that released it, and no frame in flight reads a slot
  that has one. The brief's "released and taken again within ONE frame's record" cannot happen.
- Views written in place into a slot that is handed out: only three. The fog boxes' (`fogBoxSrv[index]`, one per
  frame slot: the CPU has waited for that slot's last frame, `wait_cpu(st.retired[index])`). NRD's own tables (one per
  frame slot and dispatch, 4 slots for the tracer's 3). The shared textures' range (`share::publish`: a number is
  handed out again only after `release_due` found its retire fence done).
- Every `st.release.add(st.consumer.value, ...)` inside `trace` (the fog's cells and reservoirs, the sky's clouds, the
  filters' and the denoisers' textures after 120 idle frames, the mirrors' records, NRD's confidence, the waves'
  copies): each frees a thing no command of the frame being recorded uses (checked one by one against where it is
  read). `Nrd::destroy` frees at once, and every caller has waited for the GPU or 120 frames first.
- `DeferredRelease` and the heap's queue are first in, first out: a later fence in front only holds others back.

The scene's records (which are laid out anew every frame, and what is kept)
- Mesh handles are never reused (`e.nextMesh++`, not reset by `TGPT_ClearScene`): a handle kept anywhere names its
  mesh or nothing, and every use looks for "gone" first. A frame traced with an OLDER scene list than the one the
  maintenance event retired meshes by skips those instances.
- The mesh buffer is the registered meshes' records, then the swaying cloths', the groups', the water pieces'; the
  geometry buffer the meshes', then the groups', then the water's levels. Every place after the first part depends
  on a count before it; each count's change either marks the records (`meshRecordsDirty_`) or changes a compared
  list. The instance buffer, the lights with the lamps' tree, the ranges of the plants, the groups' records and the
  cloth's records are uploaded every frame.
- The plants after PT-125's fix: ranges every frame with this frame's member numbers; members whenever the groups
  in use or their order changed, and whenever a group is new (which also covers a new group at a freed group's
  address: `uploadedPlantGroups_` compares pointers); a vertex keeps its range's number within its group. A group
  that links micromaps checks the mesh's build number each frame (`memberMaps` against `ommSerial`) and is never
  posed again once they went; within the frame that lets them go they are kept to that frame's fence.
- The water: a piece's triangle records keep its page's view and its place in the page, both its own for as long as
  it lives (a page goes only when no piece has had a block in it for 120 frames); the record number is set anew in
  every frame the piece is used; a block goes back to its pool only from a piece no frame has used for 120 frames.
- The cloth: a copy is keyed by mesh, materials and transform; its refit names its mesh's indices in the frame its
  instance is traced.
- Characters: the unpack writes them, the scene keeps every mesh written this frame alive, builds or refits, then
  lists it.
- Compaction batches: the still meshes' by handle and the very address the size was read from (a still mesh is
  built once); the micromap structures' also by the build's serial; the plants' by cell and serial.
- The micromaps: the queue and the read-backs hold mesh pointers and are cleaned when a mesh or its maps go.
- The light grid and its far lists: made anew whenever anything they are made from changes (the hash holds every
  light's place, kind, range, the clusters and where their records start); a cell's light numbers are below the
  count they were made with.

Numbers kept by the reuse of samples
- RTXDI's lights: `RAB_TranslateLightIndex` bounds a kept number by both frames' counts and takes it only when the
  slot holds the same identity in both; an emissive triangle's slot the same way; the two emitter buffers take
  turns, so the previous frame's records are the ones it read. A scene load resets the reuse (`resetNext`).
- The bounced light's reservoirs keep places, not numbers, and the reuse is skipped in a frame the origin moved.
- The fog's reservoirs keep a light's number and identity: used only if this frame's lists hold that number and the
  identity matches, and then only as a proposal. Their history also checks the scene's clear count.
- The radiance cache is keyed by world cells (the origin's move is in the key) and emptied at a scene load.
- The mirrors' records count only in the frame whose serial they carry. The caustics' landed light is built and
  read in one frame (PT-130's order); its room is replaced only in a frame that builds both levels again before any
  reader, and a frame without the pass hands the passes no view of it.
- The glass's two words (instance and material per pixel) are written and read in the same frame.

The shared textures
- A texture's number in a record is looked up again whenever a texture came or died (`share::generation`: the
  materials, hair's and eyes' maps, the programs, the decals), or every frame (the fog boxes, the foam, the rain,
  the sky). A dead texture's D3D12 side and its view stay until the GPU is past the last frame that could name it.

A scene load (`GpuScene::clear`, `release_scene_extras`, `share::clear`, `TGPT_ClearScene`)
- Everything that names a mesh, a group, a piece, a slot or a texture is dropped or marked: the arenas whole, the
  compaction batches and read-backs, the queues, the decals' top levels, the emitters' slots, the water, the
  caustics' room and waves, the wet store, the strand atlas. What is not reset is checked at its next use (the far
  lists against the lights, the grid by its hash, the published view numbers by the next `record`).
- A frame submitted before the clear and traced after it carries the old list, characters and handles: all of them
  resolve to "gone".

## Seen on the way (not stale numbers; for whoever owns them)

- The mesh and geometry records never shrink. Handles are never reused and a mesh seen again is registered anew, so
  `meshRecords_` (48 bytes a handle ever given) and `geometryRecords_` (16 bytes a submesh ever built) grow for the
  whole session, and both buffers are uploaded whole whenever a mesh comes or goes or the cloth's, the groups' or
  the water's records change (near water: most frames of a walk). Small today (6,300 handles: 0.3 MB); a cost that
  only grows. reccpu's ground.
- `texture_share.cpp`'s `kPending` is never assigned (`kill` takes the last published frame's fence, which is right).
- A mesh still converting after 10 s at a scene clear is let loose on purpose; when it finishes it is uploaded into
  the new scene's buffers though nothing can name it, and its views are never given back. Rare.
- Two skinned instances with ONE mesh handle would build one structure twice side by side. The plugin gives every
  character its own; the native side does not check.

## Not done, and not looked at

- No DLL built, nothing run on the GPU, no offline fixture for F1 (a mesh decal whose mesh is compacted in a quiet
  frame: its ray query must still find the mesh; its mutant: the early way out as it was).
- RTX Mega Geometry's trial paths (`mega_geometry.cpp`, the partitioned top level and the clusters: trial numbers 1
  to 8). They are compiled in and do nothing at trial 0. The partitioned top level KEEPS instances from frame to
  frame by address (`megaFresh_`): the very kind of thing this audit is about, to be read before that trial is used
  again.
- The plugin (C#) beyond four look-ups; the D3D11 side's effects observer (`vfx_*.hpp`); the atlas tiles' reuse.
- `BACKLOG.md` and `docs/PT-REQUESTS.md` are not edited in this copy (shared files, two sessions). Proposed entries:
  - "A mesh decal's own top level named its mesh's bottom level by an address that moves at compaction: a GPU reset
    where a scene has mesh decals (found by stale, 2026-10-12, by reading; fixed in main ..., not yet seen in a scene
    with mesh decals)".
  - "The emitters' instance table was by a frame's record numbers: a glow's bounced light doubled or lost after
    plants' groups were made (found by stale, 2026-10-12, by reading; fixed in main ...; to see: a camp's torch
    light on a walk)".

## For the merge

- Three files, and this report: `native/pathtracing/src/scene.cpp` (hunks at the base's lines 3437, 3527, 3575 in
  `build_emitters`; 3802, 3814, 3867 in `upload_decals`; 4218 in `retire_meshes`), `scene.hpp` (754 `DecalMesh`, 813
  two members), `tests/source_checks.py` (one test put in before `test_ingame_harness`, the base's line 4520). Each
  file keeps its own line ends (the two sources LF, the checks CRLF: counted by bytes).
- reccpu's copy changes `record`, `plan_plants`, the light grid and a few lines of `clear` and `retire_meshes`'s
  end; none of this copy's hunks is on a line of theirs (nearest: their change at the base's 3654, `build_light_grid`,
  and 4239; in the checks 4430, campspeed's 4753). F2 needs one thing of `record`: `sources` must stay "each still
  record's place in the scene's list". The new check quotes four lines of `scene.cpp` and two of `pt_sway.hlsl`
  word for word: a merge that rewrites one of them must carry its meaning into the check.
- AGENTS.md's "76 checks" is out of date (118 with this one); not edited here (a shared file).
