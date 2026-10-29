# Brief: path tracing fits the card's video memory (PT-87) - one worker, hand over early, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/memory-1.md`; write it early and after every step.

## The owner, 2026-10-09, playing with path tracing on (a 16 GB card, 4K with DLSS: the tracer at 1924 x 1084)

"I very, very frequently get this ['Path tracing paused: 129 textures unavailable (94.6 MB)'], so I'm barely able to
test PT. Not sure where these insane perf issues come from"; "Seems like we're constantly exceeding our VRAM
budget"; "it feels like we don't clean them up as we go? I walked around, then I moved to a small interior - it is
forever stuck not loading PT waiting for textures". This is the first job of the project now: the owner cannot test.

## What was measured (read first, in your copy: `docs/PT-REQUESTS.md` PT-87's newest notes: grep the heading)

- Windows gives the game 13.0 to 13.8 GB. The game itself holds about 9 GB. The tracer's own line, every 10 s in
  `TGPathTracing.Native.log` ("PT memory bytes: scene= meshGeometry= acceleration= scratch= plants= cloth= skinned=
  records= sharedTextures= atlases= targets= histories= caustics= wetStore= yardstick= radianceCache= transfer=
  denoiser= retired= total="), in that session, in MB:

  | Where | total | acceleration | geometry | plants | shared textures | atlases | targets | histories | denoiser (NRD) | caustics | transfer |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | nothing traced yet | 1,819 | 1 | 0 | 0 | 0 | 261 | 540 | 530 | 262 | 0 | 145 |
  | outdoors, a town | 4,238 | 627 | 208 | 4 | 1,142 | 347 | 557 | 530 | 262 | 215 | 145 |
  | outdoors by the water | 6,702 | 918 | 320 | 765 | 1,228 | 347 | 557 | 530 | 262 | 1,322 | 145 |
  | a small interior | 2,734 | 97 | 48 | 39 | 373 | 261 | 557 | 530 | 262 | 216 | 145 |

  The 1,322 MB of caustics came from a change that is OUT of your base again (it was 215 MB in every run before).
  Windows counts the game's textures the tracer holds open a second time (`texture_share.cpp`'s header says how that
  was measured): that is how sharing works, not yours to change.
- The log of that session, read only (the main tree's copy of the game's logs is not kept: ask for nothing, the
  table above is what you need). The main tree's harness runs hold the same line for other scenes:
  `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261009-1*\TGPathTracing.Native.log`.
- Over the budget a traced frame took 1,490 ms. Near it the tracer stops opening textures and pauses; that rule
  (`texture_share.cpp` `kBudgetShare`, 0.97 since today) governs texture opens ONLY: the tracer's other memory is
  under no rule at all.

## The job, in this order

1. **The fixed 1.8 GB with nothing traced, and what an interior with no water does not need.** Go through every
   allocation behind `targets`, `histories`, `atlases`, `transfer`, `caustics`, `denoiser`, `yardstick`, `wetStore`,
   `radianceCache` (`tracer.cpp`, `scene.cpp`, `gpu.hpp`, `nrd_denoiser.cpp`: grep the names the memory line sums):
   a table in the report: what it is, its size and format at 1924 x 1084, who reads it, whether it is needed with
   Denoising None / NRD / Ray Reconstruction / Ray Regeneration, with the Caustics row off, with no water in the
   traced scene, with no debug view on. Then: allocate what a setting or the scene does not need ONLY when it is
   needed, and release it when it no longer is (the caustics' buffers with no water in the scene; a denoiser's
   buffers when another is chosen; a debug view's and the yardstick's when off). Bit-exact only: no format gets
   coarser, no resolution drops, nothing is left out of the picture ("No limits that change the picture").
2. **Hand that over FIRST, within your first hour**, as soon as it compiles, written into the report with the
   memory you expect in the four rows of the table above. The coordinator builds it and reads the same line in the
   game; go on meanwhile.
3. **The parts that grow with the scene**: `plants` (765 MB by the water: what each swaying plant costs and why;
   what would halve it with the same picture), `acceleration` and `meshGeometry` (which structures are compacted
   and which are not, and why; vertex and index formats), `atlases` beyond the fixed part. For each: what it is
   made of, the options with their saving and their risk. Build the ones that are certain not to change the
   picture; write the rest up as choices.
4. **One rule for all of the tracer's memory**, not for texture opens alone, and one line for the player: when the
   tracer cannot have what a scene needs, the notice says so in plain words with the two numbers (what the game
   and path tracing need together, what the card gives), and tracing resumes by itself when it fits. Design it in
   the report (what is refused first, what is never refused); build only what is small and sure.
5. **The line the next session reads**: add to the 10-second memory line what Windows gives the game and what it
   counts (`IDXGIAdapter3::QueryVideoMemoryInfo`, as `texture_share.cpp` reads it), so that "the game's own share"
   can be read off any log.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- DO NOT COMPILE THE SHADER PASSES unless you change a shader (you should not need to): your copy's shader sources
  are the main tree's, so take the main tree's finished headers. Wait, with ONE background script, until the main
  build is done (`%TEMP%\tgb\BUILD-FIRST` is gone AND
  `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\build\native\generated\pathtracing\pt_specular_ser.h` is newer
  than 2026-10-09 18:47), configure your build folder `%TEMP%\tgb\COPYNAME` with `-DTGPT_WITH_NRD=OFF`, then COPY
  (read only on the main side) every `build\native\generated\pathtracing\*.h` of the main tree into your build
  folder's `generated\pathtracing\`, so that `tools\Build-PtShaders.ps1 -BuildDir <it>` says "0 to compile". Then
  `cmake --build` compiles C++ only. NRD's own buffers you read in the code; its path is built by the coordinator.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"). A small check of your own in
  a `..._ONLY` mode if you need one (the memory line of a small fixture before and after), through the main tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs; give the test a memory ceiling.
- No ABI or size change of `TGPT_Frame`; `TGPT_Status` may grow at its end if the plugin and the native side change
  together (say so). All 32 option bits are used.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's code: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit, check
  every hunk landed.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Build the plugin and the harness too if you touch them (0 errors, 0 warnings). `python tests/source_checks.py`
  passes.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: the megabytes saved in each of the four rows (expected, and measured where
  you could), what was built, what is written up as a choice, what is left, that it compiles and the checks pass,
  the report's path.
