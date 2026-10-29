# Neural Rendering feedback - 2026-10-07

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

NR-43 (owner,10-09): Mono reinstall complete; deploy all three mods and the official NR model, then
run the same final gameplay smoke used for the normal version. Mono game references and BepInEx5.4.23.5
confirmed, no old normal plugins left. NR/FG/BU1.2.3 net472 builds succeed, staged/package contents
checked; deployed13:49 and hash-verified,37 other plugin/config/core/loader files preserved. Official model
E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E has valid NVIDIA signature.
All3 Mono zips refreshed in artifacts/Nexus; normal zips unchanged. Source105 passes (PT merge resolved).
Scratch probe uses Mono BaseUnityPlugin and reflection for private AllSettingsUI.Apply/CancelAndClose.
Game launched13:52 through GPU guard; PID22988. Native log selects NVIDIA SDK backend and finds model
available; FoA Mod Manager loads alongside all3 mods. Final two-cycle smoke PASS13:54:25.776:
DLSS SR Performance50%, FG2x/Reflex, NR Quality then Performance, forced GC and nested controls.
NR resumes with219/317 evaluations,1115 total, zero failures. FG suspends in menus then resumes;
NVIDIA reports414/601 output frames from217/315 source frames after resume (includes settling).
No logged errors or crash. Gameplay and real Video screenshot inspected. Probe removed, configs
hash-restored, all117 save archives unchanged. All43 deployed payload hashes and official model rechecked.
Game closed, no waiter/harness lock. All6 normal/Mono1.2.3 zips ready; not published. Evidence:
build/nr/mono-r17/{deployment-result.json,package-result.json,source-checks.log,combo-probe/run.log,
combo-probe/bepinex.log,combo-probe/bridge-native.log,combo-probe/fg-native.log,combo-probe/gameplay.png,
combo-probe/gameplay-options.png,combo-probe/result.json}.

NR-42 (owner,10-09): FG/BU should be repackaged too. Done: normal-version1.2.3 zips in artifacts/Nexus,
matching the exact successful NR-39 gameplay deployment, including the shared Options fix. FG23 files,
49,576,745 bytes, SHA256046A0770E8E75753130B55BDB113B41729CA7BB57EF2767E61EFFFD571AEE819;
BU16 files,129,779,388 bytes, SHA256EB2B78C02A00218ED07239F0F325E857A8F4BFBBA9855115DA0338D316C289F5.
Extracted and verified payload hashes/version/architecture/vendor signatures/paths/Vortex metadata.
Previous normal zips backed up in build/nr/il2cpp-r17/combo; Mono zips untouched. Source104 currently
99 pass/5 JSON errors from concurrent PT scenarios merge markers, recorded under PT-26; no source changes
or further GPU run. Owner asks whether they can switch now: yes, all3 normal zips ready, game closed.
Next: rebuild/package/deploy NR/FG/BU Mono once game/BepInEx5 installed.

NR-41 (owner,10-09): package the tested normal NR zip, then redeploy after Mono/BepInEx5 reinstall.
Packaged artifacts/Nexus/TGNeuralRendering.zip (1.2.3,657433 bytes,10 files, SHA256 BCB01F1D836E).
Extracted and checked payload hashes, version/runtime, allowed paths and Vortex Engine Injector override.
Standard contents, no player model bundled. NR/FG/BU Mono redeployment awaits owner readiness.

NR-39 (owner,10-09): NR now works great; deploy FG and Better Upscaling for a quick smoke test together.
Normal-version1.2.3 FG/BU deployed13:36,35 payload files hash-verified, same validated native/shared managed
bridge as NR. Other mods/configs preserved. Two real gameplay pause/options/apply/resume cycles pass13:38,
DLSS SR Performance, DLSS FG2x/Reflex, NR Quality then Performance. NR resumes with253/325 new evaluations,
zero failures; NVIDIA reports487/608 output frames from254/319 source frames (includes settling after pause).
No BepInEx errors, no crash. Real Video screenshot shows all three mods and the short Quality choice.
Probe removed, original configs hash-restored,117 save archives unchanged, game closed. Evidence:
build/nr/il2cpp-r17/combo-probe/{run.log,bepinex.log,fg-native.log,bridge-native.log,gameplay-options.png}.

NR-40 (owner,10-09): call Placement choices simply Quality and Performance, and explain the difference
in the tooltip. Updated Video/FoA labels and shared tooltip: Quality after DLSS upscaling; Performance
before it, fewer pixels/lower GPU cost, detail and colour/brightness tradeoffs. Saved keys unchanged.
Nexus draft updated; README already uses short labels. Normal-version NR deployed13:36, hash-verified.

NR-38 (owner,10-09): "BTW let's move 'Local Tone' into 'Advanced'". Implemented in Video rows and FoA
Mod Manager tags, after Advanced and before Style. Value/default/config key preserved. README/Nexus updated.
Normal-version build/deploy completed13:07 alongside the NR-37 candidate fix, all8 payload hashes verified.
Managed NR9F411A3C4AF1 and bridge2BE2F31EFD7B. Official model retained; no release zip/publication.

NR-37 (owner,10-09 12:48): "It worked well"; later clicking Options crashed. In-game pause menu, with
frequent settings changes and several earlier successful Options visits. No installed files changed and
no GPU tests ran during this game session. Windows reports execute access violation at0x18 after9000 NR
evaluations (9479 model calls, style changed live). Dump: LocalAppData/CrashDumps/Fall of Avalon.exe.45724.dmp.
Mapped with game metadata/LibCpp2IL: Attribute.IsDefined -> World.MakeViewOfType -> SpawnViewFromPrefab ->
SettingsUtil.SpawnView -> VDependentOption.Update. The virtual call for the row view type jumps to0x18.
Baseline scratch probe13:04 reproduces invalid child-row memory on its first Video visit: JProperty is
passed where PrefOption is required; a later row crashes in the diagnostic ID read. No NR rendering fault
is logged. SwitchRowsOption created native arrays only in temporary wrappers, including when switching rows.
An explicit managed nativeRows field now retains the array/GC handle for each injected option. Built and
deployed13:07 with Local Tone under Advanced. Source102 passes. Repeat initially waited on Starfield.
The fixed main-menu run subsequently passed20 cycles after Starfield closed. Owner requires gameplay too:
"from in-game, then open the pause menu, change settings, go back to the game, and so on". The next run
loads the tavern with active NR, uses MenuUI.OpenSettingUI, applies NR settings, resumes interactive play,
and verifies NR evaluations advance. First gameplay attempt:12 complete cycles, then a resume assertion
without enough logged counters. The scratch cleanup itself crashed in a generated static string setter;
removed the unnecessary write, restored configs and checked all117 save archives unchanged. Owner says
the game was always minimized/unfocused, so no focus transition is established. Corrected probe logs
camera/readiness/counts and waits for rendering to resume. Final PID30912 passes20 complete gameplay
cycles13:27:08,65..196 NR evaluations after each resume,8740 total, zero failures and no resume waits.
Both placements/styles0..3/passes1..4 and nested options exercised; production fix unchanged.
Owner: "Great, it seems to work great!" Probe removed, configs hash-restored,117 save archives unchanged.
Evidence: options-probe/gameplay-final.log, gameplay-final-result.json and archived native/BepInEx logs.
Evidence: build/nr/il2cpp-r17/options-crash, options-probe/baseline.log and baseline-bepinex.log.

