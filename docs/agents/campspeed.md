# campspeed: the camp in the forest at 3 frames a second (PT-131)

Worker: Claude (Opus), started 2026-10-12 00:02. Copy `%TEMP%\tg-agents\wt\campspeed`, base `base20f`.
Brief: `docs/agents/briefs/brief-campspeed.md`. Build folder: `%TEMP%\tgb\campspeed-measure` (the measuring build),
scripts `%TEMP%\tgb\campspeed-scripts`.

## In short (kept current)

- NOTHING IS MEASURED YET. The measuring build is being made (see "State").
- By reading: of the two special branches only the WATER'S PATH changes with the Caustics row. The glass walk reads
  the row nowhere. In the water's path the row decides how `water_sun` answers at every scatter point: row off, one
  straight shadow ray; row on, the sun through the waves (kept tiles, or a patch of waves made at the point: up to 40
  rounds of up to 33 x 33 corners, 15 texture reads a corner), and its two shadow rays come LAST.
- The suspect, to be shown or refuted by the counters: rays that reach the sea's surface far from the camera or where
  it runs on under the land (the offline sea fixture's own comment: "the open sea's level running under the whole
  land", PT-80). Their scatter points lie past the kept tiles (about 60 m), so each makes its own patch; a group of
  32 threads waits for the one that draws the big patch; and under the land all of it ends at a shadow ray the land
  stops.
- Two candidate directions are IN the measuring build as switches, so that one game run shows them on and off:
  "shadow first" (1024) and "the cell by the ray's width" (8192, on trial; below).

## The plan

1. Read the two special branches for what the Caustics row changes in them. DONE (below).
2. A third page of measuring switches and counters in the log. DONE in the sources (below).
3. One measuring build and the recipe `camp-measure`, handed to the coordinator. BUILDING.
4. With the game's numbers: the fix, the same picture (no cap, Shader Execution Reordering on).

## What the reading found (nothing here is measured)

- `shade_path` (pt_common.hlsli) has two special branches: glass followed (`hit_is_glass`, `dielectric_follow`) and
  the water's own path (`water_path`). The measuring switch 16384 took both out together: 31 ms against 366.
- The Caustics row (`TGPT_OPTION_NO_CAUSTICS`) is read in these places only: `water_passes_light()` (shadow rays pass
  the surface; `water_sun`'s straight ray; `water_vertex_lights`), `caustic_through` (glass in a SHADOW ray's way: not
  in the glass walk, and not switched by 16384), `caustics_on`, and the tracer (no kept corners, no caustics' stage).
  So inside the two branches the row changes: (1) `water_sun` at a scatter point, (2) the landed light at a solid
  point under water (`caustic_light`, one short ray), (3) which shadow rays stop at the surface.
- The row does not steer a path: which rays reach water, and how long their paths are (the water's scattering colour
  and chance), is the same with the row on and off. So the row's 330 ms are per-event cost, not more events.
- The water's three hidden trials (shore foam, lamps in the body, light without the view) are off in the plugin, so
  `water_vertex_lights` (a lamp list walked at a vertex, a solve of 16 rays a lamp) does not run.
- The camp's run was by day (game time 16:35): there is a sun, `water_sun` runs at every scatter point.
- `water_sun` with the row on: a ray up to the waves; the kept tiles if the point's block of tiles lies inside the
  kept corners (1,024 a side round the camera, about 60 m; `docs/agents/perf-1.md` perf2.4); else a patch made at the
  point. The patch is all or nothing by chance (48 corners always, a bigger one with the chance 48 / corners, counted
  for more). A group of 32 threads runs the big patch whenever ONE of them draws it: at a chance of 5.6 % a thread,
  84 % of the groups pay for it. Then the foam, and two shadow rays (the point to the surface, the surface to the sun).
  The shadow rays are the last thing asked: where the land stops them, the tiles and the patch were for nothing.
- The reflections' 345 ms over 2.09 million pixels are 165 ns a pixel OF THE WHOLE SCREEN; a water pixel at the
  beach costs 86 ns (perf-1). So at the camp many pixels' rays, or their groups, do patch work.
- In bottomless water (the sea's sheet under the land has no bed under it) a path never meets a solid: it scatters
  until chance ends it, 2.5 scatter points on average by the sea's scattering colour, each asking `water_sun`.
- A path that meets the surface from below starts "in the water" (`water_path`: `entering` false). Counted, in case
  water stands over the camp's height.

## The two directions in the build

### "Shadow first" (third page, 1024): a measurement, not a fix as it stands

`water_sun` sends the two shadow rays the chosen cell sends at the end FIRST, from the place the point's own bent
ray met the waves; where they bring nothing, no tile is asked and no patch made. It moves a shadow's edge on the
water's body light by a patch's width, so it changes what the picture converges to there: it is in the build to
MEASURE what the points no sun reaches cost. An exact form exists (where the first rays bring nothing, make the
patch with a small chance and count it for more: the same mean, noise at a shadow's edge only), but a group of 32
still pays whenever one thread draws the patch, so it pays only with the patch-makers regrouped (the trial
`TGPT_REGROUP_WATER`'s number 4, built by perf-1, never run in the game).

### "The cell by the ray's width" (third page, 8192): ON TRIAL, the candidate for the fix

A patch of waves made at a point is cut into cells of half a texel of the finest wave band that still moves the
surface there (8 cm for the sea's ripples), whatever looks at the point. But the patch's sum is exact for ANY cell
(`pt_water_sun.hlsli`'s own top: "every cell's light lands somewhere, once; the cell is how fine the light's pattern
inside the water is"), and the grid is shifted by chance, so a coarser cell gives the same light with a coarser
pattern. And how fine a pattern a ray can show is its own width there (the ray cone the tracer already carries for
its textures' mips, PT-46): a pixel's width seen straight or in a mirror; metres after a rough reflection, a diffuse
bounce or a scatter point. A pattern finer than the ray is wide is averaged by the pixel anyway, as a texture's
texels under a mip are.

The trial: a patch's cell is no finer than HALF the width of the path's own ray at the point (and, as before, no
more than half the way to the surface). The kept tiles are asked as before. `water_path` carries the width: at its
start the width of the ray that met the water (`Hit.footprint`), growing along each stretch by the cone's spread;
the water's rough facets add their lobe, a solid and a scatter point make it a diffuse ray's (`DIFFUSE_SPREAD`).

What it would do at the camp, if the counters say "patches": a reflection ray off wet ground or a bounce ray is
metres wide where it meets water 60 m and more away, so its patch is 9 to 25 corners in place of hundreds, always
made (under the 48 of the budget: no chance, no group waiting for a big patch, LESS noise). A mirror's or the
camera's own ray stays as fine as today. What it does not do: nothing for the camera's own first scatter point in
the sea 60 to 130 m out (the ray is narrower than the waves' cell there).

To judge: the step's speed, its counters ("made coarser by the ray's width", corners a patch), and its picture
against step 2's (`frame.jpg`). Offline, when the direction holds: the sea fixture's body part (the sun's share of
the picture with and without it, 256 frames), a mirror over the sea (must not change), a mutant.

## The measuring build (what is added; all of it between `#ifdef PT_SPEED_MEASURE` / `TGPT_SPEED_MEASURE` and its end)

A build for players holds none of it (the passes' front end compiles with and without the define; source checks 116 OK).

### A third page of the hidden `Speed trial`

The first two pages have no free number. The third is asked with **32769 + switches** (the second page's 32768 and
the 1 that is the rule in every frame and so asks nothing by itself). `tracer.cpp` hands a third-page number's
switches to the passes in `FrameConstants.waterCeiling`'s y (0 in every other frame; no row added, no size change)
and none of the first two pages' with them; the log says "measuring switches of the third page ...".

| number | `pt_common.hlsli` | what it does |
|---|---|---|
| 2 | `SPEED_M3_NO_GLASS` | glass is not followed (`hit_is_glass`) |
| 4 | `SPEED_M3_NO_WATER` | the water's own path is not run (`shade_path`) |
| 16 | `SPEED_M3_NO_SUN` | `water_sun` gives none |
| 32 | `SPEED_M3_NO_PATCH` | no patch of waves made at a point (the kept tiles alone) |
| 64 | `SPEED_M3_NO_SCATTER` | the water's body scatters nothing (the path goes on) |
| 512 | `SPEED_M3_ONE_TURN` | the water's path ends after its first turn |
| 1024 | `SPEED_M3_SUN_SHADOW_FIRST` | `water_sun`'s two shadow rays first (above) |
| 2048 | (the tracer's, as on the second page) | every ray ends after 60 m |
| 4096 | `SPEED_M3_NO_LANDED` | no landed light asked at a solid point under water |
| 8192 | `SPEED_M3_SUN_FOOTPRINT` | ON TRIAL: a patch's cell no finer than half the ray's width (above) |
| 16384 | `SPEED_M3_GLASS_ONE` | the glass walk ends at its first interface |

128 and 256 are not used (the tracer bakes the leaves' micromaps at another level with them, on every page).

### Counters, in the tracer's log once a second

`pt_common.hlsli` `SPEED_COUNT_LIST`: 46 counters a pass, each thread's own, added at its pixel's end to a small
buffer (`speed_count_flush`; 2 x 48 words, running sums; `tracer.cpp` copies it back every frame and takes one
frame's from the next). The bounced light's pass and the reflections' alone hand the buffer in (their pass
constants' last word, `pass.b.w`, which no other pass sets). A thread that met nothing special adds nothing.

The line (one a second while a frame is traced; means A FRAME; "bounced light" then "reflections"):

```
Special branches 1s: N frames; a frame | bounced light: pixels P; water paths W (from below B, start past 60 m F,
from a point H; their starts D m from the camera on average, O over its height by U m on average, the rest under it
by V m), turns T (in the water Ti; paths of 8+ turns a, 32+ b, 128+ c), scatter points S, surface met C, solid shaded
Z, landed light asked L; sun asked Q (straight, the row off: R), surface found A (mean way M m; the path's ray there
w m wide on average, under 10 cm at n), from tiles K (tiles asked k1, corners k2), patches made X (turns x1, corners
x2, of 256+ corners x3; why: no kept corners, not their level plane, their cell too coarse, tilt unbounded, block
unsolved, off the grid, too many tiles; made coarser by the ray's width c); the sun's light reached the point Y1, was
stopped on its way Y2, none landed Y3, stopped by the shadow rays first Y4; glass walks G, interfaces I, medium
triangles J, ended on water E | reflections: ...
```

What each number answers:
- `water paths`, `from below`, `start past 60 m`, the starts' mean distance and height against the camera: WHERE the
  water is that the rays reach (the sea under the camp: under its height by some 20 m; water overhead: over it).
- `turns`, `scatter points`, `paths of 8+ / 32+ / 128+ turns`: how long the paths are.
- `sun asked`, `from tiles`, `patches made` with turns and corners, and the seven reasons: what `water_sun` does and
  why a point makes its own patch. `the path's ray there ... wide`: what the trial has to work with.
- `reached / stopped on its way / none landed`: how much of that work brought light. "Stopped on its way" is work
  for nothing.
- `glass walks`, `interfaces`, `medium triangles` (the triangles `optical_medium`'s rays without an end are handed).

### The recipe `camp-measure` (tests/ingame/scenarios.json; scene `forest`; each live step walks 150 m, holds 30 s)

| step | `Speed trial` | what it says |
|---|---|---|
| 1-game | | the game's own picture |
| 2-everything | 0 | the base (366 ms on 10-11), and the counters as they are |
| 3-no-water-path | 32773 | the water's path alone out. Fast: it is the water (and glass is cleared: 16384 took both) |
| 4-water-no-sun | 32785 | `water_sun` gives none. Fast: it is the sun through the waves |
| 5-water-no-patch | 32801 | no patch at a point. Fast: it is the patches, not the tiles |
| 6-water-sun-shadow-first | 33793 | the shadow rays first. Fast: the cost is at points no sun reaches |
| 7-trial-cell-by-ray-width | 40961 | ON TRIAL: the candidate fix. Fast, and the picture as step 2's: the direction holds |
| 8-off | 0 | everything put back |

The Caustics row stays on throughout (its "off" is known: 32 ms; and the row does not change which paths run).
Read from each step: the tracer's 10 s lines (stage times) and the "Special branches" lines of its 30 s.

### Files changed

| file | what |
|---|---|
| `native/pathtracing/shaders/pt_common.hlsli` | the third page's switches, `speed_measure3`, the counters and `speed_count_flush`, the trial's two widths; `hit_is_glass` and `shade_path`'s water path read 2 and 4 |
| `native/pathtracing/shaders/pt_water_path.hlsli` | counters; 64 and 512; 4096 beside the second page's; the ray's width carried along the path |
| `native/pathtracing/shaders/pt_water_sun.hlsli` | counters; 16, 32, 1024, 8192 |
| `native/pathtracing/shaders/pt_dielectric.hlsli` | counters; 16384 |
| `native/pathtracing/shaders/pt_gi.hlsl`, `pt_specular.hlsl` | one call of `speed_count_flush` each |
| `native/pathtracing/src/tracer.cpp` | the third page (trial, log), the counters' buffer, read-back and log line, `dispatch_pass` hands the buffer in |
| `tests/ingame/scenarios.json` | the recipe `camp-measure` (replaces the first-page one of 10-11) |
| `tests/water_sea_gpu.inl` | the sea fixture's cost part: one more arrangement, "the same sea under a roof 30 m up (in view; no sun reaches it)", 91 frames at 640 x 360 (not run yet) |

No export, no record, no FrameConstants row, no option or material bit, no plugin change.
`waterCeiling.y` (was always 0) carries the third page's switches in a measuring build.

## For the coordinator: the run

1. Deploy `...\campspeed-measure\TGPathTracing.Native.dll` (hash in the hand-over) with
   `Deploy-PtBuild.ps1 -Native -NativeFrom <it>`; the plugin is main's.
2. Copy the recipe `camp-measure` from this copy's `tests/ingame/scenarios.json` (lines 5 to 14) into main's.
3. `Run-PtHarness.ps1 -Scenario forest@camp-measure -TimeoutMinutes 30` (new passes: about 7 minutes first).
4. Send back, for steps 2 to 7: the stage times (tracer GPU, reflections, bounced light) and two or three
   "Special branches" lines of each step's hold (`TGPathTracing.Native.log`, grep "Special branches"), or the run's name.
   And whether step 7's picture is step 2's (`forest\7-...\frame.jpg` against `2-everything\frame.jpg`).
5. Put main's build back. A measuring build is never left in the game.

## Rules met on the way

- The coordinator, 00:35: NO GPU test of any kind while Starfield is open (another worker's offline test hung the
  GPU beside it at 00:20). The main tree's `-GpuCheck` exits 1 then. `campspeed-scripts\sea_cost.ps1` asks it, and
  for the process by name, right before every test start. No GPU test of this worker's has run.

## State when stopped

01:22, HANDED OVER, waiting for the game's numbers: THE MEASURING BUILD IS MADE (57 passes in 51 minutes behind the
coordinator's builds, 0 failed; the native DLL and the test exe linked):
`%TEMP%\tgb\campspeed-measure\TGPathTracing.Native.dll`, 33,704,448 bytes, SHA-256
`B386C2E73DAEFD6D2CAC3AF32125751D7F3E422686F174EED6B41C79B07C8FCA`. Source checks 116 OK.

BUILT, NOT RUN. Nothing offline is measured. What is known of the DLL at run time: it loads, its exports are found
and it takes the shared device (the offline test's first lines); whether its passes are made by the driver and
whether the counters' line appears is NOT known.

WHAT WENT WRONG, for the coordinator: this worker's one small offline run (the sea fixture's cost part) started at
01:15:48, when the main tree's `-GpuCheck` exited 0, no `RUNNING` file stood and no Starfield was open. Its first
minutes are the driver making the passes of a new build. The coordinator's harness run `20261012-011822` started the
game at 01:19:11 BESIDE it: a test that is making its passes does not show as GPU use to the harness's watch. Seen at
01:20:54 and stopped by hand at 01:21:10, before the test traced a frame. So from 01:19:11 to 01:21:10 that run had a
second program making 57 passes (CPU, and the test's video memory) beside the game: its frame times of that stretch
(its start, steps 1 and 2) are not to be trusted. The script now ends its own test the moment the game, Starfield, a
`RUNNING` or a `GAME-WANTED` file appears (`campspeed-scripts\sea_cost.ps1`, `Test-Held`), and tries again after
the wait. The gap itself is the harness's: an offline test that is still making passes is invisible to it.
No process of this worker's is running (checked 01:21:50).

When resumed with the numbers: (1) read them against "What each number answers"; (2) the fix by what they say (the
candidate: "The cell by the ray's width", as a rule and not a measuring switch, with the path's ray width carried in
every build); (3) its offline check with `sea_cost.ps1 -Part body` (the sun's share of the picture with every patch
made at its point, with and without the change) and `-Part cost` (the roofed sea), only while the check allows.
