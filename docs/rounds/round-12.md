# Round 12 - 2026-10-08 - Does the signed NR runtime use the guides?

The owner asked us to continue, then whether DLSS5 may simply struggle with this tavern. This is plausible,
not established as a scene-category limitation. NR-14 remains a release blocker. No release pivot or default
change. NativeStyle0, Quality, all strengths1, maskOff and one pass remain the settled look.

## Global Tone

NVIDIA's public sample sends GlobalToneStrength, which our310.8 adapter leaves unset. An isolated build
adds only a test export to set that key. Each case starts a fresh process and feature. The input is a
512x512 crop from actual tavern guide1307, repeated32frames, with the normal HDR adapter and full settings.
Unset,0,0.5,1 and an unset repeat produce bit-identical float output on every frame. This is evidence against
that omitted control explaining this reproduction. It is not proof that every DLL/input ignores it.

Scripts: `.testruns/nr/prepare-global-tone.py`, `build-global-tone.cmd`, `run-global-tone.ps1`.
Data: `build/nr/global-tone-check/comparison.json`. No production export or menu row added.

## Guide consumption

The per-pixel audit in R11 proved which values reach NR. This probe asks whether changing or removing them
changes the actual model output. It uses an isolated copy of the production adapter with test-only overrides.
The signed310.8 DLL and shared bridge are unchanged. All cases start fresh processes/features.

Input: actual recorded tavern colour/depth, cropped from guide1307. Eight static frames,24frames sliding
the crop two pixels per frame, then eight held. Correct current-to-previous motion is exactly+2 pixels on X
during movement, represented by rawX=-2/512 and the normal NR scale -512. No model output feeds back into
the input. This is a planar image translation, not a 3D walking reproduction or full-resolution game replay.
It isolates input consumption, with no change to the world lighting or photographed surface pixels.

Five full-setting cases: guides present, motion omitted, depth omitted, history reset each frame, guided
repeat. Three diagnostic tone cases repeat guided/no-motion/reset-each with BOTH local and skin detail0,
LocalTone1. An initial detail0/skin1 pass gave the same results, then was replaced by this cleaner isolation.

Results:
- Motion removal changes output starting on frame1. Frame0, whose history is reset, is identical.
- Repeated resets also change output. The normally guided repeat is bit-identical across all40frames.
- Removing depth produces bit-identical output on this fixture. Do not claim depth is never consumed, remove
  it from production, or interpret a planar crop as a test of new surfaces appearing behind foreground objects.
- Motion and reset also affect the tone-only case. Therefore the sample's reference to stabilising local
  structure does NOT justify claiming that lighting/colour receives no temporal guidance in this DLL.

Analysis tracks exactly the same surface pixels using the known integer translation. It excludes exposed
edges and32-pixel margins. It undoes only the HDR adapter's input shoulder, then measures encoded model
colour in16x16blocks. Units below are encoded RGB levels out of255, averaged over the three channels.

| Case | Moving step p95 | Moving range p95 | Maximum range | Mean NR effect |
|---|---:|---:|---:|---:|
| Full, correct motion | 0.3833 | 3.5127 | 5.6321 | 7.0879 |
| Full, no motion | 2.6835 | 8.1211 | 11.8882 | 6.8308 |
| Full, reset every frame | 2.9669 | 8.9664 | 14.6297 | 6.7888 |
| Tone only, correct motion | 0.3828 | 3.0286 | 3.8490 | 3.7429 |
| Tone only, no motion | 1.1567 | 3.6225 | 5.7499 | 3.0877 |
| Tone only, reset every frame | 1.1286 | 3.4887 | 5.3533 | 3.0429 |

Correct motion reduces abrupt changes by about7x in the full-setting fixture and3x in tone-only. This is a
comparison with intentionally missing guides, NOT a7x improvement over the installed mod. Normal play already
supplies them. The generated look still changes across the translated view with correct motion. It is consistent
with functioning temporal processing that smooths, but does not eliminate, changes in the model's interpretation.
It does not prove the production colour contract ideal or NVIDIA incapable of improving this scene.

`build/nr/guide-use-check/comparison.json` includes exact float differences and per-frame errors.
`same-surface.jpg` was inspected. Scripts `.testruns/nr/prepare-guide-use.py`, `build-guide-use.cmd`,
`run-guide-use.ps1 -Cases Full|Tone|All` and `analyze-guide-use.py` reproduce it. Preparation copies source
to scratch, never modifies the installed adapter. The current scratch extension zeros both detail controls
for the tone cases. Full cases retain the production settings.

## Input exposure follow-up

Owner: "What about exposure...I know renodx dlss 5 has an exposure slider that affects the image given to NR?"
Then: "Generally the exposure you have chosen has looked right. The demos you have shown of face closeups
has looked amazing at least". Preserve current exposure1 and that look. This is NR-26, not permission to
call attenuation a stability fix or to change the game's exposure.

