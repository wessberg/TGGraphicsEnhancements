# Round 17 - 2026-10-09 - NR community runtime adapter

The owner considers the existing Neural Rendering integration ready after SDR/HDR/ReShade validation and
asked to finish compatibility with the supplied community `nvngx_dlssnr.dll` for older RTX cards. A separate
Opus session continues PT in this shared tree. No PT source, shared bridge source or release archive was changed.
The NR-only IL2CPP deployment later in this round was explicitly requested by the owner (NR-35 below).

## Owner decisions

The owner approved the separate adapter after being told it needs a caller-name exception:
"Yes—contain it in a separate compatibility adapter". They then confirmed that selection must be automatic,
with only dropping the DLL and restarting. Finally they rejected a per-file allowlist because patches change
frequently. The final implementation has no pinned community hashes or file-version list. This supersedes
R16's decision to wait and the first R17 exact-file proposal. No new compatibility row or switch.

## Finding and change

Simply skipping our signature check cannot work: NGX also rejects the model's signature before loading it.
R16's `Available=0` was incorrectly interpreted as a loaded model rejecting RTX5080. Verbose driver logs and
a null module handle resolve that. A byte-identical direct probe succeeds when its caller filename includes
`nvngx.dll`; direct initialization with matching app ID/API but without that filename fails. The model can
render on5080 through this route. Exact evidence, public sources and the limits of the RenoDX comparison:
[DLSS5-COMPATIBILITY.md](../DLSS5-COMPATIBILITY.md).

Signed supported DLLs keep the public NVIDIA SDK route. Modified/unsigned NR DLLs select our separate
`TGNeuralRendering.nvngx.dll`, beside the main native DLL. It validates direct exports/API0x13, asks the model
for feature requirements, initializes using its reported identity and owns its public parameter object and
feature handles. The existing bridge device, NGX lock, rendering inputs, settings and colour path are shared.
There is no Windows trust hook, modified model/driver, renamed game, hardware spoof or second device.

SHA256 is recorded and checked again under a file handle that denies writing/replacement; it identifies the
selected file, never an approved release. Unknown future patches using the compatible interface can load.
An incompatible interface or a hardware refusal still disables NR. Signed-runtime version profiling remains
unchanged. Other NGX consumers keep their trust policy.

The package definition includes our helper and excludes the proprietary model. Native-manifest generation
can retain an older NR stage during a PT-only build; packaging new NR still requires its helper and hash.
No real zip was produced. New CPU tests cover selection, the direct ABI, parameters, file identity/locking
and feature ownership; a mock runtime demonstrates unlisted-file acceptance without running a GPU or model.

## Checks

Scratch root: `build/nr/compat-r17/`. VS2022 x64 Release /O2 /W4 compilation of both DLLs passes cleanly.
No managed ABI change, PT shader build or full PT suite. Final source102 and mocked package101 pass.

- `integrated-patched-final.log`: real main/helper rendering, live strength/styles, alpha, Off/restart and resize.
  Neutral sRGB error0 byte levels; HDR neutral error0 over0..981 scene units. Finite changed RGB.
- `integrated-signed-final.log`: the same checks pass with the helper deliberately absent.
- `coexist-patched-final.log`: RR32/NR30 on the shared bridge with FG/Streamline and PT modules loaded. RR keeps
  working through NR Off/restart. This does not test generated frames or PT geometry.
- `multipass-check-final.log`: 1/2/3/4 passes match independent chained single-pass calls exactly (maximum
  error0), with history, neutral SDR/HDR, alpha and Off checks. All three deliberate chaining faults detected.
- `runtime_tests.exe` passes automatic selection, supported direct API, file identity/locking, parameter ABI
  and private helper ABI guards. The helper interface is private ABI2; public TGNR ABI is unchanged.
- `adapter_test.exe` with `mock.dll` passes four CPU cases: compatible unlisted runtime, changed file,
  incompatible API and runtime hardware refusal. Feature creation/evaluation/release and stale-handle rejection
  use an inert fixture, not a GPU. `cpu-final.log`: all five deliberate faults detected (API, file sharing,
  support refusal, selected-file identity and handle release).
- Native manifest generation on the isolated real stage passes and records the helper hash. A separate legacy
  stage without the helper also passes, preserving PT-only builds; NR packaging still requires the helper.