NR-36 (owner,10-09): "The game crashes"; "Let's start with the official one"; suspected missing NR DLL
and leftover Mono files. Installed patched DLL hash matches and native log confirms successful initialization.
Fatal stack is Il2CppInterop `il2cpp_object_get_class` inside its generated RenderPostProcess detour: it
treats the by-reference unboxed PrepassOutput data as an object reference. NR now uses a signature-checked
native hook for this method on IL2CPP, forwarding that pointer untouched and retaining schedule restoration.
Mono retains Harmony. Deployed12:44, managed hash EF4FCFCAA47B; official NVIDIA model E16BCF15 selected.
Seven confirmed Mono DLLs backed up outside plugin scanning; only NR/bridge load. At12:46:58 PID45724 is
responsive after117s, with4800 NR evaluations, actual DLSS SR guides and a completed post-process hook frame.
Owner subsequently says it worked well; later Options crash is NR-37 above. No FG/BU coexistence claim. Missing-model
probe passes: explicit unavailable reason, working original swapchain/bridge, no NR evaluation.
The owner asks about interoperability: shared bridge/device and SR/RR input contract unchanged; verify the
normal builds of BU and FG after standalone NR. PT remains Mono-only and its guide gap PT-72 remains open.

NR-35 (owner,10-09, R17): "I've installed the IL2CPP build of the game and deployed BepInEx IL2CPP.
Please deploy the Neural Rendering IL2CPP version to the game so we can validate it works too".
Done12:22: NR1.2.3 net6.0 built in isolation, tested native/helper/shared bridge and supplied community
model installed. Eight payload hashes verified;80 other mod/config/loader files preserved, replaced NR files
backed up. Source102 passes. No game launched; normal-version/community rendering still needs an in-game
check. Receipt: `build/nr/il2cpp-r17/deployment-result.json`. PT files/jobs untouched; live Steam folder now IL2CPP.

NR-34 (owner,10-09, R17): "OK great, now update the nexus description for the mod and the readme".
Done in `docs/nexus-neural-rendering.md` (BBCode draft) and `README.md`: automatic official/community
selection, supplied DLL placement/create-folder instruction, included adapter, no per-file approvals or
update for every compatible patch. Separate5080 standalone results from community in-game/older-card
validation. README's stale signed-only/SDR-only summary removed. Local drafts only; nothing published.

NR-33 (owner,10-09, R17): "Can we validate that it will work on older cards even using my RTX 5080 somehow?"
Audit: our NR path has no RTX50 architecture/device-ID gate; it checks NVIDIA vendor, the runtime interface
and the runtime's support result. Own compute shaders targetcs_5_0; shared D3D12 creation requests feature
level11_0. Existing CPU fixtures and5080 rendering checks validate the adapter, not the runtime's kernels,
driver choices or memory behaviour on older cards. Spoofing identity or forcing PTX on5080 is not older-GPU
validation. The existing standalone NR probe can test a real older GPU without launching FoA. One card from
each claimed generation is the minimum practical next check; performance/VRAM vary within generations.

NR-30/31/32 continuation (owner, 10-09, R17): finalize NR compatibility with the supplied
`C:\Users\Frede\Downloads\patched\nvngx_dlssnr.dll` for RTX 2000/3000/4000. Investigate RenoDX's
loader and find a clean solution that preserves the normal NVIDIA integration. The owner considers
the existing rendering implementation ready after SDR/HDR/ReShade validation; this session targets
runtime compatibility, alongside a separate agent's path tracing work. Earlier scratch rejection
is a starting point, not a reason to stop investigating.

R17 decision: the owner approved "Yes—contain it in a separate compatibility adapter" after being told
that direct initialization still requires a caller-name workaround. Keep the signed NVIDIA path unchanged;
isolate the community runtime in an NR-only adapter. This approval supersedes the earlier
NR-30/32 decision to wait. Findings, implementation and validation: `DLSS5-COMPATIBILITY.md`.

R17 follow-up: automatic selection is required, with only dropping the DLL and restarting. The owner rejects
the proposed per-file allowlist: "I would prefer not having to maintain this by updating the mod each time a
new patched dll is created, that happens relatively frequently". The adapter now checks the direct runtime
interface and its support result; no pinned community hash/version list. SHA256 is diagnostic and guards
against replacement during loading. Signed DLLs still use the NVIDIA SDK path and its existing profile policy.
Real rendering and RR coexistence pass on5080; older RTX hardware and the community in-game check remain open.

NR-28 HDR continuation (owner,10-08): ReShade, RenoDX and HDR enabled for testing. Owner confirms the
RenoDX DLSS5 add-on is not deployed. Verify the final output and neutral/On/Off behaviour, preserve their
HDR configuration. Native Unity screenshots alone do not validate the post-RenoDX HDR signal.

NR-28 HDR result (R16, 21:15): ReShade and RenoDX loaded, with no RenoDX DLSS5 add-on. A temporary diagnostic
add-on captured the final presented buffer after ReShade at 3840x2160 R16G16B16A16_FLOAT. ReShade reported
scRGB and the display reported 10-bit PQ with a 1037.51-nit peak. Off, neutral, full and restored-Off each
captured six valid frames with no readback errors, invalid values or fallback pixels. The contact sheet looks
correct and Off/neutral are close. Full strength changes the intended image. Stationary full-strength frames
still vary more than Off (mean adjacent-frame HDR difference 0.00163 versus 0.00042), so this validates the
HDR signal and ordering, not NR-14 temporal stability. Unity's HDR flag remained false because RenoDX owns
the scRGB swapchain. The temporary add-on was removed and ReShade.ini was preserved.

