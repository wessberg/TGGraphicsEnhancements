# Neural Rendering measurements and limits

These are retained integration findings, not a claim of resolved motion instability.
Full runs, images, controls, hashes and chronology remain archived at this same document path.

- Frozen full-strength output varied more than Off even with held inputs and long settling. RenoDX Upscaled
  and Present comparisons also showed instability; placement alone did not explain it.
- Exact neutral colour handling was corrected. HDR glare/range failures were corrected with bounds that old
  captures fail. Do not treat those fixes as a solution for full-model movement instability.
- Correct motion consumption was demonstrated in a translated-photo fixture: block change 0.383 with motion,
  2.684 without,2.967 with reset-each. Exact repeat held. A planar depth-omission match does not prove depth unused.
- Post-SR guide alignment agreed within subpixel errors in the recorded interior; wrong previous-frame motion
  was roughly 10px. Multi-depth stair outliers were ambiguous, not evidence for a global sign/scale/delay patch.
- Per-channel Reinhard compression comparison produced p95 changes1.965/1.977/1.983 across baseline/alternate/
  repeat, within repeated-baseline variation. No meaningful stability improvement; alternate stays diagnostic.
- Additional temporal filtering was rejected. Preserve the settled appearance/exposure while investigating inputs.
- Model input range, invalid/fallback pixels, output HDR range, frozen variation and moving-scene stability are
  different metrics. A finite output or successful evaluation cannot establish stable appearance.

Current hardware/runtime boundaries: [DLSS5-VALIDATION.md](DLSS5-VALIDATION.md).
