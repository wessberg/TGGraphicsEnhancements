# PT-14: water caustics right and affordable (caustics-3 agent, 2026-10-07)

Copy `wt/caus3`, base `wt/base31`. Brief: `<scratch>/brief-caustics-3.md`, narrowed twice by the coordinator (the tracer's own
camera rays come first; finish what does not depend on how the camera's first surface is found, then stop and report).
Work list: `docs/agents/review-caustics-gpt.md`. Nothing run in game, nothing deployed or committed.
Build folders `%TEMP%\tgb\caus3-*`, scripts `<scratch>/caus3/`.

## First lines

- Built and tested: (1) the caustics' lookup is one short ray against an acceleration structure of the landed triangles, in
  place of the world-space hash: the test lake with 25 lamps went from 33.7 ms to 4.4 to 6.1 ms between runs (caustics and
  composite together, 1280 x 720; 1.9 ms with the Caustics row off); (2) the proof that HDRP's real waves arrive: an offline check with buffers as HDRP makes them against
  HDRP's formula (0.015 mm, 0.05 degrees off at most), and a log line in game that says what the waves are where the tracer
  reads them.
- Changed with it: the light is stored as floats (no half clamp), the lookup has no walk limit, a landed triangle's
  tolerance is 5 cm at most (was a tenth of its longest edge without a cap), the build takes what was written and not the
  whole room.
- Not built, on the coordinator's word (it would start a new part): correct receivers, caustics in mirrors and bounced
  light, the limits of review 9 beyond the three above, the wet floor. A first build of the receivers' fix (beams) exists as
  a prototype and is NOT in the code: it hung the GPU on the lake test. Section 4 has it, its numbers and the design.
- No export, struct or ABI change. `FrameConstants` keeps its size (7040 B); two of its fields mean something new (section 6).
- Checks: source checks 89 OK. GPU test: the whole `tgpt_gpu_test` passes ("all path tracing checks
  passed", 1,157 checks, `%TEMP%\tgb\caus3-test-final.txt`). The waves' check's four mutants each fail it.
  After that run only comments and line endings changed; the checks up to the caustics pass again on that last build
  (`caus3-test-last.txt`, DLL 6D9F60CED371).

## 1. The real waves, end to end (brief item 1)

What was wrong (review 1, confirmed, fixed by the coordinator before this job): `WaterCaustics.cs` read HDRP's wave buffers
with `as Texture`; they are `RTHandle`s, so both came back null and every slot was traced as still water. No check saw it,
because every offline check handed the native side a plain 2D texture with one band.

What this job adds:

- An offline check with the buffers as the plugin hands HDRP's over (`tests/pathtracing_gpu_test.cpp`, "HDRP's real waves,
  end to end"): two texture arrays of half floats (`R16G16B16A16_FLOAT`), a slice a band, 64 x 64 with a full mip chain,
  made with the bind flags of a random-write render texture (shader resource, render target, unordered access). The lower mips hold a wrong value (7.0), so a copy of
  the wrong mip fails. Three bands, each with its own patch size, offset, amplitude and distance fade; the surface turned
  about the world's up and moved; 14 states (the bands' offsets moving as time does, the turn and the move changing, one, two
  and three bands, and one state without buffers).
- How the tracer's surface is read: `pt_caustics.hlsl` writes one grid point a frame per slot into its counters (the point's
  x and z, the surface there, its normal); `tracer.cpp` logs it for every frame while `TGPT_LOG_WATER_SAMPLES` is set (the
  test sets it). The test works out HDRP's formula at that point on the CPU, from the same texels with the same filters
  (bilinear for the displacement, a cubic B-spline for the gradient), written from the description of HDRP's
  `SampleWaterSurface` in `docs/agents/caustics-2.md` section 1, not from the shader.