NR-30 (owner,10-08): The owner points out that the RenoDX Discord has a patched `nvngx_dlssnr.dll` for RTX
2000/3000/4000 cards and that RenoDX accepts it. TG Neural Rendering does not currently accept it. The shared loader
uses WinVerifyTrust and requires an NVIDIA signer, then requires the validated 310.8.0.0 input profile. Patching
the DLL invalidates its NVIDIA Authenticode signature, so a binary-patched file is rejected before NGX initialization.
RenoDX's compatibility route is separate evidence. Do not weaken the signed-runtime policy implicitly. Decide on
any separate opt-in compatibility path only after measuring its ABI and hardware behaviour.

NR-31 (owner,10-08): The owner supplied `C:\Users\Frede\Downloads\patched\nvngx_dlssnr.dll` for a direct check.
It is x64 and reports version 310.8.0.0, but Windows reports `HashMismatch`: the certificate identifies NVIDIA while
the file no longer matches the Authenticode signature. Its SHA256 is
`E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A`. A scratch `tgnr_gpu_smoke` run with only
this DLL swapped reports `TGNR_REJECTED` before NGX initialization and performs zero evaluations. The game was not
changed. This confirms the current rejection is the signed-runtime policy, not a version-profile mismatch.

NR-32 (owner,10-08; interpretation corrected by R17 below): A scratch-only TGNR build bypassed the signature gate to answer the ABI question without
changing production code. The patched DLL loaded, passed version 310.8.0.0 checks and reached NGX capability
querying on the shared RTX 5080 device. It reported `DLSSNR.Available=0`, so no frame was evaluated. This is evidence
that the ordinary NGX interface can load the file, not evidence that it works on its target RTX 2000/3000/4000 cards.
Do not ship or enable a bypass based on this run. A real compatibility decision needs one of those GPUs or an
official NVIDIA compatibility statement.

R17 correction: verbose NGX logs show its own signature check refused the model before loading it (null module
handle). R16's inference of successful loading and hardware rejection was wrong. The direct adapter subsequently
initialized and evaluated this exact model on5080. See `DLSS5-COMPATIBILITY.md` for the independent caller control.

NR-29 (validation,10-08): FSR FG plus Quality NR has high real-frame cost. Run202304 has15.73fps
with FSR FG+NR,35.86 with DLSS FG+NR and33.60 with XeSS FG+NR. Same4K tavern, no other GPU workload,
NR evaluation about11.5ms in all three. No runtime failure, NR evaluates every captured frame.
Follow-up202741: FSR FG with NR Off/On/Off gives18.78/15.99/18.48fps, zero errors and no other GPU workload.
Most of the slowdown is present with NR disabled. Broader FG performance investigation, cause open.
No evidence yet with the NR plugin absent. Do not attribute this to NR model evaluation alone.

NR-28 (owner,10-08): "OK, do all the other validation testing then". Run the remaining NR correctness,
build and compatibility checks. Matrix and results: DLSS5-VALIDATION.md. This authorizes validation,
not a release verdict or an extra temporal filter. Preserve settings and parallel PT work.

R15 progress: both builds, policies/mutations, signed-runtime image/fallback/shared-device and multipass
checks pass. Steam prompt resolved. FoA0.6.63 live menu5 states, standalone without FG/BU/PT, all FG backends,
native Video grey/restore, resolution changes, camera cuts and reload pass. Latest source96 passes.
Quality4K tavern83.7/32.4/86.1fps Off/On/Off, +1.67GiB VRAM. No NR rendering/default change or stability fix.
DLAA/FSR/XeSS gates also pass202304. FSR cost issue NR-29 above. ReShade/HDR and normal-runtime setup
still needed. PT combinations wait for matching geometry guides PT-72. DLSS5-VALIDATION.md.

NR-27 completed R14: owner "ok do tht". Run182643 captures128 valid Quality frames, four metres at1.2m/s.
Actual post-SR input tracks current vectors within0.139..0.155 render pixels median, p95<=0.496.
Previous-frame vectors give9.7..10.6px, wrong sign/scale8.1..28.6px. Same-frame colour alignment after
jitter is0.060..0.065px. Local outliers inspected, no guide correction justified. No lighting fix or
complete integration claim. Diagnostic NR pair deployed18:26, no defaults changed. Game closed,
117 saves/configs restored, Source95 passes. Details/limits: round-14. No extra temporal filter.

NR-27 (owner,10-08): "So can we do anything else at this point then?" Next specific input check is actual
post-SR colour alignment. R11 tracked original pre-SR colour and proved guide values, not the exact colour
Quality NR consumes. Added full-resolution nr-source/nr-result diagnostic captures and neural_boundary.py.
Native build/source95 pass, analysis compiles. Deployment refused while Starfield runs, no files changed.
Another session then holds the shared harness with FoA running. This diagnostic has not run in game.
Final owner ruling: "Yeah, let's not do our own extra filter thing on top." Do not implement the proposed
temporal filter, including as an optional trial. Nothing of that filter was implemented. Audit and improve
the native integration instead. This supersedes the earlier "We can try".

NR-27 research: owner questions duplicating NVIDIA's temporal stability with our own layer.
NVIDIA's public DLSS5 description puts
temporal stability in the model using engine motion vectors. Its vk_gltf_renderer evaluateNr supplies
colour, depth, motion, controls and resets, then copies the result without a separate temporal filter.
No recommendation for our proposed filter was found in those sources. The proposal is now rejected.
Sources and limits are in round-13. No rendering changes from this clarification.

R12 owner feedback: current exposure generally looks right and the face close-up demos look amazing.
Preserve that baseline. A darker or weaker image with lower variation is not automatically an improvement.
Owner asks whether DLSS5 struggles with this tavern. Plausible scene-specific limitation, not proven.
R12 probes show the runtime actually uses motion/history, including with both detail controls0 and tone1.
On a translated photo, removing motion raises moving block step p95 from0.383 to2.684 encodedRGB levels.
This is not an improvement over installed NR, which already supplies motion. GlobalTone unset/0/.5/1 has
no effect in the recorded-tavern probe. Details, scope and limitations: round-12.

