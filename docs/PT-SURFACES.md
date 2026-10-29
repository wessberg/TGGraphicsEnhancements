# TG Path Tracing: round pots that look low-poly (PT-04) and the "bloomey" outdoors (PT-05)

Written 2026-10-06 by a review agent. Read-only work: this agent changed nothing in the mod, built nothing in the
tree and ran no game and no GPU test. State: proposed, except where a line says "in the tree". It answers PT-04 and
PT-05 of `docs/PT-REQUESTS.md` and touches PT-03, PT-12, PT-16, PT-32, PT-33, PT-35, PT-36, PT-38 and PT-40.

What it rests on: the harness's kept frames and pictures (`artifacts/harness/20261006-134130`, `-140217`, `-145526`,
`-155214`), one F8 capture of 2026-09-27 22:56, the tracer's code, HDRP's source (branch 6000.0/staging, read only) and
the game's own day and night code (TG.Main 1.30.012, decompiled, read only). Crops, the scripts behind the numbers and
a copy of that F8 capture are in `artifacts/agents/surfaces/` (not in git).

Line numbers are the tree's at 16:55 that day. The tree was being edited meanwhile, so look for the quoted text if a
number is off.

What happened to the tree while this was written (the main session, 16:35 to 16:50):

- B1 below is **in the tree**, in full, and in one place more than this file first had (a directional light in the
  lamp list, `pt_common.hlsli:1553`). `CHANGELOG.md` has it.
- B4 is **in the tree** (`HdrpPasses.cs:914`).
- Reflection Boost leaves grass and leaves alone (`docs/PT-VEGETATION.md`, change 2; `pt_prepare.hlsl:66`).
- B2 is **in the tree** in the third of its three forms: Reflection Boost raises the mirror share of metals only
  (`pt_common.hlsli:441`, `reflection_boost`; 16:50).
- A1, A2 and A3 are in no build. A1 and A2 were applied to a scratch copy of the tree of 16:50 and compiled there with the
  build's own DXC flags (`-WX`): 37 of 37 shader builds pass, each alone and both together
  (`scripts/shader_check.py`). They were not run on a GPU.

"Before tone mapping" means: a picture taken back through an approximate inverse of the game's tone curve (HDRP's
ACES), so that two pictures compare as light and not as screen levels. It is an estimate: good to about a fifth in
the darks, poor near white.

## The short answer

**A. Pots.** Four things make round things look low-poly. None is HDRP's normals on pots. None is a coarser LOD.

1. **Glass jars are drawn from flat triangles.** Some things the tracer draws from its own copy of the mesh, not
   from HDRP's picture: glass the game draws as see-through (the jars, windows, broken glass), and props the game
   draws outside its G-buffer. For those it takes each triangle's own flat direction as the normal. A round jar is
   then a prism: every face is one flat mirror. The far side of every glass bottle is bent the same way. The mesh's
   smooth normals are on the GPU already (they came with the caustics). They are not used here.
2. **A fade per flat face in the direct light** (`terminator_softening`), on every opaque thing. On half of each
   face it leaves the face's own flat light. It was meant for faces. Round props do not need it.
3. **With NRD, the filters stop at every face's edge.** They are steered by a normal made per flat face. So each
   face is smoothed alone and ends as one tone. Ray Reconstruction is not steered that way.
4. **At the tavern shelf with Ray Reconstruction, the cups are lit as smoothly as in the game.** They look flat
   because they are several times brighter there (the traced gain alone is x4). The tone curve squeezes a smooth
   ramp near white into one flat tone.

Changes: A1 the mesh's smooth normals for what the tracer draws itself. A2 the fade for skin only. A3 a one-line
candidate for the built-in filter, to test. Nothing for the exposure.

How sure: 1 is in the code and shows on the jars of `refract-jar` and on the bottle of `caustics-glass`. 2 is in the
code and in a model of its formulas; no kept picture isolates it. 3 and 4 are measured at the shelf. Which of 1 and 2
makes the candle wax of PT-40 faceted is not known: one run with two debug views tells (A.5).

**B. "Bloomey".** In order:

1. **The tracer's sun, moon and lamps were not dimmed by the game's fog. The game's are.** HDRP dims every light by
   its fog before it lights a surface. Measured directly at one shadow's edge that both pictures have: the game's
   sun on a wall is 19 to 23 % of the traced sun at the castle market, and 21 to 24 % on the harbour terrace. HDRP's
   fog rule gives 23 to 30 % there. At night it is the moon: about 2 % in the game, 100 % traced. At Cuanacht the
   fog is thin and the gap is small. **In the tree since 16:35** (B1).
2. **Reflection Boost 0.1.** It doubled how much every non-metal mirrors (4 % to 7.9 %) and took nothing away
   elsewhere. It was 46 to 63 % of the lift at Cuanacht and 18 to 26 % at Horns of the South. **Changed in the tree
   at 16:50**: only metals mirror more now (B2).
3. Reflections at full strength, where the game itself multiplies all its reflections by 0.5 outdoors and dims them
   again in corners. Not measured apart yet (it needs a Reflections Off step).
4. Bounced sunlight and no fake darkening in corners. Both are what path tracing is. The game has neither: by its
   settings no bounced light outdoors, and it darkens corners by a fixed rule, even in direct sun.

Not the cause: the sky's light (level with the game's in shade), Indirect Color (color only), the radiance cache, the
clamps, Fog Adaptation, Dark Adaptation. And not clouds: the game's cloud layer casts no shadows, and its sun stays
on in the rain (B.5).

B3 (the game's x0.5 on reflections) is the owner's call. B4 is in the tree.

How sure: cause 1 is in HDRP's own code, shows in six scenes, and is measured at two shadow edges with the size that
code predicts. Cause 2 is measured. Causes 3 and 4 are read from the game's settings and not measured apart.

---

## Part A. Vases, jars and cups that look low-poly

### A.1 What the pictures say

**The glass jars** (`refract-jar`, run `20261006-155214`). Crop: `A-40-jars-game-vs-traced.jpg`.

- In the game's picture the two jars are round and clear. Traced, their shoulders are flat panels with sharp edges,
  each of its own tone. A lamp's reflection is a white block on one face, where a round jar shows a thin streak.
- The run's log names the path: `Glass: "Mat_Prop_Jars_01" (HDRP/Lit) is transparent in the game: drawn by the
  tracer while tracing (seen through, refracted, tinted), HDRP's own passes hidden`. The same line is there for
  `MI_Window_Glass_02`, `Mat_Prop_BrokenGlass_01`, `Mat_Prop_BrokenGlass_01_Yellow` and
  `Mat_Crafting_Bottle_08_Glass`.
- (The big square blocks and the jars being opaque are PT-35, another matter.)

**A glass bottle** (`caustics-glass`, the baseline run). Crop: `A-45-terrace-bottle-body-game-vs-traced.jpg`.

- The game draws its bottles into the G-buffer, so the near side of a bottle has HDRP's smooth normal. Traced, the
  glass still shows flat panels with straight edges on its shoulder and body: what is seen through it breaks at
  every face of its far side.

**The candle wax** (`candle`, PT-40). Crop: `A-41-candle-wax-game-vs-traced.jpg`.

- The game's wax is smooth. Traced, it is crumpled into flat polygons, and the pool of wax at the foot is white
  faces with black ones between them. Which of the paths below draws the wax is not known (A.5 tells).

**The tavern shelf** (the stand-in for `glass-jars`: cups, bottles and a clay vase a few metres away; frames of the
14:55 run, kept as `artifacts/agents/surfaces/data/snap_*glass-jars*.npy`). There is no see-through jar in it.

- The traced gain there is x4.0 (x3.7 in the 13:41 run).
- With Ray Reconstruction the light across the cups and the right white bottle follows the game's curve. Scaled to
  their own peaks the curves lie on each other: the mean gap on the cups is 4 to 7 % of the peak. Crops:
  `A-11-cups-profiles-scene-linear.png`, `A-13-cups-ratio-rr-nrd-over-game.png`,
  `A-32-shelf-bottles-vase-profiles.png`.
- With NRD there are steps at the faces' edges that the game's curve does not have: 14 % of the peak in one pixel on
  the right white bottle (the game's 10 %, Ray Reconstruction's 9 %), 38 % on the left one (9 % and 16 %), and the
  vase's neck is three level steps (0.63, 0.81 and 1.0 of its peak; Ray Reconstruction leaves one short level
  stretch, the game's curve none). Crops: `A-42-shelf-vase-bottles-game-rr-nrd.jpg`,
  `A-43-shelf-white-bottle-game-rr-nrd.jpg`, `A-44-shelf-vase-game-rr-nrd.jpg`.
- On screen the lit side of a cup sits at 106 of 255 in the game's picture and at 219 traced (left cup; 120 and 234
  for the right one). The part of the ramp from 30 % to 100 % of its peak spans 71 levels in the game's picture and
  41 traced (76 and 28 for the right cup). The roundness is still in the light. The tone curve flattens it.
