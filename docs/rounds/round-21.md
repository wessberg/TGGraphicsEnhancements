# Round 21 - 2026-10-30 - the dependency updates and the game's move to Unity 6000.3

Started from the owner's list: DLSS Frame Generation Preset C, FSR 4.1 on RDNA 2, the new DLSS-NR, new game updates;
"update these dependencies, integrate them as best as possible, and consider if we need to change our inputs to them".
Mid-way: "Nevermind the path tracing mod right now - that mod is tabled until further notice, it's the three other mods
we want to get updates out for right now". Asked about Preset C: a row that only appears when Frame Generation is DLSS.

## What changed outside

| Thing | Before | Now | Where |
|---|---|---|---|
| Game | Unity 6000.0.64f1 | 1.31.004, Unity 6000.3.21f1, Steam build 26298687 (installed 10-30 11:38) | 249 of 286 managed DLLs new |
| DLSS (SR, RR, FG DLLs) | 310.9.1 | 310.9.16 | `DLSS-SDK-310.9.16` |
| Streamline | 2.14.1 | 2.14.16 | `Streamline-2.14.16`, `-bin` |
| FSR SDK | 2.3.0 | 2.4.0: upscaler 4.1.2, FG 4.0.2, Ray Regeneration 1.2.1 | `FidelityFX-SDK-2.4.0` (`bin` = `Kits/FidelityFX/signedbin`) |
| DLSS-NR | 310.8.0.0, 165 MB | 310.9.16.0, 254 MB, NVIDIA-signed, inside NVIDIA's public RTX Remix 1.6.1 x64 zip | `DLSS-NR-Runtime\nvngx_dlssnr-310.9.16.dll` |

The old SDK folders stay beside the new ones. RTX Remix's source (github.com/NVIDIAGameWorks/dxvk-remix, MIT) is the
only public example of NVIDIA's own use of Preset C and of DLSS-NR: `rtx_dlfg.cpp`, `rtx_ngx_wrapper.cpp`,
`rtx_dlss_neural_rendering.cpp`.

## What broke on Unity 6000.3, and what a compile did not show

- FSR and XeSS upscaling fell back to STP in silence, and NR recorded nothing: `CustomPass.ExecuteInternal` takes a
  `ScriptableRenderContext` now (TargetParameterCountException). Fixed in `Shared/CustomPassRecorder.cs`, Mono and IL2CPP.
  Every released 1.2.3 player on this game build has it. **The compile passed; only the run showed it.**
- NGX's optimal-settings query answers 33.3 % for DLAA: `DlssModes.cs` holds DLAA at 100 %.
- NR refused the new DLL (a version gate): 310.9.16.0 and 310.8.0.0 are the known profiles now.
- TG Path Tracing does not compile: `SetupBeforeExecute` takes `IUnsafeRenderGraphBuilder` (changed), and Unity's
  assemblies want netstandard 2.1 (CS1705), where referencing the game's facade loses `ReadOnlySpan` under net472. Tabled:
  `Build.ps1 -Skip TGPathTracing`. Its old 1.2.3 plugin still loads with Mode Off. A proper fix compiles the Mono
  projects against the game's own mscorlib.
- The harness's plugin compiles again (netstandard reference, `InFrustum` by hand, `SceneHandle`). NVIDIA Overlay's memory
  counter (28 GB on a 16 GB card, 0 % of the engines) held every game start back: its memory no longer counts.

## The owner's questions

