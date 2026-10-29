# Brief: the traced light brightened where it is dark, held where it is bright (the lantern's "option 4b", PT-73 e) - one agent, a small job

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there ("How to talk to the owner", "The order of
judging, and Ray Reconstruction last", "test what catches defects and nothing more"). Report:
`docs/agents/highlights.md`; write it early.

## Why

A lantern in a tavern room burns the wall beside it out to white (`docs/PT-REQUESTS.md` PT-73, the notes that start
"NOT fixed, a decision for the owner: the lantern" and "The lantern, shown to the owner"). The cause is not the light:
the tracer brightens its whole picture by one automatic gain (here 5.3 times) because the game's exposure is set for
fill lights the tracer leaves out, and that gain lifts the wall at the lamp as much as the dark room. The owner saw
four pictures and a preview (`artifacts\harness\20261008-001912\glass-jars\lantern-option4-lamp.jpg` in the main tree) and
said of "ours as now": "looks best overall, but the light is blown out"; of no gain: "most realistic, but the room is too
dark"; and of the fourth option, after the coordinator recommended its firmer form (4b) with one correction: "ok, do
that".

## What to build

Where the traced light's gain is applied (`traced_gain`, `traced_gain_at`, `traced_gain_through` in
`native/pathtracing/shaders/pt_common.hlsli`; used by `pt_compose.hlsl` where the traced light is stored for the
composite, by the meter `pt_meter.hlsl`, by the water and fog paths; the managed side sets it in `HdrpPasses.cs`
`UpdateTracedGain` and logs "traced light x5.3 (2.4 EV ...)"), make the gain a function of the pixel's own brightness:

- With L the pixel's traced light as the game's exposure alone would show it (gain 1), G the automatic gain there, and
  W a brightness left as it is: `L' = L * G / (1 + L * (G - 1) / W)`. Dark pixels get G; a pixel at W gets 1.
- **Never below L**: above W the pixel keeps the game's exposure (gain 1), so a flame and a lamp's glass stay the
  brightest things. The curve must be continuous and smooth there (no visible band): choose the join with care and
  say what you chose.
- Apply it to the pixel's luminance and scale the colour by the same factor (no hue shift).
- W: about 0.4 of what the game's tone mapping shows as white at its exposure (the preview's 4b). Find what "white"
  is in the units the composite stores (pre-exposed radiance; HDRP's tone mapper), make W one constant with a comment,
  and say how you fixed its scale.
- Where G is 1 (daylight) nothing may change: bit for bit the same picture.
- It is how Dark Adaptation works from now on, not a new row; the row's tooltip already says "The brightest parts of
  the view are kept from turning white": make that true and keep the tooltip honest.
- Everything that undoes or predicts the gain must stay consistent: the meter divides the gain out
  (`pt_meter.hlsl`), the fog composite and Fog Adaptation use it, denoisers get their inputs before or after it (say
  which, and keep NRD's input linear in light: the curve belongs after denoising or at the composite, not in what the
  denoiser accumulates), the exposure log line. Read how each uses the gain before you change it.

## How it is judged

- Offline (`tests/pathtracing_gpu_test.cpp`): a dark room with one bright patch: with G = 5 the dark part is 5 times
  brighter, the patch at W unchanged, nothing above W darker than with G = 1, the order of brightness kept
  (monotonic); with G = 1 the picture is the same as without the change. Build; your checks; the whole GPU test ONCE
  at the end with its FAIL lines written down (known on the base, not yours: "the passes were made" / "all startup
  passes report completion" when the pass cache is cold and other tests share the GPU, "clear glass casts no shadow",
  "a character's eye drawn forward...", "glass bends light by its material's own index of refraction", the lake's
  timing). No mutation checks.
- In game the coordinator runs `glass-jars@lantern-choice` (its step `1-as-now` is then option 4b), `candle@quick` and a
  daylight scene (`vista@quick`): say what the lantern's wall and the picture's luma against the game's should become.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0. Wait with one background script, never by
  polling; when you end, leave no waiting shell behind. Build into `%TEMP%\tgb\COPYNAME-...`, with `-DTGPT_WITH_NRD=OFF`
  (NRD's configure writes into the shared SDK folder); say what of your change touches the NRD path so the coordinator
  tests that in the main build.
- Another session builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`, `native/bridge`,
  `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): not yours. The coordinator's files are not yours
  either (`BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`).
- Keep tokens down: grep and read ranges, never big files whole; report early. Small and local edits: other jobs are
  being merged into these files.
- No export, struct or option bit should be needed; say so if one is.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
