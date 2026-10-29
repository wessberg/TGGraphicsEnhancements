# Note for fog-froxel, part (b) (the coordinator, 2026-10-11 ~16:20)

Your part (a) is merged into the main tree and was run in the game: `artifacts\harness\20261011-161034` (main tree;
tavern-room@fog-froxel; native B5F7F8C4F141; another program's GPU test ran beside it and the game window was not in
front, so its times are rough). Read its `report.md` and `tavern-room/overview.jpg`.

- The direction is right: with NRD the fog's flicker 0.38 -> 0.21 and boil 0.23 -> 0.12 (the old route's sparkling
  streaks along the light shafts under the roof are gone in the change map); with no denoiser 0.41 -> 0.36; the
  brightness the same (luma 18.1 / 18.1; 16.5 / 16.3 with NRD).
- The cost is too high: the composite stage (where the fog runs) 3.9 ms with the per-pixel fog, 11.7 to 12.6 ms with the
  grid on (the same run, rough). Find where the 8 ms go (the cell pass, the integration, the per-pixel tail beyond the
  grid) and bring it down WITHOUT changing what the picture converges to (the owner's rule: speed options that change
  the picture are the owner's choice, shown with numbers). Look at how Remix's current runtime does it (its main branch
  differs from the 1.4 docs: /8, 64 slices, a separate reservoir grid, 32 candidates, a temporal sample cap): what it
  updates each frame, what it reuses.
- Then part (b) as your brief says: temporal reservoir reuse per cell, the stability-driven filter, the firefly clamp,
  as Remix documents them; the grid's reach (20 m now; what takes over beyond it, and why that is no limit on the
  picture); mirrors and refraction.
- Hand the coordinator a build for one tavern run (the switch off and on, NRD, then no denoiser) when (b)'s first piece
  and the speed work are in; say in your report what each piece costs.
- The old per-pixel route stays until the owner has seen and accepted the grid (then it comes out the same day).
