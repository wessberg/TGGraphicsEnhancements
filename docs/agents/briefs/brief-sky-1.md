# Brief: sky-1 - the sky into the tracer: what the game's sky is, and the design of our own (PT-112, the sky)

You are the ONE subagent the owner allows at a time (2026-10-11). Follow `AGENTS.md`. Write ONLY your report
`docs/agents/sky-1.md` in the main tree (outline within 15 minutes, keep it current; "State when stopped" at once if a
limit shows) and scratch files under `%TEMP%\tgb\sky1\`. Never: start the game, deploy, build the native tree, run GPU
tests, edit repository source files, commit. The coordinator edits the main tree at the same time (the fog); do not
touch its files. Never read the big files whole (`pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `docs/PT-REQUESTS.md`,
`docs/PROJECT-CONTEXT.md`): grep, then read ranges.

## Why

The owner, 2026-10-11: everything HDRP renders that runs sequentially with our trace or makes it wait must move into the
tracer ("so we know there's no HDRP bottlenecks and any GPU/CPU waits will come from our own rendering"); HDRP keeps only
post-processing, DLSS and the HUD. The sky: "definitely bring that over too". Today HDRP renders the sky each frame (and
whatever it renders for clouds), the tracer copies HDRP's sky into a latitude-longitude map at render event 1 (D3D11 work
the D3D12 trace waits for: `tracer.cpp` update_sky, the "sky" lat-long texture, `sky_radiance` in `pt_common.hlsli`), and
pixels that show the sky take HDRP's sky through the composite (`pt_compose.hlsl`, the "glass-over-sky" path).

## What to find out (with evidence: the game's DLLs read by reflection or strings, HDRP 17's sources online, the
## harness's audits under `artifacts/harness/*/*/1-game/audit*.txt`, the plugin's own capture code)

1. Which sky the game uses, scene by scene (outdoors by day and night, interiors): HDRP's Physically Based Sky, HDRI
   sky, gradient sky, or a custom one; its parameters and who drives them (the game's time-of-day controller:
   `TimeOfDayPostProcessesController`, `WeatherController` in `TG.Main.dll`); the moon and stars (night); clouds (HDRP
   Cloud Layer, Volumetric Clouds, or the game's own); fog's "sky colour" use of the sky.
2. What HDRP does on the GPU for it each frame (its passes, their inputs: precomputed tables, the ambient probe, the
   reflection cubemap's update, the sky's own render into the colour buffer), and what the tracer reads of it today
   (the lat-long copy; the ambient/specular cubemaps if any; the sun's colour after the atmosphere).
3. The design of the tracer's own sky: the same model with the game's parameters evaluated by the tracer (for camera
   rays that see the sky, every ray that escapes the scene, and the sun's and sky's light), so that HDRP's sky passes
   can be switched off while tracing. Name exactly which HDRP passes then stop (and how: frame settings, a Harmony
   prefix, the sky's "update mode"), what the tracer must compute and how often (a table per frame or only when the time
   of day or weather changes), the clouds' honest representation (no painted stand-in: if the game's clouds are a
   2D layer, say what the layer is and how a ray reads it; if volumetric, the medium), and the cost.
4. The risks: what else reads HDRP's sky (its reflection probes, ambient probe for the game's own lighting, the fog's
   sky colour, water), and what a player would lose or see change.

## How to stop

When 1 to 4 are answered with evidence, or when one game run is needed to answer something (then name the run exactly:
scene, what to read). A one-screen summary on top in plain words; the details below. Expected: an hour or two.
