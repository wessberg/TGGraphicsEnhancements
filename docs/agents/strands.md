# PT-01: hair's fine detail, and the see-through top of a head of hair (agent "strands", 2026-10-07)

Copy `scratchpad\wt\strands` (base `wt\base23`). Built and checked offline only; nothing here ran in the game. Crops and
scripts: `scratchpad\strandwork\` (named below).

The owner, of Ron: "His hair in vanilla is also so much finer detailed, with more detailed specular response", "it still
seems blurry/low-res and not as finely detailed as the vanilla game". Of the npc-day run `20261007-081218`: "why is
ha[i]r fogged out in the top here?". On the hair layer's fixed count of 8 cards: "There is no budgets or limits there,
this is PT, whats that about?".

## In short

- **The see-through top of the hair (npc-day)** came from the fixed count: a camera ray stopped looking after 8 cards, and
  whatever those 8 left showed the background. Above the skull of a head seen from the side, a ray grazes through many thin
  cards; the game blends all of them. Now the first 8 are lit one by one as before, and the ray goes on past them through
  every card, each stopping it by chance as much as its texel covers. The picture averages to the game's blend; the 8 sets
  only the speed. The strands' blurry reading (next point) made each card thinner there, so more cards were needed.
- **The "crown" box of the last two rounds is the wall above Ron's head**, not his hair (section 1). On his real crown the
  tracer's own picture (no denoiser) has 0.98 of the game's fine detail by that measure: the number never saw the problem.
- **Where the hair's look differs** (section 2): (1) the strands were read at the geometric mean of a pixel's footprint,
  one or two mip levels blurrier than the game where a card turns from the camera (the game forces anisotropic filtering);
  (2) the tracer lit every strand of a card alike, while RealHair lights each texel by the game's normal map (Ron's
  `Tex_H_m001_Normal`, 1024, strength 0.5) and shifts its highlights about it: the fine light and dark lines along the
  game's hair; (3) the hair's light reaches the screen a frame late at the previous frame's jitter, which costs about a
  quarter of the finest detail through the upscaler (a model of it; not changed here). The strand atlas and its mips are
  not the loss: the game's own mips are the same box means.
- **Changed** (`pt_hair.hlsl` only, plus checks): anisotropic strand reading, the chance walk past the 8th card, the normal
  map on hair's diffuse light, highlights and Ray Reconstruction's normal guide. No ABI change, no new bit.

## 1. The "crown" box measured the wall

`detail_boxes.py crown=0.465,0.36,0.53,0.41` (PT-REQUESTS PT-01, `ron.md`): in the game's frame `20261006-212905/1-game`
and the traced `20261007-030128/12-traced-no-denoiser` that box lies wholly above the head; Ron's hair starts at about
y = 0.41 (`strandwork/crownbox-g.jpg`, `crownbox-t.jpg`: the box drawn in green). It measured the wall's wood grain: the
game's wall against the traced wall. The candle's light on the wall is what moved it between rounds (0.63 to 0.47).

On the hair itself (box `top` = 0.483, 0.418, 0.525, 0.448, the crown's strands), run `030128` against the game's frame:

| picture | fine detail (detail_boxes) | of the game's | along the strands (9 px averaged along them) | coherence |
|---|---|---|---|---|
| game | 0.397 | 1.00 | 0.253 | 0.82 |
| traced, no denoiser | 0.387 | 0.98 | 0.248 | 0.78 |
| traced, Ray Reconstruction | 0.349 | 0.88 | 0.237 | 0.78 |
| traced, albedo view (color and cover alone) | 0.158 | 0.40 | 0.104 | 0.69 |

(`strandwork/strand_detail.py`: "along the strands" averages 9 pixels along the box's main direction before measuring,
which keeps strands and drops two thirds of the grain; coherence 1 = all the detail lines up along the strands, 0.5 = none.)
So by numbers the traced crown is about as busy as the game's; what differs is what the detail is. The crops show it:
`strandwork/hair-1to1.jpg` (2x, the game above, traced below) and `hair-views.jpg` (albedo view, Ray Reconstruction): the
game has long thin single strands with bright and dark ones side by side; the tracer has card-sized clumps, grain inside
them, and at the top of the crown (cards seen at a slant) an even smear. The strands' main angle on screen is 23 degrees
in the game and 10 in the tracer: what lines up in the traced crown is the cards' edges and their shadows, not strands.
A box for the next runs: `top=0.483,0.418,0.525,0.448` (and `hair2=0.49,0.415,0.52,0.44`, hair-4's).

## 2. Where the hair's fine detail goes

Stage by stage, against what RealHair draws:

1. **The strands' cover: the atlas is not the loss.** Ron's `Tex_H_m001_BaseMap` (2048, BC3, read from bundle
   `1643301ab10be0ea561d76a1deff54e7`, `strandwork/mips.py`): the game's own mips are plain box means; at every level the
   share of hair texels at or above the depth prepass (0.5) is the strand atlas's within a point (2048: 75.5 %, 512: 50.0
   against 49.8, 128: 36.4 against 35.7). The atlas block is the map's own 2048.
2. **The strands' filtering: blurrier than the game's at a slant.** The game's quality level (Ultra, `globalgamemanagers`
   QualitySettings) has `anisotropicTextures 2`: anisotropic filtering forced on every texture (Unity's forced minimum
   level 9; the hair maps' own level is 1, filter bilinear). The tracer read the strands at the geometric mean of the
   footprint's two sides: a card turned 75 degrees from the camera (a 4:1 footprint) one mip level blurrier across its
   strands than the game, at 80 degrees (6:1) 1.3 levels. That is the crown's top and the top of any head seen from the
   side: the albedo view's smear at the top of Ron's crown (`hair-views.jpg`), and thinner cards where the npc-day veil is.
3. **The strands' light: one light per card where the game has one per texel.** RealHair's forward pass (round 1's dump,
   `hairwork\realhair\fwd.txt`): t18 is `_NormalMap`, unpacked (x red times alpha, y green, times `_NormalStrength`, z
   lerp(1, sqrt(1 - x^2 - y^2), strength)) into the mesh's tangent frame (lines 168-207); that normal N gives the diffuse
   light, `saturate(dot(N, L)) / pi` (lines 1351, 1384: HDRP's Hair.hlsl `Lambert() * clampedNdotL`), the ambient light
   (the probes at N, lines 475-484) and the highlights' shift (`ShiftTangent(T, N, shift)`). Ron's hair has
   `Tex_H_m001_Normal` 1024x1024 at strength 0.5. The tracer lit the strand about its own direction alone (Kajiya-Kay's
   diffuse, the same for every texel of a card whose flow is even: Ron's flow map lies along -v within 3 degrees) and
   shifted the highlights about the strand's normal facing the camera: no strand of a card was lighter or darker than its
   neighbour, and the highlights were one smooth band, where the game's break up strand by strand.
4. **The highlights' maps are already the game's own** (share-2: flow, mask and specular map at the hit's footprint, the
   specular map being the 2048 base map); what they lacked is point 3's normal.
5. **Grain**: one light sample a frame per card (hair-4, item 4). Not changed.
6. **A frame late at the previous jitter (not changed, the next thing to try).** The composite lays the previous frame's
   traced light on this frame (`d3d11.hlsl composite`): it finds the texel through HDRP's motion vectors, which carry no
   jitter, so on a still picture each pixel takes the texel traced at last frame's jitter offset. A deferred surface's
   color is this frame's GBuffer (the light alone is late: harmless), but the hair layer's light carries its color and
   cover (as do eyes and anything the tracer shades forward). A model of a 2x temporal upscaler with Halton(2,3) jitter
   (`strandwork/jitter_sim.py`, on the game's crown and on Ron's base map) keeps 0.77 and 0.78 of the fine detail when each
   sample is a frame late against the same frame's. Fix idea: the composite (or the tracer's camera ray for the layer)
   takes the jitter difference into account (the tracer has both frames' jitter: `TGPT_Frame.jitter`).

## 3. The see-through top of the hair (npc-day, run `20261007-081218`)

- What it is: `head-day.jpg`, `strandwork/veil.jpg` (the game above, NRD below, 1:1). Over the skull the traced hair is
  solid; above the skull's outline (where HDRP's depth is the hillside, far behind) it is a veil, with a sharp line along the
  skull. Measured (box 0.046, 0.549, 0.098, 0.577, the hair's share by its red against the hillside beside it and the solid
  hair below): game all hair (1.9: brighter than the solid box), Ray Reconstruction 0.35, NRD 0.40. About 60 % of the
  hillside showed through.
- The character: `FirstPass_Mat_H_m001(Clone)` (Ron's hair mesh and map) on the layer ("traced as a layer, hidden by the
  mod" in the Kandra listing), scalp mask `FirstPass_ScalpMask_Male_07` as the shell. Not the brows' change of round 4
  (it needs "Brow" in the name); not the alpha rule (power 1, remap 1, prepass 0.5, clip 0.1, as the game's).
- Why: over the skull a ray meets a few cards, then the scalp mask and the skin. Above it the ray grazes along the top of the
  head through many thin cards, each seen at a slant (point 2 above made each thinner: its strands averaged with the gaps).
  The layer stopped after 8 cards with nothing after them, and the share they left showed the background. The game blends
  every card in front of its depth prepass's depth. The offline check reproduces it in small (section 5): sixteen cards of
  0.2 let 0.168 of a bright wall through with 8 and no more; the game's blend lets 0.028 through.
- The dark band across the hair: most likely the scalp mask's edge, which reaches a little past the skull's outline, seen
  through the thin cards (it lies under them, as in the game, where the cards hide it). With the cards solid it is covered.
  Not proven separately.

## 4. What changed

All in `native/pathtracing/shaders/pt_hair.hlsl` (LF kept):

- `strand_footprint` (was `strand_lod`): the pixel's footprint on the triangle, through the triangle's own map from its
  plane to its texture coordinates (both axes, so a stretched card is right too); the level of its short side, up to
  `STRAND_ANISO` (9) samples along the long side, the long side's level beyond 9:1, less the upscaler's bias as before.
  `strand_alpha` averages the samples, then RealHair's remap and prepass as before.
- `layer_tail`: past the `HAIR_LAYERS` (8) cards lit one by one, one more ray through every card behind them up to HDRP's
  surface (the shell's reach as before): each texel stops it with the chance its cover gives (`ray_chance`, salt
  `HAIR_TAIL_SALT`: the same draw when traversal reports a triangle twice), a whole texel always, the nearest kept; that
  card is lit (`shade_card`) and takes all the share the 8 left. Another surface of a hair instance ends it as before. The
  layer's cover is 1 then, or what the 8 left if the ray passed everything; the two average to the game's blend. Ray
  Reconstruction's guides describe the lit cards over the tail's share too (guides should be steady: NVIDIA's guide 3.5).
  `remaining <= 0.004` hands over to the tail too (it dropped that share before).
- `hair_normal`: the game's normal map (`TGPT_Material.normalTexture` and `normalScale`, which the plugin already sets for
  RealHair from `_NormalMap` and `_NormalStrength`, `TextureShare.Normal`), read as RealHair reads it at the mesh's own
  coordinates, laid in the card's texture frame about the strand's normal facing the camera (the tracer has no vertex
  normals for characters; the cards' flat faces lit each card as one piece). `hair_bump`: the diffuse light leans by the
  game's Lambert ratio, bumped normal against the strand's own, with a floor of 0.1 (for light from behind, the normal
  mirrored through the card); 1 exactly without a map. The highlights (`hair_specular`, unchanged) take the bumped normal
  for their shift and N.L as HDRP's do; the radiance cache is read at it; it is Ray Reconstruction's normal guide.
- `shade_card`: one card's shading (was inline in `layer`), shared by the lit cards and the tail's card.
- `tests/pathtracing_gpu_test.cpp`: three checks after the shell check (section 5). `tests/source_checks.py`: the pinned
  `strand_alpha` line follows; the anisotropy, the tail and the normal map pinned; the three checks' names pinned.
- `CHANGELOG.md` (CRLF kept): the entry at the top of the path tracing part.

## 5. Offline checks

Build `%TEMP%\tgb\strands-build` (Ninja, the share-2 configure line), installed to `strands-inst-<name>`; GPU runs waited
for no game, no live `RUNNING` and `-GpuCheck` free (`strandwork\gpu-run.ps1`). Mutants built by `strandwork\mutate.py`
(each hunk checked, the original's bytes restored and rebuilt: `strands-inst-final`, shader sha 4e7f833b404b).

Three new checks, after the shell check (the layer card's block of `tests/pathtracing_gpu_test.cpp`), each on the albedo or
lit view, pixel by pixel through the card's plane:

| check | this build (`strands-test-3.txt`) | its mutant | the game's / the bar |
|---|---|---|---|
| sixteen cards of 0.2 before a bright wall: the wall's share of the pixel (`47c`, `47d`) | 0.028 | mA (8 cards, nothing after): 0.173, fails | the game's blend 0.8^16 = 0.028; pass under 0.07 |
| a card of 32 strands (4 texels of 0.6, 4 of nothing; 2 m, 256 map) turned 80 degrees: spread of the hair's share over its pixels (`47e`) | 0.427 (mean 0.373) | mB (geometric mean, one sample): 0.122 (mean 0.253), fails | strands apart near 0.5, a veil under 0.2; pass over 0.3 |
| a white card with normal-map stripes tilted 0.6 to +u and -u, a lamp off its +u side: light toward / away (`47f`) | 7.2x (2.494 / 0.345) | mC (no normal map): 1.11x (the lamp's falloff), fails | RealHair's Lambert about the mapped normal; pass over 1.5x |

- Each mutant fails its own check and only it (mA, mB, mC: "FAILED 1 checks"); this build: "all path tracing checks
  passed", exit 0. Crops: `strandwork/tests-3.png` (the slanted card: red strands apart, and the mutant's pink veil, the
  npc-day look in small; the normal-mapped card: light and dark stripes, and the mutant's flat card).
- Unchanged checks the same: the layer card 643 red, 1121 grey (lit 0.0513 / 0.1308), the shell 754, 754, 746, 0.
- First run (`strands-test-2.txt`) failed the two new checks as first written: their cards were finer than a pixel's
  footprint could resolve (stripes 1.7 pixels a pair, a normal map read at 1.6 pixels a stripe) and the lamp nearly head-on
  (1.11x); made fair (a 2 m card, 32-texel stripes, a lamp off the side), not loosened.
- `python tests\source_checks.py`: 86 of 86. The plugin is untouched (not rebuilt). The D3D11 shaders are untouched.

## 6. What the next harness run should show

`.\tools\harness\Run-PtHarness.ps1 -Scenario hair-eyebrows,npc-day -Recipe views` after a deploy of this build.

1. npc-day (`head-day.jpg`): the top of the hair solid as the game's, no hillside through it, no dark band. The box
   0.046, 0.549, 0.098, 0.577 by the script in section 3: the hair's share near 1 (was 0.35 RR, 0.40 NRD). Without a
   denoiser the hair's outer edge may sparkle a little more (the tail's card is drawn by chance); a denoiser averages it.
2. hair-eyebrows: `python <scratchpad>\detail_boxes.py top=0.483,0.418,0.525,0.448 hair2=0.49,0.415,0.52,0.44 --
   <212905 1-game> <run>\hair-eyebrows\02-traced-rr <run>\hair-eyebrows\12-traced-no-denoiser` and the same with
   `strandwork\strand_detail.py`: the strand-aligned number and the coherence should rise toward the game's (0.253, 0.82),
   the strands' angle toward the game's 23 degrees; to the eye, thin light and dark strands along the flow where the
   tracer had clumps, a sharper top of the crown, highlights broken up strand by strand. The old crown box measures the wall.
3. `LogOutput.log`: a "Shared textures" line counting the hair's normal maps among those opened (RealHair's `_NormalMap`
   is registered like any normal map). If none is shared the normal-map part does nothing (no harm).
4. The exposure and gain line about as before: the normal map's lean averages to 1 over a strand; the solid top adds the
   hair's light where the background showed.

## 7. Risks

- Cost: up to 9 strand samples a candidate card at a slant (each a bilinear pair of levels), and one more ray (and one more
  card's light) for pixels whose 8 cards leave anything. Only hair pixels; not timed (other agents used the GPU).
- The tail's card is picked by chance: more grain at the hair's thick edges without a denoiser, averaged by one.
- The normal map's lean with a floor of 0.1 is the game's Lambert ratio, not the game's absolute light: where the light
  grazes the card the stripes can be contrasty (up to the floor's limit). If the owner finds strands too stripy, the floor
  or the strength is the knob.
- The guides call the tail's share hair whether or not the tail's ray found a card: for a ray that passed 8 cards that is
  almost always so.
- The bumped normal is Ray Reconstruction's normal guide now: it varies texel by texel like the game's normal-mapped
  surfaces (NVIDIA asks for the shading normal), but it is new for hair.

## Files, ABI, bits

`native/pathtracing/shaders/pt_hair.hlsl`, `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`, `CHANGELOG.md`,
this file. No ABI change (stays the main tree's 0x00011B00), no FrameConstants change, no new option or material bit, no
plugin change.
