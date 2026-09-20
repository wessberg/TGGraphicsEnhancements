# Resolved in tested setups: FSR frame generation HUD and hand flicker

Status: reporter confirms HUD/hand results are now correct, including ReShade and
RenoDX DLSS 5, with and without upscaling, and the other upscalers tested. The follow-up
below extended the input-orientation fix from FSR to DLSS-G and XeSS-FG, and the reporter
confirms all three now look equally good.
Last worked 2026-09-20. Changes are **uncommitted**. Mono visual verification is complete;
the IL2CPP hand-history port below is awaiting in-game verification. Read this before touching the FSR path.

## The orientation fix was general, not FSR-specific (2026-09-20)

After the FSR work landed, the reporter said FSR now looks better than **both** DLSS Frame
Generation and XeSS-FG: DLSS has a wobble during movement that FSR does not. That points back at the
one change made for FSR alone.

The earlier reasoning was that DLSS-G and XeSS "retain their existing inputs and projection
matrices" because FSR 3.1 reconstructs from scalar FOV/near/far and so cannot describe the
inversion. That does not hold. A projection matrix describes a sign convention, not the row order of
a texture. Streamline and XeSS sample depth and motion at the coordinates of the frame they
interpolate, which is the presented back buffer, and neither exposes any way to say a tagged texture
is upside down.

That the presented frame really is mirrored against HDRP's render targets is already measured by
this build, not assumed: the HUD-less copy is taken from HDRP's `AfterPostProcess` color, Y-flipped
by `HudlessUi`'s flag 1, and then compared against the presented frame at every Present. It verifies
at 2-6% differing sample points in gameplay. The depth and motion vectors come from the same HDRP
render targets, so DLSS-G and XeSS-FG were reprojecting the presented frame from mirrored vectors.

Why that reads as a wobble rather than a broken image: on an RTX 5080 DLSS-G blends its own optical
flow with the supplied vectors, so wrong game vectors degrade the result instead of destroying it,
and a horizontal pan produces a roughly uniform horizontal vector field whose mirrored rows are
close to correct. Local and vertical motion, and near geometry, are where it shows. XeSS-FG has less
to fall back on, which matches the reporter putting FSR ahead of it too.

**Change:** depth and motion copies are flipped for all three backends, the vertical motion scale and
jitter are negated with them, and `TGFG_SetCamera` turns the camera's four clip-space matrices by
`F = diag(1,-1,1,1)` so the camera describes the same space. FSR was unaffected by the matrix part -
FI 3.1.6 does not read them - which is why the FSR-only fix worked without it. The per-slot
orientation flag and the unflipped copy shaders are gone; there is one orientation in the codebase.
See `native/include/camera_orientation.hpp` and the "Input orientation" section of
`docs/ARCHITECTURE.md`.

**Validation:** `tests/camera_orientation_tests.cpp` builds reference matrices in both orientations
and derives the presentation forms through an independent matrix inverse, then checks the conversion
against them, checks that a world point reprojects correctly through the converted `clipToPrev`, and
checks that the conversion is its own inverse. The WARP copy test now covers an odd subrect height as
well. Eight native tests and the build pass.

**Confirmed in game** (Mono, RTX 5080, 2026-09-20): the reporter finds DLSS, FSR and XeSS equally
good afterwards, where DLSS and XeSS were visibly wobblier than FSR before. Note that the first
attempt at this retest ran the previous binary - the game folder's `TGFG.Native.dll` was a Vortex
symlink and the deploy had not been run - and produced "exactly like before". Check the deployed
file's hash against `artifacts/Native` before trusting a null result.

The log for the confirming run shows HUD-less input supplied on every steady gameplay frame for all
three backends (`supplied` equals `captured`, `diff` 2.4-3.1%), `fgOn` equal to `frames`, and no
errors. The gaps that do appear are the verification warm-up and menu screens, where the whole frame
is redrawn and the capture is correctly withheld.

## IL2CPP pause / Options crash: reporter confirms fix (2026-09-20)

Reporter crashed with upscaling active after opening Pause, then Options. PID 20996
crashed with access violation C0000005 at GameAssembly RVA 0xC8DE20 (read address 0x20).
The dump's main-thread stack includes the return address immediately following
`VDependentOption.Update`'s call to `DependentOption.get_ShowDependent` (0x61FFEBE).
Disassembly of that getter shows a tail call through its alternative-evaluation delegate
when present. The evidence points to the dependent-option callback path, not a GPU wait.
It does not identify which row owned the callback or establish the underlying corruption
cause: the required heap pages are absent, and shared-generic stack labels are not unique.
Ignored investigation artifacts are `build/inspect-menu-crash.py`,
`build/menu-crash-stack.txt`, and `build/il2cpp-menu-*.asm`.

The fix constructs IL2CPP TGFG switch rows without a converted `Func<bool>`.
`Il2CppOptionConditions` retains each native owner and managed visibility predicate,
and Harmony-patches the native `ShowDependent` getter only for registered pointers.
Other options use the original getter. Registration clears when rebuilding settings;
shutdown unpatches it. Predicate exceptions hide the affected row and log once.
Mono and the hand-history/rendering fixes are unchanged.

The managed IL2CPP build and stand-in Harmony regression tests pass, covering owned
and unowned rows, native-pointer aliases, changing/nested conditions, exception isolation,
GC retention, clearing and unpatch/reinstall. These tests do not exercise the native
IL2CPP detour. After deployment, the reporter replied "Works great! Zip it!" to the
requested upscaling on, Pause, Options retest. This confirms the reported menu failure
is resolved in that tested setup, without establishing the original corruption cause.

All 33 source/integration checks pass. Candidate deployed and IL2CPP installed file,
version and hash checks pass. Backup:
`BepInEx/TGFrameGeneration/Backups/20260920-163647-127-14a91270-IL2CPP`.
The current IL2CPP Nexus ZIP contains this tested fix; Mono package is unchanged.

## IL2CPP hand-history guard port (2026-09-20)