- Result: 14 of 14 states sampled; the tracer's surface is 0.0103 mm off HDRP's formula at most (0.0148 mm in the last run:
  the grid's point moves every frame), its normal 0.051 degrees
  (the waves there move the surface 13.8 to 41.3 mm, slopes to 0.154); without buffers the surface is at its level exactly.
- Mutants (`<scratch>/caus3/mutate.py`, `run-final.ps1`; outputs `%TEMP%\tgb\caus3-test-mW*.txt`). Each fails the check.
  The runs were meant to be killed at the FAIL line; the kill did not fire (my script's quoting), so each ran on to the
  end of the caustics' checks, about 2 minutes.

  | Mutant | The surface off by | The normal off by |
  |---|---:|---:|
  | clean | 0.0103 mm | 0.051 degrees |
  | mW1: the horizontal move left out | 37.26 mm | 0.057 degrees |
  | mW2: the first band alone | 12.28 mm | 8.17 degrees |
  | mW3: the surface's turn ignored for the normal | 0.0104 mm | 13.91 degrees |
  | mW4: the distance fade ignored | 5.30 mm | 9.53 degrees |

- In game, the proof is in the native log's 10 s line, after the caustics' triangle count:
  `water as the tracer reads it: slot 0 waves up to 3.0 cm high, slopes to 0.471 (at x .. z .. the surface is at .., normal ..)`.
  A slot traced as still water shows `0.0 cm` and `slopes to 0.000`. One tile corner in 256 counts for the two maxima.
- The managed half: the source check now holds the line that takes the texture behind the handle
  (`AccessTools.Property(handle.GetType(), "rt")`). The plugin's own log line is the coordinator's ("has its waves in the
  tracer" / "is traced as STILL water").

What is NOT checked: the tracer's surface against what HDRP itself drew (its depth and normal at water pixels). That needs
the water's pixels, which only the water passes have today; with the tracer's own camera rays the water is an ordinary
surface and the comparison is one debug view (the tracer's wave point along the camera ray against HDRP's depth there).
Also not followed, as before: HDRP's current maps, water masks and deformers (review, "full parity").

What the coordinator's run must show (`lantern-lake`, `lantern-seacave`, `water-day-evening`):
- BepInEx log: "has its waves in the tracer (N bands, ...)" for each water surface in reach.
- Native log: "water as the tracer reads it: slot i waves up to X cm high" with X above 0 where HDRP's water moves.
- Stage times with the Caustics row on: "caustics" a millisecond or two; "composite" within a few ms of the row off (it was
  40 to 50 ms more); "water reflections" within a few ms of the row off (it was 50 to 60 ms more).
- The picture: lines on the bed and on walls near the water now that the waves bend the light. Still wrong, and expected:
  flat patches with straight edges where wide landed triangles meet rough rock, strips without light at ledges, nothing in
  mirrors or bounced light (section 4).

## 2. The cost (brief item 2)

Measured first, from the game's own log (run 20261007-140305, `TGPathTracing.Native.log` lines 4514 to 4569), the same lake
with the Caustics row on and off:

| | caustics stage | composite | water reflections | triangles a frame |
|---|---:|---:|---:|---:|
| row on | 4 to 4.5 ms | 40 to 50 ms | 73 to 84 ms | 170k to 335k |
| row off | 0 | 0.4 ms | 23 to 25 ms | |

So the cost is `caustic_light` itself, called by the composite and by `pt_water_under`: about 45 ms and 55 ms. Review
confirmed. Why: the hash walked 8 buckets at every level that held a triangle, and followed each bucket's linked list; a
triangle sits in the bucket of its box, so thin slanted triangles (light meeting the water at a low angle) stack by the
hundred in one bucket. Offline it only showed with many lamps: one lamp over a lake-sized pool cost nothing measurable
(composite 2.81 ms on, 2.83 ms off); 24 torches round the lake's edge and the lamp: 33.7 ms.

The fix: the landed triangles' corners are the vertices of an acceleration structure (a bottom level of plain triangles, a
top level with one instance), built every frame after the light is traced; `caustic_light` sends one ray through the point
along its normal and adds every triangle it crosses whose own tolerance holds the point. The graphics card's ray hardware
does the search.

| the test lake, 25 lamps, 1280 x 720 | caustics stage | with the composite |
|---|---:|---:|
| the hash (base) | 0.65 ms | 33.67 ms |
| the structure, ray reach 0.5 cm + 0.2 % of the distance (first build) | 0.78 ms | 1.87 ms |
| the structure, each triangle's tolerance (10 % of its edge, no cap) | 1.58 ms | 5.11 ms |
| the structure, tolerance 5 cm at most (what is in the code) | 1.64 ms | 4.44 ms |
| the same, the last run (it took 166 s against 121 s: the GPU may have been shared) | 2.22 ms | 6.14 ms |
| the Caustics row off, the last run | 0 | 1.87 ms |

Other numbers:
- The build must not take the whole room: after a busy scene had grown it to 16 million triangles, a build over the mostly
  unused room took 10.7 ms. The build now takes the last frames' count and half as much again (65,536 at least); a frame that
  writes more than that loses the excess for a frame or two (counted, and the next build is larger).
- Memory: room for a million triangles is 158 MB with the structure (it was 64 MB and 8 MB of buckets). The room grows by
  doubling to 8 million at most.
- A 120 m sea under the sun: the caustics stage 0.35 to 1.2 ms between runs (the stage timers publish about once a second:
  short measurements jitter; for the same reason the lake's reading with the row off repeated the reading with it on in
  the last runs, so the table gives the totals with the row on only).
- At the game's 1924 x 1084 the lookup's share scales with the pixels (2.3 times the test's): expect the composite a few
  ms over its time with the row off at a lake with many lamps. Not measured in game.

Not done from the brief: the timers split into reflection, refraction, body light and lookup. The water passes go with the
camera-ray change, so I left their timers alone; the lookup's cost shows as the composite's difference with the row on and off.

## 3. What is in the code now (files)

- `native/pathtracing/shaders/pt_common.hlsli`: the caustics section. `caustic_light(p, n)` keeps its name and meaning
  (irradiance / pi on a diffuse surface), so its two callers are untouched. The hash's functions are gone
  (`caustic_bucket`, `caustic_cell`, `caustic_covers`, `load_caustic`, `caustic_corner_light`, `CAUSTIC_WALK`,
  `CAUSTIC_RECORD`). `caustic_tolerance` is capped at 5 cm.
- `native/pathtracing/shaders/pt_caustics.hlsl`: a landed triangle is stored as corners and light (floats) with its
  tolerance; mode 0 empties the counters, mode 2 makes the unused room inactive; `water_point` also returns how far the waves
  lifted the point and its slope; the counters carry the waves' maxima and sample. The light's tracing is unchanged.
- `native/pathtracing/src/tracer.cpp`: the buffers, the two builds, the count a build takes, the read-back and the log.
- `tests/pathtracing_gpu_test.cpp`: the lake cost case (with a check: under 10 ms), the waves' check.
- `tests/source_checks.py`: the caustics' assertions follow.
- `docs/agents/caustics-3-beams/`: the prototype of section 4, as text files. Not compiled, not part of the build.
- Untouched: `pt_compose.hlsl`, `pt_water.hlsl`, `pt_water_under.hlsl`, `d3d11.hlsl`, the plugin.

## 4. Receivers and the rest: what I measured, and the design

### Subdividing landed triangles: measured, does not work

I built it first (split a landed triangle by its edges' midpoints when it is wide, spans two surfaces or a missed ray, up to
5 rounds, through a work queue). On the test lake it wanted 39.5 million triangles a frame (the room is 16 million), queued 11
million splits a round, still left 3.3 million triangles out at edges, and the caustics stage took seconds. The reason is not a
bug: 24 torches low over steep waves send each 2 cm cell of water onto about a square metre of far wall. A flat triangle
that wide must be cut about a thousand times to follow rough rock. Removed.

### Beams: the right estimator for receivers, prototype only

Keep each water triangle and its three rays as a prism (a beam). A surface point inside the prism takes the light through
the water triangle over the prism's cross-section at that point, from the direction the rays run there, if one ray from the
point back to its place on the water is clear. This is "Interactive Rendering of Caustics using Interpolated Warped Volumes"
(Ernst, Akenine-Moller, Jensen, 2005). What it fixes by construction:
- review 2: nothing is lit that the light does not reach (the ray back to the water);
- review 3: no beam is left out because its rays landed on different surfaces or one went into the sky;
- a wave's focusing inside one cell comes out of the prism's shape (rays that close in), not only from cell to cell;
- the water's absorption and the fog are taken over the point's own distance (review 7's incoming leg);
- the light's direction is known at the point, so a glossy or metal surface can show it (review 5).

The prototype (`docs/agents/caustics-3-beams/`): the beam record and its box, the lookup `caustic_irradiance(p, n, shading,
f0, alpha, v, out diffuse, out specular)`, called from `direct_light`, so every path vertex, every reflection's hit and the
bed under water got it without a special case. Its result: a 120 m sea made 246k beams and frames took 550 to 670 ms; the
lake test hung the GPU in the bounced light stage (driver reset). Cause: a beam counts from the nearest thing its rays met
to the farthest. Where the three rays land far apart (every beam off steep waves does), that part of the beam is metres
long and its box holds half the room; every lookup then visits thousands of boxes and sends a ray for each beam that holds
the point.

### The design for the rest, against a surface record

One function, no G-buffer in it. Any ray that meets a surface (camera, mirror, bounce) has the surface's position,
triangle normal, shading normal, albedo, f0, roughness and the direction it is seen from; it calls
`direct_light` with them, and `direct_light` adds `caustic_irradiance` (diffuse and specular) to the sun's and the lamps'
light. Nothing else asks. What I need from the camera-ray pass (`wt/hair6`): that it shades its hit through `direct_light`
(or calls `caustic_irradiance` with its surface record), and that water is a surface whose refracted ray's hit is shaded the
same way. Then `g_firstCaustic`, `caustics_at` in the composite and the bed lookup in `pt_water_under` go.

The cost plan, in the order I would build it:
1. Boxes that fit. Cut a beam's counted part into pieces along its rays (a box a piece, 4 at most), and give a beam whose
   rays landed together (within its own breadth) a single thin piece. Measure the boxes a lookup visits; the target is
   under 16.
2. The ray back to the water only where it can matter: when the point is off the plane of the three landing points by more
   than the tolerance, or the beam's rays landed apart. On a wall the rays hit together, the three rays already proved the
   way is clear.
3. Fewer, wider beams where the light scatters. A tile whose rays land many pixels apart (steep waves, low light) emits
   cells of 3 x 3 or 5 x 5 with the same light in all; the tile already has every corner's ray. This bounds how many beams
   hold one point. On the lake test a wall point lies in about a thousand beams per lamp at 2 cm cells (my estimate from
   the geometry: 0.4 m waves over 12 m of water; not measured).
4. Tiles none of whose rays land in or near the view go in cells of 5 x 5 (in the prototype). This replaces the view cull
   (review 9) without dropping light: a mirror or a bounce sees it, coarser.
5. Targets at 1924 x 1084: tracing under 1.5 ms, the build under 0.5 ms per 500k boxes, lookups under 2 ms for the camera's
   pixels. The owner's 18 ms for the whole frame leaves about that.

Why not specular manifold sampling (the review's suggestion), with the number that decided it: it starts at the receiver
and solves for water points one at a time. Over rippled water one wall point is lit through very many water points: on the
test lake about a thousand per lamp (0.4 m waves over 12 m of water; an estimate, not a measurement). A beam or mesh method
adds them all by construction;
a solver would have to find each one, per pixel, per frame, and the unbiased form needs repeated trials per solution. It
suits calm water with a few solutions. It could later refine the few brightest lines.

## 5. The review's findings: confirmed, fixed, open, or found different

| # | Finding | What I found | State |
|---|---|---|---|
| 1 | Wave buffers read with the wrong type | Confirmed (the coordinator's fix). Checked end to end offline, logged in game | Fixed, tested |
| 2 | Landed triangles light surfaces they never hit (tolerance a tenth of the edge) | Confirmed by reading; no check written | Tolerance capped at 5 cm. The fix is beams (section 4) |
| 3 | Light lost at ledges and steps (triangles left out) | Confirmed by reading. Subdividing was measured and fails on cost | Open. Beams fix it |
| 4 | The fixed mesh does not converge by moving it | Not tested. Beams give the focusing inside a cell; the grid's cell size stays a resolution | Open |
| 5 | No caustics in reflections, bounces, glossy surfaces | Confirmed by reading (`g_firstCaustic` only). Design and prototype: `direct_light` | Open |
| 6 | Water handled at the first meeting only | Confirmed by reading: reflected rays going down are dropped, lights under the water never count, the straight-ray test uses the level mesh | Open. For rays going down: follow them to the waves' lowest point, what they meet before is lit (in the prototype) |
| 7 | Too much absorption under water (`d3d11.hlsl:234`) | Not mine (water-edges agent). With beams the incoming leg is the point's own distance; the outgoing leg must then count once | Open, theirs |
| 8 | A lamp sampled in its ball's volume; glowing surfaces cast no caustics | Confirmed by reading (`light_point`, `light_weight` returns 0 for `type > 1`) | Open |
| 9 | Hard limits | Walk limit of 4,096 records: gone. Half-float clamp: gone. Record overflow: still drops a frame's excess, the room and the build's count grow for the next. Four slots, the crossing attempts, the 200 m reach, the view cull: unchanged | 2 of 7 fixed |
| 10 | The water's own shading | Not mine | Open, theirs |
| cost | The lookup is the cost | Confirmed with the game's log and offline | Fixed, tested |
| cost | "Every shaded point searches multiple hash levels" | Refined: with one lamp the hash cost nothing; the cost came with many lamps and low light (long lists in a bucket) | |
| method | Look at specular manifold sampling | Looked at; does not fit rippled water (section 4, a thousand solutions a point) | Decided: beams |

## 6. Merge notes

- No option bit, material bit, export or struct change. ABI stays 0x00011E00.
- `FrameConstants` (7040 B, unchanged layout): `caustics.x` is the light buffer (was the records), `caustics.y` the
  acceleration structure's SRV (was the bucket heads), `caustics.w` the build's count (was the capacity);
  `causticGrid.y` holds the corners' UAV index (was the bucket mask).
- The counters buffer is 48 uints (was 4); its read-back the same per frame in flight.
- `pt_common.hlsli` uses `RaytracingAccelerationStructure landed = ResourceDescriptorHeap[...]` (Shader Model 6.6): the first
  structure reached through the heap. It compiles in both variants and runs on the test's device.
- The stage list is unchanged. The log's 10 s line: "caustics: N triangles a frame, at most M (built over B, room for R);
  water as the tracer reads it: ...".
- `TGPT_LOG_WATER_SAMPLES` (environment): the tracer logs each frame's wave sample. For the test only.
- The pre-existing C4244 warnings in the GPU test are in the base too.

## 7. Ideas, not built

- The comparison with HDRP's own drawn water (depth and normal) as a debug view, once camera rays exist.
- The wet floor (brief item 6): beams from the camera-visible glossy floor work the same way as from water cells; it needs
  the camera-ray pass's surface records as its grid.
- A lamp's emitter sampled on its surface, and emissive triangles as caustic sources (review 8): a tile's pick already
  handles "one of the rest by its light"; a cluster needs a point on one of its triangles.
- The stage timers publish about once a second; a perf case shorter than that reads the previous case's numbers. The lake
  case runs 150 frames for that reason.
