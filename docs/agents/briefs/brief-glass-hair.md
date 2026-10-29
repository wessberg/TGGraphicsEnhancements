# Brief: bottles that turn black, hair that is dark and striped, armour that lost its silver (PT-73 f, i) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`: the main tree of 2026-10-07 23:40). Follow `AGENTS.md` there; read "The
owner's rulings" first ("Everything on screen is part of the main traced picture", "A mirror must show the world as
the camera sees it", "Do. not. fake. stuff.", "The order of judging, and Ray Reconstruction last", "test what catches
defects and nothing more"). Report: `docs/agents/glass-hair.md`; write it early, keep it current.

## Why

The first session of today's merged build in the game:
`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261007-224654`. Read
`docs/agents/inspection-20261007-224654.md` and `inspection-20261007-224654-gpt.md` (two independent lists of what is
wrong) and look at the pictures they name. Three things in the tavern are yours; each is something the tracer draws
itself since today (glass and eyes as surfaces of the camera rays: `docs/agents/glass-eyes.md`; hair as a surface of the
camera rays: `docs/agents/hair-6.md`).

1. **Glass bottles and jars are nearly black silhouettes** (`glass-jars/crop-shelf.jpg`: the game, traced with no
   denoiser, traced with Ray Reconstruction). The game's tall turquoise bottle, the cream one and the jars keep their
   colours and labels; traced they are dark shapes, one looks metallic with a gold highlight, a mug nearly vanishes.
   The glass worker's own open point: rays born inside an absorbing solid do not attenuate their first segment, and
   its report lists more ("remaining material and caustic gaps"). Find why a bottle is black: what the plugin reads of
   each bottle's material (its tint, absorption, thickness or wall, its label as a decal or a second material, whether
   it is glass at all or painted clay), what light gets inside and out again (the lantern is the only light; does light
   refract into the bottle and out, or is every path cut?), what is behind it. The log names glass materials. Fix it.
2. **A woman's hair is dark red-brown and coarse, with horizontal stripes** when not denoised; the game's is light
   ginger with fine strands (`hair-moving/crop-head-frames.jpg`: single frames: the game, traced with no denoiser twice,
   traced with a denoiser). The stripes first: lines across the hair that follow the screen's rows or the cards' texture
   rows: find which (the stochastic cover drawn per row? the hair texture read at the wrong mip or with a wrong UV
   derivative? half-size rays?). Then the tone: is the hair's own colour read right (the yardstick lists RealHair
   materials) and too little light reaches it, or is its shading too dark (its two highlights, light from behind, the
   bounced light a card receives)?
3. **Her armour is dull brown where the game's is silver**, the shoulder cloth brown where the game's is pale. Metal
   shows what it reflects: is the tracer's reflection of the room right and the room is simply warm and dark, or is
   the metal read wrong (metallic and smoothness from the mask map of `TG/Character/Kandra_Lit`), or do its reflection
   rays see a wrong world?

Her skin (orange-red) is another worker's (the scattering walk, `wt/candle`): leave it. The lantern's brightness is
`wt/light`'s. Materials' programs are `wt/mats2`'s.

Judged (AGENTS.md "test what catches defects and nothing more"): build; a check for each cause you fix (a tinted
bottle before a lit wall shows its colour and what is behind it; a strand's cover has no row pattern; a metal ball
in a known room reflects it); the whole GPU test ONCE at the end with its FAIL lines written down; no mutation checks
now. In game the coordinator runs `glass-jars@quick` and `hair-moving@quick` (no denoiser, then NRD): say what the
shelf, the hair and the armour should look like, and name better camera places if these scenes hide the subject (the
scenes' measured regions sit on a chair and on the ceiling today: propose the right boxes for
`tests/ingame/scenarios.json`, as shares of the screen).

## Clean as you go

What your work makes dead is deleted in this job; your report lists every workaround you met, removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling; never give up a test for want of a slot. When you end, leave no waiting shell
  behind. Build into `%TEMP%\tgb\COPYNAME-...`; NRD's configure writes into the shared SDK folder, so build with
  `-DTGPT_WITH_NRD=OFF`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files.
- The coordinator's own files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`. Everything you learned goes into your report.
- Eight workers run at once tonight (memory and start-up, materials, lights and flicker, the candle scene and heat
  haze, decals, water, speed): keep your edits small and local, and say in your report which shared files you changed.
- Keep tokens down: grep and read ranges of big files, never whole; write your report early so nothing is lost if you
  are cut off. Stop when done; ideas go in the report.
- Say if an export or struct changes (the coordinator sets the ABI number; main is 0x00012100). Option bits: none.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