The GPU logs above test the final policy, after the PT game closed at10:24. The shared GPU guard prevented
starting beside that game. A prior multipass attempt on a production DLL
failed for the absent test-only export; the proper fixture runner now stages the helper and uses `-GpuCheck`.
All final GPU checks and the three multipass mutants completed by10:26. No NR test or waiter remains.

The very first standalone direct-frame diagnostic terminated0xC0000409 after parameter population; an
instrumented rerun succeeded without a rendering fix. Later integrated GPU checks passed. Do not claim the
initial termination was diagnosed. Also do not claim RTX2000/3000/4000 support has been tested: only5080 is here.

## Build and install state

NR-37/38 follow-up (12:48 onward): owner says NR worked well, then opening Options from the in-game pause
menu crashed after repeated settings changes and successful earlier visits. No installed files changed or
GPU tests ran during that play session. Windows execute AV at0x18, PID45724. LibCpp2IL31.1 metadata mapping
and GameAssembly code identify Attribute.IsDefined -> World.MakeViewOfType -> SpawnViewFromPrefab ->
SettingsUtil.SpawnView -> VDependentOption.Update. A row's view type is invalid; no native NR failure after
9000 evaluations/9479 model calls. The dump omits the relevant heap, so it cannot identify the exact row.

A temporary NR-only IL2CPP probe logs real SettingsUtil.SpawnView calls, toggles the NR expandable rows,
and forces managed/native GC. Baseline PID50956 on the main menu reads a JProperty where a PrefOption is
required on its first Video visit, completes one close, then crashes reading another invalid row's ID.
The original SwitchRowsOption creates native OtherOptions arrays through temporary wrappers in its
constructor and on every row change. The injected option needs an explicit retained wrapper/GC handle.
Added private nativeRows ownership at both sites; no global GC/reflection workaround or renderer change.
This shared managed helper also serves the other mods' expandable rows.

NR-38: owner's request moves Local Tone under Advanced, first before Style. VideoRows and FoA tags/order
match; config keys/defaults/current values unchanged. README and Nexus BBCode draft updated. Both changes
built net6.0 and deployed13:07, all8 files hash-verified,73 other files preserved, official model retained.
Managed NR9F411A3C4AF12DEA81A3EC5DA70266938B533851565AB5C3E772DFC814A0C65E;
managed bridge2BE2F31EFD7B7010E0AE36C262C1A8F8E8738CE2E352E0049D5C3C7EA89E252C.
Native/helper/bridge hashes remain as above. Backup20261009-110726-609-60b9b461-IL2CPP. Source102 passes.

The repeat initially waited for Starfield, then launched only after it closed. Fixed main-menu PID36424
passed20 open/change/GC/close cycles, with40 Local Tone and40 Skin Detail row creations, no invalid row
or logged error. The owner correctly requires actual gameplay validation too: "from in-game, then open
the pause menu, change settings, go back to the game, and so on". Main-menu evidence is diagnostic only.
The follow-up probe loads the tavern QuickSave_3, blocks saves with the game blocker plus LoadSave.Save
prefix, checks real interactive play and NR evaluation counters, creates MenuUI and calls its own
OpenSettingUI, applies changed settings, closes Options and pause, then checks NR resumed without failures.
Styles0..3, passes1..4, both placements, Strength/Local Tone, Advanced and Automatic Mask are exercised.
First gameplay run PID45272 passed12 full cycles with active NR (styles0..3, passes1..4, both placements),
then its resume assertion failed on cycle12/13th visit. That assertion did not log enough to distinguish
missing frames from camera/readiness state; native NR logged no failure. That run is inconclusive.
Its cleanup then crashed in the scratch probe's write to AllSettingsUI.LastSettingsTab, a process-local
static string: be.755's generated setter passes the string pointer to il2cpp_field_static_set_value where
an address of the reference is required. Removed this unnecessary static-field write from the probe.
The owner says the game was ALWAYS minimized/unfocused; no focus change was established at the failure.
117 save archives unchanged, config hashes restored, probe removed before rebuilding. Corrected probe
logs all missing state/counters, waits up to15s for resume, resolves current config entries when restoring,
and makes no LastSettingsTab write. No new production-code change. Final PID30912 launched13:22 and
completed20 cycles13:27:08: PASS,65..196 new NR evaluations after each resume,8740 total, no NR failures,
no resume waits, no BepInEx errors. Owner says it works great. Screenshots of the world and Video menu
have been inspected: real tavern gameplay, Local Tone indented under Advanced.
Evidence: build/nr/il2cpp-r17/options-probe/{baseline.log,baseline-bepinex.log,fixed-main-menu.log,
fixed-main-menu-bepinex.log,main-menu-result.json,gameplay-final.log,gameplay-final-result.json,
gameplay-final-bepinex.log,gameplay-final-native.log,gameplay.png,gameplay-options.png}. Scratch probe
removed; configs hash-restored,117 save archives unchanged, game closed. No release zip.