NR-26 (owner,10-08): asks about RenoDX's exposure slider affecting the image given to NR. Confirmed
ShortFuse tool's UI describes exposure independent of the game texture. Saved multiplier1, mode1.
Exact implementation for that distributed ShortFuse build is not available in inspected public upstream.
Our diagnostic input multiplier .25/1/4 was already tested with camera turns in R9, including after the HDR
glare fix: darker input sometimes reduces late-held drift but not the whole settling change. That does not
settle the walking defect. A focused Quality/full-tone walk comparison1/.25/1 is prepared. Run170215 failed
gameplay readiness before saving frames, so is excluded.117 saves and configs restored. No new default.
Retry170616 completes384 valid frames at1.2m/s with full controls, no competing GPU work. At quarter input,
common-scale moving residual falls2.815/2.772 to2.503 while mean effect falls4.765 to3.839. Held final-screen
block p95 range rises1.102/1.145 to1.184. No same-look stability win. Direct model-space comparisons would
confuse exposure attenuation with stability, so use the compensated/common-scale results. Preserve1 and the
owner-approved face look. Game closed17:08,117 saves/configs restored. Source and limits in round-12.

Latest owner verdict, 10-08 15:13 (NR-14/NR-23): "Wow, it's really temporally unstable. Not release-ready as it is."
Evidence: `C:/Users/Frede/Videos/NVIDIA/Tainted Grail  The Fall of Avalon/Tainted Grail  The Fall of Avalon 2026.10.08 - 15.13.00.01.mp4`.
Stability remains a release blocker. The proposed pivot to release acceptance is superseded: inspect the recording,
audit the integration against NVIDIA's sample, and pursue a concrete correction. RenoDX reproducing instability
does not establish that our input contract is correct or that a stable integration is impossible.

Owner clarification: walking down the tavern changes lighting much more than turning in place. Compare
translation and rotation separately, including stairs, plants and wall shields. Latest owner report says
these stood out more than the chairs. Audit per-pixel depth/motion and optional NR inputs, not only
averaged motion grids. The video is 13.37 s at 3840x2160,775 frames. Extracted half-second
atlases and copied contemporaneous logs are in `artifacts/harness/nr-owner-video-20261008-151300`.
Logs around15:13 confirm style0, one pass, intensity/tone/detail/skin1, mask0. Reset count remains4 across
the recording. The later on-disk config has Detail0 and is not the recording's configuration.

R11 input audit161014: two128-frame Quality takes at1.2m/s, exact native guide captures, all12 frames match DLSS after documented conversion. Independent camera/depth motion error 0.008..0.016 guide pixels median, edgep95<=0.027. Inspected depth outlines match stairs and furniture. Wrong signs/scales give6.59..25.13pixels. This supports the SR guide contract on static tavern geometry, not all animated objects or PT/RR.
Run161628 compares actual game grading: three valid takes, exact late neutral, steady exposure. Variation decreases but mean model effect falls4.813 to2.249 encodedRGB levels. No same-look stability improvement established. Keep defaults and release blocker. Details and limitations in round-11. Earlier155904/160609 captures failed the new guide reader and are excluded.

NR-25 (owner,10-08): no semicolons or other AI-sounding wording in tooltips/descriptions. Overall tooltips
are good. Owner asks why Style is absent, recalling Default/Cinematic/other styles. Current Model A/B/C
maps to DLSSNR.Style0/1/2, not a separate model selector. Current NVIDIA sample labels style0 Default,
then A..G. Verify version-specific naming before assigning Natural/Cinematic labels. Preserve numeric
selection and settled look while correcting UI terminology. Stability work remains the priority.

NR-25 follow-up: both owner screenshots show independent preset/style controls. Confirmed public preset
hint DLSSNR.Hint.Render.Preset. Isolated signed-DLL check `build/nr/profile-check/comparison.json`:
24 frames per case, styles0/1/2 all differ. Changing style with reset gives bit-identical output to a fresh
feature of that style. Fresh presets0/1/2/3 give bit-identical output. No creation-only style bug established.
Earlier A/B/C comparisons were styles, not independent model weights. FINAL owner ruling: use NVIDIA
names Default/A/B/C and actual native Default, never our own Auto/recommended mapping. NVIDIA sample
Style uses0/1/2/3. `build/nr/style-c-check/comparison.json` checks all four:0 differs from1 and2;3 is identical
to2 on this input. Default0 repeated bit-identically. Expose the requested native0/1/2/3 values without
forcing0 to equal1. Existing saved Model A/B/C means0/1/2 and keeps those numeric values via display labels.
Settled old "A" is now displayed Default. Earlier "B"/"C" were native1/2, now displayed A/B.
Do not expose a separate preset selector without a demonstrated effect. Natural/Cinematic names, new
preset row and the proposed own-Auto Default were never deployed. Official sample terminology is Style.

The owner's first in-game feedback, following the 1.2.2 deployment:

Latest resumed work,10-08: faster chair runs141444/142304 each complete four takes with no GPU
contention. Actual full-NR speed0.261/0.293m/s versus0.175 before. Same A/full/maskOff/one-pass settings;
neutral remains exact and exposure steady. Pixel diagnostics deployed14:23 find no HDR fallback activation
in any captured full-NR pixel. Full model input0.01239..0.99196, output-0.00065..0.97272; at most one pixel
per frame is slightly negative. The colour changes remain. This rules out our white-output fallback for
this route, not every colour-conversion error or the model input contract. Normal-play rendering unchanged.

Focused comparison144131 tests existing peak-based Reinhard compression versus
per-channel Reinhard, with the matching inverse INPUT scale and one initial history reset in both cases.
This tests channel coupling; it is not NVIDIA's prescribed tone curve or the game's full colour grading.
Plan `build/nr/chairs-tone-map-plan.json`: Off/current/alternate/current/Off, Quality only,96 moving/96 held.
Starfield first blocked probe/deploy; that waiter was canceled before start. Once it closed, final probe
passed and the pair was deployed14:41. The five-take run has0 problems, matching routes and resets.
Tracked moving p95 current/alternate/current:1.965/1.977/1.983 RGB levels; alternate is within repeat
variation. Held model-effect ranges10.019/8.961/10.100, final32 ranges2.394/2.552/2.948. Effect magnitude
stays similar (3.852/3.879/3.849 display levels from Off); no meaningful stability win, no default change.
Atlas inspected. All117 saves unchanged/configs restored, no NR waiter. Internal diagnostics remain for
reproducibility; normal play uses the original mapping. No new stability or full HDR compatibility claim.

