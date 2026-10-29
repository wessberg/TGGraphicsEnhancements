# Ron's face: flat skin and flat eyes when traced (PT-43, PT-44)

Agent "face", 2026-10-07. Copy of the tree: `scratchpad\wt\face` (base `wt\base15`). Job: what darkens the game's eye
sockets and the eye under the lid, what gives its cheek blush and detail; fix what is ours, physically. Nothing run in
game, nothing deployed, nothing committed.

State: built; the offline GPU test (848 ok, 0 FAIL), the source checks (83) and the plugin compile pass; the new checks
fail on the base and on a mutant. Not seen in game.

## The short answer

- **The game's dark sockets and lids are not a shadow. They are HDRP's ambient occlusion on a fill light.** Ron's face in
  the game is lit by two lights: the candlestick on his table (2600 K, the tracer has it) and the light the game carries
  with the player (`HeroLight_Indoor`, 4900 K, from the camera's place; a fill light, which the owner's rule leaves out).
  A light at the camera can't throw a shadow the camera sees, and the candle is below the face. What darkens the sockets
  is HDRP's screen-space ambient occlusion, which this tavern's volume puts on direct light too (strength 0.75), plus
  the skin's own occlusion (micro shadows 0.26). Both are HDRP shading, not light.
- **What was ours:** every ray from the face started 2.3 cm out from the skin (`ray_offset`: 2 cm + 0.2 % of the
  distance), and every back face within 16 cm was passed. A lid stands a few millimetres off the eye, the brow and nose
  1-2 cm off the socket: the rays started past all of them. So the lids could never shadow the eye, and the sockets saw
  the whole room. Skin also ignored its own material occlusion (the game's skin shader has one for mouth, nostrils and
  lips), which leaves already got.
- **Changed:** a character's skin and the eyes now start their rays 1 mm above their own traced triangle, and back faces
  stop them; skin takes its material's occlusion off its bounced and sky light. Physical, no new light, no HDRP shading.
- **What it will and won't do for Ron in this save:** the lids and lashes now shadow the eye where a light comes over
  them, and the sockets lose bounced light they can't see. But in this save the face is lit from below-front by the
  candle, so the upper lid has little to shadow, and the bounced light is about 2.5 % of the face's light. The gap to
  the game's look is mostly the missing fill light and its ambient occlusion, which the owner's rules keep out. That is
  a question for the owner (see 7), not a bug.

## 1. What the game does on Ron's face (evidence)

