# Neural Rendering validation

Recorded evidence, not new tests from the2026-11-10 documentation cleanup.

## Maintained1.2.4 baseline,2026-10-30

Both game branches ran DLSS/FSR/XeSS FG, FSR/XeSS upscaling and signed NR on RTX 5080. Normal-version probe
recorded929 FSR frames,858 XeSS frames,367 NR evaluations and zero recorded failures. Mono NR measured6.4ms
GPU at 4K in the recorded scene. DLSS automatic-size correction was then checked on both branches.

121 source checks,16 CPU/native checks and maintained-mod GPU smoke tests passed in that session.
PT was excluded; no RR run. No community-model run on this build. Do not generalize to other hardware.

## Earlier1.2.3 evidence

Official/community standalone rendering, SDR/HDR/control/resize/Off and shared RR/NR cases passed on5080.
Normal-version startup/Options fixes passed 20 gameplay pause/options/apply/resume cycles. FG/BU/NR combinations
were checked on normal and Mono; six packages were verified. ReShade/RenoDX final-output capture used FP16
scRGB, with no invalid/fallback pixels; neutral closely matched Off.

## Unresolved

- Full-strength temporal/movement instability remains; neutral/range fixes do not close it.
- Community runtimes on RTX 20/30/40-series remain unverified.
- AMD/Intel-specific FG/upscaling/latency capabilities require actual runtime/hardware evidence.
- Use the dated test matrix and archived runs to assess coverage; earlier reports describe narrower states.
- Current source preparation is CPU/document validation only. No new game/hardware claim is made.

See [DLSS5-TUNING.md](DLSS5-TUNING.md) for measured hypotheses and [DLSS5-FEEDBACK.md](DLSS5-FEEDBACK.md) for IDs.