**Preset C.** It is one create-time NGX parameter, `DLSSG.LiteCandidateBlendingEnabled`. Streamline has no option for
it. `control.cpp` hooks `_nvngx.dll`'s D3D12 CreateFeature, only once a preset was asked for, and turns DLSS-G off for a
frame so the feature is made again. The row: Frame Generation Quality, Quality (default, NVIDIA's own default) or
Performance. Run `20261030-124931`, 4K, DLSS Performance, 2x, in front, stream on, A/B/A/B/A: Quality 73.2, 73.5, 73.1;
Performance 76.0, 76.1 rendered fps; 13.65 against 13.13 ms. The picture of generated frames has not been judged: the
harness's frames are real frames. Reports elsewhere: HUD ghosting under about 40 fps base.

**FSR 4.1 on the 9950X3D's Radeon.** No. `fsrprobe` (a scratch program) on device 13C0, driver of 2026-04-17, SDK 2.4.0:
offered 3.1.6 and 2.3.4; 3.1.6 ran, 10.7 ms for 720p to 1080p. AMD's list has RX 6000, Ryzen AI Max and some Ryzen AI / Z2 chips, not this
one. A newer Adrenalin driver was not tried. FSR 4 is therefore still unconfirmed in our hands: it needs a card that has it.

**DLSS-NR.** The fast network is asked for by name: `DLSSNR.Hint.Render.Preset` = B (2), as RTX Remix does ("Preset B
selects the lightweight network independently of the appearance model"). A/B against the runtime's default did not
happen: the hidden switch for it stopped NR evaluating (cause not looked for) and was removed. 6.4 ms of NR's GPU time
at 4K here; 11.3 to 11.6 ms measured on 310.8 in `docs/DLSS5-VALIDATION.md` (another scene). 80.7 fps off, 41.9 on in this scene.
NR's inputs were left as they are: the DLL adds one parameter, `DLSSNR.MaskBlendMode`, for a control mask, and we send none.

**Our inputs to the rest.** No change needed: the SL DLSS-G and RR guides differ only in the version line, the NGX headers
only by the Lite parameter, FSR's only by a corrected comment on `motionVectorScale` (ours were right). `fsr_backend.cpp`
moved off the deprecated `ffxConfigureDescGlobalDebug1`.

## Measured in game (game in front, stream on, no denoiser or tracer involved)

Run `20261030-123852`: DLSS FG 79 to 147 fps on screen (74 rendered), FSR FG 133 (66), XeSS FG 127 (63). FSR 3.1.6 and
XeSS Balanced upscaling on the RTX 5080: `failed(total)=0`. **DLSS-G generates nothing behind another window**: 80 from
80 and the No FPS gain alert, no bug.

## Open

1. DONE, THE NORMAL VERSION (the owner installed it, Steam build 26288436, Unity 6000.3.21f1, BepInEx be.755). The Mono harness cannot run
   there, so `tools\harness\Run-Il2CppProbe.ps1` (plugin `tools\harness\Il2CppProbe`) loads a save with saving blocked, walks the technologies,
   and takes itself and the configs back out. Run `artifacts\il2cpp-probe\20261030-132946`, in front, stream on, no errors, no crash, no save
   changed: DLSS FG 138 fps on screen (69 rendered), FSR 128 (64), XeSS 124 (62); Quality 69.0, 69.1, Performance 71.4 rendered fps; FSR 3.1.6 upscaling
   929 frames, XeSS 858, 0 failed; NR 367 evaluations, 0 failed, 44.3 fps; the Video tab opened with the new row in place (`video-tab.png`).
   The 1.2.4 normal-version zips are in `artifacts/Nexus`.
2. FIXED ON THE NORMAL VERSION, MONO OWED (the owner: "Switching back and forth is not acceptable"). HDRP creates DLSS at its first frame's size and makes it anew only when the input grows
   (`DLSSPass.UpdateViewState`, Graphics repo `6000.3/staging`). The native side now compares the created width with the rendered one (`TGUP_GetDlssSize`); `StpHost.TickDlssSize` sets another
   quality level for one frame, so HDRP creates it at the real size, and puts the player's back, half a second after start. Run `artifacts\il2cpp-probe\20261030-134513`: as launched 103.8 fps (76.3 before),
   DLSS FG 91 rendered (69), Quality 91.1, 89.8 against Performance 95.6, NR 57 (44). Two crops of old and new looked equally sharp, with the weather moving: not a frozen comparison. The Mono zips do not have this fix: they need the game on Mono to build.
   Before the fix (the first DLSS feature is heavy; the normal version too: 76 fps at start, 101 to 103 after the upscaler is switched and back). At game start HDRP's first frame is 3840x2160, the feature is created for it, and the
   next frames render at 1924x1084 in it: 81 fps. Re-applying DLSS changes nothing; STP and back makes a new feature created at 1924x1084: 94 to 103 fps
   (run `20261030-125804`; 113 to 117 after FSR and XeSS in `20261030-124931`). Not fixed, and the picture was not compared. The old build's log shows the same 3840 creation,
   so probably not new.
3. DONE, the owner's look: "yes FG Performance looks fine too". Mono rebuilt with the DLSS fix: run `artifacts\harness\20261030-135529`, about 112 fps from launch with no jump after re-applying DLSS (about 80 before, the same scene), NR 6.4 ms.
   Tests: source checks 121, ctest's 16 tests without a GPU, the smoke tests `tgup_gpu_smoke`, `_unity_order`, `_fg_installed`: all pass. Left out as tabled: everything that needs TG Path Tracing. No RR run. Not run on this build: the community-patched NR DLL, any card but the RTX 5080 in game.
4. DONE, NR's DLL IS BUNDLED (the owner's pick). A second opinion the owner brought held up where my reading had missed it: NVIDIA's "permits modders to build and share mods using NVIDIA technologies/binaries" from Remix's GitHub (`docs/DLSS5-RESEARCH.md`). `Stage-Runtime.ps1 -NeuralRoot`, the mod's `Signed` table; the zips are 180 MB, the DLL's hash is NVIDIA's. A player with a patched DLL copies it back after each update. The notification of the licence's supplement: the owner's to send, not sent.

## ▶ Starter prompt (R22)

Read AGENTS.md's START HERE, BACKLOG's top ruling, then "Open" above. TG Path Tracing is tabled: do not work on it unless the
owner says so. The game folder is on Mono with 1.2.4 of TG Frame Generation, TG Better Upscaling, TG Neural Rendering. `artifacts/Nexus` holds the six 1.2.4 zips, ready for the owner's upload;
the released 1.2.3 ones are in `artifacts/Nexus-1.2.3-backup`. A code change to the plugins now needs both Steam branches again (a build for the normal version needs its `BepInEx\interop`). The owner's Starfield
session reopens Starfield every few minutes: the harness waits for a gap by itself. Next: answer what players report on 1.2.4.
