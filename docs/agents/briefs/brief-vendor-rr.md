# Brief: vendor-rr - DLSS Ray Reconstruction and AMD Ray Regeneration to the letter of their guides (PT-127)

Copy: COPYNAME (pristine base: BASENAME). Follow `AGENTS.md` in your copy; read first its ruling "Follow NVIDIA's and
AMD's guides to the letter" (the owner, 2026-10-11: "Obviously we want to 100% follow NVIDIA's and AMD's implementation
guidelines for these technologies"). Report: `docs/agents/vendor-rr.md` IN YOUR COPY; the plan within 20 minutes, kept
current; "State when stopped" at once if a limit shows.

## The job

`docs/agents/review-vendor-astra.md` (a read-only review of 2026-10-11) lists where our integrations depart from the
vendors' guides. Yours are these findings, each to be brought to the guide (the guide is the yardstick, not the review:
read the guide's own words first: `%TEMP%\tgb\vendor-docs\dlss-rr-guide.txt` is NVIDIA's DLSS-RR integration guide as
text; AMD's is `FidelityFX-SDK-2.3.0/Kits/FidelityFX/docs/techniques/denoising.md` and `.../denoisers/include/ffx_denoiser.h`):

- RR 2: the specular albedo guide in RGB (it is averaged to one channel in `pt_compose.hlsl` g1.a and widened again in
  `d3d11.hlsl`): carry all three channels through.
- RR 4: hit distance handed to RR independently of the optional transparency snapshot (`native/upscaling/src/
  rr_upscaler.cpp`, `upscaler.cpp`, `src/TGBetterUpscaling/StpHost.cs`); a failure of one optional guide never drops
  the reflection information the guide requires.
- RR 18: diffuse albedo guide = the material's reflectance (no 0.05 floor, no emission-derived colour), `d3d11.hlsl`
  ~455-568 and wherever else the guides are built.
- RR 19: render sizes from RR's own optimal-settings query (`NGX_DLSSD_GET_OPTIMAL_SETTINGS`), not the SR one. Leave the
  preset (F) as it is and say in the report what the guide recommends: that is the owner's choice.
- AMD 6: Ray Regeneration gets radiance in a stable scale (as NRD does: tracer.cpp's nrdScale), converted back after;
  its history reset when that scale changes.
- AMD 14: one factor for the specular demodulation and remodulation (`pt_rrg_guides.hlsl` divides by max(spec, 1e-3),
  `pt_compose.hlsl` multiplies by spec unclamped).
- AMD 15: reset Ray Regeneration's history after any frame it did not run (track its last dispatch, as NRD's
  nrdHavePrevious does).
- AMD 28: AMD's validation/debug flags available behind a hidden debug switch; its context made off the render path if
  the guide asks for it (say how).

NOT yours (the coordinator does them, or they wait for the owner): every NRD finding, RR 3 (hair guides), RR 17
(the highlight hold before RR), RR 20 (glass's distance), AMD 5 (motion z: needs the camera pass to keep the previous
depth), AMD 7 (direct specular as its own signal), RTXDI 21 and 22. Do not touch `pt_nrd_prep.hlsl`,
`nrd_denoiser.cpp/.hpp`, the NRD block of `tracer.cpp`, `pt_primary.hlsli`, `pt_specular.hlsl`, `pt_gi_spatial.hlsl`.

## How
- Each fix as small as the guide allows; a fix that changes what the player sees with RR goes behind nothing (the owner
  ruled: follow the guide), but write in the report, per fix, what it changes on screen and how to see it.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`) then the native build of your copy into
  `%TEMP%\tgb\vendor-rr-...`; the managed build of TG Better Upscaling and TG Path Tracing. Source checks
  (`python -I tests/source_checks.py`): update the ones your changes break, with the reason.
- Never start the game, never deploy, never commit, never write to the main tree. No GPU tests at all (RR is tested only
  in the game; Ray Regeneration cannot run on this RTX 5080): say so in the report. Grep and read ranges of big files.
- Final message: five lines at most, pointing at the report.