- The proof by picture: `A-12-cup-left-same-exposure-6x.png`. Top row: the game's cup, and the traced cup shown at the
  game's exposure: both round. Bottom row: the game's own cup shown at the traced exposure, and the traced cup: both
  have the two flat faces.

**The jug and the bowl at Horns of the South** (bottom right of the `npc-day` view, 400 pixels wide; frames kept in
`artifacts/harness/20261006-140217/npc-day`). Crops: `A-20-hos-jug-*.jpg`, `A-20-hos-bowl-*.jpg`, and with the
texture divided out `A-23-hos-jug-game-traced-ratio.png`, `A-24-hos-bowl-game-traced-ratio.png`.

- The shadow line on the jug's body is a straight edge along the mesh's faces. Inside the bowl the rim's shadow is a
  row of straight pieces. In the game's picture both are smooth.
- It stays when the traced picture is shown at the game's exposure, so it is not the tone curve this time.
- The jug has about 12 to 16 sides: its ribs are straight pieces with corners in the game's own picture too
  (`A-26-hos-jug-ribs-game-vs-traced-3x.png`).
- The tracer's sun was three to four times the game's at that place (part B). That is why the game's picture hides
  the mesh and the traced one shows it.

### A.2 The mechanisms in the code

**1. Flat normals where the tracer draws from its own mesh.**

