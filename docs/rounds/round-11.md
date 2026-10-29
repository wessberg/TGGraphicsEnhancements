# Round 11 - NR input audit and NVIDIA style names - 2026-10-08

Owner's15:13 tavern recording reopens stability as a release blocker. Walking changes lighting much more
than turning. Latest targets include stairs, plants and wall shields, not only chairs. Do not conclude
that RenoDX showing the same defect proves NR cannot work correctly. Current focus remains Quality.

## What was inspected and corrected

The775-frame13.37s3840x2160 recording was extracted and all three half-second atlases inspected.
Evidence and contemporaneous logs: `artifacts/harness/nr-owner-video-20261008-151300`.
Video-time settings: nativeStyle0, full strength/tone/detail/skin, mask0, one pass. Reset count stays4.
The later config's Detail0 is not evidence of the video's settings. PT/FG off. No new blame assigned.

Current official NVIDIA sample uses Style Default/A/B/C=0/1/2/3. A separate creation preset hint exists.
Owner explicitly rejects an Auto/recommended interpretation of Default: use the actual native Default.
Previously the menu called Style0/1/2 A/B/C. The corrected labels preserve stored numeric selections:
legacy config Model A/B/C/D maps0/1/2/3, displayed Default/A/B/C in Video and FoA Mod Manager. No second
selector with unproven effect. Tooltips use plain sentences without semicolons. Default rendering stays0.

Signed310.8 GPU probes `build/nr/profile-check/comparison.json` and `style-c-check/comparison.json`:
24frames per case. Style0 vs1 mean difference0.0682619,0 vs2=0.0276031. Live style change plus reset is
bit-identical to creating that style fresh. Creation preset hints0/1/2/3 give identical pixels. Style3
is accepted and matches2 on this synthetic input. Default0 repeat is exact. This does not establish
that3 always equals2, or that these styles are separate network weights. No creation-only-style bug found.

## Per-pixel depth and motion audit

Native diagnostics now save original DLSS colour/depth/motion plus actual converted NR depth/motion
at full guide resolution for three adjacent frame pairs. The camera/depth prediction is independent
of the motion texture. Image tracking is independent of both, with observed projection jitter reported.
File reader handles the game's DXGI26 packed HDR colour,19 depth/stencil and33 typeless half motion.
Depth/stencil is copied as a whole subresource before cropping on CPU. Errors stop the take.

Two attempts are not input evidence:
-155904, `nr-guide-audit-20261008`: new capture reader lacked depth/stencil support. Later left gameplay
  at03-full, so no graded comparison. Saves117 unchanged/configs restored.
-160609, `nr-guide-walk-20261008`: depth captured but reader lacked typeless motion. Gameplay takes
  complete and1.2m/s route validates after allowing the initial already-rendering frame. Not a valid
  guide audit. Do not cite the general report's0 problems as proof of complete native capture.

Valid run161014, `artifacts/harness/nr-guide-walk-final-20261008`: two128-frame takes, Quality neutral
and full, PT/FG off, lens effects suppressed. Fixed1.2m/s,4m forward then hold. Camera route is verified
against per-render timestamps, not assumed from a frame count. No frame-index colour comparison across
these takes because their poses differ with timing. Guide screenshots and atlas inspected directly.

Results in `nr-guide-audit.json`, `nr-stages.md` and per-take `guides-<frame>.jpg`:
- All12 captured guide frames match the actual DLSS originals exactly after vertical flip/marker
  decoding and the documented half-float destination precision. No no-motion markers in this scene.
- Each geometry comparison covers2,085,616 pixels (1924x1084). Six moving pairs give median errors
  0.00784..0.01639 input pixels, p95<=0.04130. Depth-edge medians0.00315..0.00762, p95<=0.02702.
- Wrong axis signs or half/double scaling give median errors6.59..25.13 pixels. Actual motion medians
  13.17..17.42pixels. This is substantial translation, not a near-zero-motion test.
- Raw depth is reversed hardware depth, near0.01/far5000. No logarithmic or linear conversion is sent
  to NR. Linearisation is only in the independent analysis. Outlines align with stairs/beams/furniture.
- Independent image tracking's uniform residual agrees with measured jitter: Xabout+0.5pixel,
  Yopposite the recorded jitter delta. Residual-corrected median0.187..0.213pixels. Tracking has large
  tail errors at repeated textures/disocclusions, so do not describe every track as correct.
