# Materials second pass (PT-73 d/g/h, PT-64)

Worker mats2, base43, 2026-10-07/08. Code and focused validation finished; full regression has an automatic waiting runner. No game launch, deployment, or writes to the main tree.

## Result

Added RealSkin surface layers and corrected Dissolve3D base-property names. Added native diagnostics showing which
material programs have all their textures open. These are not a fix for all the outdoor differences.
Native and managed builds pass (managed: zero warnings/errors). Source checks: 93/93. New focused GPU checks pass.
Four full attempts were stopped by the required main-harness/game guard; none reached the suite's end.
The single hidden runner keeps waiting and retries after external interruption. It writes its final result and all FAIL
lines into the status block below and `%TEMP%/tgb/mats2-build/full-result.md`, then exits. No complete full run is repeated.
No in-game result or owner acceptance is claimed. Coordinator-owned records are unchanged as instructed.

<!-- MATS2_FULL_STATUS_BEGIN -->
Full GPU run completed at 2026-10-08 01:31:11. Attempt 6, exit 1; 1330 ok lines, 86 FAIL lines. NRD OFF; no RR or in-game test.

Log: C:\Users\Frede\AppData\Local\Temp\tgb\mats2-build\full-6.log

Exact FAIL lines:

~~~~text
FAIL: distant lights leave the room's light as it was
FAIL: a small lamp casts a crisper shadow than a large one
FAIL: a surface whose diffusion profile is thick (skin) is not lit through from behind
FAIL: glass bends light by its material's own index of refraction (another index lays the light elsewhere)
FAIL: the caustics and their lookup stay affordable where 25 lamps light a lake (under 10 ms here; the hash took 33.7 ms)
FAIL: with TGPT_OPTION_NO_CAUSTICS the water stops no ray and the light under it is the straight light
FAIL: no water below the floor's level: nothing changes on it
FAIL: a wall facing still water takes the sun's mirror image in it, by the water's Fresnel reflectance
FAIL: the waves split the reflected sun into bright lines and dark gaps
FAIL: three lamps' light reflected off the water adds up to each one's alone
FAIL: four lamps' reflected light adds up on average, the two weakest sampled for each other
FAIL: a wall facing still water takes the lamp's mirror image in it, by the water's Fresnel term and the whole path's falloff
FAIL: the waves split the lamp's reflected light into lines and gaps
FAIL: no reflected lamplight where something stands between the wall and the water
FAIL: hair's highlight lies along its strands (the half vector leaning into them dims it)
FAIL: Ray Reconstruction's guides and the composite's extras over layer hair
FAIL: hair stays where it is while the camera moves, with the tracer's own camera rays for every pixel
FAIL: the yardstick counts every pixel of a frame
FAIL: a still world's own motion under a moving camera is the drawn motion vectors'
FAIL: the yardstick lists the material that is read otherwise
FAIL: a reflection shows the reflected surface's bounced light, from the radiance cache
FAIL: an emissive surface sampled as a light lights the floor with far less noise
FAIL: a single-sided emissive surface lights nothing behind it
FAIL: emissive surfaces are sampled as lights except a light's own, moving ones and those past the triangle budget
FAIL: with Emissive Lights off none is sampled, and a light's own surface still lights nothing
FAIL: the composite wrote Ray Reconstruction's hit distance and color before transparency for the frame
FAIL: every mesh with alpha-tested instances has opacity micromaps
FAIL: a mirror shows the wall in the fog as HDRP fogs a view of it from the mirror's place (a constant fog color)
FAIL: a mirror shows the wall in the fog as HDRP fogs a view of it from the mirror's place (the fog's color from the sky)
FAIL: the inside of the hero's first-person body does not show in a mirror 0.5, 1 or 3 m ahead (the back of its head, never drawn from in front)
FAIL: the metallic half of a mask map bounces its specular color's light
FAIL: the metallic half of a mask map is reflected lit as a metal, not black
FAIL: a material without a number, or whose texture died, reads its atlas tile as before
FAIL: the game's normal map tilts the shading at a hit, in the mesh's tangent frame
FAIL: a surface drawn forward takes its roughness from its mask map: read linear, from its shader's channel, remapped
FAIL: a metal drawn forward has no diffuse color
FAIL: a mask map not baked yet reads as its remaps' low ends: the least metal at its roughest
FAIL: KF_Architecture's base layer is its color map times its tint
FAIL: KF_Architecture's smoothness is its mask's alpha times its base smoothness, fading with distance where the material says so
FAIL: grass is its map times its healthy color, toward its dry color by the game's noise over the world
FAIL: TriplanarArchitecture2's layers are laid over the world, not by the mesh's coordinates
FAIL: TriplanarArchitecture2's layer B comes in by its blend map and its height
FAIL: HDRP Lit's detail map lightens the base color as HDRP's Lit does
FAIL: HDRP Lit's detail map moves the smoothness as HDRP's Lit does
FAIL: the yardstick counts a character's pixels
FAIL: a character's pixels move by its own previous positions (the skinning's of the frame before)
FAIL: a surface seen by camera rays only casts no shadow
FAIL: clear glass casts no shadow
FAIL: a character's eye drawn forward is shaded by the tracer, not left to HDRP
FAIL: with Crevice Light, ReSTIR's bounced light reaches the opening of a dark pocket (reuse counts a pixel only for samples it sees)
FAIL: with Crevice Light, ReSTIR's bounced light deep in a dark pocket is neither lost to the boiling filter nor inflated by restarted histories
FAIL: the radiance cache brings the light deep in a dark pocket most of the way to four bounces'
FAIL: frames with 128 characters trace
FAIL: the water's reflections are filled, with the full weight, and finite
FAIL: the water reflects a light panel only the traced scene has, yellow and bright
FAIL: the water reflects the red wall where its mirror sends it
FAIL: the water reflects the green wall where its mirror sends it
FAIL: the water reflects the sky through the ceiling's hole
FAIL: the water's reflections keep their edges
FAIL: where HDRP's SSR found the whole reflection, the traced one replaces it
FAIL: where HDRP's SSR found part of the reflection, the traced one replaces it
FAIL: the fill is divided by HDRP's reflection lighting multiplier
FAIL: the fill is pre-exposed with the traced frame's exposure
FAIL: with a moving camera the water keeps reflecting the panel where its mirror sends it
FAIL: translucent surfaces (leaves, grass) are lit through from behind in the water's reflections
FAIL: the wax 3 cm under a candle's flame takes the light physics gives it, no more
FAIL: without the flag, 5 cm round a light let its flame light a wax column's side as a leaf
FAIL: a candle's light at its flame no longer lights its own wax from inside: the wax's top shadows its sides
FAIL: the cylinder's far side fills the measured part of the view
FAIL: a wax cylinder lit from one side: its far side takes the light a reference random walk gives
FAIL: a piece of the same wax grown into it changes nothing: the walk counts the pieces it is in
FAIL: lit as a plain surface, the far side is black
FAIL: a thin slab of wax (an ear) passes the light a reference random walk gives for its thickness
FAIL: a thicker slab passes less, and less of the blue than of the red
FAIL: a thick flat piece of wax takes the light a reference random walk gives
FAIL: a thick flat piece of wax under even light sends back its color, as a painted surface does
FAIL: a deep fold in a wax lump is lit by the light that went in elsewhere, redder than it went in
FAIL: lit as a plain surface, the fold is black
FAIL: the light a wax slab passes to a path is colored as the reference random walk's
FAIL: the hanging sheet is drawn and traced
FAIL: a swaying cloth is traced in the pose HDRP draws it, at two times, within a millimetre
FAIL: the yardstick counts the moving box's pixels as a moving thing's
FAIL: a moving thing is traced where HDRP draws it this frame
FAIL: the scan's copy of a moving thing is not traced where it was
FAIL: a hidden moving thing and its scan's copy are traced nowhere
FAIL: no frame failed
~~~~