NR-36 follow-up (12:36-12:47): owner reports startup crash, asks for the official model first, suspects missing
model/Mono leftovers and asks about interoperability. Installed patched model hash matched; native logs show
successful community initialization in both crashed sessions. Only NR/bridge loaded. ErrorLog instead gives an
access violation in Il2CppInterop's generated RenderPostProcess trampoline, converting the non-blittable
`in PrepassOutput` to an object. The installed be.755 HarmonySupport implementation confirms that conversion.

Fix: IL2CPP alone uses `Il2CppPostProcessHook.cs` via the existing runtime DetourProvider. Exact signature is
checked, the struct pointer stays opaque, every native argument/result is forwarded and the camera's temporary
upsampling schedule is restored in finally. Mono keeps its existing Harmony prefix/finalizer. No global
interop patch, shared device change, model/input change or workaround for an absent model. The helper's
GenerateTrampoline must be called before Apply; reading OriginalTrampoline alone does not prepare it.

At the owner's request selected official signed310.8 model E16BCF15..., backing up the patched copy. Seven
confirmed net472 Mono DLLs (FG, BU, PT and their bridge copies, PT harness) moved out of plugin scanning with
verified backups: `BepInEx/TGNeuralRendering/Backups/20261009-103924-050-MonoCleanup`. All other files retained.
Rebuilt net6.0 and deployed12:44; managed NR hash
`EF4FCFCAA47B1DBB460B2A5FB28AD850ECDA798D3BC180761E276DA16560D306`. Native/helper/bridge unchanged. Source102
passes. Standalone missing-model probe passes (state1, clear missing-file reason, original swapchain/bridge
working, zero evaluations), `build/nr/il2cpp-r17/missing-model.log`; no GPU worker remains.

Added `Run-PtHarness.ps1 -LaunchOnly` for either installed runtime: same GPU counters and five quiet seconds,
shared run lock during launch, no Mono harness plugin, configuration changes, save loading or automated input.
Started through this route12:45, PID45724. Official model selected/initialized and the new post-process hook
completed its first frame. At12:46:58 the game is responsive after117s; native log records4800 NR evaluations
using actual DLSS SR inputs. These prove execution, not a visual-quality verdict. Evidence:
`build/nr/il2cpp-r17/crash-before/` and `crash-fixed/`, deployment receipt in the parent folder. Game is LEFT
RUNNING for the owner. The official model stays selected. No commit/push or release zip.

Interoperability answer: same one-device bridge and SR/RR guides as before; normal-version FG/BU combinations
remain to test after standalone NR. PT remains Mono-only; existing PT-72 guide integration still needs work.

NR-35,12:22: the owner installed the normal game and BepInEx IL2CPP and explicitly requested NR deployment.
Confirmed GameAssembly/metadata and Doorstop's IL2CPP target; game closed. Built only NR and its bridge/preloader
dependencies for net6.0, using `--artifacts-path build/nr/il2cpp-r17/managed -p:TargetFrameworks=net6.0` to isolate
outputs from PT. The initial no-restore attempt found Mono-only bridge restore metadata; the isolated restore/build
passed, with three NuGet vulnerability-feed warnings and no compiler errors. Existing interop references date10-06;
actual game startup remains necessary to establish compatibility with the currently installed game.