- What a ray brings back from a hit is the triangle's own normal and nothing smoother:
  `native/pathtracing/shaders/pt_common.hlsli:619` ("geometric"), made at
  `:735-742` (`fetch_hit`: the cross product of the triangle's edges).
- See-through glass: `pt_prepare.hlsl:36-38`. That normal is stored as the shading normal and as the
  geometric one. The reflection (`pt_specular.hlsl:56`, at a mirror's roughness of 0.03) and the way into the glass
  (`:102`) both use it.
- The way out of glass, for every glass thing, bottles too: `pt_specular.hlsl:116` (`far.normal`).
- Props the game draws outside its G-buffer, found by the camera ray: `pt_prepare.hlsl:177-178`. Only eyes
  and hair keep HDRP's normal. The comment above says why: "the traced mesh has only its faces'". That was true
  before the caustics. Not any more.
- Things seen in a reflection or through glass are lit by their triangle's normal too
  (`pt_common.hlsli:1893`, `shade_path`). Not proposed for now (A.4).
- The smooth normals are there: `src/TGPathTracing/SceneCapture.cs:252-257` sends every static mesh's vertex
  normals, `native/pathtracing/src/scene.cpp:207` and `:374` keep them on the GPU, and the caustics read them
  (`pt_common.hlsli:1192`, `caustic_vertex`). Characters' meshes bring none; they stay as they are.
- When: `CHANGELOG.md:209` ("Glass the game draws as transparent (the alchemist's jars, windows, potions ...) is
  drawn by the tracer"). Before that HDRP drew the jars, round. That fits the owner's "now".

**2. The fade per face.** `pt_common.hlsli:423-433`, `terminator_softening`. It is used for every surface that
is not a leaf or cloth: lamps at `pt_rtxdi.hlsli:141`, the sun at `pt_di_spatial.hlsl:81`.

- It compares HDRP's smooth normal with a "geometric normal" that `pt_prepare.hlsl:68-89` makes from HDRP's
  depth. On a pot that is one direction per flat face, with a jump at every edge.
- Where a face leans less to the light than the smooth normal does, the factor times the smooth light comes out as
  the face's own flat light (the smooth cosine cancels). So half of every face is lit flat, and the light jumps at
  the face's edge.
- A numpy model of exactly these formulas (`scripts/a_model.py`, plot `A-30-terminator-model.png`):

| pot | largest step at a face's edge | light lost |
|---|---|---|
| cup, 12 sides, 8 cm, 3 m away | 0.20 to 0.24 of the full light | 3 to 5 % |
| jug, 20 sides, 30 cm, 1.2 m away | 0.11 to 0.14 | 1 to 2 % |
| pillar, 12 sides, 1 m, 6 m away | 0.20 to 0.24 | 3 to 5 % |

- The steps are largest next to the shadow line, where the light is weakest, so there they are a third to all of
  it. A face that just faces away from the light goes fully dark from edge to edge, which makes the shadow line a
  straight line along the mesh.
- Why props do not need it: the factor is there to hide a shadow ray that starts on a flat face and is stopped by
  the pot itself. But the tracer starts those rays 2 cm plus 0.2 % of the distance outside the surface
  (`pt_common.hlsli:960`, `ray_offset`; `:333-335`, `shadow_origin`). In the same model no such ray is stopped by
  the pot, at any of the three sizes, wherever the smooth normal says "lit". Without the factor the result is the
  smooth ramp, as in the game.
- Why it was added: `CHANGELOG.md:738-742`: jagged shadow edges across faces, caused by shadow rays that started
  4 cm inside the head. Two fixes went in together: rays start outside, and this fade. The first fixed the cause.
  The second was never tested alone. `docs/PROJECT-CONTEXT.md` 9.7 lists it under skin, but the code applies it to
  everything.

**3. The filters and the per-face normal.**

- The built-in filter weighs a neighbour by that per-face normal to the power of 64
  (`pt_atrous.hlsl:57`). Two faces of a 20-sided pot are 18 degrees apart: weight 0.04. Of a 12-sided one:
  0.0001. So it never smooths across a face's edge, and what it smooths inside a face ends as one tone.
- NRD is handed the same normal (`pt_nrd_prep.hlsl:42`). That is a measured choice (its header: a normal map's
  normals made its history fail, "three times the flicker"), and `tests/source_checks.py:1224` pins it.
- With NRD the built-in filter still does the direct light and the reflections. With Ray Reconstruction neither
  filter runs, and its guide normal is HDRP's smooth one (`d3d11.hlsl:754`, `:790`).

**4. The exposure** (the shelf). `src/TGPathTracing/HdrpPasses.cs:516-533`, `AutomaticGain`. At the shelf: the
picture's median is very dark (log2 -7.1), Dark Adaptation 50 % asks for 2.0 stops, and the rule that keeps the
brightest fiftieth under 1.4 times white (`Highlight`, line 501) allows 2.1. The cups are in that brightest fiftieth.
This is a look, not a defect in the shading. It belongs with PT-16 (the tavern's haze) and the open question whether
the tavern is too bright.

### A.3 What it is not

- **HDRP's normals on pots.** For what is in the G-buffer the tracer shades with HDRP's smooth normal buffer
  (`pt_prepare.hlsl:49`). The Normals view of the F8 capture is smooth across a round pole.
- **A coarser LOD.** Within 30 m the finest LOD is traced (`src/TGPathTracing/SceneCapture.cs:196-204`). And for
  what is in the G-buffer the faces that show come from HDRP's own depth, not from the traced mesh.
- **Bounced light.** Its rays that point under a flat face count as dark (`pt_gi.hlsl:38`). On a 12-sided pot that is
  under 2 % of them. Too little to see.
- **Ray Reconstruction's guides.** They use HDRP's smooth normal.

### A.4 The changes

**A1. The mesh's smooth normal for what the tracer draws itself.** Priority 1. Shaders only. No new bit.

`native/pathtracing/shaders/pt_common.hlsli`, `struct Hit` (line 619), add one field after `normal`:

```hlsl
    float3 normal;     // geometric, world, towards the ray's origin
    float3 rounded;    // the mesh's own smooth normal there (its vertices', TGPT_MeshDesc), on the same side; the geometric one where the mesh brings none
```

`pt_common.hlsli`, `fetch_hit`, after line 742 (`h.normal = ...`), add:

```hlsl
    // The mesh's smooth normal: its vertices' normals over the triangle, to the world by the transform's cofactors (as
    // caustic_vertex takes them), on the side of the triangle the ray met. What the tracer draws from its own mesh (glass
    // HDRP draws as a transparent surface, a prop HDRP draws forward) is shaded with it: by the triangle's own normal a
    // round jar was a prism, each of its faces one flat mirror.
    h.rounded = h.normal;
    if (mesh.normals != INVALID) {
        ByteAddressBuffer vn = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.normals)];
        const float3 local = unpack_normal(vn.Load(idx.x * 4)) * (1.0 - bary.x - bary.y) + unpack_normal(vn.Load(idx.y * 4)) * bary.x + unpack_normal(vn.Load(idx.z * 4)) * bary.y;
        const float3 r0 = objectToWorld[0].xyz, r1 = objectToWorld[1].xyz, r2 = objectToWorld[2].xyz;
        const float3 world = mul(float3x3(cross(r1, r2), cross(r2, r0), cross(r0, r1)), local);
        const float worldLength = length(world);
        if (worldLength > 1e-12) h.rounded = (dot(world, h.normal) < 0.0 ? -world : world) / worldLength;
    }
```

`pt_common.hlsli`, before `trace_closest` (line 881), add:

```hlsl
// A smooth normal turned to face the viewer where the curve it describes passes the outline of the flat triangle it lies
// on (toViewer: unit, from the surface): at least 3 degrees to the viewer's side, so the surface reflects and is lit there.
float3 facing_normal(float3 n, float3 toViewer) {
    const float c = dot(n, toViewer);
    return c >= 0.05 ? n : normalize(n + toViewer * (0.05 - c));
}
```

`native/pathtracing/shaders/pt_prepare.hlsl:36-38`, old:

```hlsl
            const float3 n = glassHit.normal; // towards the camera
            surf[p] = float4(glassHit.position, length(glassHit.position - g.cameraPos.xyz));
            nrm[p] = uint2(pack_normal(n), (uint(0.03 * 255.0 + 0.5) & 0xFF) | 0x100u | (pack_normal22(n) << 10));
```

new:

```hlsl
            // Reflected and seen through by its smooth normal (the mesh's vertex normals); rays start from its triangle's own.
            const float3 n = facing_normal(glassHit.rounded, -toSurface / max(surfaceDist, 1e-6));
            surf[p] = float4(glassHit.position, length(glassHit.position - g.cameraPos.xyz));
            nrm[p] = uint2(pack_normal(n), (uint(0.03 * 255.0 + 0.5) & 0xFF) | 0x100u | (pack_normal22(glassHit.normal) << 10));
```

`pt_prepare.hlsl:177`, old and new (and correct the comment above it: a character's mesh brings no normals, a
prop's does):

```hlsl
                normal = (eyeSurface || (strands && !behindShown)) && dot(normal, hit.normal) > 0.5 ? normal : hit.normal;
```
```hlsl
                normal = (eyeSurface || (strands && !behindShown)) && dot(normal, hit.normal) > 0.5 ? normal : eyeSurface || strands ? hit.normal : facing_normal(hit.rounded, -dir);
```

`native/pathtracing/shaders/pt_specular.hlsl:116`, old and new:

```hlsl
            const float3 leaving = refract(t, far.normal, GLASS_IOR);
```
```hlsl
            const float3 leaving = refract(t, facing_normal(far.rounded, -t), GLASS_IOR);
```

Source checks: `tests/source_checks.py:2044` pins the old `refract` line: change it there. Line 1906 pins an older
form of the `normal = ...` line (with `!passed`) that the tree no longer has; it fails today (see "Seen on the way").

On screen: a glass jar reflects and bends light as a round thing. A lamp in it is a thin streak again. The panels
inside the glass of bottles go. Wax and other props drawn outside the G-buffer are shaded smoothly, if that is their
path.

What could go wrong:

- **A jar will bend what is behind it much more.** The tracer treats glass as solid between its two sides. A hollow
  jar then works as a thick lens. With flat faces that showed as a prism. With smooth normals it will show as a
  strong lens: the wall behind stretched or flipped. That is PT-35's subject, not a reason to keep the faces.
- A mesh whose own normals are hard on purpose (a cut gem, a lantern's panes) stays faceted, as HDRP draws it. Right.
- A mesh with broken normals falls back to its triangles (the length test).
- A2 belongs with it for props drawn outside the G-buffer: with smooth normals and the fade still on, half of each
  face would stay flat (cause 2).
- It costs three small reads per hit. In the compile check only `pt_prepare`, `pt_specular` and `pt_gi` changed
  size; every other pass compiled to the size it had.

Later, same idea: `shade_path` (`pt_common.hlsli:1893`) lights what a reflection or glass shows by the triangle's
normal. A pot in a mirror or behind a window is faceted there. `hit.rounded` can serve for the cosine; the ray's
start must stay on the triangle's side. Source checks pin those lines. Look at A1 in game first.

**A2. The fade for skin only.** Priority 2. Shaders only.

`native/pathtracing/shaders/pt_common.hlsli`, the `Surface` struct (line 304), add one field:

```hlsl
    float2 hairLobe;
    bool skin;         // HDRP's skin (a solid diffusion profile): terminator_softening is for it alone
};
```

`pt_common.hlsli`, `load_surface`, after line 363 (`s.eye = ...`), and in the `else` at line 382:

```hlsl
        s.eye = (bits & 8u) != 0;
        s.skin = (bits & 128u) != 0;
```
```hlsl
        s.albedo = 0.5; s.f0 = 0.04; s.forward = false; s.complex = false; s.glass = false; s.transmittance = 0.0; s.hdrpShaded = false; s.eye = false; s.skin = false;
```

`pt_common.hlsli:427-428`, old:

```hlsl
float terminator_softening(Surface s, float3 l) {
    if (s.translucent) return 1.0;
```

new (the first two lines stay, one is added):

```hlsl
float terminator_softening(Surface s, float3 l) {
    if (s.translucent) return 1.0;
    // Skin only. On the world's round props (a cup, a jug, a pillar: a dozen flat faces under smooth normals) the fade is
    // per face: where a face leans less to the light than the smooth normal, the light it leaves is the face's own flat
    // cosine, with a step at every face's edge. Their shadow rays start ray_offset outside the surface and are not
    // stopped by the prop itself, so there is nothing for it to hide.
    if (!s.skin) return 1.0;
```

`native/pathtracing/shaders/pt_prepare.hlsl`, after line 266 (`const bool thin = ...`) and in line 272:

```hlsl
    const bool skin = deferred && !forward && solid_transmission(s, feature);
```
```hlsl
                                           (hdrpShaded ? 32u : 0u) | (hairLobes ? 64u : 0u) | (skin ? 128u : 0u)) / 255.0)));
```

`tests/source_checks.py:1571-1585` (`test_path_tracing_shadow_terminator`) pins the old lines. They all stay; add a
check for `if (!s.skin) return 1.0;` and correct its comment.

The flag: `docs/PT-VEGETATION.md` (its change 3) asks for the same flag 128, the last free one of that byte. Only one
of the two can have it. A2 does not need it. The skin flag can live in the byte beside the color instead, which today
holds only "drawn forward" as 0 or 255 (`pt_prepare.hlsl:269`, read at `pt_common.hlsli:355` and nowhere else):

```hlsl
    mat[p] = uint2(pack_rgba8(float4(color, forward ? 1.0 : skin ? 0.25 : 0.0)),
```
```hlsl
        s.forward = color.a > 0.5;
        s.skin = color.a > 0.125 && color.a < 0.375;
```

(and no `| (skin ? 128u : 0u)`, no `s.skin = (bits & 128u) != 0;`). Both forms pass the compile check.

On screen: round props lit by a lamp or the sun get their smooth ramp back, and the shadow line on them is a curve
again. Clearest on the pots at Horns of the South.

What could go wrong:

- Parts of characters that are not skin (armour, cloth) lose the fade too. Their triangles are small, so no step is
  expected. Look at `metal` and `npc-day`.
- Very coarse curved things with faces wider than about half a metre and sharp bends (a boulder, an arch) can get
  the old hard step back along the shadow line. It is at most the sine of half the bend of the full light, in the
  dimmest part. Look at a boulder with the sun low over it, Direct view.
- A simpler variant is to drop the fade for skin as well. Not proposed: the owner accepted faces with it.

**A3. The built-in filter across the faces of one smooth thing.** A candidate, to test. It changes the picture only
with NRD and with the built-in denoiser.

`native/pathtracing/shaders/pt_atrous.hlsl:57`, old and new:

```hlsl
            float wn = pow(saturate(dot(s.geoNormal, sq.geoNormal)), 64.0) * pow(saturate(dot(s.normal, sq.normal)), normalPower);
```
```hlsl
            // An edge is where the surface itself turns, by its own orientation or by its smooth normal, whichever turns less:
            // across the flat faces of a round pot the smooth normal barely turns, and the pot is filtered as the round thing it is.
            float wn = max(pow(saturate(dot(s.geoNormal, sq.geoNormal)), 64.0), pow(saturate(dot(s.normal, sq.normal)), 64.0)) * pow(saturate(dot(s.normal, sq.normal)), normalPower);
```

It compiles. A flat wall with a normal map is filtered as before (its own orientation agrees). A real corner still
stops the filter (both normals turn). What could go wrong: the choice of the per-face normal has harness numbers
behind it (`CHANGELOG.md`, "geometric normals and a spatial variance estimate remove the grain"). Run the offline
numbers before and after. A bevelled edge with smoothed normals is now filtered across. NRD's own guide stays as it
is: its choice is measured and pinned.

**A4. Nothing proposed for the exposure at the shelf.** It is the owner's look (Dark Adaptation, Exposure). Recipe
A.5 shows the cups at the game's exposure.

**The straight pieces in traced shadows** (the rim's shadow inside the bowl) are the mesh's real outline. Nothing
cheap fixes that. With B1 the sun is three to four times weaker at that place and they fade as they do in the game.
Later, if the boulder case shows: the proper cure is to move the shadow ray's start onto the smooth surface the
vertex normals describe (Hanika, "Hacking the Shadow Terminator", Ray Tracing Gems II). It needs a camera ray per
pixel in `pt_prepare`.

### A.5 Harness recipes that settle it

**Which path draws a thing.** Run at `refract-jar`, `candle` and `caustics-glass`.

```json
[
  {"name": "1-game", "set": [{"key": "Mode", "value": "Off"}], "traced": false, "settle": 3, "frames": 8},
  {"name": "2-traced", "set": [{"key": "Mode", "value": "On"}, {"key": "Denoising", "value": "Ray Reconstruction"}, {"key": "Debug view", "value": "None"}], "traced": true, "settle": 6, "settleFrames": 240, "frames": 8},
  {"name": "3-surfaces", "set": [{"key": "Debug view", "value": "Surfaces"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "4-normals", "set": [{"key": "Debug view", "value": "Normals"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "5-direct", "set": [{"key": "Debug view", "value": "Direct"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "6-specular", "set": [{"key": "Debug view", "value": "Specular"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "7-off", "set": [{"key": "Mode", "value": "Off"}, {"key": "Debug view", "value": "None"}], "traced": false, "settle": 1, "frames": 0}
]
```

How to read it:

- **Surfaces** (`pt_compose.hlsl:187`): cyan is a thing the tracer draws from its own mesh, outside the G-buffer.
  In the G-buffer: grey is plain Lit, blue is subsurface scattering, green and red are HDRP's see-through-light
  materials with a thin profile, orange the same with a solid profile (skin). (See-through glass shows the color of
  what is behind it.)
- **Normals** shows the normal the tracer shades with. Flat faces on the jars today; smooth after A1.
- For the wax:
  - cyan in Surfaces and faces in Normals: cause 1, and A1 cures it;
  - grey or blue, smooth Normals, faces in Direct: cause 2, and A2 cures it;
  - orange: A2 keeps the fade there, as on skin. If Direct shows faces, the fade must tell skin from wax (by the
    profile's number);
  - green or red, smooth Normals: neither. Then the next suspects are the rays' starts, which are set per flat face
    (`shadow_origin`, `pt_common.hlsli:333`; `pt_gi.hlsl:26`).

**Pots in the G-buffer.** Run at `glass-jars` and at `npc-day` (the pots: region 0.76, 0.82, 1.0, 1.0). A save
`PT pots` would be better: round pottery in direct sun, one step away.

```json
[
  {"name": "1-game", "set": [{"key": "Mode", "value": "Off"}], "traced": false, "settle": 3, "frames": 8},
  {"name": "2-traced", "set": [{"key": "Mode", "value": "On"}, {"key": "Denoising", "value": "Ray Reconstruction"}, {"key": "Debug view", "value": "None"}, {"key": "Exposure", "value": "0"}], "traced": true, "settle": 6, "settleFrames": 240, "frames": 8},
  {"name": "3-direct", "set": [{"key": "Debug view", "value": "Direct"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "4-indirect", "set": [{"key": "Debug view", "value": "Indirect"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "5-specular", "set": [{"key": "Debug view", "value": "Specular"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "6-normals", "set": [{"key": "Debug view", "value": "Normals"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "7-game-exposure", "set": [{"key": "Debug view", "value": "None"}, {"key": "Exposure", "value": "-2"}], "traced": true, "settle": 8, "settleFrames": 240, "frames": 8},
  {"name": "8-traced-nrd", "set": [{"key": "Exposure", "value": "0"}, {"key": "Denoising", "value": "NRD"}], "traced": true, "settle": 8, "settleFrames": 240, "frames": 8},
  {"name": "9-off", "set": [{"key": "Mode", "value": "Off"}, {"key": "Denoising", "value": "Ray Reconstruction"}, {"key": "Debug view", "value": "None"}], "traced": false, "settle": 1, "frames": 0}
]
```

How to read it (the gain holds still while a debug view is on, so the views compare with step 2):

- **Direct** settles cause 2. Today: steps from face to face near the shadow line, and a straight shadow line.
  After A2: a smooth ramp. (The view has no texture in it, so steps are easy to see.)
- **Normals** must be smooth here. If it shows faces, the thing is drawn by the tracer and it is cause 1.
- **Indirect** should be smooth. **Specular** shows whether reflections add faces.
- **7-game-exposure** at the shelf: gain x4 and Exposure -2 make x1. The cups should look as round as the game's.
- **8-traced-nrd** against step 2 is cause 3.

---

## Part B. Why outdoors looks "bloomey"

### B.1 The numbers

Light before tone mapping, traced over game, by how dark the game's pixel is (`why-bright` run, 14:04, before B1):

| the game's pixels | Cuanacht | Cuanacht, Reflection Boost 0 | Horns of the South | there, Boost 0 |
|---|---|---|---|---|
| darkest 5 % | x2.03 | x1.56 | x2.73 | x2.42 |
| 5 to 25 % | x1.67 | x1.25 | x2.72 | x2.35 |
| 25 to 50 % | x1.43 | x1.20 | x1.92 | x1.68 |
| 50 to 75 % | x1.13 | x1.07 | x1.25 | x1.17 |
| brightest quarter (mostly sky) | x1.02 | x1.02 | x1.03 | x1.02 |

The sky and the fog are the same in both pictures (x1.01), as they should be: they are HDRP's.

Chosen surfaces, red / green / blue (boxes drawn in `B-02-*-regions.jpg`, `B-11-*-regions.jpg`):

| Cuanacht | game to traced |
|---|---|
| cart, the boards facing the camera (the side away from the sun) | x2.0 / x1.6 / x0.8 |
| soil under the cart, in its traced shadow | x0.60 / x0.54 / x0.55: darker than the game's |
| open soil beside it | x1.85 / x1.66 / x1.48 (x1.36 / x1.19 / x1.02 at Boost 0) |
| right wheel, the wooden face | x2.0 / x1.9 / x1.9 |
| rock, shaded left face | x1.56 / x1.49 / x1.39 |
| big rock, face with crevices | x1.36 / x1.23 / x1.09 |

| Horns of the South (rain) | game to traced |
|---|---|
| big rock, steep dark face | x1.46 / x1.39 / x1.44 |
| crevice under its overhang | x2.2 / x2.1 / x2.2 |
| rock top with moss, facing up | x2.2 / x2.1 / x2.2 |
| flat wet rock | x2.1 / x1.9 / x2.1 |
| wooden pole | x4.0 / x4.0 / x4.4 |
| grass, near | x5.1 / x4.7 / x6.9 |
| clay pot | x3.4 / x3.4 / x3.6 |
| tree trunks in the fog | x1.11 / x1.11 / x1.14 |

Is the lift sky-colored? No.

- At Cuanacht the added light is warm: red to green 2.2, blue to green 0.34 (the game's own light there: 1.65 and
  0.54). The sun there is 3585 K, orange. On the cart's shaded boards the blue even goes down.
- At Horns of the South it is plain white: 1.07 and 0.89 (the game's own: 0.91 and 0.86). The sun there is 7340 K.
- So the lift has the sun's color in both places, not the sky's.

### B.2 Cause 1: the game's fog dims its sun, the tracer's sun was not dimmed

**What HDRP does.** In its light code (`Runtime/Lighting/LightEvaluation.hlsl`), `EvaluateLight_Directional`
multiplies the sun by the fog's transmittance (how much light gets through) from the lit point up along the sun's
direction, and `EvaluateLight_Punctual` does the same for every lamp over the way to the lamp. The fog is the height
fog of the Fog volume: `1 / mean free path` at and below its base height, thinning above it with a scale height of
0.144765 times the layer's depth (`VolumeRendering.hlsl`, `OpticalDepthHeightFog`; `Fog.cs`,
`UpdateShaderVariablesGlobalCBFogParameters`). This is always on while fog is on. It is not the visible haze: it is
light taken from the sun before it reaches a surface.

**What the game does with it.** Its day and night system puts the fog's base at the hero's height at all times
(`DayNightSystem.HandleFog`), so the hero always stands in the thickest part. The sun's strength in the lights' list
(90,000 lux at Horns of the South, 117,000 at Cuanacht, a "moon" of 60,000 lux at night) is what the artists set
with that fog in front of it.

**What the tracer did.** It takes the sun's color times intensity times dimmer
(`src/TGPathTracing/SceneLights.cs:316-321`,
`:437-451`) and lit with all of it. It already knew the fog: `HdrpPasses.cs:385-405` reads the same
three numbers and `pt_common.hlsli:533` (`height_fog_depth`) has the same formula, used only for the gain.

**The direct test.** Take one sun-shadow edge that both pictures have, on one surface. The step in light across it is
the sun's own light and nothing else: sky light, bounce and reflections are the same on both sides. (Script:
`scripts/c_edge2.py`. Crops: `B-21-market-wall-shadow-edge-game-vs-traced.jpg`,
`B-20-terrace-shadow-edge-game-vs-traced.jpg`.)

| place (baseline run 14:55) | lit over shaded, the game's | lit over shaded, traced | the game's step over the traced one | what HDRP's fog rule leaves there |
|---|---|---|---|---|
| castle market, a building's shadow on a wall, in rain (0.68) | x1.74 | x6.3 | 0.19 (RR), 0.23 (NRD) | 0.23 at the hero's height, 0.26 ten metres up (sun 34 degrees up) |
| harbour terrace, a table's shadow on the floor | x1.7 | x3.2 | 0.21 (RR), 0.24 (NRD) | 0.30 (sun about 43 degrees up) |

- The same edge, the same place in both pictures. So the game's sun is there, with its shadow, four to five times
  weaker than the traced one. The second run (`20261006-155214`) gives the same numbers at the market.
- It is grey: the same share in red, green and blue (0.19, 0.19, 0.18 at the market). Fog dims so.
- The game's sun is a little weaker still than the fog rule says (by a tenth to a quarter). Likely causes, not
  measured apart: the traced sun also makes a highlight that Reflection Boost doubles (the terrace floor is glossy),
  and the game takes some direct light away near things (its ambient occlusion works on direct light at 0.75).
- The sun's height: read from the hero's own shadow in the traced picture (the camera sits in the hero's head, so
  the head's shadow lies where the sun's rays point): 34 degrees at the market at 10:29, 32 at the beach at 09:10
  (`B-22-*-hero-shadow-*.jpg`, `scripts/b_sun_height.py`). The game's day formula (`DayNightSystem`:
  `HandleLightsRotation`, and `HandleShadowCasterRotation` with its 30 degree floor) fits that with a tilt of 46
  degrees and gives the other saves' heights.

**How much, place by place** (before B1; the sun's height by that formula):

| place (baseline run 14:55) | time | fog's mean free path | sun or moon up | the fog leaves | traced over game, sunlit ground |
|---|---|---|---|---|---|
| Cuanacht (`vista`) | 13:32 | 682 m | about 44 degrees, if the tilt is the same there | 0.81 (0.75 to 0.85) | x1.3 at Boost 0 |
| beach (`water-day`) | 09:10 | 204 m | 30 to 32 degrees | 0.31 to 0.34 | x2.05 to x2.16 |
| harbour terrace (`caustics-glass`) | 12:47 | 145 m | 43 degrees | 0.30 | x2.1 to x2.6 |
| castle market (`metal`), rain | 10:29 | 144 m | 34 degrees | 0.23 | x1.95 to x2.9; deep shade x1.03 |
| Horns of the South (`npc-day`), rain | 14:13 | 144 m | 44 degrees | 0.30 | x2.0 to x2.2 |
| Horns of the South, `night` | 23:21 | 9 m | 30 degrees (the floor) | 0.02 | moonlit planks x5.4, in the rail's shadow x2.3 (the gain's x1.4 is in both) |

The castle market is the cleanest case: every sunlit surface is x2 to x2.9, and the deep shade under an awning is
x1.03. More sun, the same shade.

**The pictures.**

- `B-08-hos-f8-views.jpg`: the F8 capture at the laundry poles in the rain, one camera for all views. The game's
  picture: an overcast day. The traced one: a sunny day with a hard shadow of the laundry on the grass. The Direct
  view: the sun, full strength. In the laundry's shadow the diffuse light is level (0.041 in the game's picture;
  0.069 traced, half of it reflections). In the sun the grass is x3.2.
- `B-10-baseline-metal-game-vs-traced.jpg`, `-water-day-`, `-caustics-glass-`: soft daylight in the game, hard bright
  sun traced.
- `B-10-baseline-night-game-vs-traced.jpg`, `B-23-night-bridge-game-vs-traced.jpg`: a torch-lit night in the game, a
  cold moonlit scene with hard shadows traced. There is a second cause in that picture: see PT-38 under "Seen on the
  way".
- `B-10-baseline-fog-game-vs-traced.jpg` (Broc Meala): the far cliffs are dark shapes in the game's picture and flat
  light grey traced. That is PT-33, which the main session traced to Fog Adaptation and fixed the same afternoon
  (`CHANGELOG.md`). The sun's fog can add to it on rocks the fog does not hide entirely. Not measured apart.

Lamps: small indoors, real in dungeons and at night. What the fog leaves of a lamp 10 m away: 0.94 in the tavern,
0.85 in Sagremor Castle, 0.38 at night at Horns of the South.

### B.3 Cause 2: Reflection Boost

As it was in every picture here (until 16:50): roughness went a tenth of the way to 0.05, and the mirror share (f0)
a 0.07 share of the way to 0.6, on everything but skin.

- A non-metal: 4 % to 7.9 %. Almost double.
- A metal: 70 % to 69 %. Unchanged (it only got a tenth glossier). That was the opposite of PT-32.
- Nothing was taken from the diffuse color, so it was light added. On a dark surface under a bright sky that is a
  lot: soil reflects 6 to 10 % diffusely, and the boost added about 4 points of mirror.

Measured: 46 to 63 % of the lift at Cuanacht, 18 to 26 % at Horns of the South (table B.1).

As it is in the tree now (`pt_common.hlsli:441`, `reflection_boost`; called at `pt_prepare.hlsl:67` and `:185`,
written out again at `d3d11.hlsl:773`): everything gets the gloss, a metal twice; only a metal mirrors more. A
non-metal keeps its 4 %. Grass and leaves get no boost at all (16:38).

### B.4 Causes 3 and 4: what the game had there

From the harness's state files (`*/1-game/audit-a.txt`):

- **Reflections x0.5.** `IndirectLightingController.reflectionLightingMultiplier` is 0.5 outdoors (Cuanacht, the
  beach, the harbour) and rises with rain (0.76 to 0.96); 1.0 indoors. It scales every reflection HDRP adds. The
  tracer's reflections ignore it (`pt_specular.hlsl:56-91`). The rain raising it is the game's "wet" look.
- **A dial on the sky's own light too.** `indirectDiffuseLightingMultiplier` is 1 in most places, 1.5 on the harbour
  terrace (the Fisherman Village's volume; the Sunken Village, the swamp and two more volumes have 1.5 too) and 2 at
  the All-Mother's shrine. The tracer's sky light does not follow it (`HdrpPasses.cs:188` turns HDRP's off; the
  traced sky is the sky's picture times Sky Intensity). That makes traced shade darker there, not brighter.
- **Corners darkened by rule.** Ambient occlusion: intensity 1, radius 3 m, and 0.75 of it on *direct* light too
  (`directLightingStrength`); specular occlusion 0.5; micro shadows 0.26. The tracer turns these off
  (`HdrpPasses.cs:187-192`) and traces real shadows. So crevices and the ground under grass get their real light,
  which is more than the game gave them.
- **No bounced light**, as far as the state file shows: no probe volumes (the camera's frame setting is off),
  screen-space GI off. The game's shade is the sky's light, the same on every side. The tracer adds the sun's bounce
  off the ground. That is the warm x1.5 to x2 on the cart's shaded boards.

The other sign of the same thing: where walls close a place in, the traced picture is darker than the game's, because
the game's sky light ignores walls (the shaded side of the market wall: x0.66 to x0.72; `doorway`; PT-39).

The traced reflection itself is standard (one GGX ray, Fresnel going to 1 at flat angles for anything that mirrors
2 % or more, `pt_common.hlsli:452-455`). On the open soil at Cuanacht with Boost 0 its excess over the game's is
small: the added light has no blue in it, and the sky there has.

### B.5 Checked and not the cause

- **The sky's light.** Rays that leave the scene read HDRP's own sky cubemap, in HDRP's units, times Sky Intensity
  (`pt_common.hlsli:1516`, `sky_radiance`; `tracer.cpp:1227-1247`; `d3d11.hlsl:154-173`). In deep shade the two
  pictures are level or the traced one is darker (the awning x1.03, under the cart x0.6, the laundry's shadow).
- **Clouds.** The game's cloud layer casts no shadows: both layers have Cast Shadows off in the state files of run
  `20261006-155214` (`stack|CloudLayer.layerA.castShadows false`, `layerB` too, at `npc-day`, `metal`, `campfire`,
  `candle`). And the game's sun stays on in the rain: the market's wall above is in rain of 0.68 and has the
  building's shadow, lit side x1.74 of the shaded side. So there is no cloud shadow to carry over
  (`docs/PT-VEGETATION.md`'s change 1b has nothing to do, as far as these files show). That answers PT-36: under the
  game's own renderer clouds cast no shadows either.
- **The air's own haze.** HDRP also dims a sun that "interacts with the sky" by the air between it and the ground.
  The game's world light does not (`interactsWithSky false` on every `ShadowCaster`; only the two lights that draw
  the sun's and the moon's discs have it, and they light nothing). And the dimming measured above is grey.
- **A cookie on the sun.** None in any audited scene (`cookie null`). The game can give its shadow caster one by day
  (`DayNightSystem.UpdateShadowCasterOwner`); the tracer would ignore it (`SceneLights.cs:437`).
- **The camera's sky probe** (`pt_probe.hlsl`, 0.64 outdoors) only scales what HDRP still shades. It darkens, never
  lifts.
- **Indirect Color 3** keeps the bounced light's brightness exactly (`pt_gi_spatial.hlsl:62-69`). After the surface's
  own color the change is a few percent either way. It does shift color: a warm bounce loses all its blue, which
  fits the cart's boards (blue x0.8).
- **The radiance cache** passes light on once per bounce (`pt_common.hlsli:1886`, `shade_path`). **The clamps**
  (`pt_restir.hlsli:12-20`) only cut.
- **The composite** replaces HDRP's lit color; nothing of HDRP's lighting is left under the traced light
  (`d3d11.hlsl:649-657`, `:687-690`).

### B.6 The changes

**B1. Dim the traced sun and lamps by the game's fog, as HDRP does. In the tree since 16:35.** Shaders only. No new
bit. What is there now:

- `pt_common.hlsli:553-560`: `directional_fog` and `sun_fog`. Its formula was checked here against HDRP's rule and
  against the tracer's own `height_fog_depth` over 100 km, for three fogs, six heights and four sun angles: equal
  to 1e-16.
- the sun at a pixel `pt_di_spatial.hlsl:78-87`; at a path's vertex `pt_common.hlsli:1710`; on hair
  `pt_hair.hlsl:165`; its disc in glass `pt_specular.hlsl:88`; off water `pt_compose.hlsl:113`; the share of
  HDRP's own light `pt_hdrp_shadow.hlsl:62`.
- lamps `pt_common.hlsli:1559`; a directional light in the lamp list `:1553`.

On screen:

- Horns of the South by day: sunlit ground, walls and grass three to four times darker than before, close to the
  game's. Their bounce into the shade drops with them. An estimate for the darkest quarter there: about x1.4 of the
  game's where it was x2.7 (x1.25 at Reflection Boost 0). It rests on the F8 capture's sky-off picture, in which the
  sky is 29 % of the traced light's median.
- The white glints on wet grass and rock are mostly the sun's own highlight: they drop to a third (PT-03, PT-12).
- Hard sun shadows lose contrast, as the sun under them is weaker.
- Night: the moon near the ground drops to about 2 %.
- Cuanacht: the sun x0.8. A small step.
- Traced surfaces then get the same sun as the things HDRP still shades beside them (particles, water, transparents).

The number to check in game (recipe B.7): the game's step over the traced one at the two shadow edges of the table
above should be near 0.8 to 1 where it was 0.19 to 0.24.

What could go wrong:

- **The night.** With the moon at 2 % the night is lit by lamps alone, and at the `night` save the torch's own two
  lights are not traced (PT-38, under "Seen on the way"). Until they are, that place will be near black and Dark
  Adaptation will lift noise. Look at `night` first.
- The gain gives part of the daylight back. It is automatic: a darker picture gets more of it, and it lifts the
  shade too. By day it was x1.0. Read the exposure and gain line after the change (it removes light).
- The clamps on bounced light and reflections are still sized by the full sun (`tracer.cpp:1689`). They are now
  three to five times looser against a sunlit surface. Watch for sparkles in bounced light.
- In shade the traced picture can end a little under the game's: the game's sky light ignores walls and roofs.
- Mods that thicken the fog (TG Volumetric Fix, Better Volumetric Fog Addon) dim the traced sun as they dim the
  game's. That is the point, but it is new.

**B2. Reflection Boost raises the mirror share of metals only. In the tree since 16:50** (PT-32; the code's comment
carries the owner's word of that day). This file had listed three ways on: leave it, a lower default, or this one.

- Stone, brick, wood and soil keep the extra gloss and stop mirroring twice as much.
- What is not measured: how much of the step in table B.1 this takes off. That step is the old form's (Boost 0.1
  against 0). The new form keeps the gloss part on non-metals. Recipe B.7, step 3, on the new build tells.
- After B1 the old boost would have been the largest part of what was left at Cuanacht.

**B3. The game's two dials: the owner's call.** The game halves all its reflections outdoors, and raises the sky's
light by half in some places. Following either is copying an artist's dial, not physics. If wanted for the
reflections: the managed side sends the game's own multiplier (the volume stack's value divided by the camera-light
share the mod itself puts into it) in `TGPT_Frame.forward[3]` (free; `[2]` holds the fog's Max Fog Distance since
15:55), the native side passes it in `FrameConstants.cacheOrigin.w` (free), and `pt_specular.hlsl:89` multiplies the
traced reflection by it (not what glass lets through, line 137). No struct changes size. Measure first (recipe B.7).
The sky's dial could ride on Sky Intensity the same way (`HdrpPasses.cs:914`).

**B4. Sky Intensity 0 means off. In the tree since 16:38** (`src/TGPathTracing/HdrpPasses.cs:914`). It was taken as 1
(`tracer.cpp:1674`).

**Nothing proposed** for the bounced sunlight and the real shadows in corners. They are the tracer's point.

### B.7 Harness recipes

**The fog on the lights, in game.** The plain `ab` recipe at `metal` and `caustics-glass`, then the edge test of B.2
on the kept frames: light on each side of the edge from (1565, 250) to (1660, 465) at `metal`, and from (3480, 1180)
to (3830, 1262) at `caustics-glass` (4K pixels; the saves' cameras are fixed), game and traced, before tone mapping.
Before B1: the game's step is 0.19 to 0.24 of the traced one. After: near 0.8 to 1. `tools/harness/analyze.py` could
take such edges per scenario, as it takes a region.

**What each part adds.** Run at `vista`, `npc-day`, `metal`, `water-day`, `night`.

```json
[
  {"name": "1-game", "set": [{"key": "Mode", "value": "Off"}], "traced": false, "settle": 3, "frames": 8, "audit": true},
  {"name": "2-traced", "set": [{"key": "Mode", "value": "On"}, {"key": "Denoising", "value": "Ray Reconstruction"}, {"key": "Reflection boost", "value": "0.1"}, {"key": "Reflections", "value": "All Surfaces"}, {"key": "Sky intensity", "value": "1"}, {"key": "Radiance cache", "value": "true"}, {"key": "Indirect color", "value": "3"}, {"key": "Exposure", "value": "0"}, {"key": "Debug view", "value": "None"}], "traced": true, "settle": 6, "settleFrames": 240, "frames": 8},
  {"name": "3-reflection-boost-0", "set": [{"key": "Reflection boost", "value": "0"}], "traced": true, "settle": 8, "settleFrames": 240, "frames": 8},
  {"name": "4-reflections-off", "set": [{"key": "Reflections", "value": "Off"}], "traced": true, "settle": 8, "settleFrames": 240, "frames": 8},
  {"name": "5-sky-off", "set": [{"key": "Sky intensity", "value": "0"}], "traced": true, "settle": 8, "settleFrames": 240, "frames": 8},
  {"name": "6-cache-off", "set": [{"key": "Sky intensity", "value": "1"}, {"key": "Radiance cache", "value": "false"}], "traced": true, "settle": 8, "settleFrames": 240, "frames": 8},
  {"name": "7-indirect-color-1", "set": [{"key": "Radiance cache", "value": "true"}, {"key": "Indirect color", "value": "1"}], "traced": true, "settle": 8, "settleFrames": 240, "frames": 8},
  {"name": "8-direct", "set": [{"key": "Indirect color", "value": "3"}, {"key": "Reflections", "value": "All Surfaces"}, {"key": "Debug view", "value": "Direct"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "9-indirect", "set": [{"key": "Debug view", "value": "Indirect"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "10-specular", "set": [{"key": "Debug view", "value": "Specular"}], "traced": true, "settle": 3, "settleFrames": 120, "frames": 8},
  {"name": "11-off", "set": [{"key": "Mode", "value": "Off"}, {"key": "Reflection boost", "value": "0.1"}, {"key": "Reflections", "value": "All Surfaces"}, {"key": "Sky intensity", "value": "1"}, {"key": "Radiance cache", "value": "true"}, {"key": "Indirect color", "value": "3"}, {"key": "Debug view", "value": "None"}], "traced": false, "settle": 1, "frames": 0}
]
```

How to read it:

- Each step keeps what the one before it set. Step 5 needs B4 (with a build before it, use 0.01).
- 2 minus 3: Reflection Boost. 3 minus 4: the traced reflections at the game's own gloss (cause 3).
- 4 minus 5: the sky's diffuse light. What is left in 5 is the sun, its bounce and the lamps.
- 6 and 7 against 4: next to no change in brightness is expected.
- 8 is the cleanest picture of cause 1: before B1 a sunny Direct view where the game's own picture is overcast.

**Old against new in one run** needs a hidden config key and one option bit (below); then:

```json
[
  {"name": "1-game", "set": [{"key": "Mode", "value": "Off"}], "traced": false, "settle": 3, "frames": 8},
  {"name": "2-traced-fog-dims-lights", "set": [{"key": "Mode", "value": "On"}, {"key": "Denoising", "value": "Ray Reconstruction"}, {"key": "Fog dims lights", "value": "true"}], "traced": true, "settle": 8, "settleFrames": 240, "frames": 8},
  {"name": "3-traced-as-before", "set": [{"key": "Fog dims lights", "value": "false"}], "traced": true, "settle": 8, "settleFrames": 240, "frames": 8},
  {"name": "4-off", "set": [{"key": "Mode", "value": "Off"}, {"key": "Fog dims lights", "value": "true"}], "traced": false, "settle": 1, "frames": 0}
]
```

**What only the game can tell.**

- The sun's height at each save. The harness's state file has the sun's strength but not its direction. Add the main
  light's forward vector to the audit and the fog's share is exact for each scene.
- Which path draws the candle wax, and how much of the pots' look is the per-face fade: recipes A.5.
- The share of the traced reflections and of the sky at each place: steps 4 and 5 above.
- Whether nights look right after B1 with Dark Adaptation, once the torch's lights are traced.

### Offline checks to add with the changes (`tests/pathtracing_gpu_test.cpp`)

- A1: a 12-sided cylinder with smooth vertex normals, its material flagged `TGPT_MATERIAL_SEE_THROUGH`, a small lamp
  beside the camera. The Normals view across it must turn evenly, and the lamp's reflection must be one narrow
  streak. Break it on purpose (`rounded` set to the triangle's normal): steps of 30 degrees and a block one face
  wide.
- A2: the same cylinder as a plain Lit surface, lit from the side. Its direct light across the lit side must have no
  step at a face's edge. Break it on purpose (the fade back on for all): steps of about 0.2 of the full light.
- B1: a sunlit floor with fog numbers in the frame. Its direct light must be the fog-free value times the fog's share
  (for 144 m, a base at the floor, a top 824 m higher and a sun 45 degrees up: 0.31). Break it on purpose (the factor
  out): the full value. The same for one lamp 10 m away.
- The harness never runs Ray Reconstruction, so all three also want the in-game recipes above.

---

## Bits and fields these changes would need (none taken)

- A1: none. It reads `MeshRecord.normals` (`pt_common.hlsli:208`), which is there.
- A2: `pt_prepare`'s per-pixel flag 128, the last free one in that byte (1 to 64 are taken, PROJECT-CONTEXT 9.12). It
  is not a `TGPT_MATERIAL_*` bit. `docs/PT-VEGETATION.md` asks for the same one; A2 can do without it (A.4).
- A3: none.
- B1: none. Only for the old-against-new recipe: one `TGPT_OPTION_*` bit ("lights not dimmed by the fog"; the next
  free one is 33554432) and a hidden config key "Fog dims lights".
- B2 and B4: none (both in the tree).
- B3, if the owner wants it: `TGPT_Frame.forward[3]` and `FrameConstants.cacheOrigin.w`, both free floats today.

## Seen on the way

- **PT-38, the night by the torch.** Two causes. The moon at full strength (B1). And the torch's own two lights are
  not traced: the state file has them 2.6 and 2.8 m from the camera, 2400 K, on, named `PointLight_Ambience` and
  `PointLight_Cookies`, with no parent object; the tracer keeps a light only if its own name or one of five parents
  names a flame or a lamp (`src/TGPathTracing/SceneLights.cs:45`, `:60`), and the baseline run's `LogOutput.log`
  lists `* "PointLight_Ambience" x261 ... under "(none)"` as left out. Measured before tone mapping: the post's face
  toward the torch is 9.4 in the game's picture, orange, and 3.7 traced, grey. Crop:
  `B-23-night-bridge-game-vs-traced.jpg`. The game also has a light on the hero at night
  (`Light_HeroLight_Outdoor`, on, range 8 m), left out by the owner's ruling.
- **PT-36**: answered in B.5 (no cloud shadows in the game either).
- **PT-35**, not checked: after the first pane the ray may be stopping at the jar's own far wall and shading it as a
  solid thing (`pt_specular.hlsl:126` follows glass once). That would make a hollow jar opaque and beige.
- **PT-39** is the other sign of B.4's last point (a walled-in place lit by the game's sky light, which ignores
  walls).
- PROJECT-CONTEXT 9.12 names 65536 as the next free `TGPT_MATERIAL_*` bit. `TGPT_MATERIAL_HAIR_LAYER` has it
  (`tgpt_api.h:192`). The next free one is 131072.
- PROJECT-CONTEXT 9.7 lists the terminator fade under skin. The code applies it to every opaque surface.
- The Reflections row says Off "keeps HDRP's reflection probes". On traced pixels nothing of HDRP's is kept: the
  composite replaces the lit color. Off leaves only the lamps' and the sun's own highlights.
- The source checks: 6 of 76 fail on the tree as it stood at 16:35 (run read-only): `test_path_tracing_abi`,
  `_characters_light`, `_hair_highlights`, `_no_placed_lights`, `_rtxdi`, `_water_reflections`. They pin older text
  (the ABI number, the hair cards' code, `FrameConstants`' size, the CMake pass list).
- The harness reads the mod's "interior" flag when a save has loaded, before the mod has looked
  (`src/TGPathTracing.Harness/Runner.cs:306`): `candle`'s `scenario.json` says interior for an outdoor place, while
  its traced step does not.
