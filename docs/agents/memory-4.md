# memory-4: the atlases' second count given back (PT-87)

Worker `memory4`, copy `<scratch>\wt\memory4` (base `base81`), build folder `%TEMP%\tgb\memory4`
(`-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON -DTGPT_REGROUP_FEED=ON -DTGPT_LAMP_TREE=ON -DTGPT_WATER_FLIGHT=ON`, the
main tree's shader headers copied in: C++ only). Brief: `docs/agents/briefs/brief-memory-4.md`. Scripts:
`<scratch>\m4\`. Started 2026-10-10 05:51. What it builds on: `docs/agents/memory-2.md`, section "memory3", M3-6.

## State when stopped

(Rewritten after every step. The newest line is the truth.)

- 06:05: the brief, M3-6 and the code read; the design written (section 1).
- 06:13: the cut written and compiled (C++ only, no shader compiled).
- 06:22: run `a1`, the first build: the cut works (the count -345 MB, every pixel the same). Five of the check's
  own comparisons failed for a reason of the check's: 196 MB of other memory left in the middle (section 3).
- 06:35: runs `a4` (my case alone), `a5` and `a5-1080` (the whole memory-2 check with my case, 1280 x 720 and
  1920 x 1080): **every check passes, the numbers are exact** (section 2). Source checks 111 of 111.
- 06:36: one last change after `a5`, to a log line's words only (a line closed early now says "the next move
  came before the count could follow").
- 06:41, **STOPPED HERE: THE HAND-OVER IS READY** (the section below). The last build (tracer DLL E453B36073B0,
  0 errors, no shader compiled) ran my case alone (`a6`) and the whole memory-2 check with it at 1280 x 720 and
  1920 x 1080 (`a7`, `a7-1080`): every check passed, the same numbers as `a4`, `a5`, `a5-1080` (section 2).
  Source checks 111 of 111. The copy equals the frozen `<scratch>\wt\memory4-handover1\`. No script of mine is
  left running. NOT DONE, and not mine: the game.

## HAND-OVER

**What it is.** The game's device holds the tracer's atlases only around a bake. Windows then counts them once.
That gives the game back **260 MB of its video memory budget, 345 MB once a hair has been baked**, whenever no
texture has been baked for two seconds, and always while path tracing is off or paused for textures. Nothing on
the card changes and no pixel changes.

**What changed** (five files; no shader, no plugin, no export, no struct):

| File | What |
|---|---|
| `native/pathtracing/src/tracer.cpp` | the cut (`GameSide`, `keep_side_handle`, `let_go_side`, `take_up_side`, `hold_sides`, `tend_sides`, `tend_sides_quiet`), the six bake functions ask `hold_sides` before they take a bake, the memory line's new figures |
| `native/pathtracing/src/texture_share.hpp` | a comment (what `own_shared` is told now) |
| `tests/pathtracing_memory4_checks.inl` | NEW: the check (case 11) |
| `tests/pathtracing_memory2_checks.inl` | includes it; `TGPT_TEST_MEMORY4_ONLY=1` leaves cases 5 to 10 out |
| `tests/source_checks.py` | one new check, `test_path_tracing_atlas_sides_held_only_around_a_bake` |

Frozen copy: `<scratch>\wt\memory4-handover1\` (see "Files" at the end for the hashes).
Merge: `python tools/agents/merge_copy.py memory4 --base=base81 --root=<scratch>`.

**What to build.** C++ only: `cmake --build build\native`, install, deploy with `Deploy-PtBuild.ps1`. No
`Build-PtShaders.ps1` run is needed for this (no shader file changed).

**The recipe** (yours: the game): `.\tools\harness\Run-PtHarness.ps1 -Scenario tavern-room -Recipe memory`, then
`-Scenario spec-terrain`. Then
`Select-String -Path <run>\TGPathTracing.Native.log -Pattern "PT memory: the game's device|ownSharedOnce="`.

**What the lines should say.**

1. Two seconds after the scene's last texture was baked:
   `PT memory: the game's device let go of the colour atlas (192 MB, 2.0 s and N traced frames after its last bake),
   the alpha atlas (64 MB, ...), the cookie atlas (4 MB, ...); the tracer's device holds and reads them as before.
   Windows' count: A MB before, B MB after (-260 MB for 260 MB of atlases, read 1 of the tracer's calls later)`.
   In the game the atlases need not go in the same frame (each goes two seconds after ITS last bake): then there
   are several such lines, each with its own atlases and its own MB. With a character in view there is also
   `hair's strand atlas (85 MB, ...)`: 345 MB in all.
2. When a new texture arrives: `PT memory: the game's device took up again for a bake: the colour atlas (192 MB,
   opened by its kept handle in 0.05 ms). Windows' count: ... (+192 MB for 192 MB of atlases, ...)`, and two
   seconds later it goes again.