- NR stages: both128-frame takes valid, neutral exact, held exposure stable, zero HDR fallback pixels.
  Full-NR held effect range2.27encodedRGB levels. No visible stability fix claimed.

This strongly supports correct SR inputs on this frozen tavern route. It does not prove moving-object
vectors, all materials, PT/RR geometry or every scene. PT-72 remains open. The model still needs colour
contract validation. No production motion/depth adjustment was made from this audit.

## Built and installed

Both NR managed runtimes and the Mono harness compile. Native isolated build/install succeeds.
Short signed-model probe validates deferred early guides/late colour, exact late neutral output, full
output change and diagnostic-only scope. Final Source95 and the four camera-route CPU checks pass.
No broad GPU suite or mutation campaign while stability is wrong. No bridge/PT/FG/BU/model files deployed.
Installed16:10 NR Mono75E05F513FF405370EC87F3E8E0678EDB90311F617FEA7D23FC1724DAD1E4B98,
native1A3C400263646D66D7D613E881590FFEFC9E35D1E42275EFEB1861BB54DE5534.
Build, scratch install and game hashes match. Vortex must retain these files. No zip/commit/push.

## Actual game-grading comparison

Harness-only `neuralPostToneMap` defers evaluation to HDRP UberPass output beforeFinalPass/UI, using the
same copied DLSS depth/motion. It omits our HDR compression/restoration and encodes the game's graded
linear SDR once. All lens/bloom/grain effects are suppressed equally per frame, no saved setting edits.
This does not change either player placement. It is not HDR support.

Run161628, `artifacts/harness/nr-graded-walk-20261008`: three128-frame takes, current/full,
graded/neutral, graded/full. All valid, identical settings and1.2m/s route, exact graded neutral,
steady exposure. Correct late placement and suppressed lens effects recorded.
Read `graded-comparison.jpg` (inspected), `graded-comparison.json`, `nr-stages.md` and `.testruns/nr/graded-report.py`.
Frames selected by nearest measured travel distance are for visual comparison, not identical-pose proof.
The same held endpoint allows the final32-frame screen range comparison.

| Measure | Current full | Graded neutral | Graded full |
|---|---:|---:|---:|
| Mean absolute model effect, encodedRGB levels | 4.813 | 0 | 2.249 |
| Moving warped-effect p95, coarse64x36 grid | 2.692 | 0 | 1.619 |
| Final32 screen block range p95,16x16 blocks | 1.242 | 0.801 | 1.059 |
| Final32 screen block max range | 2.211 | 1.191 | 1.449 |
| Final32 model-effect max cell range | 2.973 | 0 | 1.327 |

The graded image changes less, but its measured effect is also about half as large. The normalized
moving variation is not lower (2.692/4.813 vs1.619/2.249). This is not proof of better stability at the
settled look and not grounds to change defaults. Graded colour shifts the look closer to vanilla in
the contact sheet. Coarse motion compensation is limited at object edges. No universal quality verdict.
The successful test rules out a total failure of the late diagnostic or a neutral conversion error.
It does not settle colour-space correctness in every HDR setup.

Run161628 quit16:18:13. Both valid runs leave117 saves untouched and restore configs. No NR waiter or
shared lock at final check. Normal rendering remains nativeStyle0/Quality/full/onepass.

NVIDIA sample's additional inputs: per-material ControlMask (currently omitted) and GlobalToneStrength
control (not set by our310.8 adapter, its effect needs a version-specific test). Sample SR uses raw
reversed hardware depth; RR uses linearViewZ. It exposes no scene-light list. Reference:
https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp#L475-L559
Do not invent normals/material/light inputs or claim optional masking fixes temporal history.

## ▶ Starter prompt (R12)

Read final R11 grading outcome before planning more tests. NR-14 still blocks release. NativeStyle0,
full controls, maskOff, one pass remain settled unless new visual evidence supports a correction.
The per-pixel SR guide audit is strong evidence against a broad sign/scale/flip/depth-format bug.
Keep its limitations. Do not rerun a broad style sweep or the failed per-channel Reinhard comparison.
Use short individual scene runs, fixed real walking speed when reproducing owner movement, normal
GPU-free gate, both games closed and shared harness ownership. Preserve parallel PT edits and117 saves.