The inspected ShortFuse26.1003.2350 UI explicitly describes NR exposure independent of the game texture.
The user's current ReShade.ini has DirectNeuralRenderingExposure1 and ExposureMode1. Public upstream inspected
still does not contain that build's NR implementation. Do not claim exact slider math was source-verified.
Separate public Generic source snapshot9bb6c0fec57ecb28c77938f3feb8155433396d93 was read into ignored
`build/nr/renodx-review/generic-exposure`. Its exposure feed distinguishes applying an exposure texture from
already-exposed colour, and documents problems with its own content-based adaptation loop. Our normal input
scale is fixed1, not such a loop. This is useful comparison code, not evidence about installed ShortFuse.
Reference: https://github.com/PEQHUB/RenoDX-DLSS5-Generic/blob/9bb6c0fec57ecb28c77938f3feb8155433396d93/shaders/v6_exposure_scale.cs_5_1.hlsl
No code copied into our production adapter.

R9 already tested diagnostic input scale.25/1/4 at both placements, also after the glare fix. Those camera-turn
tests sometimes showed less late-held drift at.25, not consistently less change across the whole settling period.
This round checks the owner's WALK instead:1/.25/1, nativeStyle0, full controls, maskOff, one pass, Quality,
four metres at1.2m/s,128frames per take, lens effects suppressed equally. The input multiplier is undone using
the original input shoulder on output. Model, Local Tone and game exposure remain unchanged.

First attempt170215 waited for Starfield/GPU, launched only after the normal five-second quiet gate, then
failed gameplay readiness before saving a frame (UIState, interactiveFalse). Excluded.117 saves/configs restored.
Game.PlayState now also reports application focus, cursor mode and named popups on failure. It does not bypass
readiness or dismiss unknown menus. Retry170616 passes without triggering that additional failure diagnostic,
so the first failure's cause remains unknown. Source95 passes and the harness builds.

Valid `artifacts/harness/nr-exposure-walk-retry-20261008`:384frames, three valid takes, no competing GPU job,
steady held exposure, no HDR fallback. Route validation confirms1.2m/s and4m. Read `exposure-comparison.jpg`
(inspected), `exposure-comparison.json` and `nr-stages.md`. `.testruns/nr/exposure-report.py` reproduces analysis.

| Measurement | Input1 | Input.25 | Input1 repeat |
|---|---:|---:|---:|
| Mean model-space effect | 4.811 | 2.116 | 4.812 |
| Moving model-space effect p95 | 2.746 | 1.360 | 2.720 |
| Mean compensated effect, common scale | 4.765 | 3.839 | 4.765 |
| Moving compensated effect p95, common scale | 2.815 | 2.503 | 2.772 |
| Held final-screen block range p95 | 1.102 | 1.184 | 1.145 |
| Held final-screen block maximum range | 2.203 | 2.281 | 2.219 |

Do NOT report the first two rows as a50% stability improvement. The lower exposure changes model-space units.
The compensated comparison converts both restored scene input/output to one fixed display-like scale before
forming and motion-warping the effect. On that common scale variation falls about10%, effect about19%. Final
screen held variation is slightly worse. No same-look stability win established. Keep input1, consistent with
the owner's verdict. This does not prove every possible exposure value fails. Avoid another broad scale sweep.
These are coarse64x36 model/scene grids and16x16 capture-image blocks, not per-object masks or perfect
reprojection at disocclusions. Fixed walking speed means different frame times give different sampled poses.
Contact-sheet poses are nearest travel distance, not identical-pose pixel comparisons.

## Outcome and limits

No production mod binary, user setting, save or normal rendering code changed. The diagnostic harness was
rebuilt/installed for the game runs. Each short offline probe ran after the GPU check reported free. The game
runs used the normal five-second free gate. Valid run quit17:08:01,117 saves/configs restored. FoA and all NR
waiters are closed, shared lock absent. Starfield is running again at the final check, with no NR job queued.
No broad GPU suite, package, commit or push.
Parallel PT work preserved.
The stale Controls paragraph in DLSS5-TUNING was corrected to match the already deployed NVIDIA names.

NVIDIA sample reference:
https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp#L475-L559
It supplies image/depth/motion and optional masks, not a scene-light list. It specifically mentions local
structure stabilisation. Our observed tone-only response is stronger evidence about signed310.8 than inferring
that other effects cannot use history from that comment. Do not describe this as missing motion processing.

## ▶ Starter prompt (R13)

NR-14 remains open. Do not repeat sign/flip/scaling sweeps, GlobalTone sweeps or this guide-consumption probe
without new evidence. The SR geometry audit is strong and motion genuinely influences NR temporal output.
GlobalTone produced no effect on this runtime/fixture. Depth removal produced no effect on planar translation,
which is not a reason to remove real depth. Quality/full/default look stays. A scene-specific model limitation
is plausible, not a proven universal ceiling. A useful next experiment needs a concrete remaining contract
difference or real-view disocclusion hypothesis, not another broad preset sweep or lowering tone as a fix.
Owner approves current exposure/face look. The new quarter-exposure walk does not establish better stability
at that look, so keep1. Do not compare differently exposed model-space errors without a common output scale.
Use one scene, real walking speed, normal GPU-free gate, preserve PT edits and the owner's117 saves.
