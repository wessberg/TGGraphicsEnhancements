# PT-45 phase 3: texture sharing made safe for every player

Agent report, 2026-10-07. Built and tested offline in a copy of the tree (`wt/share3`, base `wt/base17`). Nothing ran in
the game, nothing was deployed or committed. Phase 1: `docs/agents/share-1.md`; phase 2: `share-2.md`; the probe and
its "Other cards" section: `texture-share.md`.

## In three lines

1. **A self-test on the player's own card.** Before the hook flags anything, a worker thread shares one texture of each
   format the game uses and reads it back through the tracer's own path. Only formats that come back bit for bit are
   flagged; a failed format keeps the atlas copies; if none passes, the hook stays out. 82 to 168 ms on its own thread,
   0.03 to 0.06 ms on the game's.
2. **Hair and eye maps follow mip streaming.** When Unity swaps a hair or eye map for a new D3D11 texture, the plugin
   registers the new one and the native side points every hair and eye map record at it.
3. **A video memory cap.** A texture is opened only while Windows' count for the game (which counts an opened texture a
   second time) stays under 90 % of the budget Windows gives the game, and only if it takes at most a quarter of what is
   left, so the largest stop first. Kept-back textures keep their atlas tiles and are asked for again every 10 s.

## Words used here

- **Legacy flag**: `D3D11_RESOURCE_MISC_SHARED`, the old sharing flag the hook adds. Microsoft does not check it and
  says its behaviour "might vary from driver to driver".
- **Self-test**: a check the mod runs at every start, on the player's PC, before it relies on the flag.
- **Budget**: how much video memory Windows lets the game use before it starts moving the game's memory to the PC's
  own RAM (`IDXGIAdapter3::QueryVideoMemoryInfo`, "local" memory).
- **Map record**: the tracer's note of which shared textures a hair or eye material reads (phase 2's `set_map_record`).

## What changed

### 1. The start-up self-test (`native/pathtracing/src/texture_share.cpp`, `self_test`)

