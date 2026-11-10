# Maintained-mod integration notes

Baseline: version 1.2.4, Unity 6000.3.21f1/HDRP 17. These notes retain integration lessons; detailed investigations remain in the local archive.

## Frame Generation

All three backends need the measured input orientation correction; projection matrices do not eliminate it. Separate HUD-less colour from UI and keep game/rendered/presented frame identity consistent. DLSS-G does not generate while the game is behind another window; this explains background No FPS gain reports.

Use runtime capabilities for modes/multipliers/latency. Preserve the single Low Latency selector. Documented exclusive-fullscreen conflicts use the game's own borderless setting and disable the conflicting row. Do not fight Screen.SetResolution every frame.

DLSS FG Quality/Performance changes a create-time parameter, `DLSSG.LiteCandidateBlendingEnabled`; Streamline exposes no direct option. The NGX create hook and one-frame feature recreation implement it. Recorded foreground streamed A/B/A tests found a small rendered-rate gain; generated-frame appearance requires its own review.

## Upscaling

Unity 6000.3 changed CustomPass.ExecuteInternal's signature. Shared/CustomPassRecorder.cs handles Mono/IL2CPP; compilation alone did not catch the earlier reflected-call failure. FSR/XeSS silently fell back before this fix.

HDRP created DLSS at its first full-size frame and retained that size after render resolution fell. TGUP_GetDlssSize plus StpHost.TickDlssSize trigger automatic recreation and restore the selected mode. Both branches were tested on 10-30; do not reintroduce manual toggling.

DLAA is always 100%, even when NGX's optimal-settings query returns 33.3%. Vendor quality migration rounds down. STP keeps render scale; FSR keeps its own sharpening/default runtime. RR uses the shared bridge device and its own optimal-size query.

## Normal version

Hook signatures and unmanaged retention matter independently of a successful build. Retain native child-row arrays through Options/menu rebuilds; recycled row memory caused the 1.2.3 Options crash. Twenty gameplay pause/options/apply/resume cycles passed after the fix.

Changing Steam branch changes assemblies used by local builds. An old BepInEx/interop directory is not evidence of the current API. Verify actual installed branch and loader, back up real files rather than Vortex links, and restore by replacement rather than merge.

## Neural Rendering

Signed SDK runtimes use normal NGX dispatch; compatible modified runtimes automatically use the isolated community adapter. No per-file allowlist. Runtime exports/API/support determine acceptance; hashes remain diagnostics. Other NGX consumers retain their trust policy.

The 310.9.16 signed model is bundled under the existing release policy; patched files must be reapplied after updates. Model/input/HDR support and hardware validation are separate claims. RTX 5080 tests do not prove RTX 20/30/40-series execution. Motion instability remains documented; no extra temporal filter was accepted.
