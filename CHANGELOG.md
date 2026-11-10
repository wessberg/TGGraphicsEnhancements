# Changelog

Source version 1.2.4. Release publication after the recorded1.2.3 release is not verified here.
The complete earlier change-by-change record, including measurements, remains in the local development archive
described in [DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Unreleased: source preparation, 2026-11-10

- Replace conversational development records with concise requirements, issue registers and integration notes.
- Preserve all 1,185 pre-cleanup tracked files in a SHA-256-verified local archive.
- Separate PT's merged baseline, pending validation, missing worker changes and Unity compatibility blocker.
- Remove archived scratch copies, prompts, logs and generated images from public paths.
- Add read-only SDK inventory and an auditable source export without private files or Git history.
- Renderer behaviour, vendor SDKs, installed game files and player packages unchanged.

## 1.2.4: prepared 2026-10-30

- Update DLSS to 310.9.16, Streamline 2.14.16, FidelityFX 2.4.0 and signed Neural Rendering model310.9.16.
- Handle Unity 6000.3 CustomPass signature changes for FSR/XeSS upscaling and NR on both game branches.
- Recreate incorrectly sized startup DLSS automatically; preserve the selected quality mode.
- Add DLSS FG Quality/Performance selection using the create-time LiteCandidateBlending parameter.
- Use the NR lightweight-network preset and bundle the signed model under existing release policy.
- Six normal/Mono packages prepared and tested; upload status unverified. PT excluded after its managed build broke.

## 1.2.3: 2026-10-09

- Neural Rendering official/community runtime selection through an isolated adapter.
- Normal-version startup hook and retained Options child-row lifetime fixes.
- FG, BU and NR combinations checked on both branches; six packages released.
- Later Defender investigation found timestamp-only differences between a flagged FG build and a clean one.

## Earlier milestones

- 1.2.1, 2026-10-06: compatibility with the game's built-in FSR2 Upscaling-row update.
- 1.2.0, 2026-09: troubleshooting and integration fixes across FG/BU/bridge.
- Shared bridge architecture: separate player mods, one shared D3D12 device.
- Native FG/upscaling: DLSS, FSR and XeSS backends, latency integration, HUD separation and measured orientation fixes.

## Path Tracing development, not a player release

Own-camera migration, renderer/material capture, SER redesign, plant opacity micromaps, traced water/caustics,
froxel fog, full NRD inputs and stable-record crash fixes reached the10-12 handoff. Migration, correctness,
appearance and performance remain incomplete. PT was tabled on10-30. [PT-STATE.md](docs/PT-STATE.md) records
the exact boundary; successful source preparation does not establish new renderer validation.
