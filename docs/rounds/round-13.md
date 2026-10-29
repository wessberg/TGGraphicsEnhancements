# Round 13 - 2026-10-08 - The actual colour entering Quality NR

Owner asks: "So can we do anything else at this point then?" NR-14 remains the release blocker, NR-27
tracks this follow-up. Current exposure1 and the face close-up look are owner-approved. Full Local Tone
stays1, nativeStyle0 (displayed Default), one pass, maskOff, Quality after upscaling.

## What is different about this check

R11 confirmed exact DLSS guide copies and independent camera/depth motion. Its independent image tracking
used the colour going INTO SR. Quality NR uses SR OUTPUT. The same-frame observer/resource identity and
queue ordering support correct timing, but they are not a measurement of those output pixels. Test that
boundary before blaming the model or changing guide alignment. No timing defect is claimed.

Native evaluate now calls the existing diagnostic image writer for `nr-source` immediately before colour
conversion/evaluation, and `nr-result` after the NR result has been restored into scene colour. Both use
the actual colour resource/viewport. The writer is inactive in normal play and retains six frames per
take, three adjacent pairs, as before. Existing raw DLSS colour/depth/motion captures remain separate.
No shader, NR parameter, placement, ABI, default or model change.

New `tools/harness/neural_boundary.py`:
- Validates gameplay, route, frame/camera identities, extents and exact guide copies first.
- Reads the actual NR-boundary colour at full resolution, downsamples on CPU to guide resolution for
  analysis. Units explicitly remain render-resolution pixels.
- Tracks SR output between frames independently of motion, including an interior-only statistic excluding
  depth edges. Uniform residual is reported and NOT silently fitted away.
- Compares same-frame pre-SR colour to SR output with recorded jitter, versus the previous-frame colour
  without motion as a deliberately wrong frame hypothesis.
- Reports a previous-frame motion alternative, whose sample locations are approximate, not an exact
  simulation of a delayed renderer. Tracks NR output separately to distinguish newly generated variation.
- Produces `nr-boundary-audit.json` and `boundary-<frame>.jpg`.

The analysis compiles but has NOT yet processed real boundary captures. Optical tracking has limitations
at disocclusions, repeated textures and model-generated structure. Do not overstate its future verdict.

## Built, not installed

`build-ranges.cmd` builds only NR and its probe in `build/nr`, then installs to `build/nr/install`.
The sandbox could not access the WinGet CMake executable. The authorized outside-sandbox build succeeds.
Log `.testruns/nr/boundary-build.log`. Source95 passes (`r13-source-checks.log`). No GPU test runs.
Built/scratch native hashAACCD74648CC9E81E732B817A12D5C3522294DA8887EA57BF61F4951058D572E.
Installed native remains1A3C400263646D66D7D613E881590FFEFC9E35D1E42275EFEB1861BB54DE5534.

Deployment script refused before modifying files because Starfield was running. Later another session
took the shared harness and started FoA. Observed owner PID47536, lock `20261008-175533`, FoA PID43476.
Do not steal that lock or close its game. No NR waiter is queued and no new NR binary is installed.
The installed NR pair remains the16:10 build recorded in R11/R12 and tuning-deployment.json.
No PT/FG/BU/bridge changes, package, commit or push.

## Earlier proposal, now rejected

Explained that an optional temporal filter could follow surfaces with motion and smooth abrupt changes in
NR's lighting edits while retaining full Local Tone. That may introduce trails or delayed reactions to real
lighting changes. It is only a proposal, not implemented, not approved as a default and not proven useful.
Complete the actual input-alignment check first. No broad preset/exposure sweep and no claim that current
models necessarily cannot render this scene stably.

## Follow-up: the proposed extra temporal filter

Owner: "surely that isn't what NVIDIA recommends" and "We can try, but sounds like we'll be building our
own temporally stable NR layer on top of what is supposedly already a temporally stable NR solution".
This initially permitted a possible later trial. The final ruling below supersedes that permission.

Checked primary public sources again:
- NVIDIA's [DLSS5 description](https://www.nvidia.com/en-gb/geforce/news/dlss-5-3d-guided-neural-rendering/)
  describes stability supplied by the model using engine motion vectors and a straightforward Streamline
  integration. This is a product description, not a complete input contract for our supplied runtime.
- NVIDIA's [sample evaluateNr](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp#L461-L589)
  provides colour/depth/motion, controls and reset, evaluates NR, then copies its output back. There is no
  separate application temporal filter in that path. No recommendation for our proposed filter was found.

Corrected the framing: our proposed filter would be an optional workaround with its own trails/lag risks.
It must not substitute for the pending input audit or be presented as a normal required NVIDIA step.
We have not established an unavoidable model limitation. No filter implemented and no rendering/default
change in this follow-up. Keep the owner-approved exposure and face look. Documentation-only clarification.

## Final owner ruling: no extra filter

"Yeah, let's not do our own extra filter thing on top."
Do not implement the proposed extra temporal filter, even as an optional trial. None was implemented,
so there is no filter code to remove. Continue the native integration/input audit. Earlier discussion of
a possible trial is superseded. No rendering/default changes. Another session still holds the harness
at this follow-up (`52608 held-run 2026-10-08T18:22:37`). Do not interfere with it.

## ▶ Starter prompt (R14)

Read R11/R12 before rerunning any old tests. When both games are closed and no other session owns the
shared harness, deploy the prepared NR diagnostic build using `.testruns/nr/deploy-tuning.ps1`. It backs up,
replaces Vortex files and checks build/scratch/game hashes. Tell the owner to retain the changed files.
Then run ONE scene through the normal GPU-free gate:

```powershell
.\tools\harness\Run-PtHarness.ps1 -Plan build/nr/boundary-plan.json -Out artifacts/harness/nr-boundary-20261008 -KeepFrames -TimeoutMinutes 8 -GpuWaitMinutes 2
python tools/harness/neural_boundary.py artifacts/harness/nr-boundary-20261008
```

The plan is128 full-NR frames, four metres at1.2m/s, normal exposure1, full controls, PT/FG off and lens
effects suppressed equally with previous audits. It must capture all six exact nr-source/nr-result frames.
Read the atlas and inspect outliers before deciding whether alignment is correct. New hypothesis concerns
upscaled colour, not another attempt to flip the already verified raw guides. If the run fails readiness,
exclude it. Keep117 saves/configs untouched. Existing installed game may be in another agent's test session.
