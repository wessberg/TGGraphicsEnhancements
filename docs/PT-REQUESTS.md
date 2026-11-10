# Path Tracing issue register

Reconciled 2026-11-10. PT remains tabled. States distinguish historical evidence from completed validation.
Review marks historical evidence that still needs independent validation. The issue remains open.
The full original requirement, observations, measurements and next steps remain indexed by the same ID in the
local archived docs/PT-REQUESTS.md; see [DEVELOPMENT.md](DEVELOPMENT.md). [PT-STATE.md](PT-STATE.md) resolves later supersessions.

IDs are stable and never reused. Add a row for each new defect/request, with status and next action.
The two new entries concern existing configuration/build defects; no renderer behaviour was changed.

| ID | Issue | State | Next action |
|---|---|---|---|
| PT-01 | Hair, eyebrows and lashes look broken when traced | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-02 | Turning path tracing off does not give hair materials their render queue back | Fixed/game checked | Retain state-restoration coverage. |
| PT-03 | Grass and vegetation shade badly under the tracer | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-04 | Round things look low-poly (stepped) when traced: cups, jars, cloth folds, shoulders | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-05 | The traced picture looks bloomey outdoors | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-06 | The air above candles boils | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-07 | Fog lighting migration | Superseded by PT-37 | Continue the traced-fog work in PT-37. |
| PT-08 | Rendering performance target | Superseded by PT-99 | Use PT-99. |
| PT-09 | Boiling on thin bars and elsewhere | Partial | Inspect remaining scenes under NRD after restart. |
| PT-10 | Terrain blending in the game itself | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-11 | Skin: a white shine on the neck when traced | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-12 | Wet rock (and wet wood) turns white and glossy when traced | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-13 | Glass you can see through | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-14 | Caustics | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-15 | The saw-tooth on a door's metal strap | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-16 | The tavern's haze | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-17 | The dialogue light of TG Conversation Lighting Overhaul | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-18 | Eyes too small | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-19 | NRD is much noisier than Ray Reconstruction, keeps less fine detail, and costs more | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-20 | Complete game-state restoration when PT is disabled | Verify restoration | Recheck complete restoration including materials and fog. |
| PT-21 | Armour and metal reflections | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-22 | Outstanding appearance comparisons | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-23 | Known gaps in the tracer | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-24 | Other-mod coverage in test runner runs | Coverage recorded | List actual Loading lines and combinations before compatibility claims. |
| PT-25 | Missing scenario saves | Review historical evidence | Use stable named saves; do not overwrite personal saves. |
| PT-26 | The test runner itself | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-27 | Black spots that flicker on floors | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-28 | Parallax occlusion mapping with self-shadows for bricks and walls | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-29 | Traced rain | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-30 | Traced dust | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-31 | Traced smoke and fire | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-32 | Metals should reflect more than other surfaces | Implemented; review | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-33 | Far cliffs and mountains show lit up through the fog | Harness evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-34 | The crystal cave: caustics, boiling, soft reflections | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-35 | Refraction through a glass jar | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-36 | Moving cloud shadows | Planned | Trace moving clouds using authored sky content. |
| PT-37 | Traced fog lighting | Partial; accepted lamp look | Complete fog-only lights, authored controls and physical sky/ground illumination. |
| PT-38 | Night by a torch: the torch's warm light is gone | Harness evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-39 | An outdoor corner by day goes dark when traced | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-40 | Candle wax looks dark and faceted when traced, wicks white | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-41 | The heat haze around flames should bend the picture through the tracer | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-42 | Waves at the shore: no blending, no foam | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-43 | Skin looks flat when traced: no blush, no detail on the cheek, no dark around the eyes | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-44 | Eyes are flat discs when traced: no shadow from the eyelid | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-45 | Original-resolution game textures without private copies | Partial migration | Remove remaining atlas readers/caps without reducing texture fidelity. |
| PT-46 | A mirror must show the world as the camera sees it | Partial; mirror gaps | Extend mirror PSR/material fidelity to moving/curved/translucent cases. |
| PT-47 | Lights at glowing things are left out (crystals, magic, orbs, fireflies) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-48 | The mineral cave's lake, traced: thick blue haze, a dark slab of water, white-edged grass | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-49 | What moves must be traced as it moves; every cloth found as cloth | Partial | Find all cloth and match the live game deformation. |
| PT-50 | Remove limits that change convergence | Open limits remain | Replace capacity/traversal caps with convergence-preserving designs. |
| PT-51 | Replace transport approximations | Partial | Audit transport stand-ins against PT-DESIGN. |
| PT-52 | Waves leave the ground wet behind them (wet sand, wet rock) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-53 | NPC hair moves when the player moves | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-54 | Hair glows on top | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-55 | Eyebrows flicker | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-56 | Ron from behind: two layers of hair, a still one on the head and a see-through one around it | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-57 | Material, hair and caustic playtest regressions | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-58 | Mirror/fog and subsurface integration | Historical integration | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-59 | The tracer's own camera rays for every pixel | Implemented | Keep own camera rays as the only production first-surface route. |
| PT-60 | Remove obsolete renderer workarounds | Cleanup pending | Remove G-buffer route, surface offsets, dead settings and tests as dependencies disappear. |
| PT-61 | Match game geometry detail/LOD | Partial | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-62 | Track renderer correctness gaps | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-63 | Complete world, motion and decal capture | Partial | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-64 | Material fidelity for all game shaders | Partial | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-65 | Full-resolution rays and explicit lower-resolution options | Requirement; verify controls | Verify full/half/quarter controls without silently lowering quality. |
| PT-66 | Trace later path bounces | Historical; reconcile | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-67 | Traced particles and physical emitters | Open | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-68 | Bright dots (fireflies) on lit water | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-69 | Two findings of the outside review, tested: both real | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-70 | Outdoors the tracer's own picture is wrong: pale rocks, flat grey ground, dark grass | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-71 | Complete own-renderer migration | Incomplete | Finish sky, VFX, materials and capacity migration. |
| PT-72 | Publish the tracer's geometry guides to DLSS, shared with NR | Partial guides | Verify traced depth/motion/material guides through SR/NR/RR. |
| PT-73 | Post-migration visual findings | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-74 | Path tracing takes over a minute to come on: compile ahead, and tell the player | Partial startup work | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-75 | NRD changes what the picture converges to: dim, bounce-lit places come out darker and redder | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-76 | Tinted see-through glass no longer tints the light that passes it | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-77 | Physical leaf transmission | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-78 | A texture the engine made before our hook cannot be opened: a copy serves, until the hook is in from the start | Partial texture sharing | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-79 | A wall torch's lights and two blue fires were left out as fill lights: the tavern's room went dark | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-80 | Water/mirror/caustic regressions | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-81 | Traced water: seabed illumination, waves and foam | Open | Restore physically expected illumination on the shallow seabed; inspect waves/foam. |
| PT-82 | Scene inspection findings | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-83 | Things the game shows are missing from the traced world | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-84 | Every edge is a 2-pixel stair-step and thin things are rows of dots: the traced picture is not smoothed | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-85 | NRD makes some things worse than no denoiser: lit plants flash, single dots, a fine grid, a doubled glow | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-86 | Nothing looks wet in the rain, and fine ground relief is lost | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-87 | Path tracing still pauses itself when video memory runs short (6 textures unavailable) | Open | Measure current VRAM with stream on, cache and PSR allocations included. |
| PT-88 | In a mirror a metal breastplate is pure black; seen directly it is not | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-89 | The inspection's smaller new findings (2026-10-08) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-90 | A candle mirrored in water is a burnt-white blob, far brighter than the flame seen directly (2026-10-09) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-91 | The game minimized while tracing: a frame takes up to 2 s, then the game is gone (2026-10-09) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-92 | Characters are made of flat triangles: a guard's helmet in bands, a cloak in facets | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-93 | Grass lit by a fire boils with NRD: 460 speck spots a frame at the campfire, the game 1 (2026-10-09) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-94 | The sun's light under water is only written where the camera looks (2026-10-09) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-95 | A guard's helmet has none of its textures: plain chrome where the game's is dark hammered iron | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-96 | One start without memory, and every later start traces without DirectX Raytracing 1.2: a frame of 2 s, and no word (2026-10-09) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-97 | A test runner run can be steered by real input: the game in third person, menus opened, outfits saved | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-98 | Things pop in over time while tracing, and a patch of grass was not there at all when the pictures were taken | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-99 | Playable performance and CPU/GPU accounting | Open; target not met | Rebaseline streamed foreground A/B/A before claiming later savings. |
| PT-100 | With NRD everything seen through glass is a blur (2026-10-09) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-101 | GPU scheduling must exclude other open games | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-102 | Lights of the scene loaded before stay in the tracer's list after another save is loaded (2026-10-09) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-104 | With NRD the whole picture is smoothed in play: floor planks, surfaces | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-105 | A man's hair is a soft blur | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-106 | Eyes and hair of people look wrong in play | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-107 | The game's own rendering still runs under the traced picture: its time and its memory | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-108 | The rain's wetness is missing from the traced world: dry, flat ground where the game's glints (2026-10-09) | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-109 | RTX Mega Geometry integration and grouped plants | Enabled locally; redesign unmerged | Fix PT-148; recover/reimplement grouped plants; keep supported path enabled. |
| PT-110 | SER, opacity micromaps and RTXDI integration audit | Partial; vendor audit | Retain leaf level6/material-key SER; finish vendor audit. |
| PT-111 | Cold driver shader-compilation time | Open | Reduce shader/driver compile cost without dropping required acceleration. |
| PT-112 | Remove duplicate HDRP rendering; migrate sky and VFX | Opaque done; sky/VFX open | Capture simulation buffers for off-screen VFX; trace sky/transparents. |
| PT-113 | 44 unresolved historical GPU-test failures | Open; baseline failures | Reconcile each historical failure against a recovered buildable baseline. |
| PT-114 | The caustics cost a room 4 ms a frame and make nothing there | Merged; cleanup pending | Remove obsolete caustic path after preserving the merged grid result. |
| PT-115 | Reuse bounced-light samples for rough-surface highlights | Proposal | Compare traced rough-highlight estimators and their speed/appearance. |
| PT-116 | At most 4 water surfaces are traced; the rest stay HDRP's | Open capacity limit | Support all loaded water surfaces without a fixed four-slot picture limit. |
| PT-117 | The performance review's findings | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-118 | Volumetric noise and motion history | Grid merged; coverage gaps | Motion validation, complete cache coverage and secondary-ray candidates. |
| PT-119 | Eyes dark from almost every angle but straight on | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-120 | Bottles in the tavern show no translucency, reflection or refraction | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-121 | No heat haze or smoke from fires | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-122 | Planks and ground look oversmoothed | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-123 | No contact shadows between objects and tables: that rasterized look | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-124 | Textures took very long to arrive; stutter as new things stream in | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-125 | GPU hang from stale plant pose records | Fixed; 40 game walks | Keep stable plant identity and moving-scene regression. |
| PT-126 | In rain, the traced fog over open sky is a flat, too-bright cyan patch with hard edges | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-127 | Vendor-correct NRD/RR/RRG/RTXDI integration | Partial; comparisons pending | Main fog check; NRD motion off-blur; glass B then A; SH; RR17/AMD7/RTXDI21-22. |
| PT-128 | Reflection-sort chooser reads wrong timestamp stage | Open; verify current index | Check named stage boundaries before any sort-timing conclusion. |
| PT-129 | Second-load reflections hang | Deferred reproduction | Do not reproduce again without a new request; retain bounded-loop fixes. |
| PT-130 | Caustics must execute before current-frame readers | Order fixed; appearance pending | Inspect current-frame caustics under moving water. |
| PT-131 | Water transport cost; invalid underground-camera measurement | Measurement corrected; cost open | Use grounded camera; isolate expensive physical underwater transport. |
| PT-132 | The traced fog loses the light of lamps with a range and of glowing surfaces on long rays | Open on secondary rays | Remove anchor-list omissions for ranged lamps/emissives on long secondary rays. |
| PT-133 | A Shader Model 6.9 pass returns garbage for a value that is zero on a skipped branch | One pass fixed; audit pending | Keep split sums; test every no-sun branch before rewriting their shape. |
| PT-134 | Stale mesh-decal BLAS addresses | Fixed; offline mutant only | Inspect a real mesh-decal scene after restart. |
| PT-135 | Emitter identity across scene-record rebuilds | Fixed; game motion pending | Inspect emitter stability during moving plant-group updates. |
| PT-136 | Outlier lights corrupt FP16 storage reference | Lamp case fixed; audit open | Audit real emitter identity and FP16 scaling without clipping physical light. |
| PT-137 | The mist's cache is fed only where the camera sees fog, and read all around | Open | Populate all cache regions read by scattering, not only visible camera cells. |
| PT-138 | The fog's cells: what the rule leaves open | Fixture merged; main run pending | Run corrected 17 main-tree cases; keep rejected undimmed-sky trial out. |
| PT-139 | A pure mirror shows a lamp as a noisy glow: its highlight is sampled through a lobe the mirror does not have | Open | Use physical emitter surfaces and correct pure-mirror highlight sampling. |
| PT-140 | Memory diagnostic sums cumulative changes incorrectly | Review historical evidence | Report net or maximum change, not sum of per-call movements. |
| PT-141 | A flickering bright line along the water's edge with Caustics on | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-142 | A very low sun over a steep sea: a coarse cell's triangle with a corner turned from the sun is left out, the water's glow a little dark | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-143 | A plain two-sided surface is passed from behind within 15 cm: `passes_through` asks for a cut-out where it means two-sided | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-144 | Smoke in fog is a dark blob in front of the mist | Review historical evidence | Reconcile the archived evidence and inspect the relevant scene after compatibility is restored. |
| PT-145 | Optional one-frame-late rendering | Unmerged; copy unavailable | Recover/reimplement; prove switch-off equivalence; review added latency and RR/FG limits. |
| PT-146 | Steady flame sampling reach | Chosen; comparison pending | Plugin-only hidden comparison with native-resolution frozen/moving frames. |
| PT-147 | Game lamp strengths depend on scene-load order | Measured; game cause open | Find game-side intensity setter; keep scene load order fixed. |
| PT-148 | Mega Geometry trial transition invalidates retained plants | Open | Restore retained plant plan on transitions before mixed-trial runs. |
| PT-149 | Fresh-build Mega Geometry default conflicts with required enabled configuration | Open; docs-only finding | On PT resumption, align fresh configure with enabled-on-supported requirement. |
| PT-150 | Unity 6000.3 managed API/framework build blockers | Blocked; PT tabled | Update SetupBeforeExecute and Mono reference strategy in an isolated build. |
