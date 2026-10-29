# PT-35 and PT-13: the detail tiles ran out, and jars were not see-through (the surfaces agent, round 2, 2026-10-06)

Written in the copy `wt/surfaces2` (base: `wt/base5`). Two agents wrote it: the first was stopped at 20:58 with its
edits on disk, the second read every diff, fixed three gaps (below), finished the checks and wrote this file. Built,
installed to a scratch prefix, checked offline. Never deployed, never run in the game.

## The answer in short

1. **The tracer had 32 detail tiles and they ran out.** A detail tile is a 512x512 square of the tracer's texture atlas.
   Hair color, eyes and see-through glass need one, because the camera sees them straight from the atlas. Every
   character's hair, beard and brows took a tile each, and no tile ever came back. A dozen characters took all 32.
   After that jars showed their texture in big squares, and hair fell back to the old blocky path.
2. **Now there are 128, and they are a pool.** Hair with the same maps and values shares one tile. Hair gives its tile
   back when its material is gone or gets new colors. Hair never takes the last 16: those stay for glass and eyes.
3. **A jar is now a thin shell, not a block of glass.** The tracer bent light as if the jar were solid glass from its
   front wall to its back wall. That made it a lens: one patch of the wall behind it spread over the whole jar. Now
   the tracer looks straight through and tints by the glass, as the game's own blending does.

## Numbers the merge needs

