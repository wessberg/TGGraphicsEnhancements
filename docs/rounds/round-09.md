# Round 9 - 2026-10-08 - NR input conditioning and HDR glare fix

The owner said to continue. Full Local Tone remains 1. The previous 600-frame retained-input test
did not establish a vendor defect: our input contract still needs validation. PT work continues independently.

## Diagnostic implementation

Added a harness-only input multiplier, restricted to active stage diagnostics. It scales pre-exposed HDR
before the existing reversible shoulder and divides it out after the inverse shoulder. Normal play uses 1.
This tests sensitivity to input brightness, not a new exposure setting for players or a substitute for the
game's exposure. It records the actual multiplier per frame, resets history on a conversion change, rejects
changes during retained replay and restores 1 when diagnostics end or fail. Frame/status ABI unchanged.

The plan `tools/harness/neural_plan.py --signal` compares 0.25, 1 and 4 at both placements, in the tavern and
the Ron close-up. All model controls stay full strength, A, mask Off. Each take has 48 moving and 128 held
frames after one warm-up cycle. Actual DLSS guides and pre-lens placement remain unchanged.

Both managed targets, the native build and shared harness compile. Source checks 93 pass; stage evidence
checks 13 pass and reject 11 deliberate faults. The real GPU probe verifies the conversion against independent
CPU math, checks its inverse through the nonzero-strength output shader, and checks automatic restoration.
Maximum relative inverse errors are 0.000004894 / 0.000014733 / 0.000065575 at the three scales.
Removing the inverse multiplier deliberately fails the real HDR assertion. Restored build and probe pass.
Logs: `.testruns/nr/signal-*.log`; evidence: `build/nr/signal-probe`, `signal-mutant`, `signal-restored`.

Deployed only the NR pair at 00:25, backed up and hash-checked against build and scratch install:

- Managed: `182C4F803FD8872D7122D5BFE3666278AC2C5C880E052C055202A70E7E5F66DA`.
- Native: `5C0E3D7EB1857D9363515F329BC350AF76FB88061170E79F516F73EF90601BE7`.

## Runs

`artifacts/harness/nr-signal-20261008`, run `20261008-002838`: 16 takes complete in 6.2 minutes, no report
problems or competing GPU work. All 12 NR takes pass stage and route checks. Both screen atlases were inspected.
Brighter model input increases late variation; darker input reduces some ranges and effect strength but is
not a consistent fix. Input scaling stays 1. All 117 save files unchanged; settings restored and game closed.

The tavern atlas exposed NR-17: bright points before upscaling, especially at diagnostic input scale 4.
The old output shader inverted the model's NEW peak. A model value approaching white therefore caused an
unbounded HDR increase. At the ordinary scale 1, the largest final-frame input/output cell averages were
3.36/16.20; at scale 4 they were 3.41/191.37. No model control was above 1. NR-14's variation already present
at model output remains a separate issue, so lower Local Tone is still not a fix.

Changed restoration to use the ORIGINAL input shoulder. For source HDR RGB S and input multiplier k,
the model input before sRGB encoding is C=k*S/(1+k*max(S)). Decoded model result N now restores as
N*(1+k*max(S))/k, applying the model's relative colour gain to the original HDR pixel. The implementation
recovers this factor from retained C, so no extra scene texture copy is needed. Zero strength stays bit-exact.
The existing nonfinite/out-of-range model-pixel guard remains. No new player setting or intensity reduction.

`tests/neural_hdr_capture_test.py` requires full model controls, actual evaluation and matched camera routes.
It checks the conservative measurable cell bound: mean(output_channel) <= mean(sum(source_RGB)) + 1/k,
with 4% tolerance for packed HDR quantization and floating-point transfer. It also requires a nonzero model
effect AND a nonzero scene effect, preventing a bypass from passing. The old capture fails the HDR bound.
The real GPU probe passes the fixed inverse, neutral preservation and all existing cases. Source94 passes
(the concurrent PT work added a check). NR-only deployment at 00:44; native hash
`66563493B08EAE1AA2B75742B6CFCAF2BCCAF1CC61231DFACBA53BBE8F226065`, managed unchanged from 00:25.
The matching 16-take run `20261008-004444`, `artifacts/harness/nr-hdr-gain-20261008`, completed in 9.8 minutes.
All 12 NR cases pass the new HDR bound; maximum bound ratio 0.688857. The old capture fails at 1.995x.
Default tavern Before now has final-frame input/output cell maxima 3.2873/3.16145. These are cell averages,
not individual pixel peaks. Both final-frame atlases were inspected: the stress-case bright white speckles
are gone. The owner has not yet judged the fix. NR-14's model-output variation remains.
The report has no capture failures and one warning: permitted concurrent PT GPU tests reached 97.6%, so
timings are unusable. All 117 saves unchanged, configs restored, FoA closed and the harness lock removed.
This round totals 32 completed takes. Stage analysis, atlases and signal-summary.json are in each run folder.

Prepared `.testruns/nr/model-history-plan.py` and `build/nr/tuning/model-history.json`: A/B/C, both placements,
both scenes, Off bookends, 48 moving and 192 held frames. Retain colour/depth with zero motion and one reset
at the first held input. This separates model responses under our current contract; it cannot certify that
the contract matches a different/newer runtime profile. This plan was NOT run: Starfield started after the
completed comparison, then the owner requested a status report and handoff because tokens were running low.
No NR launcher or waiter remains. At 00:56 Starfield was PID 46916 and FoA was closed.

Read-only FG/BU motion review: their generic copies do not declare Unity's X>1 marker contract for every
source. Do not broaden NR's known-Unity-SR marker decoder to RR/custom guides without explicit source
encoding ownership. No FG/BU changes were made; PT's matching geometry guides remain PT-72.

## Sources rechecked

The [NVIDIA sample](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp) still uses
tone-mapped display input and render-resolution guides scaled to display pixels. Its
[tone mapper](https://github.com/nvpro-samples/nvpro_core2/blob/main/nvshaders/tonemapper.slang) produces sRGB
LDR. The input-brightness study preserves that encoding. Neither this sample nor ShortFuse UI strings
establish our reversible curve as the recommended curve for the older signed 310.8 runtime.

## ▶ Starter prompt (R10)

Read this round, DLSS5-TUNING and the open NR feedback first. Latest installed NR pair is the 00:44 build
above; do not overwrite concurrent PT work. Check current game processes and the shared harness lock before
any new launch. The next prepared comparison is build/nr/tuning/model-history.json, A/B/C with retained
inputs at both placements in tavern and face scenes; it has NOT run. Validate the older runtime's input
contract, keep full Local Tone, and do not select a default by one temporal maximum. NR-14 remains open.
NR-17 passed the new bound and visual inspection, with owner acceptance pending. PT geometry guides remain
PT-72; HDR/ReShade/standalone/normal-version acceptance is still open. No commit, push or release zip requested.
