# Brief: omm-chars - opacity micromaps for characters and hair (PT-110 M4), and their solid triangles out of the shader's test

Copy: COPYNAME (pristine base: BASENAME). Follow `AGENTS.md` in your copy; read first its rulings "Follow NVIDIA's and
AMD's guides to the letter", "No limits that change the picture", "The game is the first test" and "Code that is switched
off still costs frame time". Report: `docs/agents/omm-chars.md` IN YOUR COPY; the plan within 20 minutes, kept current;
"State when stopped" at once if a limit shows.

## Why

Speed is the owner's first order of work ("under control first": PT-99). In the tavern (run `20261011-161034`, the main
tree's `artifacts\harness`, its `TGPathTracing.Native.log`, the line "PT cut-outs"): 567,982 cut-out triangles a frame,
of which characters 421,395 in 32 instances (212,962 of hair) with NO micromap; micromaps settle 16.6 % of the pieces, the
shader's alpha test is asked about the rest; and 109,800 character triangles that are no cut-out still go to the
shader's test with them. A micromap is in the texture's space (the UVs), so a skinned character keeps its map whatever
its pose. `docs/agents/omm-1.md`: read "Written up, not built" whole (M4 and its trap: the camera's own walk reads hair's
alpha from the strand atlas, not the alpha tile; a piece is bare only if both readings say so) and the M1, M3 sections
by grep (M1, the plants' refitted structures linking their meshes' micromaps, is the rule since round 20: the answer
M4 waited for). NVIDIA's OMM guide and the DXR 1.2 spec on micromaps with skinned/refitted structures are the yardstick.

## What to do

(a) Why the 109,800 non-cut-out character triangles still reach the shader's test (M3 marks submeshes; the log says "0
    more are spared"): find it and fix it if it is ours (a submesh with no cut-out is OPAQUE in its structure: NVIDIA's
    rule), the picture unchanged.
(b) M4: the characters' and hair's cut-outs with micromaps, baked once a mesh and material from the texture each reading
    uses (the alpha tile, and for hair also the strand atlas: bare only where both are bare, covered only where both are
    covered, unknown elsewhere), linked to the character's structure at every build or refit of its pose (as M1 does for
    the plants), behind a hidden switch first (the "Micromaps trial" bits of TGPT_Frame.flags 6 to 13 have room: say which
    value; or a frame flag bit 20 and up). Hand the coordinator a trial build for ONE tavern run (the switch off and on,
    the world frozen and running, no denoiser) within your first two hours, or say why not.
(c) After the game answers: the check of your own that every pixel is what the alpha test alone makes it (the existing
    `TGPT_TEST_MICROMAPS_ONLY` / `TGPT_TEST_OMM_ONLY` modes of `tgpt_gpu_test` are the pattern; run only your mode), the
    report with the cut-out line's numbers before and after.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: your own mode only, only
  while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (a cold pass cache
  needs `TGPT_TEST_STARTUP_SECONDS=900`); NEVER the whole `tgpt_gpu_test` or ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\omm-chars-...`. Another worker (megageo-4) is changing the plants' structures in `scene.cpp`: keep your
  changes to the characters' path and the bake, and say every function you touch so the merge can be checked.
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files. Say every export, record,
  FrameConstants row, option or flag bit you add.
- Final message: five lines at most, pointing at the report.