Native/executable identities: %TEMP%/tgb/mats2-build/full-final-hashes.json. This is the only non-interrupted full attempt; earlier attempts stopped for the main harness/game guard.
<!-- MATS2_FULL_STATUS_END -->

## Evidence

Read AGENTS, BACKLOG, relevant PROJECT-CONTEXT/dead ends, latest rounds, materials.md in full, merge-4.md,
and both inspections. Read the 20261007-224654 LogOutput.log and viewed forest/game-vs-traced, vista/crop-cliff,
vista/crop-cart, glass-jars/crop-floor, hair-moving/crop-head-frames. All paths refer to that main-tree harness run.
Research/build outputs are under %TEMP%/tgb/mats2-*. Previous shader decodes read from scratchpad/mw.
Exact RealSkin/Dissolve3D resources decoded privately; no game code or decoded instructions copied into this tree.

### Outdoors

Forest line 1234: same surface 67.8%; among those colour 0.8%, normal 10.1%, roughness 45.1% differ.
Vista line 1466: same surface 62.6%; colour 9.1%, normal 37.1%, roughness 48.7% differ; sky for both 34.5%.
Those material-channel totals are shares of matching surfaces, not the entire screen.

| Material | Place / colour / normal / roughness, % of its pixels |
|---|---|
| Forest Meadow grass #1174 | 62 / 0 / 10 / 36 |
| Forest CliffFormationCity #747 | 7 / 0 / 3 / 27 |
| Forest MeshTerrain HOS #718 | 35 / 3 / 27 / 57 |
| Forest PineLeaves #1168 | 39 / 0 / 5 / 39 |
| Forest TreeBranchBarricade HDRP/Lit #1076 | 2 / 0 / 2 / 46 |
| Forest BeechLeaves #1171 | 10 / 0 / 3 / 50 |
| Forest PineBark #985 | 4 / 4 / 2 / 51 |
| Vista MeshTerrain Cuanacht #1508 | 0 / 0 / 71 / 86 |
| Vista CliffFormation #1465 | 6 / 0 / 72 / 93 |
| Vista SagremorStoneDirt #1621 | 4 / 79 / 8 / 91 |
| Vista Cliff01Black #1469 | 2 / 0 / 8 / 0 |
| Vista CuanachtFloorTiles #1516 | 1 / 13 / 35 / 97 |
| Vista Balustrade #1527 | 3 / 97 / 8 / 54 |
| Vista Cliff04Black #1464 | 5 / 2 / 23 / 94 |
| Vista LastTreeTrunk #1778 | 2 / 3 / 22 / 97 |
| Vista LastTreeBranches #1690 | 9 / 26 / 30 / 67 |