10-08 follow-up: the owner says to focus on Quality; Performance remains available with settled defaults,
but is outside the current investigation. They ask whether colour-space differences explain instability
and whether a tone-mapped NR input rules out HDR. NVIDIA's public sample explicitly says "NR expects LDR
input" (`src/dlss.hpp`); that demonstrates its LDR path, not a full HDR contract for signed310.8.
Tone mapping itself can target SDR or HDR. Our early adapter compresses pre-exposed linear scene colour
into sRGB LDR, evaluates NR, then restores using the original input's brightness scale. This is not the
sample's actual post-tone-map image and does not establish correct HDR appearance. Unity native HDR
output is gated off. RenoDX HDR can operate while Unity reports HDR inactive; that gate does not validate
or reliably exclude external HDR. Keep native HDR support and RenoDX HDR compatibility unverified.

NR-24 (10-08 12:30): the new slow chair-route capture failed before saving any frame in all four takes,
including NR Off. `ScreenCapture.CaptureScreenshotAsTexture()` returned null. Run20261008-122849,
`artifacts/harness/nr-chairs-quality-20261008`; no temporal or colour verdict. Do not blame NR or RenoDX
without isolating the capture failure. RenoDX Unity HDR was loaded, RenoDX NR was absent, Unity hdrOutput
false. FoA quit,117 saves unchanged/configs restored. The straight-walk harness extension is compiled,
but that run could not validate its in-game camera path. Later valid evidence follows below.

NR-24 follow-up: owner confirms ReShade is off again. The independent PT session also reproduced null
screenshots with ReShade loaded, then captured four scenes successfully after it was disabled. This
narrows the capture failure, without identifying the underlying resource-format/copy error. FrameGrab
now records native Unity capture warnings/errors without changing the player's log configuration.
The harness retains its ordinary PT/ReShade guard; only explicit NR compatibility recipes may opt in.
It also starts both saved NR mode and remembered mode Off, restoring the original config afterward.

Owner: "Why do you keep stopping work?" Questions steer the ongoing investigation; answer and continue.
Run20261008-135437 (`artifacts/harness/nr-chairs-quality-20261008-135437`) completed with ReShade off:
four takes/960 frames, matching2m forward walks, actual NR evaluations and exact neutral scene grids.
The atlas shows the requested chairs. Exposure is constant while held; full-strength model-effect range
reaches9.424 encoded RGB levels versus0 at neutral. The input still varies, so this does not isolate a
vendor defect or prove correct colour conditioning. Tracked-feature p95 changes: full1.699, neutral0.429,
Off control0.388. The general analyzer now validates requested walks rather than calling them accidental
camera movement; reanalysis reports0 problems. No NR rendering/default change or new stability fix.

Owner: "Move a little faster than that." Plan changed from144 to96 moving frames over the same2m, keeping
96 held frames. The first faster queue was canceled before launch. Run20261008-140059 used the earlier
contention permission, but the owner stopped the game: "Had to stop the game because the GPU was too busy".
PT tests used up to58.4% GPU; only Off completed. Exclude this run from comparisons and speed claims.
All117 saves unchanged, configs restored, lock cleared, no queued NR launcher. Future NR game tests use
the normal five-second GPU-free gate; do not reuse the earlier contention permission. Do not stop other
agents' GPU jobs. No automatic retry is pending. Capture with ReShade remains unvalidated (NR-24).

NR-23 update (10-08 12:04, OWNER-OBSERVED): the owner thoroughly tested RenoDX DLSS5 Upscaled, including
its settings, with/without RenoDX HDR, with/without TG Graphics Bridge, and with/without FG. With matching
settings it is "actually just as unstable as our implementation". This supersedes the earlier impression
that RenoDX is much steadier. It reproduces without those optional components; none is required to trigger it.
The owner finds DLSS5 less stable here than in other games; the reason is not established.
Further owner confirmation: RenoDX DLSS5 has the same problems at **Present** too. This weakens a single
wrong injection point or missing pre-NR post-processing as the explanation. Moving NR later is not a fix
demonstrated by these tests. Shared scene/input/model behaviour remains to isolate; no vendor-bug conclusion.

Exact route: load `PT eyebrows-flickering` (scenario `eyebrows-flickering`), start at the two seated men in
the corner, turn 180 degrees, walk slowly forward and watch the changing shadows/exposure/lighting around
the chairs where the lady sits. This is a different view from the existing Ron close-up/tavern camera sweep.
Retain as the reproduction route for any future stability claim. No automated capture of this route yet.

State: shared cross-integration symptom, not a demonstrated TGNR-only regression. Model behaviour or inputs
common to both integrations remain possible; this does not prove either a vendor defect or an unfixable
limit. Keep settled defaults and NR-14/23 open; do not add smoothing or weaken the effect as a presumed fix.
No new agent GPU run, deploy or rendering change in response to this report.

Owner follow-up: a game-specific integration might improve on RenoDX, though expectations are low;
possibly the base game's lighting makes NR react inconsistently. Treat this as a hypothesis, not a finding.
Earlier identical-colour/depth, zero-motion replays also drifted, so changing scene lighting alone cannot
explain all observed instability. Those replays do not validate our colour conversion or prove a model bug.
Next useful comparison: the exact chair route with NR Off/On, paired source/model/output records and local
tracking of those chairs. Determine whether source changes are amplified or stable source regions acquire
changes. Test a specific input-preparation correction only if evidence supports it; no broad model sweep,
default reduction or extra temporal smoothing presented as a fix. Native access is an opportunity, not proof
that TGNR can exceed RenoDX's stability. Any PT comparison must respect its separate guide issue PT-72.

Pipeline-order audit (owner asks whether Unity processes the low-resolution image first): checked the
installed HDRP assembly as well as Unity's 6000.0/staging source. RenderPostProcess supports BeforePost,
AfterDepthOfField and AfterPost upscaling. HDCamera selects DLSSInjectionPoint from the active pipeline
asset. TGNR's BeginPost temporarily overrides this to BeforePost while enabled, for both placements.
Exposure and BeforeTAA custom post-processes still precede it; DoF, motion blur, bloom, colour grading,
tone mapping, CA and vignette follow it. Fog and scene lighting are already in the incoming image.
Thus "before all post-processing" was too broad. RenoDX Upscaled only identifies its relation to DLSS;
the active schedule during the owner's RenoDX run was not recorded. Do not assert identical complete
ordering from that label alone, nor claim this explains the shared instability. Read-only audit; no deploy.

