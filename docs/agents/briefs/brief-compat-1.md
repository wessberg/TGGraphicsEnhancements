# Brief: is the tracer built for Shader Execution Reordering, Opacity Micromaps and RTXDI? (PT-110) - a read-only review

Read the MAIN tree (read only): `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`. Write ONE file, your report:
`C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\reviews\compat-1.md`
(the coordinator files it under `docs/agents/`). Change no source. Start no build, no test, never the game.

## The owner, 2026-10-10

Of Shader Execution Reordering, when a pass measured slower with it: "Do not turn that off. If it makes it slower
with that on, then it's either not due to that, or due to us not designing our tech with it in mind." Then:
"definitely investigate if there's anything we can do to any of our rendering to make it more compatible with
Shader Execution Reordering, Opacity Micromaps, and RTXDI." The ruling is in `AGENTS.md` ("Shader Execution
Reordering stays on; the tracer is designed for it"): read it, and "No limits that change the picture", "No
shortcuts", "Speed is no longer left for last". Players' frames today: 80 ms in a tavern, 109 in a town, 165 to
200 at a beach, at 1924 x 1084 rays for 4K on an RTX 5080.

## What is known (read first: `docs/PT-REQUESTS.md` PT-99, its notes of 2026-10-09 and 10-10: grep the heading;
`docs/agents/measure-run.md`; `docs/agents/perf-1.md` sections 1 to 5 and "perf2.2" to "perf2.4";
`docs/PROJECT-CONTEXT.md` section 9 and DEAD ENDS)

- REORDERING today: only the bounced light's and the reflections' passes have a variant for it: one ray
  generation shader each that does everything inline (inline ray queries, `dx::MaybeReorderThread(material &
  0xFFFF, 16)` once, the shading inline; `pt_common.hlsli` near `PT_SER`). No hit objects, no hit shaders, no
  shader tables. Measured in the game (ms, the bounced light / the reflections): the tavern 56.0 / 11.6 with it
  and 13.9 / 15.0 as plain compute passes; a town 60.9 / 29.3 and 27.8 / 28.4; the beach 42.6 / 140.2 and 70.8 /
  317.7. ANOTHER WORKER measures that pass's variants in the game right now and writes its design
  (`<scratch>\wt\ser1\docs\agents\ser-1.md`, if it is there yet: read it, do not repeat it). YOURS IS THE REST:
  every other pass and the whole frame.
- MICROMAPS today (the tracer's own log lines of 2026-10-10): "opacity micromaps on 175 meshes (1953
  instances)" in a town; in a tavern "micromaps: meshes=46 ... triangles=63022 instances=173 15MB" and "tested by
  the shader: 49 instances, every triangle (547286 triangles, 109800 of them of submeshes that are no
  cut-out)"; in a town 11,303 plants are posed for the wind every frame (6.4 M triangles refitted, 3.9 ms). A
  ray alone, nothing shaded, costs the bounced light's pass 28 ms in the tavern where the camera's own rays over
  the same world cost 2.5.
- RTXDI today: `RTXDI-Library-3.1` (in the repo root, gitignored: READ it, and NEVER copy or quote its source into
  your report: describe in your own words, name files and functions only) drives the direct light's reservoirs
  (`pt_di`, `pt_di_spatial`) and the bounced light's (`pt_gi`, `pt_gi_spatial`, `pt_gi_boil`), with the tracer's
  own bridge (`pt_rtxdi.hlsli`, `pt_restir.hlsli`), its own light grid and its own lists of lamps and emissive
  triangles. With no ray at all the bounced light's pass still costs 14.4 ms in the tavern and 4.0 at the beach.

## The job: for each of the three, across ALL of the tracer's rendering

1. **What the vendor's own documents and reference code say the technique wants** (Microsoft's DirectX
   Raytracing specification for reordering and micromaps; NVIDIA's guidance and samples; RTXDI's integration
   guide and its sample application's passes and settings; cite each source with its link and what it says, in
   your words).
2. **What the tracer does, read from its code** (`native/pathtracing/shaders`, `native/pathtracing/src`: grep,
   then ranges; the big files are 3,000 to 5,000 lines).
3. **Each gap, as one entry**: what we do; what the technique wants; what it costs us or what we leave on the
   table (in ms or MB where the notes' measurements let you say it, "not measurable from what is there" where
   not); THE ONE GAME RUN THAT WOULD PROVE IT (which scene, which variant, which line of the tracer's log); how
   large the change is (lines, files, a day, a week); what it risks. Things to look at, not a complete list:
   - Reordering: which passes have a variant and which do not (the direct light's, the walk, the caustics', the
     spatial reuse); where threads diverge most (the water's path, glass, subsurface walks, the lamps); what a
     thread carries at each point; hint bits and keys; hit objects from ray queries against `TraceRay` with hit
     groups; the plain passes kept beside (video memory and the first start's 4 to 7 minutes of pass making).
   - Micromaps: which cut-out geometry has none and why (the plants in the wind's pose? characters' hair cards?
     decals? meshes "waiting"?); the subdivision level and format chosen; whether the passes' ray flags and the
     top level's flags let the driver use them everywhere (shadow rays, the lamps' rays, the water's path); what
     the inline alpha test in the queries' loops costs where a micromap already settled the triangle; micromaps
     on geometry that is refitted every frame.
   - RTXDI: how our integration differs from the reference one (presampling of local lights and its tiles,
     ReGIR against our grid, the temporal and spatial resampling's settings and bias correction modes and what
     each traces, the visibility reuse, the boiling filter, the GI reservoirs' settings, the checkerboard and
     the reservoir packing), and which difference costs time, noise or correctness.
4. **Rank them**: the five that would give players the most, first; then the rest. Say plainly where you are
   guessing.

## Rules

- Read only. One file written, your report, outside the repo (the path above). No build, no test, no game.
- RTXDI's and NRD's sources: never copied or quoted. Unity's, HDRP's and the game's code: learn, copy nothing.
- Every finding names the file and the line of ours it is about, and the source it rests on.
- Plain words, short sentences; numbers and file paths welcome; no claim without its source or its "not
  measured". Never the word the source checks ban (say "prototype" or "on trial").
- Your final message: eight lines at most: the five findings that matter most, each in a line with its expected
  gain and the run that proves it; then the report's path.