Re-read Cliff blobs 156/157, terrain 55, triplanar 52 and actual material properties. Verified those shader chunks
match the exact named shaders in the game. No justified change to these formulas found. Terrain's stored
BaseColorRemapMin/SmoothnessRemapMin properties really are unused by the compiled variant: adding them is wrong.
CliffFormation has normal strengths of 3 and authored different smoothness ranges across close/far/moss layers.
Replacing these with a rougher constant to fit this capture would be a guess. Near Cliff01 already has roughness 0%.

The game's ScreenSpaceWetness pass runs at AfterOpaqueDepthAndNormal and rewrites camera normal/roughness using
rain, accumulated moisture and a top-down shelter depth array. PrecipitationController accumulates moisture at
precipitation * 0.25 per second, dries at its configured rate, enables above 0.05 precipitation unless suppressed.
The tracer's current wetting concerns its water, not this rain system. This missing material effect plausibly explains
the forest's dry look across multiple unrelated shaders, including ordinary Lit. The capture does not log moisture
or pass enabled state: not proven to explain all 45.1%. Do not tune only Cliff or darken/shine every dry material.
Follow-up: read the weather simulation state and evaluate wet surfaces at world hits with traced shelter. Do not
restore a screen-space wetness pass. Source: decompiled ScreenSpaceWetness and PrecipitationController, read only.
True mesh tangents are also missing (already recorded in materials.md); the tracer derives a triangle UV frame.
That can cause the terrain/cliff/rug normal errors. Geometry is outside this brief, so no vertex layout change here.