Earlier NR-23 report (10-08, owner after personal play): severe changing darkness/shadows around tavern chairs during
SLOW forward movement, with Local Tone at both minimum and maximum. Performance is visibly steadier than
Quality, and RenoDX DLSS5 is much steadier than TGNR. Owner also says earlier TGNR playtests looked steadier.
This supersedes camera speed as a sufficient explanation for NR-22. Latest multi-pass old/new synthetic
agreement only limits that regression hypothesis; it does not validate the input contract or integration.
Audit earlier/current game evidence and RenoDX/NVIDIA colour, depth/motion sizing, timing and history.
Do not treat lowering tone or switching to Performance as the fix. No settings/placement change yet.
Owner confirms the RenoDX comparison is **Upscaled**, superseding earlier Present notes. At 11:50 the
owner offers a fresh RenoDX test; leave game files and GPU alone. Suggested matched controls: TGNR Off,
RenoDX Upscaled/A/one pass/full Local Tone, same chairs and slow forward walk. Result now recorded above.

NR-22 (10-08): owner saw much worse one-pass colour/exposure/temporal changes in the new tavern run.
Track as a possible regression, not closed by successful evaluation counts. New recipe used16 moving frames
and32 settling frames vs earlier48/60. Captures show zero skips/failures/extra resets and held exposure
constant. Old009321545816 and newE011F0C82733 produce bit-identical output across120 synthetic HDR
frames (held/moving/held, real model, full A, same bridge/DLL). Actual in-game one-pass sweep1.153s
vs earlier6.001s, maximum yaw step9.57 vs3.26 degrees. Harsher movement was an initial hypothesis;
NR-23's slow-walk report rules it out as a sufficient explanation. Do not soften defaults to hide it.
The fast recipe was a functional stress check, not comparable temporal-quality evidence. Retain the
earlier48 moving/96 held frames for quality tests and report speed. Evidence: artifacts/harness/
nr-passes-20261008 and build/nr/single-regression/comparison.json.

NR-20 (10-08): "commit to these settings", explain Performance drawbacks in tooltips, offer less colour/
lighting change. Owner accepts both modes with A, strengths/tone/detail/skin1, maskOff. Quality stays the
default placement. Updated shared Video/FoA descriptions: Performance can soften detail and has larger
colour/brightness settling after movement; Local Tone reduces lighting/colour changes while Detail remains
separate; Strength reduces the whole effect and zero preserves the source. No independent colour-versus-
lighting control is established for this runtime. Existing player values remain; no forced config reset.

NR-21 (10-08): other models/styles and above-1 multipliers, perhaps under Advanced.
Owner clarification: "2x" means multiple NR passes, not native intensity above1. Implementing Advanced
Passes 1-4 (default1), independent history per stage, output fed into the next, HDR restored once at the end.
Strength/tone/detail settings apply to every pass, not final opacity; zero preserves the exact input.
Implemented and built both versions; exact-input GPU check passes1-4 at Strength0.5/1 and rejects three
broken chains. The initial oracle's decode/encode round trip changed input by less than1e-6 but moved output
by up to0.012; a scratch raw readback establishes exact agreement without that conversion. No threshold
relaxation. Deployed11:22 after PT finished; tavern7 takes valid, Off restored,117 saves unchanged.
Model invocations and captured pass counts match; four passes visibly exaggerate the effect. Keep defaults at1.
Game closed, configs restored, no queued NR launch. Menu appearance remains unverified in Video/FoA.
Advanced Off/On in Video and FoA: Model A/B/C, Passes1-4, Detail, Automatic Mask and conditional Skin Detail.
Placement/Strength/Local Tone stay accessible. Hiding advanced controls keeps their values and NR history.
The tested 310.8 A/B/C choices all use DLSSNR.Style; no independent weight/model selector established.
NVIDIA's current public sample sliders use 0..1; no separate Global Tone control inferred for the older DLL.
After Starfield closed, the menu update was deployed at 10:31 and hash-verified. Both managed versions compile
and Source95 passes; real-game menu acceptance remains pending. "Commit to" means settle settings, no git commit.
Isolated probe sends native Intensity1/1.5/2 directly, bypassing the production adapter's range only in a
scratch copy: full-strength float RGB output is bit-identical at two sizes. Baseline repetition is identical;
positive control 0.5/0.75/1 changes output (mean RGB difference0.0024-0.0053). Seventy-two evaluations total,
no game. This indicates a cap at1 for overall intensity in the supplied runtime on these inputs; no useful
Strength overdrive to expose. This probe answered a different question from the owner's intended repeated passes.
Evidence: `build/nr/overdrive-check/comparison.json`, scripts `.testruns/nr/*overdrive*`; production native unchanged.

Owner decision 10-08 (NR-05/NR-11): "that settles A after upscaling with full Local Tone is the quality
setting for the plugin." Quality selection is now settled; NR-14 is not thereby declared fixed. Asked
whether Before has good separate settings: A/full Local Tone is the recommended baseline there too,
with Strength/Detail/Skin Detail 1 and mask Off. No separately optimized combination consistently wins.
C is gentler and improves some face/tavern measurements, but its held forest variation is worse; reducing
tone weakens the effect the owner wants. Performance still has greater slow settling after movement.
The owner has not yet accepted this Performance recommendation. No new sweep, build or deploy needed.