`build/nr/il2cpp-r17/deploy.ps1` prepared and validated the eight NR payload files before installation through
`Install-TgPayload`, with backup and replacement of Vortex links. It reused the R17 tested native/helper and
shared native bridge, hash B339C546F80C71791CF13E0B269EE228AAF11464572B45577C6889A695BF8BBC,
from the isolated smoke install. It also installed the owner's supplied patched model (E67DEE20...). The model
stays outside the package manifest. No release zip or shared artifacts were written. Eight installed hashes
match,80 other mod/config/loader files are unchanged, source102 passes. Managed NR SHA256:
`3A037170F751E4D23C88925DEE2421A8559C6B2074D3A5E82D92B7D4F90860FA`.
Receipt: `build/nr/il2cpp-r17/deployment-result.json`. Replaced files were backed up in
`BepInEx/TGNeuralRendering/Backups/20261009-102251-372-922c717e-IL2CPP`. No game launched, no new rendering claim.
The live Steam folder is now IL2CPP; PT deployment/Mono harness must wait for the switch back.

The owner proposes a temporary switch to the normal game version for NR testing while PT compiles. Read-only
checks find active `dxc` shader compilers and no harness RUNNING marker. Native compilation does not depend on
the game branch, but `src/Game.References.props` reads Mono Managed assemblies and BepInEx core from the live
Steam folder configured in `Build.Local.props`. Later managed builds need preserved references or a hold;
PT deployments and game launches must also wait. The active jobs' possible post-build deployment steps were
not established. Back up before switching, including root loader files as well as BepInEx; current Vortex
symlinks require real file contents in the backup. Use a separate BepInEx 6 setup, then replace it with the
Mono backup after switching back. No job stopped, backup made, game branch changed or files deployed.

NR-34 follow-up: owner requested the Nexus description and README update. Both local drafts now explain
automatic official/community DLL selection, the bundled adapter, no per-file allowlist, replacing compatible
patches without a mod update, and creating the DLL folder if needed. README's old signed-only/SDR-only
summary was corrected. Community standalone5080 results remain distinct from pending in-game/older-RTX
validation. Nexus remains BBCode, wording reviewed for plain first-person player text. Nothing published.

NR-33 follow-up: the owner asks whether5080 can validate older cards. Read-only audit found no50-series
gate in our NR code; transfer shaderscs_5_0 and bridge feature11_0. NVIDIA's compatibility guide confirms
that GPU binary targets and PTX compilation depend on architecture; forcing PTX targets the GPU present,
not a simulated older one. Identity spoofing would not establish older-GPU correctness. The existing standalone
probe can run on actual older cards without FoA. No model inspection, new GPU run or code change for this
question. Findings and primary source recorded in DLSS5-COMPATIBILITY; source checks run before handoff.

Scratch main DLL SHA256: `86182BD7B283881AC440D2E750C4539230D6C32151844E182FED4A2AE2986799`.
Scratch helper SHA256: `19A9D75FFFF9072961B86FA9EF452F49A9CEB401EDAB59F4AED9E004BA8145C5`.
Their copies in the isolated patched-runtime install and NR-35 game deployment match. No release zip, commit or push.
The supplied model is unchanged: `E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A`.

NR-39/40/41 follow-up13:36-13:40: built normal managed FG/BU/NR in isolated outputs and only the
TGFG.Native/TGBetterUpscaling.Native targets in build/nr (no PT build/shared artifacts replacement).
Deployed35 FG/BU files and8 NR files, manifest/hash checks, same managed bridge2BE2F31EFD7B/nativeB339.
FG native83947A2AC8F1615E70AA668A619855989090A46E81E820CC421FDDA0143CB73A;
BU nativeC75EC35401897B023FD06D8060BA672216CD726CD0450BFF683CEDEB59FA790F;
NR managed75CF59F20D83CB24D85DE1A78C7C5922DE1B8B395B74F9DAC88624C7616F7AE4.
Native NR/helper/official model unchanged. Video/FoA short Quality/Performance labels, saved values unchanged;
tooltip explains after/before DLSS upscaling, pixels/cost/detail/colour tradeoff. Nexus draft updated.