### Memory versus formula

Forest program-reference misses rise from 33 to 57 while native budget-blocked textures rise to 257. Vista reaches
571 missing program references and 869 budget-held textures (3243/4095 open; 637 resource swaps). program_of requires
EVERY present texture: one unavailable layer map disables the whole program. Managed messages saying a material is
computed as its shader describe building its record, not successful native readiness.
The colour yardstick threshold is an absolute 0.1 linear difference. Dark textureless terrain can still score colour 0%.
Thus the current log cannot tell whether each white railing, flat ground or polished far cliff ran its program.
New native Yardstick material programs line gives material ID, program ID and missing-slot bitmask from UPLOADED
records. Zero means all required slots open; ffffffff means absent/invalid record. Compare with the named managed
line for the same sample before assigning blame. Memory/tile implementation is untouched; another worker owns it.

### Tavern, portrait and smaller findings

Tavern around log line 668: same surface 99.4%, colour 0.6%, normal 5.3%, roughness 0.1%.
FloorRag #572: 0/0/31/0; MI_Wood #417: 1/2/1/0; KFAtlas #420: 1/0/9/0; AnimalRug #585: 2/0/0/0.
551 program texture references, zero misses, 772/772 textures open; later 1540/1540 open.
The floor crop does not establish rotated UVs: much of the apparent layout follows the same seams. These measurements
do not justify changing its texture or tiling. The near-black rug and fur loss also have matching base colour/roughness;
lighting/transport and the rug's true normal frame need investigation. The inspection's 18-19% roughness figure belongs
to later outdoor candle lines, not this tavern sample. No evidence identifies a metallic bottle material error yet.
Yellow vista twigs do have colour differences (LastTreeBranches 26%); first check missing layer maps.

Alisa head #8: colour 0%, normal about 2%, roughness about 3%; body colour 0%, normal 2%, roughness 5%.
This does not establish base-colour capture as the cause of the severe red/dark lit portrait. The shader has another
relevant difference: its tattoo emission expression contributes a nonzero self-light term even with all tattoo
emission sliders at zero (base colour times 2^-3 before its exposure arithmetic). Alisa has all four sliders at zero
and no tattoo mask, so this is not merely a glowing-tattoo edge case. The compiled GBuffer shader adds that term to
its lighting output. Our capture has no RealSkin emission reader. This can brighten the game's skin without changing
the albedo yardstick. Its share of this captured brightness is not measured. Copying a constant glow onto ordinary
skin to match a picture would conflict with the no-fake-light ruling, so I have not added it. Do not blame the
scattering walk from this comparison alone. Actual tattoo emission and per-texel scattering still need proper
integration. I did not change the walk. Missing RealSkin surface layers are real regardless of the lighting symptom.

## Changes

- RealSkin program 9: diffuse tint, head overlay/cavity (G/B/A, not R/G/B), regional/head/ear/neck smoothness,
  base/head/micro normal layers, tattoo colour/opacity and normals. Body variant omits head-only maps. Exact head/body
  fragment variants 471/488, parameter blobs 404/406. All traced hits use it, including mirrors.
- Dissolve3D underlying surface reads _BaseTint and the shader's misspelled _NormalStrenght, with mesh UV.
  Moving dissolve coverage and edge emission remain missing: this is not a completed dissolve shader.
- Native readiness diagnostics described above. No pass-creation/memory/lighting/temporal code changed.
- No structs, exports or option bits changed. Added material-program ID 9, count now 10. Coordinator owns ABI number.

