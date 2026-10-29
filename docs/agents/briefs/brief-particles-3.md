# Brief: particles-3 - decode the game's VFX Graph particles from the captured buffers (PT-112 step C, PT-67)

You are the ONE subagent the owner allowed on 2026-10-11 (~07:45). Follow `AGENTS.md`. Write ONLY your report
`docs/agents/particles-3.md` in the main tree (outline first, keep it current; if a usage limit shows, write "State when
stopped" at once) and scripts or decoded output under your own scratch folder
`%TEMP%\tgb\particles3\` (create it). Never: start the game, deploy, build the native tree, run any GPU test, edit any
source file of the repository, commit. The coordinator works in the main tree at the same time (the fog); do not touch
its files. Never read the big files whole (`pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `docs/PT-REQUESTS.md`,
`docs/PROJECT-CONTEXT.md`): grep, then read ranges.

## Why

The owner, 2026-10-11: the game's translucent effects (candle flames, fire, smoke, sparks, waterfalls) must leave HDRP and
be traced: "Nothing like that left in HDRP, only HUD and post processing". They are VFX Graph systems whose particles live
in Unity's own GPU buffers (no public API). A capture of those buffers ran once and was never decoded. Without the layout,
the tracer cannot read a single particle. Your job is that layout, proven on the captured bytes.

## What exists

- Run `artifacts/harness/20261009-150909` (recipe `particles-capture`, scenes `candle` and `campfire`, Path Tracing Off):
  `particles-report.md`, `particles-summary.json`, `vfx-overview.jpg`; per scene and phase (`vfx-live-facing`,
  `-away`, `-returned`, `vfx-frozen`) `frame-N-simulation.json` (each candidate compute dispatch: its program's hash,
  its buffers' bytes before and after, lifetime ids, views, indirect arguments) and `frame-N-image.json` (every loaded
  graph's public state, transforms, exposed properties, output materials); raw bytes under `simulation/`, the compute
  programs (DXBC, the game's: local evidence only, never copied into the repository or quoted beyond what a layout needs)
  under `simulation/programs/`. The folder is large: list it with `find -maxdepth`, never walk it whole at once.
- `docs/agents/particles-2.md` (the capture's author: what is proven, what is not; the known candle OUTPUT layout:
  attribute address `144*I + 32*P` position, pivot +12, horizontal scale +16, seed at `144*I + 128 + 4*P`; graph values
  176-byte stride, output count +0, size +144, seed +156), `docs/agents/effects-inventory.md`, `docs/agents/
  design-particles-fog.md` (how each family is to be traced once decoded), the effects inventory of round 20
  (`artifacts/harness/20261011-034434/effects-report.md`).
- Tools: the DXBC programs can be disassembled with the Windows SDK's `fxc.exe /dumpbin` or `D3DDisassemble`
  (d3dcompiler_47.dll) from Python. Unity's VFX Graph source (com.unity.visualeffectgraph, its generated compute shaders'
  conventions: attribute buffers, `deadList`, `deadListCount`, `instancingCurrentOffset`, layout of `attributeBuffer` by
  "structure of arrays" per capacity) may be read online for the conventions; the game's own programs decide.

## What to answer, each with evidence (a byte offset in a captured buffer, a line of a disassembled program)

1. For the candle flame family and for each of the campfire's systems (fire, smoke, sparks, debris, glow): the attribute
   buffer's layout (position, size or scale, colour, alpha, age, lifetime, alive flag, texIndex or flipbook frame, angle,
   velocity, seed), the capacity, how the live set is known (alive flag, dead list and its count), and whether positions
   are in world or in the system's local space (and where that transform is).
2. Proof: decode the captured bytes of one frame into particle records and show they are particles: positions lying at
   the candles and the fire in the scene (the saves' camera and the systems' transforms are in the image json), ages
   within lifetimes, counts that change between frames as the live phases do, nothing in the frozen phase moving. A
   picture: the decoded positions projected over the run's game frame (the camera's matrices are in the step json), as
   dots, saved in your scratch folder and named in the report.
3. What a reader in the tracer needs each frame: which buffers (by the observer's lifetime ids or by how to find them
   from a VisualEffect component or its batch), at which point of the frame they hold the current state (after which
   dispatch), and what an adapter would copy (bytes a particle). Whether the update runs for systems the camera does not
   see (the `-away` phase) and whether it runs when HDRP's transparent drawing is switched off (say so if the capture
   cannot tell: then name the one game run that would).
4. The families the capture does not cover (waterfalls, moths, Wyrd effects, trails): what would be needed.

## How to stop

When 1 and 2 are answered for the candle and at least the campfire's fire and smoke, or when the evidence cannot answer
them (then say exactly which one capture would, and stop). Report numbers and offsets, not guesses; label anything not
proven by bytes as unproven. Expected size: a few hours. Write the report's first outline within your first 15 minutes.