The installed IL2CPP interop exposes `SkinningManager.RunCopyPrevious(CommandBuffer)`,
but its generated caller count is zero. Inspection of GameAssembly.dll, using BepInEx's
method-address database and Iced disassembly, confirms `OnBeginRendering` inlines the wrapper:
at RVA 0x1052D05 it calls `RunComputeKernel` (RVA 0x107C250) with the copy-kernel field at
offset 0x1C; at 0x1052E3B it calls the same method with the skinning kernel at offset 0x18.
The standalone copy/skinning wrappers at 0x107C230/0x107C240 merely load those fields and
tail-jump to the dispatcher. Hooking only RunCopyPrevious would miss the native render loop.
Ignored inspection artifacts: `build/inspect-il2cpp-kandra.py`, `build/disasm-il2cpp-kandra.ps1`,
and `build/il2cpp-*.asm`. These RVAs are evidence only and are not embedded in the plugin.

The IL2CPP branch now Harmony-patches `RunComputeKernel(CommandBuffer,int)` and guards only
the manager's live `_copyKernel`, always allowing `_skinningKernel` and other dispatches.
It keys the per-frame history set by the native `Pointer`, because different managed wrappers
can represent one native manager. Mono retains its existing RunCopyPrevious hook and object key.
Both paths allow the first copy each Unity frame, preserve Off behavior, clear state on disposal,
and permit retry after a failed copy. The IL2CPP finalizer uses prefix state so a failed skinning
dispatch cannot clear a successful history copy. Matching copy/skinning IDs fail open for skinning.

The IL2CPP build succeeds against the installed interop. Regression tests compile the production
IL2CPP branch and patch a stand-in dispatcher with the installed Harmony runtime: all three FG
backends, single/multiple cameras, native-pointer aliases, separate managers, dynamic kernel IDs,
unrelated dispatches, overlapping IDs, frame gaps, Off/on, copy/skin failure isolation, and
unpatch/reinstall pass. These tests do not run the game's native detour or GPU skinning; confirm
`guard installed ... RunComputeKernel` and duplicate-copy counters in the next game run.
The production Mono branch also passes the shared stand-in/Harmony regression suite on .NET 6.
The IL2CPP package is deployed, with installed file/version/hash verification passed. Backup:
`BepInEx/TGFrameGeneration/Backups/20260920-162053-937-9cd9cc62-IL2CPP`.

## Visual success and runtime confirmation (2026-09-20)

Reporter: "You nailed it! It worked with DLSS 5 and Reshade too! And with and without
upscaling. Checked the other upscalers too. Everything's perfect!"

The successful logs identify the previously missed pass conclusively:
`Observed RenoDX final-output draw: kind=1 count=6 instances=1 contextType=0 capturePending=0`.
Kind 1 is **DrawIndexed**, using six indices on the immediate context. Both PID 18932 and
45072 record it. The first observation precedes HUD capture, as expected during startup;
subsequent replay and verification succeed. Adding DrawInstanced alone could not catch it.

PID 45072 reaches 4,800 final-pass replays at Unity frame 8116. Steady gameplay samples
show verified HUD-less images supplied to FG with zero capture rejections; for example FSR
captures/checks/supplies 252/252/252 with 4.3% differing sample points, and DLSS-G supplies
258/258 with roughly 4.5% differing points. The previous uncorrected capture differed across
roughly 93% of the frame and was withheld. Pixel verification thresholds were not relaxed.
The later SDR run PID 28580 also supplies verified HUD-less images to DLSS-G, FSR and XeSS;
samples without NGX evaluations use HDRP depth/motion input successfully with DLSS-G and FSR.

Record the user's DLSS 5 / ReShade / upscaler coverage as visual test results, rather than
inferring every combination from one log. This closes the reported regression for these
tested setups. The shader recognition remains version-specific; arbitrary effect chains
and future add-on versions are not implied by that result. No further renderer changes
were made after confirmation. Earlier sections below preserve the investigation history.

## DrawInstanced alone still missed the pass; remove triangle-only assumption (2026-09-20)

Reporter still sees HUD flicker with the DrawInstanced build. PID 40064 again shows
successful captures and recognized shader creation, but no replay, no skip diagnostics,
zero verification checks and zero HUD-less supplies. The earlier draw-method explanation
was incomplete: the log does not establish which draw API/geometry the final pass uses.

Both hooks rejected all draws except three vertices starting at zero, before inspecting
the shader or logging anything. The new observer covers all six explicit/indirect draw APIs,
including indexed copies, and no longer assumes the game uses a fullscreen triangle.
The replay still uses its own triangle. It retains the shader identity, current-frame,
immediate-context, target, viewport and live-binding checks, plus normal pixel verification.
Direct draws with zero elements or multiple instances are not replayed.

Diagnostics now report the first recognized draw's API, count, instance count, context type
and pending-capture state, even if it occurs before capture. Pending captures report cumulative
draw counts every five seconds, including command-list execution. Command-list execution is
observed only: its restored pipeline state cannot safely identify the shader inside the list.
Shader-tag attachment failures are now reported too. This distinguishes API/geometry misses
from shader binding, deferred submission and capture-order issues on the next run.

Validation: seven native CTests and 33 source/integration checks pass. The new WARP test
installs the **production MinHook draw detours** through Context4, calls all six APIs through
the base context, verifies live shader visibility before caller restoration, and checks GPU
pixels to confirm forwarding of direct/indexed/indirect arguments. It covers a four-vertex
draw, six-index draws, nonzero offsets and command-list observation. Earlier WARP shader-only
tests could not catch missed draw entry points or the triangle-only filter. These tests still
do not establish the game's draw path or visual success; another runtime observation is needed.
Installed Mono file/version/hash checks pass. Deployment backup:
`BepInEx/TGFrameGeneration/Backups/20260920-155126-049-3a1ed96a-Mono`.

## RenoDX shader recognized, but draw hook missed its execution (2026-09-20)

Reporter still sees HUD flicker. PID 36020 recognizes the supported RenoDX shader and
captures HUD-less images without rejections, but records **no final-pass replays**, zero
verification checks and zero HUD-less supplies. The previous build therefore never applied
its new color correction in game; shader recognition and WARP tests were insufficient.