Still missing from RealSkin: per-texel scattering/thickness regions and tattoo emission/Wyrdlook animation. No imitation
blur or invented absorption added. Snow/Snow (47), Cliff_Snow (34), KF_Layered (8), dissolve coverage, relief and remaining
variants stay open. Thin cloth's fixed 0.3 has no supporting material parameter: physical weave/thickness needed.
PT-64 is not complete.

## Validation

- Native: TGPathTracing.Native and tgpt_gpu_test built in `%TEMP%/tgb/mats2-build`, NRD OFF. Nothing deployed.
- Managed: Release/net472, cached dependencies through an empty offline feed, zero warnings/errors.
- Source checks: 93 pass, including the new program ID/property reads and safe startup wait.
- Focused GPU: `skin-4.log`, exit 0, all five new material assertions pass. These are raw material views on panels;
  they isolate surface arithmetic from skin transport. Body/head RGB 0.21582/0.07416 against 0.21586/0.07496;
  roughness 151/139 against 151.5/138.6 (out of 255); tattoo RGB 0.14124/0.09082/0.07819;
  normal 0.2568/0.0020/-0.9662. Dissolve3D tint, graph normal and alpha-times-smoothness also pass.
- No mutations, NRD, RR, deployment or game launch by this worker. NRD is absent as required for isolated builds.