Two-cycle gameplay probePID24784 completed13:38:43 PASS. DLSS SR Performance (50% render scale),
DLSS FG2x/Reflex, NR Quality then Performance. Actual pause/options/apply/resume and forced GC.
NR advances253/325 evaluations, zero failures. NVIDIA reports487/608 output frames from254/319 source
frames after resumes (includes settling, not a display measurement). FG suspends in menus and resumes.
No BepInEx errors or crash. Inspected gameplay and Video screenshot with all three mods and short label.
Probe removed, configs hash-restored,117 save archives unchanged, game closed. Source102 passes.
Evidence: build/nr/il2cpp-r17/combo-probe/{run.log,bepinex.log,error.log,bridge-native.log,fg-native.log,
up-native.log,gameplay.png,gameplay-options.png}; combo/deployment-result.json.

Owner requests tested zip, then NR/FG/BU redeployment after reinstalling Mono/BepInEx5. Packaged normal
NR1.2.3 artifacts/Nexus/TGNeuralRendering.zip:657433 bytes,10 files,
SHA256 BCB01F1D836E5D62CFBCB403F6E307B36A3984ADA2EBFC57810198D5EA6A7BAA.
Exact tested payload; BepInEx plus package manifest and Vortex Engine Injector override, no model bundled.
Extracted and checked payload hashes, version/runtime/allowed paths and override. Not published.
Await Mono readiness; no automatic relaunch/redeployment during Steam switch.

NR-42 packaging follow-up: owner asks about FG/BU. Both normal1.2.3 zips now rebuilt from the exact
NR-39 deployed/staged payloads, not current concurrent sources. Isolated per-mod staging excludes other
mods; each zip extracted, all file hashes/versions/architecture/vendor signatures/allowed paths/Vortex
override verified. Existing normal zips backed up in build/nr/il2cpp-r17/combo, Mono zips unchanged.
FG artifacts/Nexus/TGFrameGeneration.zip:23 files,49,576,745 bytes,
SHA256046A0770E8E75753130B55BDB113B41729CA7BB57EF2767E61EFFFD571AEE819.
BU artifacts/Nexus/TGBetterUpscaling.zip:16 files,129,779,388 bytes,
SHA256EB2B78C02A00218ED07239F0F325E857A8F4BFBBA9855115DA0338D316C289F5.
NR zip unchanged. Evidence: combo/package-normal-result.json. Source checks104:99 pass,5 JSON parse
errors from concurrent PT merge markers in tests/ingame/scenarios.json (line5), PT-26 note added, no PT
content changed. Evidence: combo/package-source-checks.log. No game/deploy/GPU run; game closed.
Owner can now switch: all3 normal zips ready. Rebuild/package/deploy Mono NR/FG/BU after owner readiness.

NR-43,13:49-13:54: owner completes Mono/BepInEx5 reinstall and requests NR/FG/BU deployment plus the
OFFICIAL NR DLL; then the same short real gameplay smoke as final verification before release.
Fresh game has Mono references, BepInEx5.4.23.5 and FoA Mod Manager only; no old normal plugin/bridge.
Built all3 net472 plugins into build/nr/mono-r17/managed,0 warnings/errors. Reused exact tested normal
staging for native/vendor files, no PT build or shared artifacts replacement. All3 Mono zips extracted
and checked (payload hashes, native architecture, vendor signatures, managed version, paths, Vortex).
Deployed13:49,43 payload files hash-verified,37 existing plugin/config/core/loader files preserved.
Official model E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E signature valid.
Mono managed: NR AA3122C23DF477697165C76B10A3281B7886D51B6289DDA6EC0F0131D527ADC0;
FG A3598FC141524B66D585741B329D8F85F3AAD93820764F4143929B80165EE78F;
BU 5C2EC7C814B37CED5652A3F2521DAA65B6B8B3ABFBDAB4AB70BE1E47C8E2F2EA;
bridge51295D636EEBF3604D21C2C601ED50ACD0CEEDA06FDC64AE73C814434B1AAEB9.

GPU guard launched MonoPID22988 at13:52. Official signed SDK route/model available, all3 mods and FoA
Mod Manager load. Two actual tavern pause/options/apply/resume cycles finish13:54:25.776 PASS.
DLSS SR Performance50%, FG2x/Reflex, NR Quality then Performance. Styles0/1, passes1/2, strength1/.7,
tone1/.4, Advanced/Automatic Mask nesting, forced GC. NR resumes219/317 evaluations,1115 total,0 failed.
FG correctly suspends in menus/resumes after: NVIDIA reports414/601 output frames from217/315 source
frames, settling included. No BepInEx errors/crash. Gameplay and Video screenshots inspected.
Scratch probe uses Mono BaseUnityPlugin; private AllSettingsUI.Apply/CancelAndClose invoked by reflection.
Probe removed, original configs hash-restored,117 save archives unchanged. All43 installed payload
hashes plus official model checked again after cleanup. Game closed, no NR waiter/harness lock.
Source105 passes; earlier PT scenario JSON merge is resolved. No production code change for this smoke.