The hook covered `ID3D11DeviceContext::Draw` only. ReShade's command-list
[`draw()` implementation](https://github.com/crosire/reshade/blob/main/source/d3d11/d3d11_impl_command_list.cpp)
calls `DrawInstanced`, including single-instance full-screen draws. The bridge now hooks
both entry points and uses the same post-draw replay while RenoDX's shader and constants
are still bound. Instanced replay requires exactly one instance starting at zero; the
existing triangle, current-frame, context, backbuffer and viewport checks remain in place.
Skipped recognized passes now log each rejection reason once to distinguish timing,
target, viewport and constant-binding failures. The hand-history fix is unchanged.

Validation: all six native CTests and 33 source/integration checks pass. The installed
shader is still uniquely recognized. Its WARP test now covers both `Draw` and
`DrawInstanced`, passing 288 color comparisons. This validates shader execution through
both APIs, **not the in-game detour or visual result**. Next game run must show
`RenoDX Unity HUD-less final pass replayed`, followed by verification checks and increasing
HUD-less supply; otherwise inspect the new skip messages. Do not lower verification thresholds.
Deployed to Mono with backup
`BepInEx/TGFrameGeneration/Backups/20260920-151802-754-0eaa8072-Mono`.

## ReShade device fix verified; RenoDX final color conversion missing (2026-09-20)

Reporter: hands still look great, HUD still flickers during camera movement. Latest PID 46448
confirms the device fix: HUD captures succeed with **zero capture rejections**. However,
pixel verification rejects them across 93% of sample points (later 95.2% and 66.8%).
No HUD-less image is supplied to FSR. This is a separate failure from the prior -4 rejection.

The installed add-on is RenoDX Unity v0.2026.325.2215. Its final-output replacement shader
(`final_0x20133A8B`) performs additional gamma correction, UI-white scaling, gamut/clipping
and output encoding after our AfterPostProcess capture. Treating that intermediate as
already-final scRGB misses this operation. Public source inspected at
[NotVoosh/renodx-unity](https://github.com/NotVoosh/renodx-unity/blob/main/src/games/unityengine/final_0x20133A8B.ps_4_0.hlsl);
the installed stripped shader was also extracted and disassembled to verify its behavior.
Ignored source checkout: `build/renodx-unity`; extraction: `build/inspect-reno-shaders.py`.

The new native path preserves an oriented FP16 HUD-less intermediate and recognizes RenoDX
Unity's final output shader. On that shader's full-screen three-vertex draw into TGFG's
source backbuffer, it replays the **actual pixel shader, live b13 constant-buffer range and
sampler** on the HUD-less intermediate, then applies the normal scRGB-to-PQ bridge encoding.
It does not copy RenoDX preset values from an INI, interpret its live struct layout, change
RenoDX settings, or modify the rendered backbuffer. Scratch context state is restored.
Only a current valid frame on the bridge's immediate context and full-size backbuffer qualifies.
Unknown output shaders remain unsupported; ordinary image verification remains required.
The shared result is available to all three FG backends.

Release add-ons strip reflection. The installed final shader is uniquely identified by its
3992-byte DXBC container and checksum `66ce044202e8bfe15f65bf998f2d3b63`; it was found at
file offset 48386896. Unstripped builds can be recognized by their narrow resource/signature
contract. Future stripped shader revisions need explicit validation; do not silently accept
arbitrary post-processing shaders. This is a final-output conversion replay, **not replay of
arbitrary ReShade effects or the RenoDX DLSS 5 add-on**, which still require validation.

Validation: six native CTests pass. The installed add-on scan finds exactly one supported
shader. Running that exact extracted shader on WARP passes 144 comparisons across 80/203-nit
white, gamma settings 0/1/2, constant-buffer offsets 0/16, and a nonuniform image to verify
UV orientation. The tests caught and corrected the separate replay vertex signature required
by the installed pixel shader. The ordinary capture test now includes lossless mode 4 (120
color comparisons), plus the existing 120 depth/motion comparisons. These tests do not prove
in-game hook timing or that HUD flicker is fixed.
Installed into Mono; installed file/version/hash checks and 33 source/integration checks pass.
Backup: `BepInEx/TGFrameGeneration/Backups/20260920-144100-876-24f0d0a2-Mono`.

Next validation: same ReShade/RenoDX setup with DLSS 5 off. Check `Recognized RenoDX Unity
final-output shader`, `RenoDX Unity HUD-less final pass replayed ...`, then normal HUD-less
verification and supply counters. If replay succeeds but comparison still fails, capture both
images for analysis; do not relax the verification threshold.

## ReShade/RenoDX: HUD capture rejected by proxy-device identity (2026-09-20)

Reporter re-enabled ReShade/RenoDX through the mod manager, without the DLSS 5 add-on,
and HUD stutter/flicker returned. Run PID 30700 has `dxgi.dll` and
`renodx-unityengine.addon64` active. RenoDX upgrades the source swapchain to FP16 scRGB;
TGFG presents format 24 / color space 12 (HDR10 PQ). Managed capture reports
`R16G16B16A16_SFloat, Unity HDR=False, flags=1`. Kandra history guard remains active.

The immediate failure is **not yet a color-difference verification failure**: every eligible
HUD-less capture is rejected with `-4` before conversion/comparison. Logs show
`captured=0`, `supplied=0`, `checks=0`, and, for example, `rejected=361(last -4)` while
all 361 depth/motion pairs and FSR submissions succeed. `hudless_frame()` returns -4
when `resource->GetDevice()` has a different COM identity from the bridge's device.
ReShade's proxy device has its own identity even when it represents that same device.

The HUD capture now falls back to the resource device's immediate context and the existing
`same_context()` verification already used for NGX depth/motion capture. It accepts either
the real context's COM identity or the bridge's private-data owner tag forwarded by a
wrapper. Null/unrecognized contexts and unrelated devices remain rejected. No pixel
verification threshold, HDR encoding or capture stage was changed. This correction applies
to the shared HUD-less input for FSR, DLSS-G and XeSS.

Native build and all five native tests pass (including WARP color conversion and readback).
These tests do not execute ReShade's device proxy. Next game validation must confirm
HUD capture rejection -4 disappears, followed by successful image verification and
increasing HUD-less supply. RenoDX may expose a further color/timing mismatch after this
device check is fixed; do not claim visual compatibility from the build result alone.
Installed into Mono; installed file/version/hash verification and 33 source/integration
checks passed. Backup: `BepInEx/TGFrameGeneration/Backups/20260920-141551-568-4365e62c-Mono`.
Pre-fix native log tail saved to ignored `build/reshade-hudless-rejection.log`.

## History guard confirmed active; hand vectors corrected (2026-09-20)

Reporter after the history-guard build: "Seems you fixed it! Hands are way better now."
The latest run (PID 46828) logs guard installation and duplicate-copy counts 1 at Unity
frame 842, 417 at 1258, 1018 at 1859, and 1733 at 2574: one duplicate prevented per frame
through those intervals. This confirms the repeated-history-copy behavior in this setup.

Capture `fsr-inputs-46828-242776328` is copied into `build/`; analysis is
`build/analyze-motion-history.py`, using the same masks and reprojection calculation as
the prior captures. During a fast vertical camera turn:

| Measurement | Second vs first capture frame | Third vs second capture frame |
|---|---:|---:|
| Median hand MV, render pixels (X, Y) | (0.000, -0.180) | (-0.008, 0.000) |
| Camera-only hand motion, render pixels (X, Y) | (4.38, -60.25) | (-2.10, -51.73) |
| Hand error, same screen position | 0.00808 | 0.00856 |
| Hand error, supplied motion warp | 0.00800 | 0.00883 |
| Background error, same position | 0.04818 | 0.04565 |
| Background error, supplied motion warp | 0.01138 | 0.01096 |

The hands now have near-stationary screen-space vectors despite substantial camera motion,
consistent with their actual movement, while world reprojection remains useful. The large
erroneous hand warp seen before is gone. These are source inputs, not generated output;
the reporter's visual improvement provides the complementary presentation evidence.

AvalonAtlas is an optional minimap mod, not a TGFG dependency. The guard references only
the game's Kandra skinning manager; it neither loads nor calls AvalonAtlas. Without extra
camera contexts, the first and only history copy each frame passes through normally.
The Harmony regression now explicitly checks eight consecutive single-camera frames for
each of FSR, DLSS and XeSS, and passes along with the existing scenarios. No production
code or installed files changed for this check. A game run with AvalonAtlas removed has
not been performed. HDR, IL2CPP, transparent spell effects and broader scene/backend
comparisons remain separate validation work; do not claim those are covered by this run.

## Material passes alone insufficient; preserve Kandra history across contexts (2026-09-20)

Latest capture: PID 46480, `fsr-inputs-46480-241961687`, frames 1442-1444, copied into
`build/`. The managed log confirms HDRP camera/object motion are both enabled and the
material correction enabled all three active opaque hand/body materials:
`Mat_Male_Body_Base_Hero(Clone)` (RealSkin), `Mat_CHGenL_01_Arms` and
`Mat_ChMgenM_07_Torso_AgedMetalVest_02` (Kandra_Lit). HUD-less delivery remains verified.
Despite this, hand vectors still follow camera motion. `build/analyze-motion-fixed.py` finds:

| Measurement | 1443 vs 1442 | 1444 vs 1443 |
|---|---:|---:|
| Hand error, same screen position | 0.00767 | 0.00773 |
| Hand error, supplied motion warp | 0.05360 | 0.02692 |
| Median hand vector distance from camera-only reconstruction, render pixels | 0.032 | 0.156 |

Do not compare absolute error reduction with the earlier capture as proof of improvement:
the camera turned more slowly. The supplied vectors still make hand reprojection worse.
Background reprojection improves: for 1443 vs 1442, error falls from 0.01918 at the same
screen position to 0.00709 with the supplied motion warp.

Read-only DXBC disassembly of the shipped Kandra_Lit MotionVectors vertex variants 64-67
confirms the DOTS + KANDRA_SKINNING path loads `_GlobalPreviousPositions` (stride 12),
as well as current skinned vertices (stride 20), and projects the old positions through the
previous transform/view-projection. It does **not** require enabling `_ADD_PRECOMPUTED_VELOCITY`.
Extraction/disassembly is in ignored `build/disasm-motion.py`, `build/motion-vs-*.asm`.

Mono.Cecil inspection establishes a possible history overwrite:

- `KandraRendererManager` subscribes to `RenderPipelineManager.beginContextRendering`.
  Every context calls `SkinningManager.RunCopyPrevious`, then runs skinning again, without
  a Unity frame-count guard. Bone matrices are collected in PreLateUpdate.
- Installed AvalonAtlas's `DungeonCamSource.UpdateFrame` calls `_cam.Render()` for the
  minimap. Its Kandra hiding changes/restores layers and refreshes filter settings; it
  does not unregister the hand rig. The TGFG log sees `AvalonAtlasDungeonCam`.
- An extra rendering context can therefore replace the prior frame's world-space vertices
  with the current frame's vertices before the main camera's motion pass. This would leave
  camera-only motion even though the shader correctly reads the previous-position buffer.
  Runtime duplicate-copy counts and another input capture are still required to confirm it.

`KandraMotionHistory.cs` now patches only `RunCopyPrevious(CommandBuffer)` on **Mono**.
While any FG backend is selected, each skinning manager records one history copy per
`Time.frameCount`. Further copies in that frame are skipped, but all skinning updates
continue. Different managers remain independent. Off permits the original behavior; stop
unpatches the hook. A failed original copy retains its exception and allows another attempt.
The guard logs installation and duplicate copies prevented (first occurrence, then at most
once every ten seconds). It is not enabled for the uninspected IL2CPP implementation.
This necessarily preserves history for all characters sharing the manager, not just hands.

The production Harmony hook passes a standalone net472 regression harness using the installed
Harmony runtime and a stand-in skinning manager: all three backends, two cameras, two managers,
frame gaps, Off/on, copy failure/retry, unpatch and reinstall. This does not validate Unity's
GPU scheduling. Mono build has zero warnings/errors; 33 source/integration checks pass.
Native code is unchanged. See `docs/TESTING.md` for the harness command.
Installed into Mono; installed file/version/hash checks passed. Backup:
`BepInEx/TGFrameGeneration/Backups/20260920-135845-760-a80b20f9-Mono`.
Persistent capture configuration remains F6 / debug flags 2.

Next game validation: restart through Steam, turn with armored hands visible in the same
interior/minimap scene, press F6 once during the turn, and continue moving briefly. Check
`FG Kandra motion history: prevented ...` before interpreting the new hand vectors. Neither
the suspected minimap interaction nor the remaining visual hand fix is confirmed yet.

## Hand capture: camera-only motion on first-person geometry (2026-09-20)

F6 worked in run PID 44696. `fsr-inputs-44696-240251046` contains source frames 1258-1260,
with both armored hands and spells visible during a camera turn. A copy is in `build/`, with
previews from `build/inspect-capture-latest.py`. `build/analyze-motion.py` reads all three
raw inputs and measures previous-frame reprojection. This is now direct evidence, not a
guess from the presentation log:

| Measurement | 1259 vs 1258 | 1260 vs 1259 |
|---|---:|---:|
| Median hand MV X, render pixels | +42.04 | +42.04 |
| Hand error, same screen position | 0.00956 | 0.01069 |
| Hand error, supplied motion warp | 0.10617 | 0.10531 |
| Background error, same position | 0.02970 | 0.02964 |
| Background error, supplied motion warp | 0.01174 | 0.01161 |
| Median hand vector distance from camera-only reconstruction, render pixels | 0.095 | 0.086 |

Errors are mean absolute RGB differences in normalized encoded SDR color, resized to the
1896x1068 input resolution. The hand mask is reversed depth >0.02 (near <0.5m), eroded by
13x13 pixels; the background mask uses depth <0.008, eroded and cropped away from image edges.
This mask is an analysis choice for this capture, **not a runtime depth heuristic**. Both
frame comparisons favor stationary hands by about tenfold, while the world favors the supplied
vectors. The raw vector scale/sign is therefore supported for the world. Hand vectors instead
track camera-only motion, about 85 display pixels sideways, despite their nearly stationary
screen position. Depth silhouettes align with color, and HUD-less delivery remains verified.

Read-only game asset inspection with UnityPy + generated Mono type trees found
`resources.assets` material 17, `Mat_Male_Body_Rough_Normal`, shader `TG/Character/RealSkin`,
explicitly disables `MOTIONVECTORS`. Several other character materials do too. The shader has
a MotionVectors pass and compiled player variants (36 vertex, 84 fragment); these are in
`m_PlayerSubPrograms`, not the empty legacy `m_SubPrograms` list. Kandra's draw job sets
HasMotion (2) and Object motion mode (1), and copies previous skinned positions before skinning.
The pipeline asset supports motion vectors. This points to disabled material passes as a
concrete cause to address; the actual active armor material is to be confirmed in runtime logs.

`FirstPersonMotion.cs` now scans only the hero's `fppParent` Kandra rig while any FG backend is selected.
For opaque materials with an existing motion pass, it enables that pass if disabled and logs
the renderer/material/shader. It uses Kandra's actual RenderingMaterials, including instances,
and restores the prior disabled state when equipment is retired, the hero unloads, FG is
switched Off, or TGFG stops. Shared materials may also affect other renderers using that same
material during FG. No near-depth mask, zero-vector override, material replacement, shader
keyword change or game asset write is used. Transparent spell effects remain a separate case.

Next validation: test the hands with FSR and press F6 during a turn to capture corrected inputs.
Confirm `FG first-person motion pass enabled` names the active hand materials, then compare
hand vectors and image residuals again. A successful build alone does not establish the visual fix.

Material-pass correction built and installed into Mono; installed versions/hashes verified.
33 source/integration checks pass. Native code was unchanged from the five-test capture build.
Backup: `BepInEx/TGFrameGeneration/Backups/20260920-133118-697-b366d981-Mono`.
F6 remains the manual capture key, persistent debug flags 2. Added a log of HDRP's effective
camera/object motion flags so a disabled frame-level pass is distinguishable from material state.

The reporter subsequently requested the best available motion inputs for DLSS and XeSS too.
The correction now applies to all three FG backends, and remains enabled across backend switches;
Off still restores the material state. HDRP motion diagnostics use the generic `FG` prefix.
This change does not alter each SDK's coordinate convention or broaden the FSR-only raw capture.
Built and installed into Mono; 33 source/integration checks and installed hashes passed.
Backup: `BepInEx/TGFrameGeneration/Backups/20260920-133506-374-d466dade-Mono`.
Visual and corrected-input validation of the material-pass fix remains pending.

References: [Unity material pass controls](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/Material.SetShaderPassEnabled.html),
[BRG motion requirements](https://docs.unity3d.com/cn/6000.0/ScriptReference/Rendering.BatchFilterSettings-motionMode.html).

## Follow-up: HUD confirmed fixed; hands still flicker (2026-09-20)

Latest reporter result after the orientation fix: **much improved, but hands still flicker
when moving the camera**. The latest run confirms the new Y-flipped inputs and stable verified
HUD-less delivery in SDR. Do not treat the remaining hand flicker as resolved.

The game uses Awaken.Kandra's GPU skinning renderer. Metadata/IL inspection shows its batch
filter enables object motion and its skinning manager maintains previous positions; there is
no established missing motion-vector flag to patch. Next evidence: advanced `FSR debug flags`
bit 32 captures three consecutive eligible moving-gameplay source frames once per arming.
It waits for verified HUD separation and active FSR, then queues color, depth and motion copies
before doing readback or disk IO. Files are row-packed little-endian GPU data plus JSON layouts,
normalized motion scales, jitter and camera metadata in `BepInEx/TGFrameGeneration/diagnostics/
fsr-inputs-<pid>-<tick>`. The three-frame limit is 384 MiB; saving can briefly hitch. This records
scene images, not generated output. It changes no FSR inputs or interpolation settings.
Compare hand boundaries and their previous-frame reprojection against the world before changing
another rendering parameter. The GPU readback test checks 315 values across three distinct
frames, including row-padding removal and stopping/rearming. Five native tests and 33 Python
checks pass.

Diagnostic build installed into Mono; installed hashes verified. Game config debug flags changed
from 2 to 34 to arm the capture. Restore to 2 after collecting it (the original config is saved
at `build/fsr-before-input-capture.cfg`). Deployment backup:
`BepInEx/TGFrameGeneration/Backups/20260920-124657-476-af3ac0ec-Mono`.
Awaiting a run with FSR, hands visible and camera movement; inspect the capture locally afterward.

First diagnostic run (PID 30180) produced **no capture**: Session.Tick still clamped debug flags
to 31, turning the configured 34 into every SDK debug overlay/checker and dropping bit 32.
Removed that redundant clamp; ConfigEntry already validates 0-63. Inspected the compiled Mono
DLL IL to confirm the ConfigEntry value reaches NativeBridge.SetDebugFlags unchanged. Rebuilt,
33 Python checks passed, installed hashes verified. Config remains 34. Corrected deployment
backup: `BepInEx/TGFrameGeneration/Backups/20260920-125241-336-1bac294c-Mono`.
PID 30180 did confirm continued verified HUD separation (383/383, 392/392, 401/401 supplied),
but supplies no evidence about hand depth/motion. Another capture run is required.

PID 40592 successfully saved `fsr-inputs-40592-238757734`, frames 1087-1089. Copies are also
in the workspace's ignored `build/` directory, with color/depth/motion previews generated by
`build/inspect-capture.py`. These show a room and seated NPC before first-person hands appear;
depth silhouettes align with presentation color, but the largest motion-vector component is
only 0.00004232 (under 0.1 display pixel vertically). The matrix threshold triggered on tiny
camera bob at the first verified HUD-less frame, not the later camera turn. This cannot establish
correct hand vectors, their signs during a turn, or the cause of the remaining flicker.

Added an optional `FSR input capture key` (default None). For this investigation it is F10, and
the persistent debug flags return to 2, disabling automatic startup capture. Press F10 while
turning with hands visible: bit 32 is cleared across presents and rearmed after 0.3 seconds.
Repeat presses take another three-frame set; no rendering parameters change. Next required
evidence is a manually triggered capture during visible hand flicker.

PID 47668 produced no capture request or new input files. The reporter pressed F10 and opened
FoA Mod Manager. Installed configs confirm F10 is also bound to Mod Manager's OpenHotkey and
AvalonAtlas IconDump (F9/F11 are also bound by Atlas). Changed only TGFG's capture binding to
**F6**, with debug flags still 2. Checked all installed BepInEx `.cfg` bindings: no other F6
assignment found. The arming helper now checks for that conflict before writing. Next run:
with FSR active and hands visible, press F6 during a camera turn, continue for several seconds,
then exit. There is still no capture of the hands, so no hand-motion fix is established.

The reporter confirms the HUD flicker is gone with the current-color capture. Native run
PID 34672 confirms it: `HUD-less yes`, `verified`, and supplied counts equal captured counts
through sustained camera movement (for example 354/354 and 350/350). Differences are about
2-6% of the sample points in gameplay. Source/output format is 28 (SDR); reported display peak
is 1037.51 nits. First-person hands still flicker with FSR when the camera moves.

Source review found a coordinate mismatch in the remaining FSR inputs. HDRP uses
`GL.GetGPUProjectionMatrix(..., true)` for rendering into RTs, then Y-flips scene color in its
final blit. Our HUD-less color applies that flip and is now verified against Present, but our
depth/motion copy preserved the original RT rows. The NGX motion scales describe the upscaler's
pre-final-blit space, not presentation space. The earlier comparison to FSR3Unity was an
upscaler comparison and did not establish correct post-Present FG input orientation.

The follow-up corrects FSR input rows within the valid depth/motion subrect and reverses the
vertical motion scale and jitter. Its input log should now show `(-1896, +1068)` and
`inputs Y-flipped to presentation`. DLSS-G and XeSS retain their existing inputs and projection
matrices; FSR 3.1 instead reconstructs from scalar FOV/near/far parameters. Frames captured for
a different backend orientation are withheld if the selection changes before Present.

Validation: four native tests and 33 Python checks pass. WARP now checks 96 color channels plus
120 depth/motion values using original/flipped rows, an offset viewport, signed FP16 motion and
dispatch bounds. This proves the conversion code; the reporter subsequently confirmed improvement
but not resolution of hand flicker, as recorded above.
The follow-up is installed in Mono; installed hashes pass. The preceding HUD-fixed build is
backed up under `BepInEx/TGFrameGeneration/Backups/20260920-123314-344-3e59f8d2-Mono`.

Source references:
- [HDRP projection](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/Camera/HDCamera.cs)
- [HDRP final blit](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/HDRenderPipeline.RenderGraph.cs)
- [FSR input preparation](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/framegeneration/fsr3/internal/ffx_frameinterpolation.cpp)

## Previous follow-up: current-color capture (2026-09-20)

The reporter confirmed that **SDR also flickers**. The latest inspected native run uses
source/output format 28, FSR create flags 8, and still supplies no HUD-less image. HDR luminance
is therefore not a sufficient explanation; no speculative luminance override was added.

The old `endCameraRendering` swapchain copy and `rendersUIOverlay` override have been replaced
with an `AfterPostProcess` HDRP custom pass. HDRP's `HDUtils.PostProcessIsFinalPass` explicitly
forces an intermediate RT when this pass exists. A new optional `TGFG_SetHudless` packet and
render event 4 copy that RT inside the graph, while it is alive. The conversion handles its valid
viewport, Y flip, sRGB read/write, and Rec709/Rec2020 HDR nits. It leaves the engine's UI path intact.
The present-time verification still gates delivery to all three frame-generation backends.

FSR now also resets history when switching between backbuffer and HUD-less inputs, and rebuilds
its context when the input flags or debug-checker flag change. These changes do not move lens
effects after FG: that needs effects rendered on the generated frames, rather than treating the
difference between pre-effects and final color as UI.

Validation: Mono build, native build, four native tests (including 96 D3D11 WARP comparisons of
the actual conversion shader), and 33 Python checks pass. Installed into Mono; installed version
and hash checks pass. Backup: `BepInEx/TGFrameGeneration/Backups/20260920-122047-306-fb17b6b1-Mono`.
HUD separation subsequently confirmed; see the follow-up above. HDR validation remains pending.
Next run: look for the managed `HUD-less HDRP color` layout line, native `HUD-less frame verified`,
FSR `HUD-less yes`, and increasing `supplied`, then compare moving HUD/hands in SDR and HDR.
The sections below preserve the preceding investigation; their description of the old capture
and UI override is historical.

## Symptom

With Frame Generation set to FSR, the HUD wobbles on any camera movement and first-person content
such as the player's hands flickers and jitters. DLSS Frame Generation and Intel XeSS-FG are stable
in the same scenes, on the same inputs, on the same machine. The reporter has used FSR frame
generation in other titles and says it does not normally look like this, so "FSR 3.1 is analytic
rather than machine learned" does not explain it.

## Environment

Verify against the native log at `BepInEx/TGFrameGeneration/Native/TGFG.Native.log` rather than
assuming, because several of these were wrong earlier in the investigation.

| Fact | Value | Where it shows |
|---|---|---|
| GPU | NVIDIA RTX 5080 | `GPU vendor 0x10DE` on the bridge ready line |
| Display | 4K at 240 Hz | reporter; new log line prints the luminance range |
| Game branch | Mono, BepInEx 5 | `Runtime target` in a diagnostic report |
| Upscaling | DLSS Performance, 1896x1068 into 3840x2160 | `AMD FSR inputs:` line |
| Source swapchain | FP16 scRGB | `source format=10` on the bridge ready line |
| Output swapchain | R10G10B10A2, PQ | `D3D12 format=24`, colour space 12 |
| ReShade and RenoDX | installed but **disabled** | reporter, confirmed 2026-09-20 |

The FP16 source swapchain is therefore the game's own HDR, not RenoDX. An earlier round of this
investigation assumed RenoDX was active and drew a wrong conclusion from it.

## Eliminated

Empirically, by the reporter, each with no change to the flicker:

- Film grain off, and separately vignette, chromatic aberration and motion blur off.
- A much higher base framerate.
- NVIDIA Reflex off.
- VSync on and off.

Together those remove frame pacing, latency technology and per-frame noise as causes. The reporter
also asked whether lens effects ought to run after frame generation. There is no blanket rule
that native integrations move all such effects after FG. Intel explicitly requests HUD-less
color with all post-processing applied. NVIDIA recommends reducing motion blur with DLSS-G;
AMD supports rendering film grain in the per-present callback after interpolation. TGFG's
current color capture remains AfterPostProcess, before overlay UI; moving film grain or other
effects after FG would require a separate presentation-stage implementation. The reporter's
effect-off tests did not eliminate the original flicker, subsequently traced to HUD capture,
FSR input orientation and repeated Kandra history copies.

References: [Intel XeSS-FG guide](https://www.intel.com/content/www/us/en/developer/articles/technical/xess-fg-developer-guide.html),
[NVIDIA DLSS-G guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md),
[AMD FSR UI composition](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-interpolation/#ui-composition).

By reading the FSR 3.1.6 frame interpolation source:

- Motion vector scale, jitter, near and far planes, the inverted-depth and low-resolution motion
  vector flags, and the render and display sizes all match both the NGX values the game gives DLSS
  and the reference Unity integration (`ndepoel/FSR3Unity`).
- `cameraPosition`, `cameraUp`, `cameraRight` and `cameraForward` are not used by FI 3.1.6 at all.
- Near and far order does not matter: FSR takes the minimum and maximum itself, then swaps them for
  inverted depth.
- `maxRenderSize` only drives allocation and a `RenderSize()/GetMaxRenderSize()` UV scale, which is
  self-consistent for any value at or above the render size. The old wrong value was wasteful rather
  than necessarily harmful. It was corrected anyway.
- Debug flag 2 produced no reset indicators in game, and the shader only paints them when `Reset()`
  is true, so FSR is not resetting frames.

## Changed this session

All deployed, none committed. `git diff` shows the full set.

1. **HUD-less frame for all three technologies.** HDRP 17 draws the screen-space overlay UI inside
   the last camera's render graph, so `endCameraRendering` normally comes after the HUD. TGFG clears
   `SupportedRenderingFeatures.active.rendersUIOverlay` each frame from the gameplay camera's begin
   callback (`src/TGFrameGeneration/HudlessUi.cs`), which makes the engine draw the overlay UI after
   the pipeline instead. Render event 2 copies the back buffer into the frame's shared texture, and
   it is fed to DLSS-G as `kBufferTypeHUDLessColor`, to FSR as `HUDLessColor` in the per-frame
   configure, and to XeSS as a HUD-less tag with UI composition switched on.
2. **Present-time verification** of that capture, `TGFG_HUDCHECK` in `native/shaders/interop.hlsl`.
   It counts, on a one-in-four pixel grid, how many sample points differ between the presented frame
   and the capture, between the capture and the previously presented frame, and between the
   presented frame and the previously presented one. One 16-byte readback through a three-deep
   staging ring with `D3D11_MAP_FLAG_DO_NOT_WAIT`. Thirty consecutive good results verify the
   capture, thirty bad ones withhold it, and still frames are skipped.
3. **FSR max render size** now follows the resolution the game actually renders at. The context is
   rebuilt on the first frame with inputs and whenever the render resolution changes.
4. **Diagnostics.** An `AMD FSR inputs:` log line; an advanced setting `FSR debug flags` wired to
   `TGFG_SetDebugFlags` (1 tear lines, 2 reset indicators, 4 debug view, 8 pacing lines, 16 FSR's own
   input checker); and logging of the presentation colour space, the display's luminance range and
   any HDR metadata the game sets.
5. **Low Latency may be Off with FSR.** DLSS-G still requires Reflex and XeSS-FG still requires XeLL,
   which are their vendors' rules. FSR requires neither.
6. **DLSS-G interface recomposition** now follows the verified HUD-less image. It was hard-coded off,
   which meant the HUD-less tag had nothing to act on.

## Lead A: the HUD-less capture is one frame late

The HUD-less image has never reached any technology. Every pipeline line in every run logs
`supplied=0`, and the verification withholds it with the stale message.

What the verification measured:

| Situation | Capture differs from the presented frame |
|---|---|
| Standing still | 0.0% |
| Moving | 58.9% |

A correct capture would sit steady at whatever share of the screen the HUD covers, roughly 5 to 15%,
regardless of movement. Swinging between nothing and most of the screen is the signature of a copy
that is one frame late: standing still, the previous frame is identical; moving, it differs
everywhere. The log states the same conclusion directly, that the capture equals the previously
presented frame.

ReShade is disabled, so a swapchain proxy is not the cause. The remaining explanation is that Unity
writes our swapchain buffer late, through an intermediate blit at the end of the frame, so at
`endCameraRendering` our buffer still holds the previous frame.

Fix direction: take the HUD-less image from HDRP's own post-processed colour rather than from the
back buffer, through a custom pass at `CustomPassInjectionPoint.AfterPostProcess`, the same way
depth and motion vectors already come from HDRP in `src/TGFrameGeneration/HdrpInputs.cs`. In
`HDRenderPipeline.RenderGraph.cs` that injection point runs on `postProcessDest`, which is exactly
what `BlitFinalCameraTexture` then writes to the back buffer, and it precedes
`RenderScreenSpaceOverlayUI`. Points to handle: the render target handle may be larger than the
viewport, so the copy needs a rectangle like the depth and motion vector path already uses; and the
camera colour format will not always match the client swapchain format, where the existing FP16 to
PQ encode covers the HDR case and the SDR case needs thought.

Note that if the capture is taken from HDRP's colour, the UI is not in that buffer no matter who
draws it, so the `rendersUIOverlay` override becomes unnecessary and could be dropped.

## Lead B: HDR luminance, the only colour input unique to FSR

FSR converts the presented frame back to linear light before it does anything with it. From
`ffx_frameinterpolation_common.h`:

```hlsl
case FFX_BACKBUFFER_TRANSFER_FUNCTION_PQ:
    fLinearRgb = ffxLinearFromPQ(fRawRgb) * (10000.0f / MaxLuminance());
```

`MaxLuminance` comes from the game's HDR metadata when it sets any, and otherwise from
`getMonitorLuminanceRange` on the swapchain's containing output. Everything FSR decides afterwards
runs on those values, including `fGameMvBias`, where it compares its own optical flow reconstruction
against the one built from the game's motion vectors and picks the better match per pixel. A wrong
scale puts every threshold in that comparison on the wrong part of the perceived-luminance curve,
which would make the per-pixel choice between two different warps unstable. That reads as flicker,
it is worst on moving high-contrast content, and it is FSR-only because neither DLSS-G nor XeSS is
given a luminance range.

The logging for this is deployed but **has not been read from a run yet**. That is the single
cheapest next action.

## Also worth checking

- The swapchain is FP16 with ReShade disabled, so the game is in HDR, yet the
  `HDROutputSettings.main.active` check in `HudlessUi` returned false, since the capture turned on.
  Establish what Unity actually reports. If Unity's HDR output is active, HDRP renders the UI into a
  separate offscreen buffer with alpha (`RenderTransparentUI`, composited in `BlitFinalCameraTexture`).
  That buffer would be the ideal UI texture for all three SDKs, better than a HUD-less image alone,
  and the `rendersUIOverlay` override may be suppressing that path.
- If HDR is eliminated: the bridge holds the global state mutex across the whole of `present()`, and
  FSR deliberately blocks inside Present to pace frames, which couples the simulation thread to FSR's
  pacing in a way neither other technology triggers. Measure frame intervals before changing it.

## Reference material

The FSR frame interpolation sources are not vendored in this repository. The shipped runtime is
AMD's signed DLL and the SDK folder holds headers only. Re-fetch the implementation from
`GPUOpen-LibrariesAndSDKs/FidelityFX-SDK`, branch `main`, under
`Kits/FidelityFX/framegeneration/fsr3`. The files that mattered were
`internal/ffx_frameinterpolation.cpp`, `include/gpu/frameinterpolation/ffx_frameinterpolation.h`,
`ffx_frameinterpolation_common.h`, `ffx_frameinterpolation_callbacks_hlsl.h`,
`ffx_frameinterpolation_inpainting.h` and `dx12/FrameInterpolationSwapchainDX12.cpp`. The HDRP
sources came from `Unity-Technologies/Graphics`, branch `6000.0/staging`, under
`Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline`.

## Reproducing the build

```powershell
.\Build-Native.ps1 -StreamlineRoot Streamline-2.14.1 `
  -NgxIncludeDir Streamline-2.14.1\external\ngx-sdk\include -Generator "NMake Makefiles"
.\Build.ps1 -Runtime Mono -Deploy      # game must be closed
python -m unittest tests.source_checks tests.test_autoload_integration
```

The native build needs a Visual Studio x64 developer environment and the CMake on `PATH`.

## Reading the log

The `Pipeline 5s:` line carries a `hudless` group with captures, rejections, frames supplied to a
technology, checks performed, still frames skipped, the last differing fraction, and whether the
capture is verified. `supplied=0` means no technology has received a HUD-less image. The
`AMD FSR inputs:` line prints once when generation starts and again whenever the render resolution
or the HUD-less state changes. With `FSR debug flags = 16`, FSR's own input checker writes its
complaints into the same log through the message callback that is already installed.