Pictures: `20261006-212905/hair-eyebrows/1-game` (the game) against `20261007-030128/hair-eyebrows` (traced, the debug
views). Crop: `scratchpad\facework\eyes3.jpg` (box 0.470,0.478,0.510,0.512 of `full.jpg`; game, traced RR, Albedo,
Direct, Indirect). Script: `scratchpad\facework\face_levels.py` (mean light in a box, sRGB decoded, and its share of the
cheek's).

**Light reaching the socket, against the cheek** (picture over the Albedo view, so the skin's own colour is divided out):

| place | Albedo view (the skin's colour) | game | traced (RR) | Direct view |
|---|---|---|---|---|
| under the eye (0.494,0.4907,0.500,0.4952) | 0.57 of the cheek | 0.41: light 0.72 | 0.56: light 0.98 | 0.91 |
| lid crease (0.494,0.4807,0.499,0.4834) | 0.69 | 0.49: light 0.71 | 0.68: light 0.99 | 0.95 |
| eye (0.4945,0.4855,0.4985,0.4885) | 0.56 | 0.25 | 0.39 | 0.88 |

So the skin's colour already has the dark under the eyes (0.57), and the tracer keeps it. The game then takes another
~0.3 of the light away in the socket and the crease; the tracer took none (0.98, 0.99).

**The light's colour on the cheek** (blue over green of the light, the picture's over the Albedo view's 0.95): game
0.64, traced 0.39; the Direct view (the traced light alone) 0.42. A 2600 K candle alone gives the traced number; the
game's whiter light needs a second, whiter source.

**Which lights** (state audit `20261006-212905/hair-eyebrows/1-game/audit-a.txt`; script
`scratchpad\facework\near_lights.py`, lux estimated with HDRP's range window at a head 1.3 m in front of the camera,
which is rough):

| light | on | strength, colour | traced? |
|---|---|---|---|
| `Hero/Misc/HeroLight/Light_HeroLight_Indoor` (at the camera, 0.7 m below it) | yes | 131072 cd, range 6 m, 4900 K, no shadows | no: "the light carried with the player" is a fill light (`SceneLights.cs` AlwaysPlacedNames, the owner's no-fake-lights rule) |
| `Lights/SM_Candlestick_02/PointLight_Cookies_01` (on the table, 0.6 m below the face) | yes | 27577 cd, range 3.3 m, 2600 K, cookie, no shadows | yes |
| `Lights/Point Light` (3.3 m) | yes | 125000 cd, range 3.7 m, 5254 K | no: a plain fill light |
| `HeroLight_Outdoor`, the bonfire's ambience light | off | | |

The two main ones are of similar size at the face (the estimate gives 59 klux and 54 klux; the head's place is a guess).

**What HDRP does with them** (the audit's volume stack, and HDRP's `Lit.hlsl` in `scratchpad\hdrp-src`):

- `ScreenSpaceAmbientOcclusion`: intensity 1, radius 3 m, `directLightingStrength` 0.75, specular occlusion 0.5. HDRP
  multiplies direct light by lerp(1, AO, 0.75) and ambient light by min(AO, the material's AO)
  (`Material/MaterialEvaluation.hlsl`, GetScreenSpaceAmbientOcclusion: `directAmbientOcclusion = lerp(1.0,
  indirectAmbientOcclusion, _AmbientOcclusionParam.w)`). A concave socket gets a
  low AO from the screen, so both lights are dimmed there. With a light at the camera, that is the only way the socket
  can come out darker than the cheek.
- `MicroShadowing`: on, opacity 0.26: the material's AO dims direct light a little more.
- `ContactShadows`: on, 0.15 m, opacity 0.5; per light, and the two main lights cast no shadows of their own.
- The skin is `TG/Character/RealSkin` (`Mat_Male_Head_*`: `_MouthCavityAO` 2.5, `_NostrilCavityAO` 3.5, `_LipsCavityAO`
  1.82; no parameter for the eyes). HDRP writes a deferred material's AO into GBuffer3's green behind its tag when the
  pixel has no baked light (`Lit.hlsl` EncodeIntoGBuffer), which is always so while tracing.
- The eye is `TG/Character/Eye/Eye` (`Mat_BaseHuman_EyesGreen`): sclera and iris tiling and smoothness,
  `_Reflection_Intensity` 0.2 (its fake reflection), `_Iris_Emission_Intensity` 1, a diffusion profile. No lid shadow
  and no darkening parameter. It is drawn forward, so HDRP's SSAO and the material's AO darken it as they do the skin.
  It has `_MASKMAP` on; whether that map's green is an occlusion the game's shader uses is not read (not decoded).
- Not a shell: the cornea is a separate transparent material, hidden while tracing and not in the traced scene
  (`CharacterCapture.cs`, `SceneCapture.cs`: transparent character parts are left out).
- The lashes are in the traced scene (alpha-tested cards, held opaque, cut at 0.5).

**Why the traced face is orange and "hard":** the traced light is the candle alone (blue/green 0.42) times the traced
gain x2.8 (step `02-traced-rr`: "traced light x2.8 (1.5 EV)", Dark Adaptation in a dark interior). On the tone curve the
cheek sits at 0.30 traced against 0.10 in the game (linear, after the curve). No other light lights the face traced.

## 2. What the traced face lacked (ours)

- **Rays started past the face's own relief.** `ray_offset` = 0.02 + 0.002 x distance: 2.3 cm at Ron. Every shadow
  ray, bounced-light ray and reflection ray from the skin and the eye began that far out along the surface's normal. A
  lid stands 2-5 mm off the eye; the brow and the nose 1-2 cm off the socket. The Direct view lights the eye 0.88 of the
  cheek and the crease 0.95.
- **Back faces near a surface were passed** (`near_back`: 15 cm + 1 %), a rule for HDRP surfaces that lie off the traced
  mesh (a tessellated arch). From the eye, a lid's inside is a back face: passed.
- **The skin's own occlusion was dropped** (leaves have had it since R6).
- The eye is camera-only in the traced scene (it shadows nothing), so its rays can start on it. A character's skin is
  the game's skinned mesh, the same frame's positions HDRP draws (`tracer.cpp`, the character copy), so a ray from it
  can start on its own triangle too.

## 3. What changed

- `pt_common.hlsli`: `Surface.onMesh` (a character's skin on its traced triangle, or an eye the tracer shades);
  `surface_offset(s)` = 1 mm + 0.02 % of the distance on the mesh, else `ray_offset`; `surface_near_back(s)` = 0 on the
  mesh (every back face is a real hit), else `near_back`. `load_surface`: skin is alpha 0.125-0.5 (0.25 as before, 7/16
  on the mesh); skin that is not thin keeps its material's ambient occlusion in the specular bytes' green (its f0 is a
  dielectric's grey, in red), as a leaf does.
- `pt_prepare.hlsl`: skin within 30 m traces a camera ray within 4 mm + 0.2 % of HDRP's depth; where it meets a
  character's triangle (passing one hair card: a brow, a beard), the pixel is lit there, with that triangle's normal as
  its geometric normal, and marked on the mesh. An eye is lit where the camera ray met it (its traced mesh) and marked
  with bit 8 (until now only HDRP-shaded eyes had it; nothing reads bit 8 for a surface the tracer shades). The skin's
  GBuffer3 occlusion is read as a leaf's.
- `pt_gi.hlsl`, `pt_gi_spatial.hlsl`, `pt_di_spatial.hlsl`, `pt_specular.hlsl`, `pt_compose.hlsl` (water caustics),
  `pt_rtxdi.hlsli`: `ray_offset(s.distance)` -> `surface_offset(s)`, `near_back(s.distance)` -> `surface_near_back(s)`.
  `pt_hdrp_shadow.hlsl` keeps the old ones (it shades HDRP's own surfaces, never on the traced mesh).
- `pt_compose.hlsl`: unchanged; its leaf line (`diffuse -= diffuseFinal x (1 - AO)`) now takes skin's too.
- `tests/pathtracing_gpu_test.cpp`: the eye-under-a-lid and skin-occlusion checks (5). `tests/source_checks.py`:
  `test_path_tracing_face_on_its_mesh`, and the pinned lines that moved.

## 4. Files, bits, ABI

Files: the shaders above, `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`, `CHANGELOG.md`, this file.
No ABI change, no new material or option bit, no plugin change. Uses: the color byte's alpha gains a level (7/16:
skin on its mesh; PROJECT-CONTEXT 9.12 says "skin is the color byte's alpha 0.25"); mat.y bit 8 now also marks an eye
the tracer shades; the specular bytes of skin that is not thin hold (f0 grey, AO, 1).

## 5. Offline checks

Build: `%TEMP%\tgb\face-new-build` (Ninja, the configure line of `scratchpad\facework\face-configure.cmd`), installed to
`%TEMP%\tgb\face-new-inst`; all shaders compile with `-WX`. The untouched base (`wt\base15`) built the same way to
`face-base-inst`.

`tgpt_gpu_test` (run only while `gpu-free.ps1` said the GPU was free), new checks in the character block: an eye (the
character's cube, camera-only, `MATERIAL_EYE`, on the box HDRP draws forward) with a lid 1.5 cm tall standing across its
top face, the sun up and behind the lid; light in the Direct view 2-6 mm in front of the lid against 4-8 cm away
(picture: `%TEMP%\tgb\face-images-new1\33-eye-under-lid-direct.png`, upside down as all the test's pictures):

| case | new (`face-test-new1.txt`) | base (`face-test-base1.txt`) |
|---|---|---|
| eye, no lid | 0.3998 / 0.3998 | 0.3998 / 0.3998 |
| eye, lid facing it | 0.0000 / 0.3998 | 0.3998 / 0.3998 (FAIL) |
| eye, lid facing away (its back face) | 0.0000 / 0.3998 | 0.3998 / 0.3998 (FAIL) |
| skin (deferred, solid profile) on the character's cube, lid facing it | 0.0000 / 0.3998 | 0.3998 / 0.3998 (FAIL) |
| skin out of the sun, its material's occlusion 1 / 0.25 / 0 | 0.0599 / 0.0245 / 0.0125: keeps 0.25 of its bounced light | 0.0598 / 0.0600 / 0.0599 (FAIL) |

New: 848 ok, 0 FAIL. Base: the four new checks fail and nothing else. The eye's own light where no lid is near is as
before (the eye check: 0.0911 against 0.0896).

Mutation: `surface_near_back` returning `near_back` again (back faces passed; `face-test-mutback.txt`): only "a lid's back
face shadows the eye too" fails (0.3998 / 0.3998); the other lid checks and the occlusion check pass. Source restored
byte for byte and rebuilt into `face-new-inst`.

`python tests\source_checks.py`: 83 tests OK. New `test_path_tracing_face_on_its_mesh`; mutation-checked (pt_gi back to
`near_back(s.distance)`: it fails; restored: OK). Plugin: `dotnet build src\TGPathTracing ... -o %TEMP%\tgb\face-managed`
succeeds (no C# changed).

## 6. What the next harness run should show

`-Scenario hair-eyebrows -Recipe views`, with this build. The numbers (game step `20261006-212905/hair-eyebrows/1-game`,
traced `20261007-030128`):

- **Direct view, light in the socket over the cheek** (`scratchpad\facework\face_levels.py cheek=0.496,0.4997,0.504,0.5069
  eye=0.4945,0.4855,0.4985,0.4885 under=0.494,0.4907,0.500,0.4952 lidcrease=0.494,0.4807,0.499,0.4834 -- <steps>`; boxes
  on the 4K `full.jpg`, Ron's left eye): now eye 0.88, under the eye 0.91, crease 0.95. Expected: the eye lower, darkest
  at its lower edge, where the lower lid stands between it and the candle below (the candle is below the face: the upper
  lid has little to shadow); the crease and under the eye little changed in direct light. If the eye is unchanged, the
  head's lids are open meshes the eye's rays slip past, or the camera ray did not find the eye (look at Surfaces: cyan).
- **Indirect view**: the sockets, the eyes' corners, the nostrils and mouth darker than the cheek (now: under the eye
  0.97, crease 1.37 of the cheek). It is about 2.5 % of the face's light here, so the final picture moves little.
- **The final picture**: under the eye and crease over the cheek, as light (divided by the Albedo view): game 0.72 and
  0.71, traced now 0.98 and 0.99. Expected a few hundredths lower, not the game's 0.71 (see 7).
- **No acne**: the cheeks, nose and neck in the Direct view as smooth as before (no dark specks).
- `detail_boxes.py` with the brief's boxes (note: its "eyes" box 0.47,0.435,0.51,0.465 is the brows and forehead on the
  4K `full.jpg`; the eyes are at y 0.484-0.489): now crown 0.30, eyes 0.80, cheek 0.71 of the game's with RR (0.49,
  0.96, 0.85 without a denoiser). Expected about the same: this change adds shadow, not detail.

## 7. Risks and open questions

- **Acne on skin** (dark specks where a ray meets its own triangle): the ray starts 1 mm + 0.02 % above the triangle the
  camera ray met, along that triangle's own normal; a light above that plane can't be met by the triangle, and a
  neighbour only where the surface bends towards it (a socket, a fold: a real shadow). Below the plane the skin's
  terminator fade is 0 anyway. Positions at the tavern's 500 m are good to about 0.06 mm. Look at Ron's cheeks and
  neck in the Direct view for specks; if any, raise 0.001 in `surface_offset`.
- **A character drawn by HDRP with other positions than the traced copy** (a frame late, or an animation the copy
  misses) fails the 4 mm test and keeps the old 2 cm offset there: no harm, no gain.
- **Back faces now stop a face's rays.** A collar's inside or a hood's lining near the neck now shadows it (it passed
  before, light leaked through). Physical, but a change: look at necks under collars in `npc-day`.
- **The skin's occlusion is unread in game.** Whether RealSkin writes one into GBuffer3, and what, is not checked (no
  debug view shows it). The tag test leaves skin as it was where HDRP wrote none.
- **Cost:** one more camera ray per skin pixel within 30 m (two past a brow or beard card). A face filling a fifth of
  the screen is about 400,000 rays at 1924x1084.
- **For the owner (not done, not mine to decide):** the game's face is lit as much by the player's own HeroLight (a fill
  light from the camera, 4900 K) as by the candle, and its sockets are darkened by HDRP's ambient occlusion on that
  light. Traced, the face has the candle alone, lifted x2.8 by the traced gain: orange, flat, bright. The rules say no
  fill lights and no HDRP shading, so the traced face will stay a candle-lit face. Whether that is the look the owner
  wants for faces is the owner's call.
- Not done: the eye shader's fake reflection, iris glow and subsurface scattering (as before); its mask map's green
  (`_MASKMAP` is on) is not read; Ray Reconstruction's guides are unchanged.