All6 normal/Mono NR/FG/BU1.2.3 zips ready in artifacts/Nexus, not published. Mono zip SHA256:
FG BEFC96DC1066CB0998E4DCE9D6735E8121EA2AEA0C0E2C92911661E0CA29B20B (49,573,796 bytes/23 files);
BU 218A484F13FA6EA0ADFB70BAC2F560C0A01524E0231AC02CCC06AE4DB478259F (129,777,404 bytes/16 files);
NR 570D1E9B3768661173755CC8F3AEEFC8E3DAF162B9FEFA2EDDE04F204BC82387 (653,696 bytes/10 files).
Evidence: build/nr/mono-r17/deployment-result.json, package-result.json, source-checks.log,
combo-probe/{run.log,bepinex.log,bridge-native.log,fg-native.log,up-native.log,result.json,
gameplay.png,gameplay-options.png}. No PT files deployed/changed. Mod packages still exclude the model.

NR-44/45 follow-up: owner cannot reproduce the Ron face demo, reports an optional Avalon dependency
warning, then asks why Default differs from A/B/C and whether four models exist. Read-only inspection:
nr-face-20261007/03-after-A is current Default0, Quality, all controls1, maskOff, one pass, 4K SDR/DLSS
Performance. Official runtime E16BCF15, PT/FG Off, CLO1.1.1 installed. Camera advances1.8m from Slot_94
and uses vertical FOV40; world held still. Face comparison sheet inspected. Current config initially
selected today's Style A/native1, then owner switched to Default/native0; latest log confirms full
controls and one pass. Our default remains vendor0. Four styles are not proof of four separate models.
R11 measured distinct0/1/2 output and style3 matching2 on one fixture, not universal2/3 equivalence.
No new matched gameplay reproduction, configuration edit, deployment or package change. Owner's game
PID49248 is running from14:05 and left untouched. Exact favourite take remains unidentified.
Avalon dependency is intentionally soft, for load order before KeepOutOfAvalon excludes our rows from
Avalon's stale-value Apply. Absent manager returns immediately. No installation required; no rendering
effect. FG/BU have the same protection. Original evidence: nr-face-20261007/{request.json,
environment.json,LogOutput.log,hair-eyebrows/03-after-A/step.json,hair-eyebrows/face-models.jpg}.

## ▶ Starter prompt (R18)

Preserve automatic signature-based backend selection, no community per-file allowlist, and the isolated caller
exception. The signed path and visual defaults stay as before. Read DLSS5-COMPATIBILITY for exact evidence and
remaining hardware/in-game checks. Final-policy GPU checks above pass. NR1.2.3 IL2CPP and the supplied patched
model were deployed at the owner's request. NR-36 then fixed the IL2CPP hook crash and selected the OFFICIAL
model. NR-37 then fixed native nested-row array lifetime;20 actual gameplay cycles pass, owner says it
works great. NR-39 FG/BU deployment and combination smoke passed13:38; NR-40 labels deployed.
NR-43 complete: game is Mono/BepInEx5 again. All3 Mono mods and OFFICIAL NR model deployed/hash-checked,
same two-cycle gameplay smoke passed13:54. Game closed, probe removed, configs/saves restored.
All6 normal/Mono NR/FG/BU1.2.3 zips packaged/verified, not published. Source105 passes. Preserve PT work.
The existing PT runner is Mono-only; use -LaunchOnly for normal-version startup checks, never its Mono runner.
Do not repeat the signed-runtime HDR/visual matrix for a loader-only change. Do not claim older RTX validation
from this5080. No extra temporal filter. Keep the PT agent's files and game session intact.
Latest: owner is playing MonoPID49248; do not close/redeploy. NR-44 Ron settings recovered above,
but no new visual reproduction verdict. NR-45 optional Avalon warning explained; dependencies unchanged.
