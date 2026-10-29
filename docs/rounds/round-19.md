# Round 19 - TG Path Tracing: speed, from 2026-10-10 17:05

Started from round 6's hand-off ("Round 6 closes" and "▶ Starter prompt (R19)" at the end of `docs/rounds/round-06.md`).
The owner's order stands (AGENTS.md): speed and video memory first, then the rendering issues and missing features,
then RTX Mega Geometry. Reordering stays on. Nothing is committed (since d31dff9); the owner commits.

The state at the start (hash-checked 17:18): native 2E4723B1AC0E and plugin 6850B285696D in the game folder,
`build\native` and `artifacts\Native` alike. The previous session's scratchpad is still there
(`...\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad`, "OLD" below): its worker copies (`wt\waterser1`, `specser1`,
`relief1`, `game1`, ...) and its scripts (`run-held.ps1`, `study-run.ps1`, `measure_read.py`, `same_picture.py`).
Codex is out until 2026-10-16 09:04. No worker runs.

## 1. The water's own pass never ran: a ComPtr trap (PT-99)

The trial build of `waterser1` (run `20261010-154747`) made all three of its passes ("Water pass trial: ... made") but
every step said "the water's own pass is asked for, but it was not made". Reading found nothing, so a copy with two
log lines (run `20261010-171929`, `water-day@water-pass`; native C6CDAD3C8A5D) said: `adopted: specularSer 1 p.handoff
0 p.waterSerT 0 p.waterSer 0 table 1 handoff 0`. The table was made, yet the passes were gone.

THE CAUSE: `adopt_passes` listed the three passes as `const ComPtr<ID3D12StateObject>* passes[3]={&p.specularSerHandoff,
&p.waterSerT, &p.waterSer};`. WRL's `ComPtr::operator&` does not give the ComPtr's address: it gives a `ComPtrRef`, and
that object's conversion to `ComPtr<T>*` EMPTIES the ComPtr first (it is meant for `&ptr` as an out parameter). So the
line threw away the three passes it named, the loop skipped every one, and nothing failed or said so. Fixed in the
copy with `std::addressof` (both places that take such an address). The main tree has no such line (searched).
A method trap for PROJECT-CONTEXT 11.

## 2. Why the driver kept 2,112 bytes a thread in the water's pass