| What | Base (`base5`) | This copy | Where |
|---|---|---|---|
| `TGPT_ABI` | 0x00011500 | **0x00011600** | `tgpt_api.h` line 18; `NativeTracer.cs` `Tgpt.Abi`; `test_path_tracing_abi`; PROJECT-CONTEXT 9.1 |
| `sizeof(FrameConstants)` | 3040 | **6112** | `tracer.cpp` static_assert; `source_checks.py` twice (the layout check and the RTXDI token list); PROJECT-CONTEXT 9.1 |
| `strandTiles` rows | 64 | 256 (`2*kMaxDetailTiles`) | `tracer.cpp` twice (FrameConstants, State); `pt_common.hlsli` `float4 strandTiles[2 * DETAIL_TILES]` |
| `kWaterConstantsStride` | 4096 | 8192 | `tracer.cpp` (the water pass's copy of the frame constants) |
| `kDetailRows` / `kMaxDetailTiles` | 4 / 32 | 16 / 128 | `runtime.hpp` |
| `kDetailReserve` | none | 16 | `runtime.hpp` (new) |
| `DETAIL_TILES` | none | 128 | `pt_common.hlsli` (new; `pt_hair.hlsl` reads it in place of `32u`) |
| Atlas | 4096x6144 | 4096x12288 | `kAtlasHeight` (a static_assert keeps it under Direct3D's 16384) |
| Exports | | + `TGPT_ReleaseHairColor(uint64_t key)` | `tgpt_api.h`, `extension.cpp`, `NativeTracer.cs` (bound in `Discover`), the GPU test's `LOAD` |
| Source checks | 78 | 80 | new: `test_path_tracing_detail_tile_pool`, `test_path_tracing_glass_is_a_shell` |
| GPU test `expect`s | 428 | 452 | `tests/pathtracing_gpu_test.cpp` |

- FrameConstants: the only change is the strand rows, +192 rows of 16 B = +3072 B. If another agent's copy adds
  fields, the merged size is theirs plus 3072.
- The ABI: the first agent left it at 0x00011500. I raised it, as 1.15 was raised for one new export
  (`TGPT_SetProfileReflectance`). Two reasons: there is a new export, and `TGPT_RegisterHairColor` may now return
  another tile for a key it knows. A plugin of 1.15 on this DLL would keep reading the old tile. If another agent's copy
  also took 0x00011600, one number for both is fine: header, `Tgpt.Abi` and the source check must agree.
- No struct changed size (TGPT_HairColor 240 B, TGPT_Material 128 B, all as before). No new option bit, material bit
  or `mat.y` flag.

## What changed and why

**Native**
- `runtime.hpp`: 16 rows of detail tiles (128). `kDetailReserve` 16. The pool's state in `Engine`: `detailTilesMade`,
  `detailTilesFree`, `detailTilesGivenBack`, `hairContents` (a tile and its count of users by the hair's maps and
  values), `hairKeys` (each hair material's tile and content).
- `extension.cpp`:
  - `take_detail_tile(e, hair)`: one allocator for glass, eyes and hair. A tile given back first, else the next new
    one. For hair it says no once only the last 16 are free.
  - `TGPT_RegisterHairColor`: hair of the same maps and values (the bytes of `TGPT_HairColor` from `diffuseMap` on)
    gets the same tile and is not baked again. The same key with new colors: baked in place if it reads its tile
    alone; else it joins the tile that already has those colors, or takes a new one, and lets go of the old. If no
    tile is free it keeps the old one. A key on a 64x64 tile keeps that tile.
  - `TGPT_ReleaseHairColor` (new): the key lets go. The last user gives the tile back: its waiting bake is dropped
    and its number queued for the render thread.
  - `TGPT_ClearScene`: the pool starts over.
- `tracer.cpp`: the strand rows are sized by the tile count. `bake_hair` clears the strand row of every tile given
  back before it writes the rows of this frame's bakes, so new hair is not drawn with the last hair's strands.
  The water pass's copy of the frame constants is 8192 B a slot.
- `tgpt_api.h`: the new export, the comments, ABI 1.16.

**Shaders**
- `pt_common.hlsli`: `DETAIL_TILES`, `strandTiles[2 * DETAIL_TILES]`.
- `pt_hair.hlsl`: the strand row is read for any of the 128 tiles (it stopped at 32).
- `pt_specular.hlsl`: `behind_sheets` and the `sheet` test. See-through glass that HDRP draws as a transparent
  surface lies in front of HDRP's surface. If it is more than half of `pt_prepare`'s window in front
  (0.5 x (3 cm + 1 % of the distance)), it is a sheet: the ray goes straight on, through up to 4 more sheets (the
  jar's far wall, the next jar), each tinting it where HDRP draws that face. Glass in HDRP's GBuffer (bottles) is
  bent in and out as before.

**Plugin** (`src/TGPathTracing`)
- `NativeTracer.cs`: `ReleaseHairColor`, ABI 1.16.
- `SceneCapture.cs`:
  - `hairHolds`: which hair materials hold a tile and the frame each was last traced in.
  - `ReleaseIdleHairTiles`: called only when hair or glass finds no tile. Hair whose material is destroyed, or was
    not traced for 600 frames, gives its tile back. Its traced materials are forgotten, so it is read anew if it
    comes back. It logs one line.
  - `FollowHairTile`: when a recolored hair material gets another tile, its traced materials take it.

**What the second agent fixed**
1. `RefreshHairColor` read the traced material before `HairTile` and wrote that copy back after (for the highlights'
   tint). So a hair material that got another tile had the old tile put back. In game that would have left every
   recolored clone with the colors it shared, the default ones. Now the material is read after `HairTile`. This is
   plugin code: only a source check covers it, no offline test can. **Look at hair colors in game first.**
2. `SceneCapture.Reset` did not clear `hairHolds`. After a scene clear the native pool starts over, and a stale hold
   made a material look served: it never asked gone hair for a tile.
3. The ABI number (above), and one exact check that hair stops 16 tiles short.

## Offline results

All on the final tree, 2026-10-06 21:19-21:24. Build: `%TEMP%\tgb\surfaces2` (Ninja, Release), installed to
`%TEMP%\tgb\surfaces2-native`; the installed DLL hashes equal to the built one.

- `tgpt_gpu_test`: **all path tracing checks passed**, 664 ok, 0 FAIL, 15667 frames traced, 0 failed
  (log `scratchpad\surf2\fin-final.log`, pictures `surf2\img-fin-final`, among them `51-jar-checker-*.png`).
- `python tests\source_checks.py`: 80 tests, OK.
- `dotnet build src\TGPathTracing -c Release -f net472 -p:DeployToGame=false -o %TEMP%\tgb\surfaces2-managed`:
  0 warnings, 0 errors.

The numbers from the GPU test:

| Check | Number |
|---|---|
| A clone of a hair material | reads the same tile; 200 clones take no tile |
| A clone colored red | the tile it shared stays green (5408 of 5408 texels green, 0 red); its own new tile 5408 red |
| A tile given back, taken by blue hair | 5408 blue, 0 red |
| Hair colors of their own | 109 took a tile each; the highest is tile 111 of 0-127 (exactly 16 left); the 110th got a 64x64 tile |
| After that hair | a glass texture and an eye still got detail tiles |
| Given back | 109 tiles by 110 materials (the one on a 64x64 tile gives none); 54 of 54 new hair colors took one again |
| The layer card | its tile lies past the first 32, and its strand row is read (layer hair checks pass) |
| A checker seen through a 90 cm shell of glass | its columns follow the bare checker's by **0.99** (before: **0.06**, the lens), with 0.97 of its contrast |

## Mutation checks

Each broke one thing on purpose, built and ran the full GPU test, then put the file back (hashes checked after).
Runs `scratchpad\surf2\fin-mut-*.log`; script `scratchpad\s2fin\mutate.py`, `run-all.ps1`.

| Broken on purpose | Result |
|---|---|
| s: no glass is ever a sheet (the jar is a block again) | FAIL "what lies behind a shell of see-through glass (a jar) is seen straight through it": 0.06 |
| a: hair never shares a tile | 7 FAIL, among them "hair materials of the same maps and values share one detail tile", "200 clones ...", "a clone needs no free tile" |
| b: hair leaves 15 tiles, not 16 | FAIL "hair leaves exactly the last 16 detail tiles alone" (highest tile 112) |
| c: a tile is never given back | 12 FAIL, among them "a hair material that is gone gives its detail tile back", "tiles given back are handed out again", the layer hair checks |

The first agent's runs (20:15-20:31, `surf2\mut-a..d.log`) also broke: "hair colored anew is baked over the tile it
shares" (6 FAIL, among them "the tile a hair material shared keeps its colors when that material is colored anew"),
and reserve 0 together with the hair layer reading only the first 32 strand rows (3 FAIL, among them "see-through
glass and an eye registered after the hair still get detail tiles" and the layer hair's two).

The plugin fixes have source checks only. Each was broken on purpose and its check failed, then passed again
(`scratchpad\s2fin\check_mut.py`): `hairHolds` not cleared with the scene; `RefreshHairColor` reading the material
before `HairTile`; the plugin's ABI left at 1.15; the GPU test's exact reserve check renamed.

## Files changed (against `wt/base5`)

`native/pathtracing/include/tgpt_api.h`, `native/pathtracing/shaders/{pt_common.hlsli, pt_hair.hlsl, pt_specular.hlsl}`,
`native/pathtracing/src/{extension.cpp, runtime.hpp, tracer.cpp}`, `src/TGPathTracing/{NativeTracer.cs, SceneCapture.cs}`,
`tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`, `CHANGELOG.md` (one entry at the top of the path tracing
part), `docs/PROJECT-CONTEXT.md` (9.1's ABI and FrameConstants; the atlas tiles line), `docs/PT-DETAILS.md` (hair
and glass: 128 tiles, a pool), and this file. Line endings as they were (LF or CRLF per file).

## What to look at in the next in-game harness run

Run these in one go, in this order: `refract-jar`, `hair-eyebrows`, `npc-candle`, `tavern-room`, `npc-day`
(`.\tools\harness\Run-PtHarness.ps1 -Scenario refract-jar,hair-eyebrows,npc-candle,tavern-room,npc-day`). One run
matters: the tiles ran out only after the first scene.

- `refract-jar`: the two jars are greenish glass with fine stains, no big squares. The wall shows through them where
  it does in the game's picture. The jars on the shelf below are there. Compare `game-vs-traced.jpg` with run
  `20261006-155214`.
- `hair-eyebrows` (not the first scene): Ron's hair is fine strands, not solid blocks with stair steps. Compare with
  `hair-eyebrows/head-two-runs.jpg` of run `20261006-203731`. The pale crown and the black brows are PT-01, the hair
  agent's: this change does not touch them.
- `npc-candle`, `tavern-room`, `npc-day`: every character's hair keeps its own color (not all one default color:
  fix 1 above), fine strands on all of them, eyes sharp. Glass on the tables see-through.
- Timing: the frame constants doubled in size. Compare the report's frame times with the last run's.

**What `BepInEx/LogOutput.log` should say**
- Every `Characters: shader "TG/Character/RealHair" (material ...) hair, ...` line says `drawn by the tracer as a
  layer over HDRP's surface`. In run `20261006-203731` (npc-day, hair-eyebrows, npc-candle, tavern-room) 9 of 23 such
  lines did. A line without it means that hair got a 64x64 tile.
- The eyes' lines say `(a 512x512 tile)`, never `(a 64x64 tile)`.
- `Detail tiles: N hair materials no longer traced gave their tiles back` shows up only if the pool ran short. With
  128 tiles and sharing it should not show up in these five scenes. If it does, count the hair lines: more than 112
  different hair colors were alive at once, or sharing is not working.
- `Characters: hair "..." colored after it was first read: ..., its tile baked again` is the recolor path (fix 1).
  The hair it names must show those colors.
- No `Hair color bake failed` and no `Material texture bake failed`.

## Risks

- **Video memory.** The atlas is one RGBA8 texture shared by Direct3D 11 and 12. It grows from 4096x6144 (96 MiB) to
  4096x12288 (192 MiB): 96 MiB more, always, used or not. 128 tiles of 1 MiB each. On an 8 GB card with path tracing
  that is small but not nothing.
- **Frame constants.** 6112 B a frame instead of 3040 B, uploaded every frame, and again for the water pass. Not
  measured in game.
- **The sheet test is a distance rule.** Glass more than 1.5 cm (plus 0.5 % of its distance) in front of HDRP's
  surface is a sheet. A see-through pane lying right on a wall is not, and is traced as before. A thick solid block
  of see-through glass drawn as a transparent surface would now be seen straight through, with no bending. The jars
  and panes checked offline are shells.
- **Sharing goes by texture addresses.** Two hair materials share when their five map pointers and all values are
  equal. If a texture is destroyed and Unity makes another at the same address while a stale key still holds the
  tile, hair with otherwise equal values would share the old picture. Unlikely; cleared with the scene.
- **A tile changes hands in a frame or two.** New hair on a tile given back can show the last hair's colors until its
  bake runs (8 bakes a frame). Its strand row is cleared first, so the hair layer waits rather than draw wrong strands.
- **Hair on a 64x64 tile keeps it** until the scene is cleared, unless hair that is gone could be released at the
  moment it was first read.
- **The plugin side has no offline test.** `ReleaseIdleHairTiles`, `FollowHairTile` and fix 1 run only in the game.

## Left open

- Nothing of this was seen in game.
- PROJECT-CONTEXT grew by 2 lines (755 to 757). It was over its 700-line budget before; not condensed here.
- `docs/PT-REQUESTS.md` and `BACKLOG.md` are the main session's: not touched in this copy.
- The lasting answer (PT-REQUESTS, the note after PT-36) is the game's real textures in the tracer. This is the patch.
