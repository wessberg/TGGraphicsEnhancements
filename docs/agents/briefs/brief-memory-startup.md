# Brief: video memory that runs out, textures that turn into tiny copies, and a minute and a half before tracing starts (PT-73 b, PT-74) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("No limits
that change the picture", "The owner's word on the shortcuts list": "Don't do copies", "Finish the migration first",
"test what catches defects and nothing more"). Report: `docs/agents/memory-startup.md`; write it early, keep it current.

## Why (all from one test session in the game: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261007-224654`)

Read `docs/agents/inspection-20261007-224654.md` first (the coordinator's 32 findings and GPT-6-astra's list beside it).
Two of them are yours, and they come before everything else because they spoil every other judgement:

1. **Video memory runs out, and textures silently become 64x64 copies.** In that run's `LogOutput.log`: "Video memory:
   the game uses 13.22 GB of its 13.46 GB budget"; "Shared textures (PT-45): 3243 of the traced scene's 4095 textures
   opened by the tracer with no copy ...; the others: 869 kept back near this card's video memory budget (their atlas
   tiles serve; asked for again every 10 s)"; TG Better Upscaling: "Video memory is nearly exhausted (12.92 of 13.39 GB);
   FSR and XeSS stop", and Ray Reconstruction stopped denoising for the rest of the session. On screen: a wooden shield
   drawn as flat orange blocks (the owner: "looked like a comic book"; `vista/crop-shield.jpg`), the vista's ground
   with no texture, far rocks like polished plastic. The owner had ruled months of work ago in one line: "Don't do
   copies"; a silent stand-in is exactly what the rulings forbid.
   The tracer's own memory through that session, scene after scene (the plugin's status line "N MB"): 252, 498, 974,
   1,583, 2,166 MB. It only grows: what a left scene held seems never to be released. Since today the traced world
   also has no distance cut and traces the drawn level of detail (more geometry, by the owner's ruling: that stays).
2. **Tracing takes over 100 s to come on** (PT-74). The owner: "obviously PT shouldn't take a minute to activate, so I
   guess the mod should add some kind of precompilation step to the game for PT when enabling it. Or at least a
   'compiling shaders...' bar in the top of the game so players know that PT is still compiling". The harness waited
   50 s in each of two steps and the tracer was still not composing; an offline test took 172 s to make its passes
   (`docs/agents/merge-4.md`), 89.6 s in another copy; before today's merges it was a few seconds.

## What to do

A. **Memory: find where it goes, with numbers, then fix what is waste.**
   - Account for the tracer's video memory by kind (meshes, acceleration structures and their scratch, the plants'
     groups, cloth copies, shared textures opened, atlases, per-frame targets, histories, caustics, the wet store, the
     yardstick, caches): a line in the log that sums to what the status line reports, per scene.
   - What a scene left behind must be released when the game unloads it (meshes and their BLAS no instance uses any
     more, textures the game freed). Find why it is not, fix it, and show the number coming back down after a scene
     change in the offline test.
   - Acceleration structures are compacted? Scratch buffers sized for the largest build and kept? Duplicate vertex
     data for meshes that are never posed? Say what each costs and take the waste out. Nothing that changes the picture.
   - **No silent stand-in.** When a texture cannot be opened, the tracer must not quietly draw a 64x64 copy as if it
     were the texture. First make room (the waste above). If the card truly cannot hold the scene: say so plainly on
     screen and in the log (which textures, how much is missing), and propose what the right behaviour is (the owner
     decides): never a picture that silently looks like a comic book. Remove the atlas-tile fallback where your fix
     makes it dead.
B. **Start-up: measure, then make the passes ahead of time, and tell the player.**
   - Which passes take the time (per pass: compile, link, pipeline-state creation), on the build of this copy.
     Likely suspects: the material programs and the scattering walk inlined into every ray-tracing pass. Reduce what is
     compiled (shared functions, fewer permutations) only where the picture is unchanged.
   - A cache on disk of the compiled passes, keyed by the shaders' hash and the driver and card, so a second start
     takes seconds; made while the game starts or loads (there is already code that makes passes while the game
     starts: grep "passes made while the game starts" in `tests/source_checks.py` and its sources), never when the
     player flips the row.
   - While passes are still being made and the row is On: a line at the top of the screen, "TG Path Tracing: compiling
     shaders (n of m)", in the game's own picture (not a log line), gone when done. Find how the mods already draw
     on-screen text or alerts (the settings rows' amber alert, any overlay) and use that; say what you chose.

Judged (AGENTS.md "test what catches defects and nothing more"): build; your own new checks (memory comes back down
after a scene is cleared; a second start reads the cache and takes under a few seconds; a missing texture is reported,
not replaced); the whole GPU test ONCE at the end with its FAIL lines written down; no mutation checks now.
In game the coordinator runs ONE session: the five scenes of the run above again; say which log lines must show
(the memory lines per scene, "0 kept back", the start-up time).

## Clean as you go

What your work makes dead is deleted in this job (the atlas-tile fallback, caps that only existed to hide memory
waste). Your report lists every workaround you met, removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling; never give up a test for want of a slot. Build into `%TEMP%\tgb\COPYNAME-...`;
  NRD's configure writes into the shared SDK folder, so build with `-DTGPT_WITH_NRD=OFF`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files. If the
  bridge would have to change, stop at a design.
- The coordinator's own files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`. Everything you learned goes into your report.
- Other workers run now on: materials' follow-up (what a hit reads: `pt_materials.hlsli`, `MaterialPrograms.cs`), lights
  and the lantern (`SceneLights.cs`, `LightRules.cs`), flicker (the temporal passes). Keep out of those files where you can.
- Keep tokens down: grep and read ranges of big files, never whole. Stop when done; ideas go in the report.
- Say if an export or struct changes (the coordinator sets the ABI number; main is 0x00012100). Option bits: none.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