The water's passes keep 2,112 bytes a thread on the driver's stack (its own count at start-up), where the listing says
a thread carries 104 to 125 bytes across the call. Counted in the compiled pass (`pt_water_pass_1.h`'s DXIL): two
local arrays of 66 x float4 = 2 x 1,056 = 2,112 bytes, exactly the driver's figure. They are `SunCorner rows[2][33]` in
`water_sun` (`pt_water_sun.hlsli`): the previous row of wave corners while a patch is made at the point (the far sea,
past the kept corners' reach, about 60 m). Their lifetime marks in the DXIL start AFTER the call and end before the
loop turns, yet the driver keeps them whole: A LOCAL ARRAY INDEXED BY A COMPUTED NUMBER, IN A LOOP THAT HOLDS A
REGROUPING CALL, IS KEPT ON THE DRIVER'S STACK WHATEVER ITS LIFETIME (the same 2 KB the bounced light's old pass and the
water's regrouping inside today's reflections' pass kept: 2,592 and 2,624). The reflections' pass that ships has the
same array after its one call, outside any loop with a call: 128 bytes.

Trial (the copy only, under `PT_WATER_PASS`, so no pass that ships changes): the patch keeps no row; the corner above is
made again (`sun_patch_corner`, the same numbers, the corners' work twice where a patch is made at the point). Both
water passes compiled without the arrays (one call each). DLL `%TEMP%\tgb\waterser1\TGPathTracing.Native.dll`
7CE844A7DE61. Queued at 17:30 for `water-day@water-pass` (`<scratch>\study-run.ps1`, which since this round also waits
for the owner's games before it installs anything); it waits while Starfield is open (the owner started it 17:25).

THE ANSWER (run `20261010-173151`, `water-day@water-pass`, the row-free build 7CE844A7DE61; Sunshine on the card up to
4.8 %, so the steps against each other only): the driver keeps 0 bytes a thread in both water passes now, the picture
is the same in every step (two takes of one step differ as much), and BOTH ARRANGEMENTS LOSE. Reflections' stage, ms:
as it ships 92 to 96 at the start, 91 to 92 at the end; arrangement 1 (the call at each turn's top, no hint) 118 to
125; arrangement 2 (after each turn's ray by a hit object, 2-bit kind) 110 to 112; the same with no hint 115 to 121.
(The row made again costs the far sea's patches some time too, which this run cannot separate; it would not turn a
loss of 20 to 30 ms into a win.) So regrouping at every turn of the water's path does not pay, small stack or not. The
design is dropped; nothing of it was merged. The trial copy stays in the scratchpad (`wt\waterser1`).
(That run had Sunshine on the card, which section 4's correction shows can hurt one variant and not another. Run
again at 18:54 (`20261010-185439`) with Sunshine idle, but an unknown process, pid 31488, gone by the end, used up to 34 %
of the card: as it ships 69.0 / 66.4 ms, as in the clean runs; the water passes 107.7, 100.1, 107.1. The same verdict by
a larger margin, from two runs flagged by two different programs: enough to keep a design dropped, not a clean number.)

## 3. Where the frame's time outside the tracer goes (PT-107)

New in main (`tracer.cpp`, logging only): timestamps on the GAME'S device (D3D11 queries, read a few frames later,
never waited for) at the tracer's events, and a part of the 10-second line, "game device a frame". Run
`20261010-173919` (`metal@split`, `tavern-room@split`, a new short recipe `split` with 25-second traced steps; Sunshine
on the card up to 4.1 %):

| ms a frame | tavern, no denoiser | tavern, NRD | town, no denoiser | town, NRD |
|---|---|---|---|---|
| the tracer's input copies | 0.2 to 0.3 | 0.2 | 0.3 | 0.3 |
| the game from our trace's start to the composite | 4.0 to 4.9 | 3.9 | 7.5 | 7.6 to 7.9 |
| the composite waits for the trace | 30.3 to 30.8 | 35.2 to 35.3 | 62.5 to 63.3 | 67.4 to 67.9 |
| the composite to the next frame's trace | 10.8 to 12.9 | 10.4 | 16.1 to 24.7 | 15.7 to 20.1 |
| the tracer's own GPU time (its 10-second line) | 32.2 to 33.1 | 37.6 | 65.4 to 66.0 | 70.3 to 70.8 |

The game alone at the same spots (the plugin's own count while tracing was off, same run): the tavern 86.5 frames a
second (11.6 ms), the town 68 to 72 (13.9 to 14.8 ms).

THE CARD TAKES TURNS: a standalone test (`<scratch>\overlap\overlap.exe`: a compute job on 16 of the card's 84
processor groups, about 25 ms alone, once on a D3D11 device and once on a D3D12 device of the same process): both at
once 51.3 ms, the SUM of the two; two queues of ONE D3D12 device at once 25.4, the longer of the two. So on this RTX
5080 and driver the game's D3D11 work and our D3D12 work never run side by side, whatever the frame's order. The
composite waits for the same frame's trace since round 6's `edges` (exact sub-pixel matching), but even the old "one
frame late" order could not have bought overlap here, only fewer idle gaps.

So a traced frame is the trace plus the game's own whole frame (11.6 ms tavern, about 14.5 town) plus a rest of about 4
ms (tavern) and 9 to 18 ms (town): our composite and Fog Adaptation, idle gaps, and whatever HDRP does more while we
trace. The stamps were extended (main, native 6C54E84BCA33) to time the composite and Fog Adaptation too, so the
next run splits that rest. Two ways to shrink the gap, neither touching the picture: render less of the game while
tracing (PT-107, `game1`'s plan in `wt\game1\docs\agents\game-1.md`), and find the rest.

## 4. The reflections' key (PT-99): a trial running

Round 6's study (`spec-ser-1.md`, runs `20261010-144426`, `-151637`): with NO hint the beach's reflections took 70.8
ms (as it ships 92.0) but the town 30.8 (24.0 with the material's key in 8 bits). The guess: with no hint the driver
keeps the screen's order, which water's rays (neighbours go alike) want, while other surfaces want like materials
together. Trial (copy `wt\speckey1`, base `base88`; DLL 8EDA53D7E567, its one call counted in the compiled pass): the
hidden "Speed trial" picks the key at run time, one call: 0 as it ships; 16 the camera's water pixels by their 32 x 32
block of the screen and everything else by its material, a bit apart (16 bits); 32 no hint; 48 the same split in 8 bits.
Recipe `spec-key` (main's `scenarios.json`); run started 17:49 at the beach, the town and the tavern.

THE ANSWER (run `20261010-175041`, the game in front throughout, Sunshine up to 4 %; `<scratch>\measure_read2.py`; the
picture the same in every step, within two takes of one step), the reflections' stage, ms:

| Step | tavern | town (`metal`) | beach (`water-day`) |
|---|---|---|---|
| as it ships (the material's key, 16 bits) | 9.3 | 22.9 | 91.1 / 93.5 again |
| the water by 32 x 32 block, the rest by material | 9.3 | 23.1 | 92.7 |
| NO HINT | 10.0 | 30.6 | **75.5** |
| the split key in 8 bits | 9.3 | 24.1 | 91.7 |

So the block key does NOT give the water what no hint gives it. With round 6's study (`20261010-144426`: 3, 4, 8 and
16 bits, the material in the high bits, all 83 to 86 at the beach), the reading is: ANY key makes the driver sort, and
the sort costs the water's paths about 15 ms (it breaks up neighbouring pixels, whose water paths go alike); no hint is
a sort-free regrouping that keeps the screen's order. Elsewhere the material's sort pays (the town 22.9 against 30.6).
No key does both, and the bit count must be one value at the call.

BUILT IN MAIN (17:20 to 18:10; the reflections' passes only, `pt_common.hlsli` untouched): the one call takes its bit
count from the frame word's bit 4 (16 by the material, 0 no hint), and the tracer picks it itself
(`State::ReflectionsSort`, `sort_measured`): it runs its choice, tries the other for 8 frames every 128 (and soon after a
camera cut), compares the two medians of the reflections' stage and keeps the faster by 5 %. Same picture either way;
reordering never off. The 10-second line says the sort, its probes and switches; a switch is logged. The hidden "Speed
trial" 16 holds the material, 32 no hint (for measuring). Source checks updated (one call, bits worked out); 114 pass.
Native E4E77F65BA58 (main, hash-checked in the game 18:24). Its check in the game: recipe `sort` (held by the material,
held with no hint, then the tracer's own pick for 60 s).

CORRECTION, THE SAME EVENING (run `20261010-183839`, `sort`, CLEAN: no other program on the card, the game in front
throughout): the reflections' stage, held by the material / held with no hint / the tracer's own pick: the beach 73.3 /
75.5 / 74.0, the town 22.0 / 30.5 / 22.6 (the pick stayed on the material, 8 probes, 0 switches; the pictures the same).
So with nothing else on the card THE MATERIAL'S KEY IS AS FAST AT THE BEACH (73.3, not 91 to 93), and round 6's clean
runs had said so too (the beach's reflections 67 to 73 in front, `20261010-160321`). "No hint wins at the water" came from
runs with Sunshine on the card (round 6's study 4.4 to 4.7 %, my trial 4 %): Sunshine slowed the sorted pass by about 18
ms and the unsorted one not at all. I read those runs step against step, believing the other program slowed every step
alike: it does not, it slowed one variant and not the other. A coordinator's mistake, told to the owner; the rule in
AGENTS.md now says not even step against step. What stays true and useful: the tracer's own pick finds the faster sort
as it runs, so while the owner streams with Sunshine it can take no hint at the beach and win back those ~18 ms; it costs
8 probe frames in 128 (in the town about 0.5 ms a frame on average). Kept, for the owner's word.

## 5. The game's own passes while tracing (PT-107): not where the time is

Built in main, plugin only: `GamePasses.cs`, a Harmony postfix on HDRP's per-camera `FrameSettings.AggregateFrameSettings`
that clears chosen frame settings for the gameplay camera while the traced light is on screen; the hidden
`Advanced / Game passes while tracing` (1 skin scattering, 2 transmission, 4 shadow maps, 8 reflection probes, 16 the
fog's lighting; 0 by default); recipe `game-passes`. Run `20261010-181120` (CLEAN: no other program on the card, the game
in front throughout; native 6C54E84BCA33, plugin 583F49AF2BAF), ms a frame:

| Step | tavern: game to composite / game after | town: game to composite / game after |
|---|---|---|
| all passes | 3.8 / 10.3 | 7.7 / 21.0 |
| no skin scattering | 3.7 / 10.4 | 7.5 / 23.5 |
| no transmission | 3.9 / 10.8 | 7.8 / 17.2 |
| no shadow maps | 4.0 / 10.9 | 7.4 / 17.5 |
| no reflection probes | 3.8 / 10.4 | 7.9 / 16.7 |
| no fog lighting | 3.8 / 10.4 | 8.0 / 17.2 |
| all passes again | 3.7 / 12.5 | (no whole line) |

The cuts reach HDRP (the fog's lighting off darkens the picture: the town 51.8 -> 50.4 levels), but none moves the
game's time beyond the run's own wander (the town's "game after" wanders 16.7 to 23.5 in steps that change nothing
there). Our own D3D11 work is small: the composite 0.1 ms, Fog Adaptation 0.1. So HDRP's skin scattering, transmission,
shadow maps, probes and fog lighting are not the 11 to 14 ms a traced frame spends on the game's device beyond the
game's own frame. The switch stays a trial (0); to remove or keep once the gap is understood.

NEXT SUSPECT: frame pacing. The harness keeps the owner's TG Frame Generation "Low latency = Reflex" (frame generation
Off). Reflex starts each frame's CPU work just in time, and our D3D12 trace is recorded mid-frame (render event 1: 11 ms
of the render thread in the town, flush 4.5 and recording 6.1): the card may idle while it waits for the CPU. New
harness option `-LowLatency <value>` (this run only; the configs come back after); run with Off started 18:25.

NOT IT EITHER (run `20261010-182435`, `split`, TG Frame Generation's own line: "Frame generation Off, low latency Off";
clean; native E4E77F65BA58): the town 89.3 to 91.7 ms a frame (game to composite 8.0 to 9.2, game after 16.8 to 24.3),
the tavern 44.6 / 48.3 (4.5, 9.3 to 10.2): as with Reflex. Also checked: the only per-frame D3D12 submission is the
trace's own list (no hidden work of ours between frames); the plugin's `ReadPixels` are once a texture, not a frame;
HDRP's water is not drawn where we trace it (`WaterSurfaceRendering.cs`). So the town's extra 10 to 14 ms a frame on the
game's device (its work while tracing, against its whole frame alone) is still unexplained. Next: split "the game
after" at the fog event (the stamps [5] to [6] and [7] to [4] are there; only the summary is to change), and a stamp at
HDRP's post-processing (the seen meter's pass, a native event of its own) to place it.

PLACED (run `20261010-190135`, the town, clean; native 1729BBB83F3C, the 10-second line now says "(to its fog N)"):
from the composite to HDRP's fog 0.4 ms; so the whole "game after" (15.0 to 23.0 ms, wandering between 10-second windows
in a frozen scene) lies between HDRP's fog and the next frame's trace: transparents, post-processing, the upscaler, the
interface, the present, the next frame's depth and G-buffer. That it wanders in a frozen world points at the CPU (the
card waiting for commands) more than at GPU work. Next count (native 23D6518CD162): at each frame's render event 1, was
the frame before's trace already done on the card? Nearly always: the CPU is late and the card idles; rarely: the CPU
is ahead.

ALWAYS (run `20261010-191319`, clean, Reflex on): 100 % of frames in the tavern and the town. The tavern 43.9 / 48.1
ms a frame (no denoiser / NRD), the town 88.5 / 89.3; "game after" 9.2 / 9.0 and 19.7 / 15.6. So the CPU is never a
frame ahead: each frame's render event 1 comes after the card has finished the frame before's trace.

## 6. The frame queue trial, and the game hung

The game's pacing as Unity holds it (a log line of the plugin's, once a session): frames queued 2, VSync count 1, no
frame-rate target. A trial set `QualitySettings.maxQueuedFrames` at runtime (a hidden setting and a recipe `queue`),
run `20261010-192506` (the town, Low latency Off, clean): as the game 95.2 ms a frame, 2 queued 91.3, 3 queued 84.7
(noisy: "game after" 27.5, 23.0, 16.4, which wanders like that in a frozen scene anyway), the CPU late on 100 % of
frames in every step. So more frames in Unity's queue do not let the CPU get ahead either.

AND THE GAME HUNG at step 5 (19:28), when the trial set the queue back from 3 to 2 with the game running. Windows
logged an Application Hang (1002) at 19:33 and closed it; no driver reset. The owner joined over Moonlight at about
19:31 and saw the game frozen; their session had nothing to do with it (the game's logs stop at 19:28). The trial
was removed the same hour (its setting, its session code, the recipe), the plugin rebuilt and installed
(0866DD092985), the owner's configs checked (Frame generation DLSS, Low latency Reflex, Speed trial 0, Mode On).
The pacing line stays, read-only. PROJECT-CONTEXT's method traps: never change maxQueuedFrames while the game runs.

## 7. The CPU's clock against the trace

To tell the two readings apart (the CPU busy, or the game's render thread held behind our trace by the driver: a
D3D11 wait for another device's fence may stall the driver's own thread until the fence is reached, and the game's
thread with it once the driver's queue is full), the tracer now writes the CPU's clock (QueryPerformanceCounter) at
each of its events, and puts each frame's D3D12 trace on that clock (the queue's `GetClockCalibration`, taken when
the frame's list is recorded). The 10-second line's new part, "the CPU's clock against the trace": the trace's start
after its inputs are flushed; the CPU at the composite, at Fog Adaptation and at the next frame's trace, each after
the trace's end; the CPU from the composite's end to Fog Adaptation and from Fog Adaptation to the next frame's trace.
Logging only (native E43DA18A3646, installed 22:19, hashes match). If the CPU reaches Fog Adaptation only just after
the trace's end while the card has 0.4 ms between them, the render thread was held behind the trace.

RUN `20261010-222055` (`tavern-room@split`, `metal@split`, clean, Reflex on as the owner has it; ms, a frame's mean):

| | tavern, none | tavern, NRD | town, none | town, NRD |
|---|---|---|---|---|
| ms a frame | 45.2 | 50.0 | 89.7 | 90.9 |
| the trace starts after its inputs are flushed | 2.5 | 2.7 | 6.1 | 6.2 |
| the CPU at the composite, against the trace's end | -33.0 | -37.7 | -65.4 | -69.8 |
| the CPU at Fog Adaptation, against the trace's end | -32.9 | -37.5 | -65.3 | -69.7 |
| the CPU at the next frame's trace, against the trace's end | 9.2 | 9.2 | 13.6 | 10.4 |
| the CPU from Fog Adaptation to the next frame's trace | 42.1 | 46.7 | 78.9 | 80.1 |

So the render thread is NOT held at our composite: it passes the composite and the fog at once. But the next frame's
trace always comes 9 to 14 ms after the trace's end, whatever the trace's length (33 to 70 ms): the CPU waits for the
trace somewhere between the fog and the next frame's trace, then needs 9 to 14 ms more. NVIDIA Reflex's own report
(TG Frame Generation's 5-second line; it measures our D3D12 queue: "gpuActive" 33 ms in the tavern is the trace)
places that wait in the NEXT frame: the CPU's present ends about 39 ms before the card finishes the frame (latency 84
ms from the simulation's start, 45 ms from the simulation to the present). TG Frame Generation's present takes 0.7 to
1.5 ms on the CPU and never waits for its buffers ("gpuWait 0.00"). With Low latency Off (run `-192506`) there is no
Reflex report, and the present is as short. Seen on the way: our plugin's decal capture runs every traced frame on the
main thread and takes 10.5 to 15 ms there (its own log line "capture ... ms"), and the scene scan 58 to 83 ms each
time it runs (town).

Next marks (plugin 34E8905CCAFB, native 413505260E89, logging only): the next frame's main thread at its Update, the
render thread at a mark issued there (render event 8), and the main thread's capture pass begun and done, each
against the trace's end. If the main thread's Update and capture come early and the render thread's mark late, the
render thread is held (the driver, while the game's device waits for our trace); if the capture comes late, the main
thread is.

## 8. The render thread is held until the card finishes the frame before

Run `20261010-224344` (plugin A96E1A36B13E, native 6DB09708AD56; marks also at HDRP's rendering begun and done on the
main thread, and at HDRP's first command on the render thread; Sunshine up to 4.8 %, so its frame times are not
quoted; the ORDER of the marks is what this run is for). Each mark of the next frame against the trace's end (ms):

| | tavern, none | tavern, NRD | town, none | town, NRD |
|---|---|---|---|---|
| main thread: the frame's Update | -30.6 | -35.2 | -57.0 | -67.5 |
| render thread: at the Update's mark | -30.6 | -35.2 | -57.0 | -67.5 |
| main thread: HDRP's rendering begun | -25.3 | -29.7 | -47.6 | -57.8 |
| main thread: our capture pass begun | -23.2 | -27.6 | -45.3 | -55.5 |
| main thread: our capture pass done | -10.6 | -15.1 | -32.5 | -42.5 |
| main thread: HDRP's rendering done | -9.6 | -14.2 | -31.2 | -41.2 |
| render thread: HDRP's first command | +7.5 | +7.5 | +10.5 | +8.2 |
| render thread: our next trace (render event 1) | +8.7 | +8.7 | +13.4 | +10.3 |

So the main thread has the whole next frame recorded 10 to 41 ms before our trace ends, and the render thread, idle
at the frame's Update, does not START that frame's commands until 7.5 to 10.5 ms after the trace's end, which is
about when the card has finished the frame before (its composite, transparents, post-processing, the upscaler, the
copy to TG Frame Generation's swapchain). Then it needs 1.2 to 3 ms to reach our trace. Neither our code (no CPU wait
of ours runs there; our render-thread work at the trace is 2.7 ms in the tavern) nor TG Frame Generation's present
(0.7 to 1.5 ms on the CPU; its frame-latency object, which the game waits on, "maximum latency 1", is released by the
present on the CPU) holds it. The NVIDIA driver profile of the game (read with NVAPI's driver settings, nothing
changed: `<scratch>\nvdrs\nvdrs.exe`) sets "Maximum pre-rendered frames" to 1 (what the control panel's Low Latency
Mode On sets): the driver keeps the game's device at most one frame ahead of the card, which matches. What it costs: the
card waits for the CPU after each trace (the game's device's "game after", 10 ms in the tavern, 16 to 19 in the town,
against the game's whole frame alone of 10.8 and about 14).

Our capture pass takes 12.5 to 13 ms of the main thread every traced frame, nearly all of it the decals
(`DecalCapture.Capture`: `FindObjectsByType<Renderer>` over the whole scene with every renderer's material array, every
LOD group's level, and `FindObjectsByType<DecalProjector>`, each frame). Not on the frame's critical path while the
render thread is held this way, but on it once the CPU may run ahead: to be cached (the scene scan's list), with
nothing in the picture changed.

The owner allowed one test (22:50, "Sounds a lot like Reflex, doesn't it? But sure"): the profile's setting cleared
("Use the 3D application setting", `<scratch>\nvdrs\prerender.exe ... clear`) for one run with TG Frame Generation's
Low latency Off too, then set back to 1 (`prerender.exe ... set 1`, chained after the run). Not Reflex: the run with
Low latency Off (`-192506`) was held the same way.

NOT THE DRIVER SETTING (run `20261010-225341`, clean but the window not in front 22:57:13 to :19; Low latency Off, the
profile's setting cleared): the render thread was released at +7.8 ms (tavern) and +11.1 (town) after the trace's
end, as with the setting at 1; frames 45.7 and 92.2 ms. The setting is 1 again (read back: "1 (this profile)"). Not
video memory either: the game uses 8.4 to 10.6 GB of its 14.85 GB budget (the bridge's line).

NARROWED (run `20261010-230631`, the town, clean; plugin 6FCF86C179E4, native E24FE52BCB4B: marks also at the
game's LateUpdate, and one queued at once, through `Graphics.ExecuteCommandBuffer`, when HDRP's rendering begins):

| against the trace's end (ms) | town, none | town, NRD |
|---|---|---|
| main thread: Update / the render thread at its mark | -57.4 / -57.4 | -67.1 / -67.0 |
| main thread: LateUpdate / the render thread at its mark | -48.2 / -48.2 | -57.8 / -57.8 |
| main thread: HDRP's rendering begun / the render thread at the mark queued then | -47.3 / +10.7 | -57.0 / +8.0 |
| the render thread at HDRP's first command / at our trace | +10.7 / +13.9 | +8.0 / +10.6 |

So the render thread is held in the 0.9 ms of the main thread between LateUpdate and the start of HDRP's rendering:
what Unity queues there for the render thread (its frame start, skinning, effects) waits until the card has finished
the frame before. Next (native 2CFDD7F7689C): the game's own Map, GetData and Flush on its device timed where they are
made, through the bridge's hooks, only while a file `measure-waits` lies in `BepInEx\TGPathTracing\Native` (made for
the run and deleted after it: nothing for players).

## 9. The call that holds the render thread: Unity's EndWrite of a buffer written from the CPU

Run `20261010-231355` (the town, clean; native 2CFDD7F7689C with `measure-waits`): of about 890 Map calls a frame on the
game's context, one a frame takes 0.5 ms or more, and it takes 57 to 66 ms a frame (the slowest 73 to 81 ms): a 4 MB
(4,194,304 bytes) STAGING buffer mapped READ_WRITE (map type 3) with no "do not wait" flag. Now and then the slowest is a
staging texture of 3840x2160 read back (map type 1; likely the harness's own frame capture). Our own calls are left out
(the bridge's internal mark), and ours use "do not wait" anyway.

Run `20261010-232045` (the town; Sunshine up to 4 %, so no times quoted; native 88240272D127, plugin 36D0B1EB6BA9: a
render mark after each of the 55 steps of Unity's PostLateUpdate, and the slow call's stack): the slow Map comes after
our LateUpdate mark and before the first PostLateUpdate step (`UniTaskLoopRunnerYieldPostLateUpdate`): in the scripts'
LateUpdate after ours or what follows it in PreLateUpdate (Entities' PresentationSystemGroup runs there), long before
HDRP's rendering (step 38, `FinishFrameRendering`). Its stack, by Unity's public symbols (symbolserver.unity3d.com,
`UnityPlayer_Win64_player_mono_x64.pdb`, 23 MB; `<scratch>\symbolize.py`): `BufferD3D11::EndWrite+0x106` <
`GfxDeviceD3D11Base::EndBufferWrite` < `GfxDeviceWorker::RunCommand` < the render thread. That is the render thread's
half of `GraphicsBuffer.UnlockBufferAfterWrite` / `ComputeBuffer.EndWrite`: what the CPU wrote goes to the card
through a staging buffer, and the Map waits until the card is done with it. Three of the game's assemblies call
`LockBufferForWrite`: `Unity.Entities.Graphics` (its `SparseUploader`: the ECS-drawn objects' per-object data, a frame's
changes; `NumFramesInFlight` 3 on Direct3D 11, its buffers reused after 4 frames), `Awaken.ECS` and HDRP's
`RenderPipelines.Core`. So it is not HDRP's rendering: taking HDRP's rendering out (the owner's question, 23:20) would
not remove it; the game's ECS sends its data whatever HDRP draws.

What it costs is the next number: the render thread is released about when the card ends the frame before, so the
card's gap may be short. Native 756E82548BD9 adds two stamps on the game's device, at the next frame's Update mark (the
end of this frame's commands) and at HDRP's first command, splitting "the game after" into: from Fog Adaptation to the
frame's end, the gap between frames, and the next frame's rendering before our trace.

## 10. What a traced frame is made of (answered)

Run `20261010-232944` (`tavern-room@split`, `metal@split`; clean: no other program on the card; the window was behind
another only while the tavern loaded, before any step; native 756E82548BD9; Reflex on as the owner has it). The game's
device, ms a frame:

| | tavern, none | tavern, NRD | town, none | town, NRD |
|---|---|---|---|---|
| ms a frame | 43.5 | 48.1 | 87.3 | 87.7 |
| the tracer's own GPU time | 32.7 | 37.3 | 66.2 | 70.4 |
| the game from our trace's start to the composite | 3.2 | 3.4 | 6.2 | 6.2 |
| the composite to its fog | 0.2 | 0.2 | 0.4 | 0.4 |
| Fog Adaptation's end to the frame's end (transparents, post-processing, DLSS, the interface) | 6.6 | 6.5 | 10.1 | 6.9 |
| the frame's end to the next frame's rendering (THE GAP: Unity's EndWrite wait is in here) | 1.1 | 1.2 | 2.7 | 1.9 |
| the next frame's rendering to our trace (its depth, G-buffer, shadows) | 1.5 | 1.4 | 4.3 | 4.3 |

ANSWER TO PT-107's QUESTION "where do the 13 to 28 ms outside the tracer go": into the game's own work on the card
(about 12.6 ms a frame in the tavern, about 20 in the town, the part before the composite shared with the trace while
the card takes turns), and only 1 to 3 ms into the gap where Unity's render thread waits (section 9). The card is busy
nearly all of every traced frame. So the frame is the trace plus the game's own rendering, and the two ways down are:
the trace itself (three quarters of the frame) and the game's own rendering under it (a quarter; the owner's "prevent
HDRP rendering altogether, which we want to do anyway", 23:20: the right direction, though the EndWrite wait is not
HDRP's and stays). Round 19's "the CPU is always late" was true and costs little: the render thread is released when
the card ends the frame before, and the card then has the next frame's work within 1 to 3 ms. The decal capture's 13 ms
of the main thread a frame is off the critical path for the same reason, a CPU cost to cut later.

NEXT for the game's share: its pass groups timed on the card while tracing (HDRP's custom pass injection points give the
splits: before rendering, after the depth and normals, before transparents, before and after post-processing), the
same in the game alone, then what the traced picture does not need goes (the shared rule: no picture change; the
game's fog, its transparents not yet traced, its post-processing and DLSS and the interface stay until the tracer
has them).

The measurements stay in the code, logging only: the CPU's clock against the trace (the 10-second line's "the CPU's
clock against the trace" and "the next frame after the trace's end"; plugin marks at Update, LateUpdate and HDRP's
rendering), the game device's split above, and the hooks on the game's Map, GetData and Flush with marks after each
PostLateUpdate step, those only with the file `measure-waits` (`<scratch>\measure_read2.py` reads them all). Installed:
plugin 36D0B1EB6BA9, native 756E82548BD9 (hashes match); no `measure-waits` file left; the NVIDIA profile's
"Maximum pre-rendered frames" is 1 again, as the owner had it.

## 11. PT-112: HDRP renders nothing under the traced picture (the owner, 23:50)

The owner: "Let's prevent HDRP rendering, yes. It will also make it easier for us to test in the future, as we know
it'll come from our own rendering whenever there are bottlenecks", and at 23:58: "OK, move fog and transparents over,
then, so we don't render anything with HDRP anymore. Post-processing effects, interface and upscaling I don't consider
part of the rendering per-se". Told: the fog is NOT traced by us today (HDRP renders and lights it; Fog Adaptation
scales its light; PT-37). The entry PT-112 holds the plan: (A) no opaque drawing by HDRP (our depth and motion vectors
into HDRP's buffers, every tracer read of HDRP's G-buffer replaced by its own rays', the yardstick a test switch), (B)
the fog traced, (C) the transparents traced; HDRP's part of each goes in the same job, and nothing vanishes from the
picture on the way.

Built for the first numbers (plugin A9D9070E60CA, native 8A6E9A1D84B5): marks at HDRP's injection points
(BeforeRendering, AfterOpaqueDepthAndNormal, BeforeTransparent, BeforePostProcess, AfterPostProcess) with the game
device's timestamps, only with `measure-waits` ("HDRP's groups on the game's device" in the 10-second line); and the
hidden `Game passes while tracing` grows bits 32 (opaque objects and decals) and 64 (transparent objects), recipe
`no-hdrp` (all passes, no opaque, no opaque nor transparent, all again). With 32 the picture is wrong (DLSS and the fog
get no depth): the run is for the time saved. Queued at 23:50 behind the owner's Starfield (the harness waits while
it is open). A read-only helper lists every read of HDRP's buffers by the tracer.

THE HELPER'S LIST (an Explore agent, read-only): with the tracer's own camera rays (the default) nothing that changes
the picture reads HDRP's G-buffer or motion vectors any more (the old route's code, the debug views, the yardstick's
comparison, the F8 report); HDRP's depth still decides three small things (which sky shows where the ray met nothing,
`pt_primary.hlsli` 557 to 564; glass over HDRP's sky losing its emission, `pt_compose.hlsl` 75; Fog Adaptation,
`d3d11.hlsl` 182), HDRP's exposure and sky stay (not opaque drawing). The real dependency is outside the tracer: the
upscalers (TG Better Upscaling's `StpHost.cs` 568), TG Frame Generation (`HdrpInputs.cs` 95), HDRP's fog, transparents
and post-processing read HDRP's depth and motion, and nothing of ours wrote them. Gap: plants swayed by the wind have
no motion of their own in the tracer yet (`pt_primary.hlsli` 280). The hair layer's depth (CharacterCapture
HoldLayerDepth) goes once ours is written.

RUN `20261010-235004` (tavern, town; clean but the window behind another while the tavern loaded; native 8A6E9A1D84B5,
plugin A9D9070E60CA: the build before A1, so the cut steps had no depth for DLSS and the fog). HDRP's groups on the
game's device with every pass, ms a frame (tavern / town): before rendering 0.5 / 0.4; the depth prepass, G-buffer and
decals (to AfterOpaqueDepthAndNormal, which in deferred comes after the G-buffer) 0.7 / 3.8; to our trace 0.2 / 0.2; the
fog's end to the transparents 0.1 / 0.1; the transparents 0.1 / 0.3; POST-PROCESSING WITH DLSS 6.0 / 6.0; to the
frame's end 0.4 / 0.7; the gap 1.2 / 1.3; plus HDRP's lighting inside the 3.2 / 5.5 before the composite (shared with
the trace's turns). So A is worth about 3 to 7 ms a frame; post-processing and DLSS, which stay, are 6. BUT with the
frame setting OpaqueObjects (and Decals) off the frame went to 667 ms (tavern) and 500 (town): the game's own Map calls
on its device rose from about 890 to about 22,600 a frame, 37 of them blocking (236 ms a frame), all in Unity's
`BufferD3D11::EndWrite` (LockBufferForWrite uploads, after our LateUpdate mark): the game's ECS (Entities Graphics,
by all signs) re-sends its object data every frame when the camera draws no opaque object. That frame setting is the
wrong lever. Next (plugin 7C1770FB18F2, native 9578314385AC): 32 is opaque objects alone, 128 decals alone, and with
32 the tracer writes its own depth and motion into HDRP's buffers (A1: `pt_compose` writes `outputs.dm`, R32G32: the
device depth of the camera's own hit and the motion as two halves; render event 2 draws them into HDRP's depth buffer
and motion vectors, `write_depth_motion`, `d3d11.hlsl` TGPT_DEPTH_MOTION; Fog Adaptation reads that depth). Recipe
`no-hdrp`: all, no decals, no opaque, all again.

The first try of that build traced NOTHING (run `20261011-000324`): the new shared output in R32G32_FLOAT could not be
made shareable on the game's device ("Create D3D11 shared texture 0x80070057"), every frame failed, and after 30 the
tracer turned itself off for the session. Split into two outputs in formats both devices already share (`dm` R32_FLOAT,
`mv` R16G16_FLOAT; native EFC67A691F40). Run `20261011-002044` (clean): DECALS OFF (128) made the frame 4 ms faster
(tavern 45.2 -> 41.2, town 87.7 -> 83.3) but for the wrong reason: `DecalCapture` follows HDRP's decal frame setting, so
the TRACER stopped tracing the decals (its own GPU time 33.1 -> 28.8, the bounced light and reflections each about 2 ms
less) and they would be gone from the picture; HDRP's own decals cost about 0.6 ms (the depth prepass group 1.2 -> 0.6,
5.8 -> 5.2). Two things to keep: our traced decals must not follow HDRP's decal setting once HDRP draws nothing, and the
traced decals cost the tracer about 4 ms a frame (worth its own look). OPAQUE OBJECTS OFF (32) alone gave the 800 ms
frames again: the frame setting, not the decals, wakes the ECS's uploads. Next (plugin 45F3820F7189): 256 leaves HDRP's
frame settings alone and empties its opaque draw lists instead (a postfix on both overloads of
`HDRenderPipeline.CreateOpaqueRendererListDesc`: no layer for the gameplay camera), so HDRP culls and prepares as before
and draws no opaque object; the tracer writes its depth and motion as with 32.

EMPTY DRAW LISTS WORK FOR SPEED (run `20261011-003003`; Sunshine up to 4.7 %, so the frame times are indicative only):
HDRP's depth prepass and G-buffer on the game's device 1.2 -> 0.1 ms (tavern) and 5.6 -> 0.1 (town); the frame 44.8 ->
43.7 and 87.3 (all passes, again) -> 81.3; no burst of uploads. BUT THE PICTURE: the town is all grey fog with only the
flames and the rain's splashes, the tavern's lower half under a teal fog with a straight upper edge (its upper part, the
rafters, is the traced picture). Two faults found on the way: the motion vectors were never written (HDRP's are R16G16
typeless; the colour reading gave no type: now `ReadAs::Motion`), and HDRP's fog reads its DEPTH PYRAMID (`SkyManager.
RenderOpaqueAtmosphericScattering`: `_CameraDepthTexture` = `prepassOutput.depthPyramidTexture`, a copy made after the
opaque passes, before our composite), so the composite writes the frame's depth into the pyramid's first level too
(`TGPT_SetDepthPyramid`; native E1B31EF101A6, plugin 77298BDB0C18; the 10-second line counts the writes: every frame
that asked, the pyramid too). Run `20261011-004911` (the town): written every frame, and STILL all fog. The teal colour
and the straight edge look like HDRP's underwater fog below a waterline (its water pass, `transparentPrepass.waterGBuffer`,
`_WaterLineBuffer`), which with nothing opaque drawn may see the sea everywhere. Test queued (plugin with 512 = HDRP's
Water frame setting): no opaque draws with and without HDRP's water, and without its volumetric fog lighting.

NOT THE WATER (run `20261011-005727`, tavern): the same teal fog with HDRP's water off; without its volumetric fog
lighting the fog turned grey and stayed: the ordinary height fog, seeing every pixel as far. THE FAULT WAS MINE: the
composite drew the pyramid as a second render target beside the motion vectors and the depth buffer, and the pyramid is
a larger texture (its mips packed beside its first level); D3D11 binds no targets of different sizes together, and the
pyramid's writes were dropped without a word (the counter counted the draw, not the write). The pyramid now has a draw
of its own (`d3d11.hlsl` psPyramid; native C6515C716BD5). Run `20261011-010330` (tavern; Sunshine up to 4 %): WITH
HDRP'S OPAQUE DRAWING OFF THE TAVERN LOOKS AS WITH IT ON (`tavern-room/3-no-opaque-draws/frame.jpg` against
`2-all-passes`): the traced picture, HDRP's fog and flames placed on the tracer's own depth.

AND THE TOWN (run `20261011-011046`, CLEAN: no other program, the game in front; native C6515C716BD5, plugin with 512):
| ms a frame | all passes | no opaque draws (256) | 256 + no water | 256 + no fog lighting | all passes again |
|---|---|---|---|---|---|
| the frame | 90.5 | 81.3 | 81.6 | 81.3 | 87.3 |
| the tracer's GPU | 66.0 | 65.4 | 65.8 | 65.6 | 65.9 |
| the game after the composite | 19.9 | 10.8 | 10.4 | 10.2 | 16.5 |
So HDRP's opaque drawing cost 6 to 9 ms of the town's frame; its water and its fog lighting nothing measurable. THE
PICTURE (`metal/3-no-opaque-draws/frame.jpg` against `2-all-passes`): the same scene; mean difference 1.94 levels
against 1.70 between the two all-passes steps, 0.10 % of the pixels more than 20 levels apart, the brightness 52.3
both; the differences sit on foliage (the ivy at the top left, the plants at the stall; `metal/diff-all-vs-no-opaque.png`):
plants in the wind's pose have no motion of their own in the tracer yet (the helper's gap, `pt_primary.hlsli` 280),
so DLSS gets the camera's motion there where HDRP gave the sway's.

What stands between 256 and the rule: (1) the plants' own motion in the wind (the previous frame's pose); (2) the
yardstick needs HDRP's G-buffer: its recipe sets 256 off; (3) clean-up: the trial bits that answered nothing or misled
(32, the frame setting that wakes the ECS: a dead end; 64, 128, 512; 1 to 16 from section 5) go, 256 becomes the rule
while tracing; (4) HDRP's decals still run on an empty depth (about 0.6 ms) and `DecalCapture` follows their frame
setting: make the capture independent, then cut them too; (5) then step B (the fog traced) and C (the transparents).

## State at 01:25 (10-11)

- In the game (hash-checked): plugin 330557A98A6E, native C6515C716BD5 (= `build\native` = `artifacts\Native`). The
  owner's configs as they were (the harness restores them; `Game passes while tracing` not set: 0). No `measure-waits`
  file. The NVIDIA profile's "Maximum pre-rendered frames" is 1, as the owner had it. Nothing committed since d31dff9.
- In main since round 6, uncommitted: the reflections' own sort pick (`State::ReflectionsSort`); the measurements of
  sections 3 and 7 to 11 (logging; the hooks and HDRP's injection-point marks only with the file `measure-waits`);
  PT-112's step A on trial (`Game passes while tracing` 256: HDRP's opaque draw lists empty; `TGPT_FRAME_OWN_DEPTH`: the
  tracer's depth and motion into HDRP's depth buffer, motion vectors and depth pyramid; `pt_compose` writes the shared
  outputs `dm` R32 and `mv` R16G16; `TGPT_SetDepthPyramid`); the other `Game passes` bits (1 to 128, 512) still in, to
  clean up; `Run-PtHarness.ps1 -LowLatency`; recipes `split`, `spec-key`, `game-passes`, `sort`, `no-hdrp`. Source checks:
  114 pass. No whole GPU test run this round (the tracer's passes changed: `pt_compose` writes two more outputs; the
  offline test is owed before this becomes the rule).

## ▶ Starter prompt (R20)

Read AGENTS.md's START HERE, then this file's sections 10 and 11 and "State at 01:25". Write the round as
`docs/rounds/round-20.md`.

The owner's order (10-10 23:50 and 23:58, PT-112): HDRP renders nothing under the traced picture; only
post-processing, the interface and upscaling stay ("I don't consider part of the rendering per-se"); the fog and the
transparent effects move into the tracer ("don't keep them in HDRP. Move them over to our tracer"). Step A (no opaque
drawing) works on trial and saves 6 to 9 ms in the town with the same picture but foliage. Next, in order:

1. Make A the rule: the plants' own motion in the wind (their previous frame's pose; today DLSS gets the camera's
   motion on swaying foliage), the yardstick's recipe with HDRP drawing (it needs HDRP's G-buffer), the decal capture
   independent of HDRP's decal frame setting (then HDRP's decals go too, ~0.6 ms), the trial bits that answered
   nothing removed (32 is a dead end: the frame setting wakes the game's ECS uploads), 256 the default while tracing,
   the offline GPU test once, a look by the owner in play (moving camera, swaying plants).
2. Step B: the game's fog traced by the tracer (PT-37's design: the fog volume's density lit by the traced lights,
   with their shadows), then HDRP's fog off.
3. Step C: the transparent effects traced (PT-71 item 7: the VFX Graph effects from the game's simulation buffers,
   the trails, the transparent meshes), then HDRP's transparent passes off.
Beside it: the trace itself is three quarters of the frame (PT-99's list), and our decal capture costs 13 ms of the
main thread a frame (whole-scene searches) and the traced decals about 4 ms of the tracer's.
Runs with "Other programs used the GPU" are not quoted for speed, not even step against step (section 4).
