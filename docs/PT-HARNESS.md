# In-game capture runner

PT is currently tabled and its managed Unity 6000.3 build is blocked. This reference does not authorize a game
run. [PT-STATE.md](PT-STATE.md) records pending validation; [TESTING.md](TESTING.md) covers broader checks.
The full original test runner investigation and scenario history remain in the [local archive](DEVELOPMENT.md).

## Entry points

- tools/harness/Run-PtHarness.ps1: GPU gate, launch, scenario execution, cleanup and analysis.
- tools/harness/Queue-PtRun.ps1: queued/trial runs after the GPU gate.
- tools/harness/Deploy-PtBuild.ps1: PT plugin/native deployment while preserving released bridge copies.
- tests/ingame/scenarios.json: stable scenario keys, save names, recipes, settings and regions of interest.
- tests/ingame/targets.json: measurement targets.
- src/TGPathTracing.Harness: local runner plugin, excluded from player packages.
- tools/harness/analyze.py: reports; motion_clips.py: motion comparison images.

```powershell
.\tools\harness\Run-PtHarness.ps1 -GpuCheck
.\tools\harness\Run-PtHarness.ps1 -Inventory
# After PT is resumed and a screen-taking run is approved:
.\tools\harness\Queue-PtRun.ps1 -Scenario 'tavern-room@nrd-motion,spec-terrain@nrd-motion' -KeepFrames
```

## Scheduling and preservation

No offline GPU test beside another open game; stop one already running when a game opens. Unknown counters
mean busy/unknown, not idle. The test runner waits for 5 quiet seconds; encoding/decoding alone is excluded from
the idle gate. Keep Sunshine streaming on for comparable performance and record it. Ask for screen control
each time a measurement run needs it. Do not start the game executable/Steam URL directly.

Use one scene/defect per short iteration. PT runs keep FG/NR off; recipes must not enable NR. ReShade state
is controlled externally; the test runner refuses incompatible PT setups. Keep saves/configs backed up/restored
and saving blocked. Never infer a requested save exists from a scenario key: read its actual save mapping.
Autosaves/quick saves can overwrite stand-ins. No save-on-exit is required by the inspected game build.

## Comparisons

A scenario suffix @recipe applies only to that scenario. Recipes run scenes in scenarios.json order, not
necessarily command-line order. Lamp strengths depend on previously loaded scenes, PT-147: compare identical
load sequences. Running steps advance time/weather; frozen steps are needed for matched illumination.

Motion steps require disableMotionBlur:true; verify motionBlurActive:false in frame metadata. The first
nrd-motion run was invalidated for blur. Step pictures are taken at walk start; walk-end.png is the endpoint.
Give native-resolution images and file paths for close inspection. Use at least16 frames for counts that
decide a change; a4-frame estimate previously misled.

Check exposure/gain/storage reference alongside radiance and denoiser comparisons. Foreground state,
streaming, compile warmup and competing GPU work affect frame time. A frozen comparison cannot validate
history during motion. Compare raw/NRD before RR; offline fixtures do not test RR.

## Output and cleanup

Runs write artifacts/harness/<run>/ with reports, metadata, logs, overview/atlas and retained frames.
Inspect images, not only scalar scores. Exit1 with flagged metrics can still mean capture completed.
Read native logs after the last extension bootstrap, because they append across game launches.
RUNNING locks are taken before waiting; dead process locks are recoverable. A STOP file also stops an
active run, so do not leave it behind while queuing work.

Clear-PtHarnessRuns.ps1 previews reclamation by default; -Apply removes older raw frames/packages/worker
builds under its documented retention rules, retaining reports and protected release packages. Do not
delete current evidence or the private historical archive. Cold shader/driver startup can take minutes;
warmup timing is not steady-state game performance.