NR-19 (code audit 10-08, after the owner's question about FG/upscalers): NR's marker correction is local
to NR. `native/framegen/shaders/interop.hlsl` TGFG_MOTION copies raw XY, and
`native/upscaling/shaders/upscale.hlsl` TGUP_COPY copies raw components for motion too. FG therefore passes
the observed Unity SR X=2 marker unchanged to its backends; BU's HDRP-input route has the same missing
decode for FSR/XeSS/RR. The bridge publishes borrowed original resources, not NR's corrected copy.
This proves an adapter gap, not measured artifacts or undocumented behaviour inside each vendor runtime.
Stock DLSS SR already receives that texture; do not call its internal handling broken without evidence.
Next: one shared, source-aware Unity motion decode, preserving generic/PT vectors, and targeted validation.
No rendering change or new game run made for this audit. NR-17 glare was NR's own HDR restore, separate.

The owner also suggests A/After/full Local Tone may be the practical ceiling of this NR runtime. Plausible:
NR-14 persists with retained colour/depth and zero motion; A/B/C/mask sweeps found no universal improvement.
Our colour-input adaptation is still a possible cause, so a runtime limitation is not established. Keep the
current working defaults and stop open-ended model sweeps; do not promise perfect stability or declare an
absolute ceiling. The shared-input audit is a concrete correctness task distinct from subjective NR tuning.

NR-18 (found 10-08 07:33): the live A/C run `nr-live-models-20261008` captured the face and tavern
outside gameplay: 12 invalid takes, initially black, including NR Off. Excluded from model comparisons.
The forest returned to gameplay; its six takes validate. Cause not established; no evidence of keyboard
interference. Added readiness checks before freezing/takes/every captured frame; stop the scene when it
leaves gameplay. Builds, with sixteen stage checks/fifteen mutations passing; runtime guard acceptance
completed by retry `20261008-092818`: 12 valid takes, all gameplay/stage checks pass. Original loading
cause remains unknown; the guard now records loading/player/UI/camera state if it recurs. Track with PT-26;
no NR waiter is queued. Latest A/C evidence and tracked-colour measurements are in round-10.
Reset-history run `20261008-094455` then stopped during its second take: loading=False, hero=True,
UI=UIState, interactive=False, camera=MainCamera, timeScale=0. Repeated-reset takes were not reached;
exclude this run from NR stability conclusions. No evidence identifies the reason for the UI transition.

Owner's status question (10-08): "is the image meaningfully more temporally consistent now than it was when
we started the project?" Answer: the specific tavern movement-stop brightness jump is about 86% smaller
after correcting placement (+1.130 to -0.163 levels), but this is not a global stability score. Invalid SR
motion markers and exaggerated HDR glare were fixed. Slow colour/coverage changes remain; the latest round
has improved diagnosis, not delivered another visible stability fix. Defaults remain provisional. Finish
the running diagnostic and stop this test cycle; no further model sweep in this turn.

NR-17 (fixed/deployed 10-08 00:44; owner verdict pending): inverse HDR restoration amplified model values near white into glare before
upscaling. Signal run 002838, tavern final frame: largest linear-HDR cell mean input/output is 3.36/16.20
at ordinary input scale 1, and 3.41/191.37 at diagnostic scale 4. The latter shows bright points in the
screen atlas. These are cell averages, not maximum pixel values. Restoring the model's relative colour gain
with the original pixel's shoulder factor fixes the amplification. Run 004444 repeats all 16 takes; all 12 NR
cases pass the HDR bound at full controls, and the old capture fails. The default Before final input/output
cell maxima are now 3.29/3.16. Viewed both atlases; stress-case bright speckles are gone. NR-14's variation
measured directly at model output is a separate unresolved issue. R10 completed 32 retained-input A/B/C
takes with mask Off/On: C is gentler and settles less After; masking gives no consistent stability win.
Independent image tracking validates signed motion at both placements to about 0.11 capture pixel in the
tavern. No model default changed and NR-14 remains open; see round-10 for measurements and limitations.

NR-16 (found 23:50): neutral Strength 0 still encodes/restores and writes the scene colour. The four neutral
takes of run 225820 have mean absolute relative error 0.232-0.237%, worst cell 1.06-1.32% (denominator floor
0.001 linear HDR). Preserve the original scene at exactly zero strength while still evaluating NR to keep
its history warm. This is a small round-trip error, not an explanation of full-strength temporal changes.
Evidence: `build/nr/neutral-stage-roundtrip.json`. Fixed/deployed 10-08 00:04: NR still evaluates but no longer
writes scene colour at zero. The exact-pixel GPU check fails the old build and passes the fix. Run 000447
confirms exact scene grids at both placements; its check rejects the older capture. Full-strength NR unchanged.

R10 live-input follow-up: A/C captured with normal moving inputs in face, tavern and forest. After has less
slow settling in these holds; Before has less immediate frame-to-frame change on tracked visible features.
The new `neural_temporal.py` separates movement across the screen from changes in the added NR colour.
C is gentler, not universally steadier; no default changed. NR-14 remains open. No claim that a stable
whole-frame average establishes stable local colour or NVIDIA's internal mask.

NR-15 (found 22:38): stage readbacks expose Unity HDRP's no-motion marker (X > 1) entering NR as thousands
of pixels of motion in small regions. Unity 6000.0's `BuiltinData.hlsl` decodes that marker to zero.
Corrected/deployed at 22:57 for observed Unity SR only, preserving generic/RR semantics. All 12 NR takes now have
held motion below 0.00361 pixel; face exactly 0. Both placements tested. NR-14's broad colour changes remain.
The shared FG/BU adapters also copy raw vectors; audit their encoding contracts separately before changing
them or the future PT guide publisher. This is not yet established as the cause of NR-14's broad colour shifts.

Latest follow-up: NR-11 keeps Before Upscaling as a performance option, with its own measured tuning if needed.
NR-14: owner sees temporal instability in colour/exposure/effect coverage during certain tests. Existing global
RGB/brightness curves and evaluation/reset counters do not measure patchy changes or internal model masks.
Add local paired-Off temporal heatmaps, including the stationary hold, before claiming stability. Exposure is
now measured per frame: two stage runs show constant exposure and colour changes already at NR output.
See the final section of DLSS5-TUNING. NR-15's marker fix works, but NR-14's broad colour changes remain.
Run 232925 adds identical-input replay at both placements, with/without one history reset request. All eight
replay takes retain constant colour/depth with zero motion, yet NR output still changes; final 32-frame local
ranges are 1.5-4.3 encoded RGB levels. Changing input/exposure alone cannot explain it. This does not settle
whether the remaining cause is the runtime's temporal response or our input contract. The 600-frame follow-up
234234 also retains constant inputs and still varies: last-32 ranges After 1.368/Before 2.056. Next HDR-to-model
conversion/profile validation. Initial launch blocked by Starfield; retry completed, FoA closed, no waiter.
NR-12 researches ShortFuse's DLSS Tool controls. NR-13 adds a real close-up face comparison, still and moving,
Off versus A/B/C at both placements, including automatic mask and skin-detail comparisons. The old Ron frames
show his upper body, not a close-up; move the harness camera closer and verify the resulting view before grading.
Mask Off is the NVIDIA sample's baseline, not a universal NVIDIA recommendation. Do not call it the best setting
without the face comparison. Shared actual NGX input observation is installed in 1.2.3 and validated in game.

Clarification and next task: the movement issue is brightness and colour changing. Before-upscaling is requested
for performance and because players will ask for it. "Model" means NVIDIA's A/B/C Neural Rendering variants,
not the DLL version. The owner explicitly requests automated in-game Off/On and movement captures, atlases and
tuning through the PT-style harness. Run those comparisons and record actual model controls, input/output images,
timings and limitations; a successful evaluation is not a visual-quality result.
Owner subsequently permits overlapping GPU tests for these runs: "so long as Tainted Grail or Starfield isn't
already running, just build, deploy, and play it. We'll live with the poor framerates." The harness's explicit
`-AllowGpuContention` option retains both game-process checks and records interfering GPU use. Default behaviour
for other work retains the five-second idle gate. Owner reiterates A/B before/after upscaling and Off/On.

| ID | Owner's request | State / evidence / next step |
|---|---|---|
| NR-01 | "It looks like it changes when I move around a bit?" | Owner wants the full lighting/colour effect. Corrected/deployed before lens effects at 20:44. Eight tavern takes pass continuous evaluation and ordering; the movement-stop brightness jump drops from +1.130 to -0.163 display levels at full tone. View-dependent grading remains; owner verdict pending. |
| NR-02 | "The menu setting should be called Neural Rendering: Off / DLSS" | Implemented in Video and FoA. Existing On is retained in config and labelled DLSS through the same ChoiceLabels mechanism as BU. Both game-version builds pass; Mono plugin deployed/hash-verified at 17:43. Evidence: `build/nr/label-deployment.json`. No native/shared/PT files replaced. |
| NR-03 | "It should expose a setting for controlling whether it sits before or after upscaling." | Both placements implemented. Since 20:44 they bracket SR/RR before lens effects and use the HDR adapter. Before evaluates 1924x1084 versus After 3840x2160; about 3.6 versus 11-12 ms model time in the quiet diagnostic, excluding transfer/game cost. RR forces After and restores the preference. No real-frame speed verdict yet. |
| NR-04 | "Is there a way we can turn it to 11 so I can stress the model?" | A/B/C, Local Tone, Detail, Automatic Mask and Skin Detail implemented/deployed; ranges stay within 0–1. Full-strength A/B/C and masking captured in three scenes. Set Strength/Tone/Detail to 1 for the full effect; no unsupported extrapolation. |
| NR-05 | "How did you select which model to use and how to tune it?" | A/B/C map to Style 0/1/2 in signed 310.8.0. New 60-take Ron/tavern/forest comparison completed at 20:58 after correcting placement. Eleven report warnings remain; exclude the final three forest movement takes. No winner selected; A and full tone/detail/strength remain the defaults. |
| NR-06 | Discovered: static restoration could compare different camera poses. | Analysis rejects different static/moving poses and FOV. Eight NR evidence tests detect nine deliberate faults. Latest diagnostic restoration passes; model run still flags small Ron/tavern Off differences and larger forest neutral/Off differences. Thresholds unchanged. |
| NR-07 | "Shouldn't DLSS 5 run before Motion Blur by the way?" | Implemented/deployed 20:44: both choices precede DoF, motion blur, chromatic aberration and vignette. Full-tone blur/hold diagnostic passes all eight takes, with NR evaluated and recorded before lens effects on every captured NR frame. This substantially reduces the stop jump; it is not proof of universal motion stability. |
| NR-08 | RenoDX HDR interplay; chromatic aberration and vignette after NR; both placement choices before these effects. | Implemented before HDRP lens effects and tone mapping on both sides of SR/RR. Reviewed RenoDX Unity HDR source: its grading/display mapping and final output encoding follow this hook; format upgrades happen earlier. Actual coexistence remains untested. Public NR add-on implementation was not found. |
| NR-09 | "Oh, should it be sitting before fog too, I wonder?" | Owner clarifies PT renders its own fog. Contract: NR sees the completed scene from the active renderer, including that renderer's fog; never double-compose HDRP fog over PT. Fog remains visible in the inspected game-rendered captures. PT's new volume path still needs separate validation; linked geometry-guide gap NR-10 / PT-72. |

| NR-10 | Discovered: PT colour and NR geometry guides can belong to different renderers. | Open, dependency on PT migration (PT-72). NR reads HDRP depth/motion; PT currently exports material/RR guides but no own depth/motion packet. Publish and consume synchronized PT geometry guides before declaring compatibility. Not established as the cause of the PT-Off movement report. |

The owner permits reading RenoDX DLSS source for inspiration. Its public hook framework was found; the actual
NR add-on implementation was not found in the inspected upstream tree/branches. Do not claim it was reviewed.
Our own HDR mod is explicitly future work, not part of this change. RenoDX compatibility remains in scope.

Runtime evidence read from the game's bridge log: signed model hash E16BCF15..., 3840x2160 evaluation,
roughly 11.5-12.8 ms per model evaluation during the owner's first run, excluding transfers. Loading is confirmed
by BepInEx's `Loading [TG Neural Rendering 1.2.2]` line. These observations do not establish the cause of NR-01.

Primary-source check: NVIDIA's [adapter](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp)
passes NR a display-resolution tone-mapped image and scales render-resolution motion into display pixels.
Its [settings definition](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.hpp) starts at
intensity 1, tone/structure strengths 1, style Default and automatic masking off. Its UI exposes Default and
styles A-G, plus local/global tone, local/skin structure and automatic masking. Strength sliders run 0-1.
The first integration left finer controls at runtime defaults. The later 310.8 adapter maps A/B/C and the supported
tone/detail/mask controls explicitly; it does not assume the sample's style labels or GlobalTone apply to this DLL.

The sample keeps temporal history when changing strength. Our updated adapter now does too, verified by the real
GPU probe. That was not the cause established for the movement report: the first run held Strength at 1.

The owner wants earlier placement for performance and player demand. Its HDR adapter is a deliberate difference
from the default path, with limits and the pending appearance check recorded in [DLSS5-TUNING.md](DLSS5-TUNING.md).
That report holds exact hashes, checks and the completed tavern/castle/forest comparisons. No saves changed;
the wrapper restored the owner's configs. The verified face close-up is `nr-face-20261007` (run 215715),
30 takes: the earlier `nr-sharp-models-20261007` shows Ron's upper body. Two restoration warnings remain.
New local colour maps confirm continuing changes while held still: worst blocks 5.27-9.97 RGB levels across
A/B/C and both placements, versus 0.956 for Off repeated. NR evaluates every captured frame without resets.
This is not a direct read of engine exposure or NVIDIA's internal mask. NR-14 remains open. No run is active.
