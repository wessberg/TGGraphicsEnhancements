# Note for fog-froxel, part (b) continued (the coordinator, 2026-10-11 ~17:00)

Your part (b) checkpoint is merged into the main tree (a three-way merge against part (a)'s state; your `worker.patch`
rebuilt it) and was run CLEAN in the game: `artifacts\harness\20261011-164811` (main tree; `-Measure`, no other program on
the GPU, the game in front; native 37ADD808936F). Read its `report.md` and its `TGPathTracing.Native.log` "stages:" lines.

- The picture: with NRD flicker 0.39 -> 0.21, boil 0.23 -> 0.12; with none 0.40 -> 0.36; luma the same (16.5/16.3,
  18.0/18.1). Keep it.
- The cost, the tavern, the stages a frame: the old per-pixel fog `fog integrate` 3.0 ms; the grid `fog cells` 0.6 to 0.7,
  `fog integrate` 9.8 to 10.5, `fog tail` 0.06. The whole tracer 35.7 ms with the old fog, 43 to 44 with the grid. So the
  integration through the cells costs 10 ms at 1924 x 1084: find out why (my guess, unverified: the medium's density or
  the fog boxes evaluated per slice per pixel, or the 3D reads) and bring it to what a march through a 121 x 68 x 48 grid
  should cost (Remix integrates once per froxel into a pre-integrated volume and each pixel reads ONE sample of it:
  `volume_integrate` in its runtime; see whether that is what Remix does and do it), without changing what the picture
  converges to. Report each piece's cost before and after.
- Then the rest of part (b): 1 to 6 stability-driven reservoir samples, the adaptive Gaussian filter, the firefly
  handling that keeps the excess, as your report lists.
- Hand the coordinator a build for one clean tavern run (recipe `fog-froxel-b`) when the integration's cost is down.
- Main has moved on since your base; your copy's files for the next merge are compared against what was merged
  (part (b) as of 16:38): keep your patch files current as you did.