- **When.** `d3d12_device_ready` (inside the game's swapchain creation) now calls `share::game_device_ready(game,
  device)`, which only starts a thread (`start`) and returns. The hook goes in when the thread ends, and only if some
  format passed. Textures the game makes before that are born unshared and keep their copies, as the design said.
  `TGPT_RegisterSharedTexture` starts the test instead if the device hook was missed.
- **On which devices.** A D3D11 device of its own (`D3D11CreateDevice` on the adapter of the shared D3D12 device's LUID;
  the game's adapter LUID is checked to match: "the D3D12 device is on another GPU than the game" otherwise) and the
  bridge's shared D3D12 device with its own queue. Never the game's context. The thread is marked internal for the
  bridge's hooks.
- **What.** 22 formats, 24 cases: BC1, BC1 sRGB, BC2, BC2 sRGB, BC3, BC3 sRGB, BC4, BC5, BC6H, BC7, BC7 sRGB, RGBA8, RGBA8
  sRGB, BGRA8, BGRA8 sRGB, RGBA16F, RGBA32F, RG16F, RG8, R16, R8, A8 (the harness's `textures.txt` lists 2, 10, 28, 29,
  34, 56, 65, 71, 72, 77, 78, 80, 98, 99; the rest are Unity's neighbours), each 2:1 and 64 KB or more with every mip down
  to 1x1 (the hook's own size rule); BC1 sRGB and BC3 again at 2048x1024 (the game's 4096 and 2048 colour and normal maps
  are these two formats: a large texture's memory layout). Filled with known random bytes at creation, `DEFAULT`,
  shader-resource only, legacy flag: exactly what the hook makes.
- **How it reads them.** D3D11 signals a shared fence and flushes; the D3D12 queue waits on it (the tracer's producer
  handoff). Each texture is opened by `GetSharedHandle` + `OpenSharedHandle`, its D3D12 description checked (size, mips,
  slices, format), and read through `view_desc`, the very view the registry's `open` writes (now one function for both).
  Two comparisons: every byte of every mip copied back to the CPU against the bytes given; and every texel of every mip
  loaded through that view against a texture D3D12 made itself from the same bytes, loaded the same way
  (`native/pathtracing/shaders/pt_share_test.hlsl`, its own root signature, counters per case: texels that differ, texels
  read, texels not all zero). Any failure at any step fails the case; a format passes when all its cases pass.
- **The result.** A 256-bit set of passed DXGI formats. The hook's `wants()` now also needs `format_passed(format)` (a
  typeless format by the format its view reads) and a plain 2D texture (`plain()`: arrays and cube maps are no longer
  flagged; the registry never opened them, so the flag on them was risk for nothing). The registry also refuses a
  flagged texture of an unproven format. A texture in a failed format reports `TGPT_SHARE_SELF_TEST` and keeps its tile.
  No format passing: hook state 4, the hook never goes in.
- **Every start, no cache**, as the design said: a driver update can break what worked.
- **One log line** in `TGPathTracing.Native.log`, the card, driver, count, time with its parts, and each case's verdict.
  The plugin's "Shared textures (PT-45)" line in `LogOutput.log` now ends with "its start-up self-test passed N of M
  formats in T ms" and names the hook's new states.

### 2. Hair and eye maps follow mip streaming

- Plugin: `SceneCapture.NativeTexture` (hair's diffuse, root, ID, mask, blend, flow and specular maps, the eyes' sclera
  and iris) calls `TextureShare.Character`, which registers the map and adds it to the mip-streaming watch phase 2 built
  for material textures. When `TextureShare.Refresh` sees it swapped for a new D3D11 object, it registers the new one and
  calls the new `TGPT_SwapSharedTexture(old, new)`.
- Native: the export, under the engine lock, points every hair, lobe and eye bake still waiting at the new texture (they
  hold the old one, so its address can't have been reused), and queues the swap. The bakes now give `set_map_record` the
  D3D11 texture each handle was read from (`GpuScene::mapTextures_`). On the render thread, after the frame's bakes,
  `apply_texture_swaps` rewrites every handle read from the old texture, or from the new one before it had a number,
  to the new texture's handle (`GpuScene::swap_map_texture`); the records are resolved again before the upload as in
  phase 2. The baked tile itself stays as it was: it is only the fallback now.

### 3. The video memory cap (`texture_share.cpp`, `within_budget`)

- Before `open` takes a slot and calls `OpenSharedHandle`, it reads the game's adapter's local memory budget and current
  count (`QueryVideoMemoryInfo`, every time: measured below as immediate, so no lag to correct for). The texture opens
  only if its size (rounded to 64 KB pages) times 4 fits under 90 % of the budget minus the count. Near the budget the
  largest stop first; nothing opens once the count is at 90 %.
- `TGPT_SHARE_BUDGET` for the rest. The plugin keeps those and asks for up to 64 of them again every 10 s (the game frees
  textures as the player travels); materials and hair and eye records then take their numbers. Textures already open
  stay until the game frees them (nothing is evicted).
- One native log line at most every 30 s: the budget, the count, the tracer's open textures and MB, the largest size still
  opened, and how many were kept back. The 30 s native summary line adds "N not opened near the video memory budget".
- Unknown budget (the query fails): no cap, as before.
- **Measured** (RTX 5080, 16 GB, budget 15209 MB): opening a 2048x2048 BC3 (5.38 MB) moves Windows' count for the
  process by +5.38 MB at once (no lag, so the count read at each open is current); the rule keeps back a large texture
  and lets a small one through at a limit 16 MB above the count, and keeps back everything under the count.
  **Only reasoned, not measured**: what an 8 GB card's budget and count look like in a real scene, whether Windows
  actually demotes memory when the count passes the budget with shared textures counted twice, and whether 90 % and a
  quarter are the right numbers. The harness on an 8 GB card (or the owner's card with `TGPT_TEST_SHARE_LIMIT_MB` set
  to a small card's budget) would tell.

## Files

| File | What |
|---|---|
| `native/pathtracing/src/texture_share.cpp`, `.hpp` | the self-test and its thread, the format gate in the hook, `plain()`, `view_desc`, the cap, `TGPT_SHARE_SELF_TEST`/`BUDGET` reasons, `selfTest` in the totals |
| `native/pathtracing/shaders/pt_share_test.hlsl` | new: the self-test's texel comparison |
| `CMakeLists.txt` | `shareTest:pt_share_test` in the no-ray shaders; ctest `tgpt_share_selftest_fails_one` |
| `native/pathtracing/include/tgpt_api.h` | `TGPT_SHARE_SELF_TEST`, `TGPT_SHARE_BUDGET`, `TGPT_SharedTexture.selfTest` (was `reserved`), hook states 3 and 4, `TGPT_SwapSharedTexture` |
| `native/pathtracing/src/extension.cpp` | `game_device_ready(game, device)`; the `TGPT_SwapSharedTexture` export; `ClearScene` clears the swaps |
| `native/pathtracing/src/runtime.hpp` | `Engine::textureSwaps` |
| `native/pathtracing/src/scene.cpp`, `.hpp` | `set_map_record`'s texture pointers, `swap_map_texture`, `mapTextures_` cleared with the scene |
| `native/pathtracing/src/tracer.cpp` | the three bakes pass their maps' pointers; `apply_texture_swaps` after `bake_eyes` |
| `src/TGPathTracing/NativeTracer.cs` | `Tgpt.ShareSelfTest`, `ShareBudget`, `NativeSharedTexture.SelfTest`, the `SwapSharedTexture` binding |
| `src/TGPathTracing/TextureShare.cs` | `Character`, `Follow` (shared by the swap and the retry), the budget retry, the log line's self-test and states |
| `src/TGPathTracing/SceneCapture.cs` | `NativeTexture` calls `TextureShare.Character` |
| `tests/pathtracing_gpu_test.cpp` | the self-test checks, the broken format's texture on its tile, the cap checks, the eye map swap check, the swapchain's time |
| `tests/source_checks.py` | `test_texture_sharing_is_safe_on_every_card`; phase 1's device-ready check updated |
| `CHANGELOG.md` | the entry at the top of the path tracing part |
| `AGENTS.md` | ctest now runs 17 tests (the new `tgpt_share_selftest_fails_one`) |

## ABI and bits

No ABI number picked (the main tree is at 0x00011900): the merge takes the next one in `tgpt_api.h`, `NativeTracer.cs`,
`test_path_tracing_abi` and PROJECT-CONTEXT 9.1. What this adds, all additive, sizes unchanged:

- Export `TGPT_SwapSharedTexture(uint64_t oldTexture, uint64_t newTexture)` (the plugin binds it: a 1.19 DLL lacks it).
- Return codes `TGPT_SHARE_SELF_TEST` (-27) and `TGPT_SHARE_BUDGET` (-28).
- `TGPT_SharedTexture.reserved` renamed `selfTest` (passed formats bits 0-7, tested 8-15, ms 16-31); still 96 bytes.
- `TGPT_SharedTexture.hook` gains values 3 (waiting for the self-test) and 4 (out: no format passed).
- No material bit, no option bit, no `FrameConstants` change, no change to any traced pass.
- Test-only environment variables read by the native DLL: `TGPT_TEST_SHARE_BREAK=<DXGI format>` (the self-test reads
  that format back one byte wrong) and `TGPT_TEST_SHARE_LIMIT_MB=<n>` (the cap's limit in MB). Unset in the game.

## Offline numbers

RTX 5080, driver 32.0.16.1692, `tgpt_gpu_test`:

| | number |
|---|---|
| Self-test, passed | 24 of 24 cases, 22 of 22 formats |
| Self-test, time on its thread | 82 to 88 ms (device and pipeline 42, textures 9, GPU 13, check 0); 153 and 168 ms right after a new DLL (device and pipeline 108 and 126: the driver compiling the pipeline anew) |
| Self-test, time on the game's thread | 0.04 to 0.06 ms (starting the thread) |
| The game's swapchain (with the D3D12 device and the thread start) | 59.5 to 62.5 ms |
| Tracer passes made | 0.6 s (0.5 s in phase 2's last run: same within rounding) |
| BC7 sRGB broken on purpose | "BC7 sRGB FAILED: D3D12 read different bytes at mip 0"; 21 of 22 formats; its texture: no flag, `TGPT_SHARE_SELF_TEST`, atlas tile 0; the hook in |
| Windows' count after opening a 2048x2048 BC3 (5.38 MB) | +5.38 MB at once, +5.38 MB 200 ms later |
| Cap, limit count + 16 MB | 2048 BC3 kept back (-28), 256 BC3 opened; log: "textures over 3.8 MB are not opened now (a quarter of the 15 MB left ...)" |
| Cap, limit under the count | 256 BC3 kept back |
| Cap, no limit | both opened when asked again |
| Eye sclera map swapped (dark grey for white), old one freed | 5408 pixels: 0.030 with the old map, 0.999 after the swap |
| Whole GPU test (final build, native A24AA0D1E5C6) | 863 checks, 0 failed, 18,895 frames traced, none failed (phase 2 ended at 757) |
| ctest `tgpt_share_selftest_fails_one` | passed (1.3 s) |
| `python tests/source_checks.py` | 84 tests, OK |
| Plugin (`dotnet build src\TGPathTracing -c Release`, to a scratch folder) | 0 warnings, 0 errors |

Run on Windows' own D3D12 runtime (no Agility SDK staged in the scratch prefix), as phase 2's runs were.

Mutation checks (each broken on purpose, built, run, put back byte for byte; `scratchpad/share3-mutate.py`,
`share3-mutate-source.py`):

| Broken | What failed |
|---|---|
| The hook flags a format whatever the self-test said | "a texture in a format the start-up self-test failed is not made shareable" (BC7 sRGB came out with the flag; the registry's own guard still refused it, -27) |
| The self-test compares neither bytes nor texels | "a format the start-up self-test reads back wrong fails alone" (22 of 22 passed) and the texture check (opened as number 3) |
| No cap | "near the video memory limit a large texture is not opened ..." and "over the video memory limit no texture is opened" |
| The cap without the largest-first rule | "near the video memory limit a large texture is not opened ..." (the 5.6 MB texture fit the 16 MB left) |
| Swaps queued but never applied to the map records | "an eye whose map mip streaming made anew reads the new texture ..." (0.030 after the swap: the dark tile) |
| 12 source mutations (hook without the verdict, hook in before the verdict, no format passing still installs, texels not compared, a game format untested, the self-test joined on the game's thread, no largest-first, the cap after the open, swaps not applied, hair maps not watched, no swap call, the ctest's broken format gone) | each fails `test_texture_sharing_is_safe_on_every_card` |

## What the next harness run should show in the log

`BepInEx/TGPathTracing/Native/TGPathTracing.Native.log`, shortly after "Shared D3D12 device: DirectX Raytracing tier":

    Texture sharing self-test on NVIDIA GeForce RTX 5080 (driver 32.0.16.1692): 24 of 24 passed, 22 of 22 formats, in N ms on a worker thread (device and pipeline a ms, textures b ms, GPU c ms, check d ms; 0.0x ms on the game's thread): BC1 ok, BC1 sRGB ok, ... A8 ok, BC1 sRGB 2048x1024 ok, BC3 2048x1024 ok
    Texture sharing (PT-45): the game's textures made only for shaders to read, 64 KB or more with their mips, in a format the self-test passed, are made shareable from the game's device on, N ms after the game's device (CreateTexture2D and CreateTexture2D1 hooked)

Then the 30 s summary as before. Only on a card with little video memory: "Texture sharing: video memory near this
card's budget (...)".

`BepInEx/LogOutput.log`, the "Shared textures (PT-45)" line: "made before the hook" a little higher than phase 2's
(textures from the first ~0.1 to 0.2 s after the device), never "in a format the start-up self-test failed" on the
owner's RTX 5080, no "kept back near this card's video memory budget" on a 16 GB card, and the end "the hook is in; its
start-up self-test passed 22 of 22 formats in N ms".

**Good**: the self-test line says 24 of 24, the hook line follows it, the game looks and loads as with phase 2, and
hair and eyes keep their sharpness after the camera has been near a character for a while (their maps streamed in).

**Failed, and what it would mean**: "could not run (...)": the self-test's own device or pipeline failed: every texture
on copies, the reason in the line. A format FAILED on the RTX: a driver change; that format on copies. "the hook stays
out": nothing passed. Many "made before the hook": the game made its material textures within the self-test's time
(then it could run earlier, or flag before the verdict and drop later).

## Risks

- **What the self-test can't catch** (as the design said): a fault that shows only when Windows moves textures out and
  back under memory pressure, a size or layout it doesn't cover, timing faults under the game's load.
- **The first ~0.1-0.2 s.** Textures the game makes before the verdict are not shared. Measured offline only; the log's
  "made before the hook" count in the next run says whether any material texture falls in that window.
- **A second D3D11 device for 0.1 s** at start, on the game's GPU (D3D11CreateDevice; the bridge doesn't hook it; a
  wrapper like ReShade may wrap it, harmless). About 10 MB of test textures and buffers, freed when the test ends.
- **Formats not on the list** (BC4/BC5/BC6H signed, R10G10B10A2, R11G11B10, R16F, ...) are no longer flagged: they keep
  their atlas tiles. None is in the harness's texture list.
- **The cap reads Windows' count, not the card's**: on an integrated GPU "local" memory is a slice of the PC's RAM; the
  same rule holds. It does not evict: if the budget shrinks after textures opened (another program takes video memory),
  what is open stays until the game frees it. Not measured on a small card; only reasoned (the probe's double count and
  the +5.38 MB measured here).
- **The swap matches by pointer.** The old texture's address is compared, never dereferenced; a waiting bake holds the
  old texture alive, so its address can't be reused while it waits. A record baked from an address the game later reuses
  for an unrelated texture is cleared with the scene or rewritten by its next bake.
- **A kept-back texture costs a retry** every 10 s (64 at most, one `QueryVideoMemoryInfo` each, microseconds).
