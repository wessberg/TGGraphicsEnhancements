# Round 14 - 2026-10-08 - Quality NR input alignment checked in game

Owner: "ok do tht", authorizing the prepared post-SR audit. No extra temporal filter is authorized.

## Result

The actual image entering Quality NR agrees with the supplied motion and current render frame.
No evidence of a whole-frame delay, wrong motion sign/scale, or persistent jitter offset was found.
This is an alignment finding, not a lighting fix or a complete NR integration certification. NR-14 remains
open. Exposure1, native Style0 (Default), full controls, maskOff and one pass stay unchanged.

Run `artifacts/harness/nr-boundary-20261008`, ID182643: one Ron's Tavern take,128 valid evaluations.
Four metres in3.339s at1.2m/s, then hold. Both games/shared harness were free at start, normal GPU gate,
no competing GPU work. PT/FG/ReShade off, frozen world, lens effects suppressed as in prior audits.
Six3840x2160 captures each of real NR input/result, plus exact DLSS colour/depth/motion. Frame/route
checks pass, raw guide copies match every pixel after intended conversion. Game closed18:28,
all117 saves untouched, configs restored, shared lock released. No waiter remains.

## Measurements

Errors use1924x1084 render pixels. Actual3840x2160 NR colour is downsampled on CPU to guide resolution.
Tracking is independent of supplied motion. Interior excludes5x5 depth discontinuities, with limits below.

| Current frame | Interior tracking median / p95 | Previous-frame vectors median | Same-frame raw/SR alignment median |
| --- | --- | --- | --- |
|1320|0.139 /0.474px|9.699px|0.065px|
|1328|0.143 /0.459px|10.015px|0.060px|
|1336|0.155 /0.495px|10.636px|0.064px|

Raw/SR alignment accounts for recorded raw input jitter. Uniform temporal residual stays within0.030px
per axis, without fitting it away. Deliberately wrong signs/scales give median8.12..28.58px. Previous-frame
raw colour without motion compensation differs by about13..16px. These are diagnostic alternatives only.
Independent camera/depth prediction agrees with supplied vectors within0.008..0.016px median across
2,085,616 pixels. Depth-edge p95 is0.024..0.038px.

## Outliers inspected, retained in the report

Near-stair interior p95 is12.36..16.72px, despite medians0.176..0.355px. Inspected all18 largest outlier
crops in `boundary-outliers-1320/1328/1336.jpg`. They occur around foreground posts, rails, cloth/rope
and surfaces behind them. Image tracking can follow foreground while the centre's motion describes
background. Repeated stair patterns also confuse matching. A5x5 exclusion does not exclude the21px
optical-flow window or its pyramid. "Interior" is not a guarantee of a single tracked surface.
Camera/depth agreement remains accurate there. These outliers do not establish a guide defect.

Wall-shield interior medians0.089..0.107px, p95 0.390..0.511px. Far stairs medians0.070..0.099px,
p95 6.20px in the first pair (only12 points),0.20..0.23px in the others. Counts are retained.
NR output geometry tracks with median0.147..0.156px. That does not mean its lighting is stable.
This covers static tavern geometry through SR, not animation, PT/RR, native HDR, every disocclusion or
every runtime parameter. The requested alignment audit is complete without a rendering change.

## Changes and validation

Installed R13 diagnostic-only native18:26 with `.testruns/nr/deploy-tuning.ps1`. Backup, Vortex file
replacement, installed manifest and build/scratch/game hash checks pass. Owner told to retain changes.
Managed NR unchanged. Other mods untouched, including the PT agent's newer native.
- Native: `AACCD74648CC9E81E732B817A12D5C3522294DA8887EA57BF61F4951058D572E`
- Mono: `75E05F513FF405370EC87F3E8E0678EDB90311F617FEA7D23FC1724DAD1E4B98`
- Backup: `BepInEx/TGNeuralRendering/Backups/20261008-162638-110-tuning`.

Extended `neural_boundary.py` with wrong-sign/scale alternatives, region counts, largest outlier data
and marked crops. CPU analysis only. Boundary/guide analyses complete. Inspected boundary1328, gameplay
atlas and all outlier sheets. Source95 passes (`.testruns/nr/r14-source-checks.log`). No broad GPU suite,
package, commit or push. Results: `nr-boundary-audit.json`, `nr-guide-audit.json` beside run `report.md`.

## ▶ Starter prompt (R15)

Requested post-SR alignment check is complete. Report it without claiming a lighting fix. No further
sign/scale/timing sweep without new evidence. No extra filter. Preserve the approved exposure/face look.
Before another run check both games/shared harness because PT work continues independently. NR-14 is
still a release blocker. Any next investigation must identify a remaining version-specific NR contract or
colour/history question not answered here. Read R11/R12 first: guide consumption, resets, exposure,
actual game grading and GlobalTone were already investigated. Do not repeat those comparisons blindly.