3. In the recipe's "off" step, within a second: the same "let go" line with `no frame having been traced for a
   second` in the brackets (if the sides were not let go already).
4. The memory line (every 10 s): `ownSharedOnce=272629760` (260 MB) while nothing is baked, `362151936` (345 MB)
   with hair's strands; `atlasSidesLetGo=` and `atlasSidesTakenUp=` count the moves. `windowsCounts` and
   `notInThisLine` are lower by `ownSharedOnce` than they would be. (One line may be written in the very call
   that let the sides go: Windows' count follows at the game's next flush, a frame later.)
5. In the game the count also moves by itself (memory-2, section 1): read the lines in a still moment. A line
   whose count had not moved yet says so ("the next move came before the count could follow").

**What must NOT be in the log.** `could not open ... a second time`, `could not open ... again for a bake`,
`no second handle`. Each means the cut switched itself off for that atlas and it is held as before (the first
and the third), or a bake is waiting for its atlas (the second). Tell me which, with the number in brackets.

**The A/B in the game.** One start with `TGPT_TEST_ATLAS_SIDES=0` in the environment the game starts in: every
side stays held, as before this job. One start without. `windowsCounts` should differ by `ownSharedOnce`.

**What to watch in the game** (what the offline check cannot say):

- Frame times around a "took up again" line. The open itself is 0.01 to 0.10 ms here. What the game's device
  pays at its first write after it (Windows puts 192 MB back on that device's list; nothing is copied, the
  texture never left the card) could not be timed offline. If long frames come with those lines, raise
  `kSideIdleMs` (one constant) or say so.
- Path tracing paused for textures. Within a second of a pause the atlases' 260 to 345 MB leave the count, so
  the cap (97 % of the budget BY WINDOWS' COUNT) can open that much more of the game's textures, and the pause
  can end by itself. When frames come back the bakes bring the second count back for as long as they bake plus
  two seconds. Near the edge this can go back and forth a few times (pause, textures open, frames, bakes, the cap
  holds the next textures back, pause): each round opens more textures, and opened textures stay open. Today it
  stays paused for good there. If the back and forth shows, say so.

## 1. The design

**What is cut.** Four textures are made on the game's device and opened on the tracer's, and Windows counts each
once for each device that uses it:

| Atlas | Size | The game's device writes it | The tracer's device reads it |
|---|---|---|---|
| the colour atlas (`st.atlas`) | 192 MB | a material's, hair's or an eye's texture is baked into a tile (`bake_textures`, `bake_hair`, `bake_hair_lobes`, `bake_eyes`), and once when a scene is cleared | every frame |
| the alpha atlas (`st.alphaAtlas`) | 64 MB | an alpha-tested texture is baked (`bake_alpha_textures`), and once when a scene is cleared | every frame |
| the cookie atlas (`st.cookieAtlas`) | 4 MB | a light's cookie is baked (`bake_cookies`) | every frame |
| hair's strand atlas (`st.strandAtlas`) | 85 MB, made by the first hair | a hair's strands are baked (`bake_strands`) | every frame |

Not cut, and why: the sky and the fog's sky (1 MB; the game's device writes them every half second), the copies
of HDRP's frame and the outputs (both devices use them every frame; M3-3), the characters' and the waves' copies
(every frame). The game's own textures are the game's to hold.

**How.** The tracer keeps an NT handle of each atlas (`keep_side_handle`, when the atlas is made). The game's
device lets go of its side, which is its texture object and its views (`let_go_side`):

- two seconds after the last bake into that atlas (`kSideIdleMs`; `tend_sides`, every traced frame, after the
  frame's bakes), or
- when no frame has been traced for a second (`tend_sides_quiet`, from the plugin's maintenance event): path
  tracing is off, or it is held back until the game's textures can be opened, which is when the card is fullest
  and no bake can come.

The tracer's device keeps the texture and reads it as before. The next bake into that atlas first opens the
game's side again by the kept handle (`ID3D11Device1::OpenSharedResource1`) and makes its view (`take_up_side`,
called by `hold_sides` from each bake function); then it bakes as before.

**Why by the clock and not "120 frames", as M3-6 had it.** Two seconds ARE 120 frames at 60 a second. But a
town ran at 8 frames a second, where 120 frames are a quarter of a minute: a walk through a town would hardly
ever see a side let go, and that is where the budget runs out. PT-98 put what a frame takes in on the clock for
the same reason. The picture does not depend on the number. `TGPT_TEST_ATLAS_SIDES=<frames>` in the environment
puts it on frames (the check uses 6, because a test's frames do not go by the clock; one step of the check runs
the clock's rule as it is).

**Why the picture cannot change.**

- What reads an atlas is the tracer's device alone (its passes, through one view in its own heap). That texture
  object, its view and its place in the heap never change. Nothing of the tracer's side is touched.
- What the game's device wrote is still there after it let go (the probe of M3-6, three times; and this check
  compares every pixel after bakes into atlases that had been let go: largest difference 0).
- A bake is never lost and never late. A bake function takes nothing from its queue until the side is held: it
  looks at its queue, opens the side if it is let go, and takes its bakes in the same frame as before
  (`hold_sides`: under the engine's lock, which is let go for the open and taken again, the queue read only
  after). Hair's tiles given back are still read in the same breath as its bakes.
- The order on the GPU is as before: the bakes are recorded on the game's context before the frame's flush and
  its fence, and the tracer's queue waits for that fence. `wait_for_scene_readers` is where it was.
- A cleared scene's clears of the colour and the alpha atlas take both sides up first. If one cannot be opened,
  its clear waits for a frame that can, and that scene's bakes wait with it.

**What a failure does.**

- Before the game's device lets go, it opens the atlas a SECOND time by the kept handle and makes a view on it:
  the proof that the way back works here and now (another driver, an injector that wraps the device). If that
  fails, it does not let go. That atlas stays held for the session, as before, and the log says so: `PT memory:
  the game's device could not open <atlas> a second time by its kept handle (0x...): it keeps holding it, as
  before`. The same if no handle could be made at the start (`no second handle of <atlas> could be made`).
- If the real re-open fails later all the same (it can then only be the PC out of memory, or the device lost):
  the log says so (`could not open <atlas> again for a bake`, at most one line in five seconds), nothing is taken
  from the bake queue, the bakes wait as they wait for their turn (their tiles keep the white of a tile not baked
  yet), and the open is tried again every frame. Once it is back, that atlas is never let go again in the session.
- Both are in the check: a test's switch makes the openings fail as if the driver had refused them.
- NOT BUILT: a way back that needs no open (a new atlas made and filled from the tracer's copy). Worth it only if
  the game ever shows the second log line.

**The switches** (environment, read when used; nothing is needed for the cut to be on):
`TGPT_TEST_ATLAS_SIDES=0` keeps every side held, as before this job; another number lets go after that many
traced frames instead of two seconds. `TGPT_TEST_KEEP_IDLE` keeps them held too (it means "nothing is let go").
`TGPT_TEST_ATLAS_SIDES_BREAK=<bits>` is the check's alone (bits 0 to 3: the second opening of atlas 0 to 3
fails; bits 4 to 7: the opening for a bake).

**The memory line and the cap's trial.** `ownShared` is as it was (what the tracer's own shared textures weigh).
New beside it: `ownSharedOnce`, the part of it the game's device has let go of, which Windows counts once;
`atlasSidesLetGo` and `atlasSidesTakenUp`, how often a side went and came since the tracer started. The cap's
trial of memory3 (`TGPT_TEST_SHARE_COUNT_ONCE`) took `ownShared` out of the count as a second count: it is now
told `ownShared` less `ownSharedOnce`, and told again whenever a side goes or comes (`tell_own_shared`), so that
it does not take out a second count that is no longer there.

## 2. What the check shows (runs `a4`, `a5`, `a5-1080`, and the last build's `a6`, `a7`, `a7-1080` with the same numbers; Windows' own runtime, 10 GB ceiling)

The check (`tests/pathtracing_memory4_checks.inl`): the room, with a box, a leaf that casts a shadow, a lamp and a
card of layer hair before a wall. Each run: a bake into all four atlases (so every side is held); 20 frames, in
which the sides are let go; then a new texture for the box, a new alpha-tested one for the leaf, a cookie for the
lamp and a new hair for the card are registered and put on screen, and the next frame bakes them (into atlases
whose sides had been let go); 27 more frames. The same frames with the sides let go (after 6 frames without a
bake), with the sides held (`TGPT_TEST_ATLAS_SIDES=0`), and let go again.

| What | 1280 x 720 alone (`a4`) | with cases 5 to 10 (`a5`) | 1920 x 1080 (`a5-1080`) |
|---|---|---|---|
| The atlases (colour, alpha, cookies, strands) | 192 + 64 + 4 + 85 = 345 MB | the same | the same |
| Windows' count before the bakes: let go / held | 1,696 / 2,041: **346 less** | 1,742 / 2,088: 346 less | 2,177 / 2,523: 346 less |
| Right after a bake into each: let go / held | 2,041 / 2,041: the same | 2,088 / 2,088 | 2,523 / 2,523 |
| 27 frames later: let go / held | 1,696 / 2,041: 346 less | 1,742 / 2,088 | 2,177 / 2,523 |
| **The picture, let go against held: largest difference of any pixel's channel** | **0** (the same run twice: 0) | 0 (0) | 0 (0) |
| Pixels the bakes changed in it | 422,223 of 921,600 | 422,223 | 948,348 of 2,073,600 |
| By the clock, no switch set: 2.7 s after the last bake | 345 less (608 frames) | 345 less | 345 less (284 frames) |
| No frame for a second, then the maintenance event | 345 less; `ownShared` 424 MB, `ownSharedOnce` 345 MB | the same | 345 less; 520 and 345 MB |
| The alpha atlas's second opening made to fail | it stays held: 64 MB more than all four let go; the log says so | the same | the same |
| The colour atlas's opening for a bake made to fail | the bake waits: 0 of its pixels, 24,338 pixels white; the log says so | the same | 0 and 54,525 |
| ... and allowed again | baked by the next frame: 11,785 magenta pixels; the atlas then stays held (256 MB more with the alpha atlas) | the same | 27,011 |
| Opening a side again by its handle | 0.01 to 0.10 ms | | |

Files: `%TEMP%\tgb\memory4\memory4-check-<run>.out.txt`, `...-<run>.native.log`; pictures
`%TEMP%\tgb\memory4-images-<run>\87-atlas-sides-*.png` (let go, held, before the bakes, the bake that waits, the
bake landed). Cases 5 to 10 of memory-2 pass beside it as before (131 "ok" lines, no FAIL, both sizes).

## 3. What the check leaves out, and what went wrong on the way

- It runs on Windows' own Direct3D 12 runtime (`TGPT_TEST_NO_AGILITY=1`), without NRD (this build has none),
  without Ray Reconstruction, without the game. None of those touch the atlases' game side. What the game adds:
  its own device's load, thousands of bakes in a row at a scene's start, the plugin's own threads registering
  textures while frames run (the check registers between frames).
- It cannot time a frame of the game around a re-open (HAND-OVER, "What to watch").
- No mutation checks (the rule while the migration runs). The check has its own contrast in it: the run with the
  bake held back shows 24,338 pixels wrong and 0 right, so the pixel comparison does see a bake that is missing.
- Run `a1`: five of the count's comparisons failed, the cut itself was right. Between the compared runs 196 MB
  left Windows' count that are not the atlases': the built-in filters' textures, which PT-87's earlier cut lets
  go after 120 frames without the built-in denoiser. With cases 5 to 10 before it (run `a3`) another 24 MB left
  in the middle (the structures' scratch room of those cases). The check now runs 600 frames first, and the
  numbers are exact (346, 0, 346) alone and with the other cases.
- The first write of the cut asked `hold_sides` through a helper that took the queue; two source checks pin the
  bake functions' own lines (`std::min<size_t>(e.<queue>.size(), by_clock(n));`, `givenBack.swap(...)`), so the
  bake functions kept their shape and ask `hold_sides` under their own lock. Hair's tiles given back and its
  bakes are thereby still read under ONE lock, as before.
- The log first wrote one line an atlas, each with the whole count's move (four lines saying "-345 MB"). Now one
  line names the atlases that moved together.

## 4. Workarounds met, and ideas

- Workarounds met: none removed, none added. The atlases themselves are copies of textures (the owner: "Don't do
  copies"; PT-45, PT-46 replace them by the game's own textures at a hit). When the last reader of an atlas goes,
  this cut goes with the atlas.
- The second count of the copies of HDRP's frame and of the outputs (77 MB at 1280 x 720, about 170 at
  1924 x 1084) cannot go this way: both devices use them every frame. They go with the yardstick (the migration).
- `kSideIdleMs` could be shorter (a side could go the frame after its bake): the open costs a tenth of a
  millisecond. Two seconds was chosen so that a burst of bakes over many frames opens a side once, not every
  frame, and because what a re-open costs the game's device in the game is not known yet.
- The memory line is written at most every 10 s; a side's own line is written when it moves (the first 64 moves
  of a start, then every thirty-second).

## Files

Changed against `base81` (SHA-256, first 12; the frozen copy in `<scratch>\wt\memory4-handover1\`):

| File | SHA-256, first 12 |
|---|---|
| `native/pathtracing/src/tracer.cpp` | 28BD517C1F7F |
| `native/pathtracing/src/texture_share.hpp` | 778F7EDA13F6 |
| `tests/pathtracing_memory2_checks.inl` | 6EB26C97BCFC |
| `tests/pathtracing_memory4_checks.inl` (new) | 6DB79C524248 |
| `tests/source_checks.py` | 9077F42F0460 |

And this report, `docs/agents/memory-4.md` (new). Nothing else differs from `base81` (every file of
`native/pathtracing`, `tests`, `docs/agents`, `tools`, `src/TGPathTracing` and the root's own files compared).

Scripts: `<scratch>\m4\memory4-configure.cmd`, `memory4-build.cmd`, `run-check.ps1` (`-Tag <name> [-Compile]
[-All] [-Also1080]`: builds, stages, waits for the GPU gate by itself, runs under the 10 GB ceiling), `eol.ps1`.