Driver pass creation took 218.3 seconds in the successful focused run and 186.4 seconds in full attempt 2.
The inherited 180-second test deadline had failed before any new material check. Added `TGPT_TEST_STARTUP_SECONDS`
(default still 180; our runner uses 900) and an early return on timeout, so an unfinished pipeline cannot be rendered.
No image threshold changed. An initial test compile error (a pointer helper outside this fixture's scope) was fixed.

The game guard stopped only our processes: focused attempts 1 and 3, then full attempts 1-4. Full attempts were
preempted at 00:04:49, 00:17:33, and subsequent reservations (exact times: full-wait.log); all were incomplete.
RUNNING is created before the main harness waits for the GPU, so even its reservation preempts this worker under
this brief's absolute rule. Four partial attempts are not four completed suites. After repeated preemption, the
same waiting workflow was transferred to a hidden self-reporting process while no GPU test was active. This lets
coordination proceed while preserving the instruction never to give up a test for want of a slot. It still checks the
game, live RUNNING PID and main -GpuCheck before starting, and stops only its own test if a reservation/game appears.
The runner identity is in `mats2-build/full-runner.json`; its output/error logs are full-runner.out/.err.

The inherited mF mutant removes only the eta=1 early return: Snell's law still leaves the ray straight, so it can be
an equivalent mutation. Survival alone is not an optical defect. The integrated glass checks (80 index-matched
interfaces, Beer-Lambert absorption, IOR 1.5 transmission/deflection) passed in the partial full runs. They should
also catch mE's forced IOR 1.5, but this is not a mutation-check claim. Full attempt 6 reached the older weak-index
sphere assertion and it fails again (also reported in merge-4): the index-1.2 result is 1.56 times the opaque-shadow
window's blocked light, outside that check's 1.1 upper bound. That window is not a general energy-conservation region
when focus changes. No optical thresholds or glass code changed. Other failures outside the new material checks
are recorded by the full-run status block; without a complete matching baseline I do not call them all pre-existing.

## Workarounds met

Replaced plain-Lit treatment of RealSkin surface layers and generic Dissolve3D tint/normal names. Retained and disclosed:
all-or-nothing program fallback, small atlas tiles, triangle-derived tangent, absent rain material effect, fixed cloth
transmission, incomplete skin transport/emission and unsupported variants. No new caps, copied maps or screen shading.

## Predicted next same-scene yardstick

No defensible promise of an outdoor percentage reduction from this patch alone. Forest roughness can remain near 45%
until rain/readiness are resolved. Vista's 49% roughness/37% normal should drop IF the memory worker restores missing
program maps; the new diagnostic distinguishes that from a formula fix. True-tangent differences remain.
Tavern roughness should stay near 0.1%; no floor-UV change. Skin's few-percent normal/roughness should not grow;
its orange darkness is not claimed fixed. Log readiness and dry/wet state before choosing tighter outdoor targets.

## Merge files and build identity

- native/pathtracing/include/tgpt_api.h
- native/pathtracing/shaders/pt_common.hlsli, pt_materials.hlsli
- native/pathtracing/src/scene.hpp, scene.cpp, tracer.cpp (yardstick logging only)
- src/TGPathTracing/MaterialPrograms.cs, SceneCapture.cs
- tests/pathtracing_skin_material_checks.inl (new), pathtracing_gpu_test.cpp, source_checks.py

Include the new .inl when merging. No CMake change. No structs, exports or option bits changed; program ID 9 is new.
Coordinator owns the ABI number. Use the new plugin and native DLL together. Base-copy line endings were restored,
with assertions that normalized source text was unchanged after compilation. No semantic edits after the validated build.

Native build/private install SHA-256:
`A314E434F662C00015CAB593CACD15A93EFD552B43C1EEC92F2CD9CACF398C5D`
Evidence under `%TEMP%/tgb/mats2-build`: build-initial.log, build.log, managed-final.log, source-checks-final.log,
hashes.json, skin-4.log, full-*.log, full-wait.log. The waiting runner also records the final loaded DLL and executable hashes.
Staging used private CMake file(INSTALL), plus a read-only copy of the existing bridge and Agility runtime.
No other mods built natively, no downloads, no shared SDK writes. The report and helper outputs stay in this copy or mats2-*.

### Reading the new missing-slot mask

IDs are local to each capture: join with that capture's named managed yardstick, not with the old IDs above.
Program 0 means ordinary Lit/no program; its zero mask is NOT a claim that every ordinary-Lit texture opened.
For a custom program each set bit is one missing slot (hex 00000020 means slot 5):

| Program | Slots, from zero |
|---|---|
| 3 Cliff | L1 colour, L1 normal, L2 colour, L2 normal, L2 world mask, L0 colour, L0 normal, L0 mask |
| 7 Terrain | Splat0, Splat1, then base/normal/mask for each of the eight authored layers |
| 8 Triplanar | A colour/normal/mask, B colour/normal/mask, blend map, blend normal |

Do not drop the Cliff's entire L2 map set when its keyword is off: its compiled no-moss variant still uses the cover
and L2 colour's red channel to modify smoothness. Only its displayed moss colour/normal are switched off. This odd
behaviour is in the game shader; changing it would change the intended surface. The named Cuanacht black cliffs in
the census enable L2 anyway. The bottle census shows ordinary HDRP/Lit with mask maps, metallic scalar 0; that scalar
alone cannot settle actual metalness because the mask's red channel controls it. No bottle special-case was added.

## Remaining material work

Alisa's authored head values: SmoothnessMax 0.28, cheek +0.39, forehead +0.38, unmasked +0.34;
normal strength 0.743, head-normal blend 0.312, micro normal 0.4 tiled 12 times. The new surface program reads these.
Dissolve3D's mask green is occlusion; that mapping is corrected too. Strawdad is unchanged because its separate
variant was not independently decoded. No unsupported assumption that similar property names mean identical math.

Coordinator should retain rain wetness at world hits, true mesh tangents, skin emission/per-texel scattering,
unsupported snow/dissolve variants and physical thin-cloth data in the request/backlog records. This worker was told
not to edit those records. Full portrait brightness and the outdoor defects are not claimed fixed by this patch.

## Research tool traps

`unity_shader_blob.py` finds raw name bytes and can match another shader sharing a name prefix. For RealSkin I used
UnityPy's exact parsed shader name and its own offsets/lengths to decompress each chunk instead. Cliff, Terrain and
Triplanar chunk identity was checked against the earlier worker's decodes. Pass selection requires the explicit
`fragment` argument before --names. RealSkin's older embedded parameter layout needed the raw parameter-name
reader in scratchpad/mw/params.py; the regular blob-name parser alone was insufficient. All decoded output remains
under the private mats2-research directory, outside this source tree.
