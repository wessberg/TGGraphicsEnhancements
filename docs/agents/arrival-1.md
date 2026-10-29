# arrival-1: the traced world is whole within seconds of a load, and the game says how far it is (PT-98)

Worker `arrival1` (Claude), copy `wt\arrival1`, base `base71`, 2026-10-09, 15:18 to 16:10.
Brief: `docs/agents/briefs/brief-arrival-1.md`. `<scratch>` below is
`C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad`.

## The answer first

- **Where the seconds go** (read from two of today's runs, below): not in building meshes. The native side had a
  town's first 1,190 meshes built 1.5 s after tracing was switched on. Then NOTHING came on screen until the
  plugin's next scan, at 2.0 s, and what that scan found new waited for the scan at 4.0 s. The world arrived in
  steps of 2 s.
- **The half minute a traced step waits in the harness is not the world arriving.** It is the recipes'
  `settleFrames` 240 (115 of the recipes' traced steps have it), counted by the frame: 30 s at 8 frames a second,
  4 s at 60. That is the recipes' choice and the coordinator's to change (see "Left").
- **Built, all three steps:** the timeline, the limits by the clock, the notice and the harness's wait. Plugin,
  harness and native side compile. **Nothing was run on a GPU. Nothing was seen in the game.**

## HAND-OVER 2 (16:10): everything. What to do

1. Merge the copy: `python tools/agents/merge_copy.py arrival1 --base=base71 --root=<scratch>`. A dry run at 16:07
   said 19 changed, 2 new, 0 conflicts (the main tree had moved on at about 16:05; still 0).
2. Build the main tree and deploy (plugin, harness and native side: the status record grew, see "ABI").
3. Run ONE scene: `.\tools\harness\Run-PtHarness.ps1 -Scenario metal -Recipe arrival`, then `npc-day` the same way.
   About a minute a scene. The recipe lets the world arrive three times from nothing in one run:
   `2-as-before` (the hidden `Advanced / Arrival as before` at 1: the plugin's rules of before), `4-by-the-clock`
   (0, the default), `6-as-before-again` (what the order alone is worth), then `8-on-again` (switched on with
   everything registered already).
4. Read the run's `LogOutput.log`: the lines that start with `Arrival`. And `progress.log`: each traced step says
   "the traced world is whole ... this step waited N s for it", or in capitals that it was not.

**What the run should show:**
- `Arrival whole after N s` three times. I expect about 4 to 6 s for the two "as before" and about 2 to 3 s for
  "by the clock" in `metal`. If the three are alike, the plugin's rules are not where the time goes: then the lines
  say which queue was the last to empty.
- In the "as before" lines: `scans: 1` for two whole seconds while `to place` is above 0 (meshes built, nothing on
  screen). In the "by the clock" lines: a scan within a quarter second of meshes getting their handles.
- `registered (+N in X ms, Y ms of it the frame's first call...)`: N far above 6 in a slow frame, by the clock. In
  "as before" with slow frames, N stays at 6 if my reading is right (not proven: see "Not proven").
- `8-on-again`: `Arrival whole after 0.0 s` or close.
- The pictures of `4-by-the-clock` (4 frames, taken when the plugin says whole) show the whole town. If something
  is missing in them, "whole" is wrong: that is the finding to send back.

**For a look without the native side** (plugin only, as a trial): the new plugin works with the installed older
native DLL. It then asks for the old status record, the native counters read 0 and the line says "tiles to bake
not counted by this native side". Built: `%TEMP%\tgb\arrival1-plugin\TGPathTracing.dll` (SHA-256 `13D72F516CDC`,
16:10:10) and `%TEMP%\tgb\arrival1-harness\TGPathTracing.Harness.dll` (`42FE61B75D64`). Only good while the main
tree's plugin sources are still the base's: build from the merged tree otherwise.

## The timeline's line (step 1)

One line a second from the moment tracing captures, in `LogOutput.log` (broken here to read; the numbers are made up):

```
Arrival starts (number 1 of this session: tracing was switched on): a line a second until the traced world is whole. <the same fields>
Arrival 1.0 s | instances: the scan listed 0, met 3105 more whose mesh has no handle yet; the tracer traces 0 of 0, 0 wait for their mesh
 | meshes: 1287 seen, 610 registered (+610 in 62.0 ms, 9.0 ms of it the frame's first call, which waits for the render thread), 677 to register, 610 to place, 560 built, 50 building, 0 failed
 | textures: 0 wanted, 0 open, 0 kept back; of 0 watched 0 at full size, 0 below what the game wants, 0 loading; 0 swaps followed (+0 by the last scan); 0 tiles to bake; 3 looks in 0.4 ms; the game's streaming: 3 loading, 12 waiting
 | materials 2 | characters: 0 traced, 2 being read | micromaps: 0 meshes, 0 to make | plants to group: 0
 | scans: 1 (+1, 56.7 ms; the last 56.7 ms, 1.0 s ago, found 1287 new meshes, 0 new materials)
 | 41.0 ms a frame (24 frames in 1.0 s, the longest 71.0 ms), tracer GPU 0.0 ms, render thread 0.0 ms
 | to come 1289
...
Arrival whole after 2.4 s (tracing was switched on). The last to empty: registered meshes a scan has yet to place.
 Each queue was last busy: registered meshes a scan has yet to place until 2.3 s; meshes to build until 2.1 s; ...
 . Not counted; plants to group for the wind (they stand still until then): 212 still; ...
 . 6 scans took 410.0 ms in all, the slowest 106.0 ms; registering meshes took 140.0 ms, of which 31.0 ms waiting for Unity's render thread; the looks at the textures took 2.1 ms. Now: <the same fields>
```

Then silence. It starts again when a scene is unloaded, when tracing is switched on again, and when things have
been arriving for 2 s after the world was whole (a walk into a new place). If tracing stops before the world is
whole: `Arrival stopped at N s, not whole`. Past 60 s without being whole, a line every 10 s.

**"Whole" is:** no mesh waits to be registered (plugin) or built (native); every registered mesh has been through
a scan, and no listed instance waits for its mesh; no character is being read back; no texture is kept back; no
tile waits to be baked; the game is loading no sharper level of a texture a traced material reads.
- The game's own loading stops counting when its count has not moved for 5 s (the line says so). Reason: if the
  game is over its streaming budget such a load may never end, and the notice and every harness step would wait
  for it. Not known whether that happens: the line shows it if it does.
- Told in the lines and NOT counted: opacity micromaps still to make (the alpha test serves meanwhile: speed
  only), plants still to group for the wind (they stand still until then), and what the last scan found new (a
  new mesh is in the queue to register; a new material is sent by the scan that found it).
- Cost of the timeline: counters and two more reads of Unity for a texture below full size, in a look that
  already read two. The line gives the looks' time ("N looks in X ms"): I expect about 1 ms a look for 1,600
  textures, not measured.

## What two of today's runs already say (read from their logs, not run by me)

The native log has millisecond stamps; the plugin's log has none (the timeline is that clock).

**`metal`, run `20261009-154425` (the coordinator's build of 15:43), step `2-none`**, in ms after the first traced frame:

| ms | what |
|---|---|
| 0 | tracing switched on, the scene empty; the first scan finds 1,287 meshes to register |
| 340 to 1,550 | about 1,190 static meshes built: up to 48 a frame, a frame about 45 ms (the scene is empty, so frames are fast) |
| 1,550 to 1,940 | nothing but 48 more. All that was found is built; the instances wait for the scan |
| 1,940 | the second scan (2 s after the first): the first frame that has instances, plants and the water's surface |
| 2,370 to 3,050 | 201 more meshes, which the second scan found: built 48 a frame, a frame now 140 to 200 ms |
| about 4,000 | the third scan lists THEIR instances (by the code: no log line says it) |
| 1,400 | the yardstick: the same surface on 11.1 % of the screen |
| 13,170 | the yardstick: 99.7 % (the next one counted; when between, no log says) |
| about 35,000 | the step's pictures. The recipe was `relief`: `settle` 12 s and `settleFrames` 240, which is 30 s at 8 frames a second |

(The times of "built" are those of each batch's compaction line, a frame or so after its build.)

Steady state there: 25,273 instances listed, 1,422 meshes, 646 materials, 515 textures, a scan 60 to 107 ms,
the tracer's GPU time 98 to 116 ms.

**`npc-day`, run `20261009-150909`, step `2-traced-missing`:** the same shape (meshes built by 1,265 ms, the first
instances at 2,000 ms, the water at 3,125 ms). The step ended at about 6,700 ms with "nothing traced where the
game drew 62.51 %": the harness had seen the native queue empty for 1.5 s between two waves.

## The limits on the path (step 2): 24 found

By the frame, by the scan or a fixed number an update. File and line are the copy's.
"Now" is what this job did. The three worst are 1, 2 and 13.

| # | What | Where | Counted | At 60 frames a second | At 8 | Now |
|---|---|---|---|---|---|---|
| 1 | A built mesh's instances are listed by the next scan | `SceneCapture.cs` 35 `ScanInterval` 2 s | by the scan | up to 2 s late, three scans for a town: 4 s | the same | a scan 0.25 s after the last (and at least 4 times the last scan's own time later) while registered meshes wait (`ArrivalScanInterval`, line 91) |
| 2 | Meshes registered a frame | `SceneCapture.cs` 85, 86 `MeshesPerFrame` 6, `MeshShare` 0.1 | by the clock since 13:50, but the clock started before the frame's first call into Unity, which waits for the render thread | 6, plus a tenth of 16 ms | 6 a frame = 48 a second, if that wait is most of a frame (not proven) | the share is counted after the first mesh: about 1,000 a second at any frame rate |
| 3 | A texture swap is seen | `TextureShare.cs` 233 (4096 a look), called once a scan | by the scan | up to 2 s late; the material reads 64 x 64 tiles meanwhile | the same | a look every 0.25 s (`TextureLookInterval`, `SceneCapture.cs` 95) |
| 4 | Mesh bytes read back a frame | `scene.cpp` 30 `kReadBudget` 64 MB | by the frame | 3.8 GB a second | 512 MB a second | x the clock, at most x4 (256 MB a frame: memory in flight) |
| 5 | A read-back is mapped two frames after its copy | `scene.cpp` 563 | by the frame | 33 ms | 250 ms for every mesh | also one frame after, once 33 ms have passed (`kReadMapMs`) |
| 6 | BLAS builds a frame | `scene.cpp` 38 `kBuildsPerFrame` 48 | by the frame | 2,880 a second | 384 a second: a town's 1,533 in 4 s | x the clock (384 a frame at 8) |
| 7 | Their triangles a frame | `scene.cpp` 39 `kTrianglesPerFrame` 3 million | by the frame | 180 million a second | 24 million a second | x the clock, at most x4 (one upload buffer a frame: about 440 MB) |
| 8 | Colour and mask tiles baked a frame | `tracer.cpp` 1158: 24 | by the frame | 1,440 a second | 192 a second: 840 textures in 4.4 s | x the clock (1 to 8) |
| 9 | Alpha tiles baked a frame | `tracer.cpp` 1455: 8 | by the frame | 480 a second | 64 a second | x the clock |
| 10 | Hair colours, hair lobes, eyes, light cookies a frame | `tracer.cpp` 1254, 1317, 1362, 1419: 8, 8, 4, 8 | by the frame | 480, 480, 240, 480 a second | 64, 64, 32, 64 | x the clock |
| 11 | Micromap triangles and bounds texels a frame | `scene.cpp` 41, 45, 2041 | by the frame | 15.7 million triangles a second | 2 million | x the clock, at most x4. Speed only: the alpha test serves until then |
| 12 | Plant groups made a frame (their plants stand still until then) | `scene.hpp` 197, 198: 24 groups, 1.5 million triangles; `scene.cpp` 967 | by the frame | 1,440 groups a second | 192 | x the clock (triangles at most x4) |
| 13 | The harness's settle | `scenarios.json`: `settleFrames` 240 in most recipes; `Runner.cs` 578 | by the frame | 4 s | 30 s a traced step | NOT changed (the recipes are the coordinator's; frames are samples there). See "Left" |
| 14 | The harness's sign that a step may be pictured | `Runner.cs` (was: `MeshesQueued` 0 for 1.5 s) | a wrong sign | | | the plugin's `Arrival.Whole` |
| 15 | Textures kept back for video memory are asked for again | `TextureShare.cs` 181 `BudgetRetrySeconds` 10 s | by the clock | 10 s of the game's picture (tracing is paused meanwhile) | the same | not changed: by the clock already, and it is the short-of-memory case (PT-96) |
| 16 | The water's mesh is read back 8 frames after it is asked for | `scene.cpp` 664, 692 `kWaterReadbackFrames` | by the frame | 0.13 s | 1 s before the water's surface is there | not changed: the water worker's file |
| 17 | Lights scanned | `SceneLights.cs` 27 `ScanInterval` 1 s | by the clock | a new light up to 1 s late | the same | not changed |
| 18 | A character's triangles are read back | `CharacterCapture.cs` 1100 (Unity's async read-back) | by the frame, a few | about 50 ms | about 0.4 s | not changed (Unity's own); counted as "characters being read" |
| 19 | A micromap not used for 8 frames is dropped | `scene.hpp` 288 `kOmmUnusedFrames` | by the frame | 0.13 s | 1 s | not changed. Speed only |
| 20 | A hair tile not traced for 600 frames goes back to the pool | `SceneCapture.cs` 117 `HairTileIdleFrames` | by the frame | 10 s | 75 s | not changed. With the pool of 128 full, new hair gets a 64 x 64 tile for that long: that one changes the picture (PT-50's kind) |
| 21 | The status the notice reads | `Session.cs` 110: every 0.25 s | by the clock | | | not changed |
| 22 | Moved scan | `SceneCapture.cs` 39 `MovedScanInterval` 0.5 s | by the clock | | | not changed |
| 23 | The scan itself: materials made and textures opened on the main thread | `SceneCapture.Tick` | one long frame, not a limit | 60 to 107 ms steady in `metal`; the first scan that lists a town: not in any log | | not changed; the timeline gives it ("the slowest N ms") |
| 24 | Reuse across frames (the light's histories) | the tracer's passes | by the frame, by nature | | a picture needs its frames after the world is whole | not changed: frames are its samples |

**How the native side goes by the clock:** each frame the tracer takes the time since the last traced frame as
"frames of 60 a second" (`tracer.cpp` `frame_clock`: 1 to 8; 1 at 60 a second and above; 7.5 at 8 a second; 8
after tracing was off) and multiplies every "a frame" budget by it (`GpuScene::set_clock`, `by_clock`). The same
work a second at any frame rate, in fewer and larger pieces. Nothing is left out and nothing is made coarser: what
does not fit a frame stays in its queue as before.

## What it costs in frame time while the world arrives

- **Registering meshes:** up to a tenth of the frame's time longer while meshes wait, as since 13:50; nothing
  when none waits. The frame's first call into Unity also waits for the render thread, as it always did.
- **Scans while meshes wait to be placed:** a quarter of the time at most (a scan at least 4 times its own
  length after the last). `metal`'s scan is 60 to 107 ms: a scan every 0.3 to 0.4 s for the 2 to 3 s of arrival,
  where it was one every 2 s. Each scan is one longer frame.
- **Texture looks:** four a second. About 1 ms each by my estimate; the line measures it.
- **Native, at a low frame rate only:** a slow frame takes in up to 8 times as many builds and tiles as before.
  At 8 frames a second a frame that had 48 builds may have 384, with up to 12 million triangles through one
  upload buffer (about 440 MB for that frame; it was about 110 MB), and read-backs of up to 256 MB a frame (it
  was 64). So a few frames get longer while the world arrives, and there are fewer of them. At 60 frames a
  second nothing changes. Watch the memory on a PC that is short of it (PT-96): the two "at most x4" are there
  for that.

## The notice and the harness (step 3)

- **The notice** (`Session.cs`, `Arrival.NoticeText`): "building the traced world: 412 meshes and 96 textures to
  go", then "... placing the last things", gone when the world is whole. One line, a number that falls. It shows
  once the world has been arriving for 2 s, as before. It counted meshes only. The performance overlay's line
  says the same (`TracingOverlay.cs`).
- **The harness** (`Runner.cs`): a traced step's pictures are taken when `Arrival.Whole` has held for 1.5 s. The
  line on screen says what is still to come. A step that reaches `waitTraced` first takes its pictures anyway and
  says so in capitals in `progress.log`. The step's result holds `worldWhole`, `toCome`, `waitedForWorld`,
  `arrivalSeconds`. With an installed plugin from before, the old wait serves and the note says so.
- Is `MeshesQueued` 0 enough? No, four ways: it is the native queue only (empty between two waves); a built mesh
  is not on screen until a scan lists it; textures are not in it; characters being read are not in it.

## ABI

- `TGPT_Frame`: untouched. No option bit used. `TGPT_ABI` as it was (`0x00012600`).
- **`TGPT_Status` grew at its end, 216 to 232 bytes** (`tilesToBake`, `instancesWaiting`, `plantsWaiting`, one
  reserved), plugin and native side together. Either side works with the other's older build: the native side
  fills a 216-byte record for an older plugin, and the plugin asks an older native side with 216 when 232 is
  refused. The source check that pins the size follows the header by itself.

## Files

New: `src/TGPathTracing/Arrival.cs`. Changed: `SceneCapture.cs`, `TextureShare.cs`, `CharacterCapture.cs`,
`Session.cs`, `Settings.cs`, `NativeTracer.cs`, `TracingOverlay.cs`; `src/TGPathTracing.Harness/Runner.cs`,
`Request.cs`; `native/pathtracing/include/tgpt_api.h`, `src/tracer.cpp`, `src/scene.cpp`, `src/scene.hpp`,
`src/extension.cpp`; `tests/source_checks.py` (one new check, `test_traced_world_arrives_by_the_clock`, and the
harness check knows `Arrival`); `tests/ingame/scenarios.json` (recipe `arrival`); `docs/PT-HARNESS.md`;
`CHANGELOG.md` (the entry sits above the earlier PT-98 entry of today, not at the head, so the merge is clean).
No shader source changed. Not touched: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
`docs/PROJECT-CONTEXT.md`.

## Not proven

- **That the first call's wait ate the share (limit 2).** It is my reading of the code and of Unity's rule that
  such a call waits for the render thread. Today's two runs do not show it: in both, the meshes were registered
  while frames were still fast. The line shows it either way ("Y ms of it the frame's first call").
- **That the native budgets ever bit in a run.** In `metal` they did for about 200 meshes (48 a frame at 150 to
  200 ms a frame: 0.7 s). They bite whenever the frame is already slow: a walk, or a load into a traced scene.
- **Every number under "what the run should show"** is an expectation.
- **Unity's `loadingMipmapLevel`** as the sign that a sharper level is on its way: read from Unity's
  documentation, not seen in this game.

## Left

- **`settleFrames` 240 in the recipes** (limit 13): the half minute of every traced step. Now that a step waits
  for the world to be whole, the count only has to cover the light's histories. That is the coordinator's and the
  owner's call, not this job's.
- **The run's report does not print the arrival yet** (`tools/harness/analyze.py`): the numbers are in each
  step's result and in `progress.log`.
- **The switch `Advanced / Arrival as before`** goes when the game has shown which way is right, with the
  recipe's two "as before" steps.
- **The native rules have no switch** (all 32 option bits are used): they are by the clock in every step of the
  recipe. To see them alone, compare this build with the build before it on a scene whose frames are slow from
  the start.
- Limits 15, 16, 19 and 20 in the table: found, not changed.
- The first scan that lists a town makes every material and opens every texture in one frame. How long that
  frame is, the timeline will say. If it is seconds, spreading it is the next job.

## Workarounds met

- The old notice's own count (`Status.MeshesQueued + SceneCapture.Waiting`, with its timer `buildingSince`):
  removed, `Arrival` serves.
- The harness's wait on `MeshesQueued`: kept only as the fallback for an installed plugin from before.
- None of the kind PT-60 lists (nothing here exists because the tracer once started from HDRP's picture).

## Compiles, checks

- Plugin and harness: `dotnet build ... -c Release -f net472`, 0 warnings, 0 errors (16:10).
- Native: `%TEMP%\tgb\arrival1` (NMake Release, `-DTGPT_WITH_NRD=OFF`), target `TGPathTracing.Native`, 0
  warnings (15:57). **No shader pass was compiled:** the 46 headers are the main tree's build of 15:43 (the
  copy's shader sources and `CMakeLists.txt` are the main tree's text, checked file by file), and
  `Build-PtShaders.ps1` said "40 in all, 0 to compile". So the NRD path of this DLL is not built here.
- `python tests/source_checks.py`: 106 of 106 (105 in the base, one new). `python tests/harness_analyze_test.py`: 6 of 6.
- **Run on a GPU: no. Seen in game: no.** No offline test was run, not even a small one: the game is the first
  test, and a new native build's first start compiles its passes for minutes.
- Scripts used (all in `<scratch>\arrival1-tools`): `native.cmd`, `headers.ps1`, `build-managed.ps1`, `eol.ps1`.

## HAND-OVER 1 (15:41), kept for the record: the timeline alone

`<scratch>\arrival1-handover\step1`: the plugin DLL (`9A8091562948`) and its five files, the timeline with none
of the changes above. Not needed any more: the recipe `arrival` shows before and after in one run. Its "whole"
was stricter than the final one (it also waited for one scan that found nothing new), so its number is not to be
compared with the final build's.
