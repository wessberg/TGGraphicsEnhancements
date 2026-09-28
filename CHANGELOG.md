# Unreleased

From 1.1.0 on, the version numbers are the ones the mods have on Nexus Mods, and TG Frame Generation,
TG Better Upscaling and the bridge they share all carry the same one. Earlier entries use the repository's
own numbers: v0.5.0 is the release published on Nexus as Frame Generation 1.0.2.

TG Path Tracing (new, prototype, Mono version)

- A character's hair takes the color the game gives it later too: the game colors hair on each character's own copy of the
  material, sometimes after the character first shows, and the tracer, which read it once, kept a far character's hair
  grey (the material's default) until it came near. Hair's color is read again every frame now.
- Hair is shaded by the tracer, not by HDRP: its color is its shader's (RealHair's diffuse map times _DiffuseColor and
  _DiffuseStrength; read as the texture alone it came out grey), camera rays stop at its strands wherever they meet them,
  it is lit through from behind as thin cloth is, and Reflection Boost leaves it alone. The eyes still keep HDRP's shading
  (darkened by the traced shadows) until the tracer reads their textures: the log now lists the eye, eyebrow and lash
  shaders' textures and values for that.
- The direct light's reuse keeps a light choice for 4 frames, not RTXDI's 20 (NVIDIA RTX Remix's choice with Ray
  Reconstruction): a hanging cage's thin bars boiled, where the temporal reuse can't be permuted. The F8 capture's
  frame-to-frame change on them: 0.40 at 20, 0.14 at 4, 0.11 without the reuse at all.
- The F8 capture's boiling comparison adds the bounced light's reuse over 4 frames instead of 8, and the image as traced
  again once settled: the first pair follows the report straight away, while the HUD and the image can still change.
- Fill Light Fog (a new Video row, On by default): the game's fill lights, which path tracing replaces, light no surface but
  still light the game's fog as in the game, so interiors keep their haze (the tavern's came from its 24 fill lights, and
  was gone). Off: only the traced lights light the fog, to compare.
- Reflection Boost defaults to 0.1 again (it was 0.2). A value saved in the config stays as it is.
- Bounces defaults to 1 (it was 2): no difference showed in game between 1 and 4, since the radiance cache carries the
  light of every bounce after a path's last, and the bounced light was most of a frame's GPU time (28 of 35 ms in the
  tavern at 1924x1084 on an RTX 5080). A value saved in the config stays as it is.
- The F8 capture measures what boils: how much the middle of the image changes from frame to frame, in 8x8-pixel blocks, as
  traced and again with the bounced light from each pixel's own 16 rays alone (ReSTIR GI's reuse left out), with the direct
  light without ReSTIR DI's reuse, and with neither, each with its difference image. The report's Boiling line gives the
  four, and the change around the crosshair. It takes about 16 seconds now.
- The F8 report gives the GPU time of each stage of a frame (the scene, preparing, bounced light and its spatial reuse,
  direct light, hair and eyes, reflections, the radiance cache, denoising, the composite, and the water's reflections),
  averaged over the last second; BepInEx's log has the same in its summary every ten seconds.
- Spatial Samples goes to 16 (it stopped at 8): more of them smooth the bounced light's boiling further, at a cost.
- Eyes take the traced bounced light where HDRP's own ambient light is taken from them: HDRP scales it by the sky's light at
  the camera, a hundredth of it indoors, and the whites of the eyes went dark between the lids (eyes too small in every
  candle-lit face). The share HDRP no longer gives is the radiance cache's light where the eye is, times the eye's color.
  Not hair: its traced color lacks the tint HDRP's hair shader gives it (blonde hair under a roof glowed).
- Reflection Boost leaves skin as the game made it: the game's skin is glossy already (a roughness of 0.18), and boosted
  its reflectance doubled, so a neck mirrored the dark chin and collar above it in sharp, reddish patches.
- The dialogue light of Conversation Lighting Overhaul lights a face as brightly as the mod sets it at the game's
  exposure: the traced light's automatic brightening of dark places is taken back out of it (three times as bright in a
  candle-lit room). The Exposure row still applies to it.
- A light whose HDRP data has Affect Specular off makes no highlight where the tracer lights with it either, as in the game
  (the F8 report's lights say "no specular" for those).
- A reflection direction drawn into the surface's own triangle (a smooth, normal-mapped curve over flat triangles: a metal
  strap across a door) is mirrored back out, as the bounced light's and the water's are.
- HDRP Unlit surfaces reflect nothing, as in the game (a portal's black core reflected the two bright lights inside it: a
  glowing white orb), and a reflectance below 2 % no longer brightens toward grazing angles (Unreal's and Frostbite's
  Schlick).
- Hair and eyes (what HDRP shades itself) no longer go dark in daylight: where the traced shadow of a lamp HDRP doesn't
  shadow fell on them, the whole pixel was multiplied by the share of that lamp's light left, the sun's light and the
  ambient's with it. The light of what HDRP shadows itself (the sun, as far as the traced sun reaches the pixel, and a
  lamp with a shadow map) now stays in the share. Emissive surfaces no longer count as lamps there, and every such pixel
  is passed on, a share of 1 too (left out, its shadowed neighbours' shares spread over it). Harness: a sunlit table
  keeps 0.65 of its light with a lamp beside it blocked, against 0.47 without the sun.
- Straw on a floor, a fringe, grass tips: what is finer than a material's alpha tile lets the light between its parts
  through. Every alpha tile's coverage stops rays other than the camera's by chance, as hair's did; cut out at half a
  texel's coverage, the tavern's straw was a solid mat to rays, and its shadow blacked out the floor seen between the
  strands. Micromaps settle only fully covered and bare texels now. Harness: a card of half-covered texels casts 0.49
  of a solid card's shadow.
- Grass, leaves, banners and laundry move in the wind as HDRP draws them, and are traced where they stand still: rays
  from the drawn surface pass what the wind moves within 20 cm of it (its own instance mask), where they met its still
  copy (the inner side of laundry black, black spots on the ground swaying with the grass). Harness: 45 floor pixels
  darkened under such a quad 12 cm up, 3104 under an ordinary one; 60 cm up it shadows as an ordinary one does.
- Grass and leaves are lit through from behind where paths meet them, as the camera's translucent pixels are: the
  underside of a sunlit blade was black to the rays that met it, and the ground under grass took no light through it.
  Harness: the floor in a sunlit canopy's shade 1.33 times as bright under a translucent one.
- HDRP's fog keeps what it hides hidden: the traced light's automatic brightening of dark places fades as the game's
  height fog hides a surface (the player's Exposure stays). Through the castle's night mist the brightening lifted what
  the mist hides.
- HDRP Unlit materials take their color from _UnlitColor and _UnlitColorMap (their _Color and _MainTex are there for
  Unity's lightmapper, and white).
- Rays Per Pixel goes to 16, and every ray counts: at 1 the bounced light is ReSTIR's, at more a share of it (30 % at 8,
  48 % at 16) is the pixel's own rays' mean. Before, the rays only competed for the one sample ReSTIR shades with, which
  left the boiling as it was. Harness: the share of a frame's blotchy error the next frame keeps, 0.34 at 1 ray, 0.17 at
  8, 0.08 at 16; a frame's error 0.108, 0.094, 0.091.
- From a review of the day's changes: the radiance cache's per-frame sums could wrap past 32 bits for a saturated color
  (each channel is bounded now, and a cell takes 512 samples a frame); lamp light comes through thin skin (ears, fingers)
  as the sun's does; shadow rays to glowing surfaces end at them, not 5 cm and more short (a leak 17 cm deep 50 m away);
  hair's layers decide independently whether they stop a ray; the composite blends one kind of traced texel at a time
  (glass with glass, HDRP's shares with shares); glass with the Reflections row off is lit as HDRP drew it (it was black);
  the pixels' first bounced-light rays feed the radiance cache counting those below the surface as no light.
- Metal is metal where rays meet it: a traced surface's metallic and smoothness come from its mask map, texel by texel
  (HDRP Lit's, remapped as HDRP remaps them, and those of the game's own shaders as their GBuffer passes read them:
  characters' and their armour's, the castle's architecture, cloth). A surface HDRP draws forward takes its roughness
  from the mask map, and a metal there has no diffuse color and its base color as its specular color (it had a
  dielectric's), as in HDRP's metallic workflow. Where a path meets a metal (bounced light, a reflection, what glass
  shows), it is shaded diffusely and can't follow the metal's own reflection, so the metal sends the light it reflects
  on as diffuse light of the same energy, its base color, as it did (one with a constant metallic kept only a fifth of
  it): with its diffuse color alone, metal armour, pots and weapons bounced no light and were black in reflections. Mask
  maps share the color textures' atlas, a 64x64 tile each, up to three quarters of it (the rest stays for color);
  BepInEx's scene line counts them. The game's Shader Graphs that tile their textures through properties of their own
  (the castle's architecture, cloth) are traced with that tiling: a wall's stone was traced as many times too coarse as
  the wall tiles it.
- Highlights on smooth surfaces: a 1e-7 in the GGX distribution's denominator capped the peak of any surface smoother
  than a roughness of about 0.12, so a glossy floor's, a puddle's, polished metal's and Reflection Boost's highlights of
  the sun and the lamps kept as little as 0.1 % of their light (and glass 2.5 %). The peak isn't capped now; the direct
  light's clamp still bounds a highlight. Harness: a lamp's highlight on a smooth floor carries as much light as on a
  rough one.
- Water Reflections (a new Video row, On by default): water reflects the traced scene. The game's lakes, rivers and sea
  are HDRP's water system, drawn after the traced light is applied, and take their reflections from HDRP's screen-space
  reflections, which only find what is on screen; the rest of a reflection came from reflection probes and the sky (and
  while tracing, dimmed by the light at the camera). Right after HDRP's screen-space reflections for water, the tracer
  fills that rest from the traced scene: the canopy overhead, the bank behind you, grass within 20 m, a lamp, whatever is
  off screen or hidden behind something. The traced reflection replaces HDRP's screen-space one entirely, its hits
  too (they miss what is off screen or hidden, fade at the screen's edges and on waves, and show the last frame's image):
  HDRP's texture is only where its water shader reads its reflection from. The water's own shading stays the game's
  (its Fresnel, colour, foam, refraction and fog). The reflections are traced from HDRP's water depth and wave normals and
  applied a frame late, as the traced light is; since no denoiser sees HDRP's water, they are averaged over up to 12
  frames and filtered over 5x5 pixels that stop at edges between near and far or bright and dark things (on reflected
  walls lit by bounced light, a grain of 0.05, against 0.17 averaged over frames alone and 0.59 in a single frame). Leaves
  and grass in them are lit through from behind, as on screen (a sunlit card seen from under it, 3.4 times brighter).
  Liquids and other see-through surfaces that take HDRP's screen-space reflections get the same. HDRP applies its
  reflection lighting multiplier to a whole reflection, so the traced part is divided by it, and it is pre-exposed with
  the previous frame's exposure, as HDRP reads it. Harness: a flat water plane reflects a light panel only the traced scene
  has, the red and the green wall and the sky through the ceiling's hole, each where the mirror sends each pixel's ray;
  every check fails with its piece undone. F8 adds a shot with HDRP's own reflections to compare, and its report lists
  HDRP's water surfaces. Water itself is never traced as a solid surface: a mesh renderer carrying a water material
  (which HDRP draws with passes of its own) is invisible to rays, so it can't shadow the lake bed.
- Emissive Lights (a new row, on by default): glowing surfaces (embers, lit windows, magic crystals) are sampled as
  lights by ReSTIR DI, triangle by triangle, instead of being found only by the bounced light's rays that happen to
  meet them. Each point of their light is counted once: the light sampling takes it as far as its reach (where it
  still gives 2 lux), a ray that meets it counts the rest, and a reflection counts it whole (ReSTIR DI gives it no
  specular light). Harness: the floor under a glowing panel converges to the same light on and off (within 0.1 %), a
  frame's error around its foot is 1.7 times lower with ReSTIR's reuse and 5 times with its temporal reuse alone, its
  direct light matches a Lambertian square's to 0.1 %, and its reflection is as bright as with the row off. The
  radiance clamps keep its light too: off, they cut it to 22 % as rare bright hits, on 98 % stays, so rooms lit by
  glowing surfaces come out brighter than before.
- A glowing surface right around a kept light (a lantern's glass round its candle, within the light's radius and
  25 cm more) is that light's surface and lights nothing itself, row on or off: the light has its light, and the
  lantern's surroundings got it twice. Reflections and glass still show it glowing.
- Emissive surfaces the scene capture saw move since its last scan (a carried lantern, a swinging door), characters,
  stand-ins and the ones past a budget of 16,384 triangles (the brightest and largest are kept) are left to the rays
  that meet them. The Emissive debug view shows which glowing surface is which; the native log's 10 s line counts
  them, and BepInEx logs the first 40 emissive materials with their nits and names shaders whose glow the tracer
  doesn't read.
- Traced emission is what the camera sees: HDRP's exposure weight is applied (at 0 a glow looks equally bright at any
  exposure, so the light it stands for is its color over the exposure, about 10,000 times it at EV 13), and so is
  Albedo Affects Emissive; a single-sided surface glows from its front only (rays from behind it saw it glow too).

- Path tracing on or off: the Global Illumination mode (HDRP's direct light and shadows with the traced bounced light
  added) is gone, and with it Indirect Intensity. The Path Tracing row offers Off and On, On by default; a config set
  to Global Illumination or Full becomes On. Insert turns path tracing on and off.
- Direct light's ReSTIR reuse is NVIDIA's RTXDI library (3.1, compiled in): the tracer's own candidates (the lights
  whose range reaches a pixel, from its light grid) go into RTXDI's reservoirs, and RTXDI's temporal reuse, boiling
  filter and pairwise-MIS spatial reuse take them on, with the decorrelation NVIDIA and AMD ask for with their
  denoisers (permutation sampling with a new random number every frame). A light choice is kept for up to 20 frames
  (RTXDI's default), the final shadow ray is always traced, and spatial results never feed the next frame. The
  config's hidden "Direct light history" sets another length for testing.
- Bounced light's ReSTIR reuse is RTXDI's too: the tracer's rays find this frame's path, and RTXDI's temporal reuse
  (permutation sampling every frame, a search around the reprojected pixel, the reconnection Jacobian), boiling
  filter and spatial reuse take it on. A light path is kept for up to 8 frames (RTXDI's default; before, the
  tracer's own reuse was least grainy keeping one for a single frame, because it decorrelated its samples too
  little), and a path no older than 30 frames. The config's hidden "Bounced light history" sets another length
  (1 comes closest to the old behaviour).
- Lighting that changed with the camera's direction and caught up over a few seconds: the tracer's clamp on bright
  bounced light, direct light and reflections was measured on screen, against HDRP's auto exposure. A hall lit by a
  sunlit courtyard's bounce was mostly the clamp itself, about 80 times over it, so its light rose and fell with the
  exposure as the camera turned between bright and dark (and with a ten-second average of it, tried first, it rose
  and then sank over several seconds). The clamp is measured against the scene now: four times a white surface in
  full sun, or lit by the brightest light 1 m away, so sunlit bounce is never cut and nothing depends on where the
  camera looks. Traced light is stored relative to that reference, which keeps HDRP's physical units within half
  precision's range. A new harness check renders the same frames at two exposures; the old clamp failed it by 14%.
  The one-frame-old traced light is also put into the current frame's exposure before it is applied, and DLSS Ray
  Reconstruction gets HDRP's pre-exposure as HDRP's own DLSS pass hands it over, clamped to 0.2..2 as HDRP clamps it:
  at this game's exposures (about 0.0001 to 0.006) that is always 0.2, and NVIDIA's guide says Ray Reconstruction
  doesn't use exposure inputs anyway, so changes of exposure reach its history as changes of brightness.
- Lights are chosen by what they belong to, not what they are called: every light inside a flame, candle, lamp or
  torch prop is traced, including the ones this game names PointLight_Ambience (the room light of its candlesticks,
  wall candelabras and torches). A candlestick whose only light has that name lit nothing, while its neighbour's
  PointLight_Cookies_01 lit the whole room. Fill lights outside any source prop stay out, as do the player's own light
  and lights for fog.
- Lights fall off as physical lights do: traced as far as they still give 50 lux (inverse square, up to 40 m), not to
  HDRP's range, an artist's cut-off that made a chandelier's light stop 2.7 m below it (above everyone's heads) and a
  candle's 0.5 m from its flame. HDRP's fade with the camera's distance isn't applied to traced lights either: a lamp
  past it went dark and cast no shadow, then came in as you walked up.
- Conversation Lighting Overhaul's dialogue light is traced: it hides its light from the lookup the tracer used
  (HideFlags.DontSave), so faces in dialogue got none of it while HDRP's own shading of hair did (white hair).
- Rays see no sky indoors: interiors float in the sky above the world (the tavern at 500 m), and a ray slipping through
  a seam between walls and floor found the open sky, about 100 times brighter than the room: thin blue-white lines along
  wall and floor edges, and sparkles.
- The lights the tracer traces light what HDRP still shades itself (hair up close, glass) with the traced light's gain
  too, so those come out as bright as the traced surfaces around them.
- Indirect Color no longer clips: it scaled the bounced light's color around its brightness and clamped channels below
  0, which turned a candle's warm bounce off brown wood pure red at 3 (green and blue at 1 to 5 of 255 across the
  tavern). It stops where a channel would reach 0, keeping the brightness.
- The traced light's gain keeps its brightest fiftieth at white (was the brightest tenth: in a tavern lit by candles
  most of the frame is dark, and a candle-lit table past that point went white).
- Direct light: the boiling filter weighs a sample by the light it gives (RTXDI's weighs its inverse probability, and
  the stricter filter dropped weak lights' light instead of fireflies); the shadow ray goes to a new point of the
  light's disc every frame (a penumbra's noise held for up to 20 frames, then jumped: fringe shadows flickered); and
  RTXDI's spatial reuse no longer discounts neighbours it skips (a brief darkening after disocclusions).
- The F8 capture holds the camera light and the gain still while it compares images, and its light table lists each
  light's position, traced reach, cookie and fade distance. It also saves the image twice a quarter of a second apart
  with the difference between them (what flickers or boils stands out), a new Specular debug view (the traced
  reflections alone), and names what the traced scene has under the crosshair (its mesh and material).
- NRD, a new Denoising choice: NVIDIA's Real-Time Denoisers (RELAX), the fallback DOOM: The Dark Ages uses when Ray
  Reconstruction is off, for GPUs that run neither Ray Reconstruction nor AMD's Ray Regeneration (a Radeon RX 7900
  XTX, for one). It denoises the bounced light, which the built-in denoiser smoothed and let boil; the direct light and
  the reflections keep the built-in denoiser's own filters, which hold the shadows sharp and stay still on
  normal-mapped surfaces (given to RELAX as well, the sun's shadows blurred to three times the error and a bump-mapped
  floor flickered three to five times as much). In the harness the error where the light is all bounced (under the
  ceiling) is 10 to 18 % below the built-in denoiser's, with about the same flicker. Denoising starts on Ray
  Reconstruction, then Ray Regeneration, then NRD, then Built-in, whichever this PC runs, and a pick of Ray
  Regeneration or NRD now stays while the game starts (whether they run was known only once a frame was traced, and
  Denoising followed the best one until then).
- A radiance cache (the Radiance Cache row, on by default): the bounced light reaching each part of the world is kept
  from frame to frame, and every path ends on it where it used to stop, so light keeps bouncing past the Bounces
  setting's last bounce, and a reflection or glass shows a surface lit by bounced light as bright as it is instead of
  by its direct light alone (a reflected wall in shadow was black). The paths fill it themselves: a quarter of the
  screen's tiles trace one more ray each frame from their paths' last surfaces, and the bounced light's first rays count
  for the surfaces they leave. Its cells are the world's, finer near the camera, one for each side of a surface; a
  camera cut keeps them, a new scene starts them over. In the harness's furnace (a closed box whose walls glow and
  reflect half the light that reaches them) one bounce ending on the cache comes to 0.994 of the walls' exact
  brightness, where paths alone come to 0.75 with one bounce and 0.97 with four, and a wall seen in another is twice
  as bright with it, as it should be; under the harness room's ceiling one bounce with it is as bright as four without.
  It takes 36 MB of video memory.
- See-through glass: the glossy texels of glass (HDRP's roughness below 0.5 there; a label or a cork in the same texture
  is rough and stays solid) have a glass's mirror finish and show what lies behind them, within 15 m; the game's
  materials are left as they are (raising a mask-mapped one's smoothness did nothing: its upper end is already 1, so
  stage 1's finish never reached the wine bottles). The ray is refracted into the glass
  (an index of refraction of 1.5) and out where it meets the glass again from inside, so a full bottle bends what is
  behind it as a lens; what it finds is shaded as a reflection's surface is, tinted by the glass's color and weighted
  by the light the Fresnel term lets in, beside the reflection, and the glass has no diffuse light of its own.
- Light scatters under skin: the traced light replaces HDRP's lighting, which HDRP spreads under skin after it lights
  it, so skin came out as hard as plaster, its shadows' edges sharp across a face. Surfaces with a solid diffusion
  profile (skin, teeth, the whites of the eyes) now have their traced diffuse light spread over them as their HDRP
  profile sets, per color channel (red farthest, as in skin), with the Burley diffusion and the filter radius HDRP's
  own subsurface pass uses, before the albedo is multiplied back in (so the skin's texture stays sharp). It keeps the
  light's total, and fades with distance as a profile's millimetres shrink below a pixel. BepInEx's "HDRP diffusion
  profiles" line gives each thick profile's radius.
- Glass (bottles, flasks, vials, potions, jars, glasses: known by their materials' names, since the game draws them
  opaque with the label and the cork in the same texture): rays pass through it by chance, as bright as its color is,
  whether they carry a shadow, the sky's or bounced light, so a bottle's shadow is as dark as the light it stops
  (clear glass casts none, a bottle of dark wine a dim one) instead of a hard black sliver. BepInEx logs each glass
  material once.
- Coloured glass colours the light through it: a candle's or the sun's light through a green bottle reaches the table
  green, not just dimmer. The direct light's final shadow rays (the light RTXDI chose, and the sun) and those at the
  paths' bounces take each crossing's color, channel by channel (a closed bottle's two crossings let through its
  color); RTXDI's reuse and the rays of the sky's and bounced light still pass glass by chance, as bright as its color.
  Only rays that meet glass pay for it. In the harness a cube of green glass lets through exactly its color of the
  light an opaque one stops, from the sun and from a lamp, grey glass 0.36 of it and clear glass all of it, and the
  bounced light under a sheet of green glass turns 1.6 times greener.
- The denoisers take see-through glass as the near-mirror it is, its light all in the reflections: DLSS Ray
  Reconstruction gets no diffuse albedo there, a specular albedo that covers the light the glass lets through as well
  as what it reflects, the tracer's roughness 0.03, and the distance to what is seen behind it as the hit distance, so
  the image seen through a bottle moves with what it shows; AMD's Ray Regeneration gets the same, with a material type
  of its own; NRD leaves glass alone, as it does the sky.
- Bottles reflect the candles and lamps around them, and the sun. Glass is nearly a mirror, and its highlights came
  from the direct light's samples, a point of each lamp's disc or of the sun's with the specular lobe evaluated there,
  whose peak is capped for a surface that smooth: in the harness 2 % of a lamp's mirror image reached a glass cube, and
  2.5 % of the sun's a glass pane. The glass's reflection ray now meets the lamps' spheres (the size the game gives each
  light, as its shadows soften by it) and the sun's disc itself, and shows their light where it does: 98 % of the lamp's
  and 94 % of the sun's.
- Dark Adaptation measures the view as it is seen, HDRP's fog included (a small copy of the image before
  post-processing, read back every fourth frame): a view the mist fills is not a dark one. Measured on the traced
  light alone, Sagremor Castle's night mist counted as darkness, the traced rocks and walls got four times the light
  against the fog, which HDRP draws as the game has it, and they showed through what the game's fog hides. Where
  nothing HDRP draws is brighter than the traced light (the tavern), nothing changes. The F8 report's exposure line
  gives the seen median.
- An effect's lights (a portal, a spell: kept for "VFX", "Magic", "Portal" and the like, with no flame or lamp in their
  names) light only as far as their HDRP range, as the game draws them: a portal's two 131,072 cd lights, a glow within
  0.6 m of it in the game, were traced 40 m far and lit the whole alcove around it white. Flames and lamps keep their
  physical reach: in the tavern and the exteriors captured so far, nothing else changes.
- Fill lights left out while tracing stop lighting the fog by HDRP's switch for that (affectsVolumetric) instead of a
  fog dimmer of 0: the game's light overrides (time of day, triggers) remember a light's fog dimmer when they start and
  write it back when they end, so an override that started while path tracing had it at 0 left that light's fog dark
  after path tracing was turned off. The game's own dimmers are no longer touched for them.
- Hair far away keeps its color: there a character has only its first hair pass, which the tracer shaded with the hair
  texture (the game colors hair in its shader), so it came out grey; camera rays leave hair to HDRP's hair shader now,
  as HDRP's transparent hair layer is up close. The hair still shadows the face. Camera rays stop at hair whatever the
  tracer's alpha test says (HDRP's own decided which pixels are hair): by the tracer's, a ray slipped between strands
  to the scalp behind them, and the far hair flickered between HDRP's color and the traced grey. Nor does a camera ray
  that misses a far character's few hair cards trace the head behind them: every pixel HDRP draws forward on a
  character (hair, brows, eyes) stays HDRP's, since its skin and clothes are drawn deferred.
- Hair's shadow lets light through between its strands: a ray stops at hair by chance, as much of a texel as the strands
  cover (its 256x256 alpha tile holds that coverage), so a layer of hair lets part of the light through. Cut out where a
  fifth of a texel was covered, every hair card cast a full, hard-edged shadow on the face and neck, as dark as a solid
  sheet. In the harness a card whose strands cover half of every texel casts 0.50 of the shadow the old rule gave. Hair
  gets no opacity micromaps, which would decide it by the cutoff again.
- Light through thin skin: ears, nostrils and fingers lit from behind glow red as HDRP lights them (the thin-object
  transmission the game's skin profile uses), where lit as a solid surface a backlit ear was as dark as the back of a
  head. The light is HDRP's own: its transmittance of the skin's thickness (the GBuffer's, remapped as the diffusion
  profile sets) through the profile's Burley scattering, tinted by its transmission tint, with HDRP's cosine wrapped 15
  degrees past the terminator; the subsurface scattering spreads it with the rest. The traced geometry decides whether any
  of it gets there: from just under the skin a ray towards the light must leave it within 3 cm (an ear's far side, a
  finger's) and reach the light unblocked, so a head behind a face lets none through, nor does a character the traced
  scene lacks. In the harness a backlit skin quad over a 4 mm slab gets 0.0622 where HDRP's formula gives 0.0623, three
  times as much red as green as its profile tints it, and over a 30 cm block nothing; lit from the front it is lit as
  before. BepInEx's "HDRP diffusion profiles" line gives each thick profile's tint and thickness range.
- Traced shadows on hair and eyes (the Hair Shadows row, on by default): HDRP shades them itself, with the lights the
  tracer traces but shadows only from the few it has shadow maps for (the sun's cascades, a fire or two), so a candle
  behind a wall or a head lit an NPC's hair while the traced face beside it was dark. Their color is now darkened to the
  share of their light the traced shadows leave: the direct light of the lights HDRP doesn't shadow, as much of it as
  reaches them, against the bounced light arriving there from the radiance cache. Never brightened, and nothing is
  shadowed twice: the lights HDRP shadows and the sun while it casts HDRP's shadows are left out. One shadow ray a pixel a
  frame, averaged over its neighbours and over time. Without the radiance cache, or with the row off, they keep HDRP's
  light, and so does anything drawn forward that the traced scene lacks (it may be unlit: a glow card, an impostor).
  Eyebrows are drawn after the traced light and keep HDRP's. In the harness a table HDRP shades is darkened to 0.46 of
  HDRP's light under a lamp a block shadows, and stays at 1.00 without the block, when HDRP shadows the lamp, and under a
  sun HDRP shadows.
- Characters are traced to 100 m (45 before: past it they floated in the sun, where HDRP's cascades used to shadow them),
  the nearest first as before, at most 64 renderers. Their acceleration structures are refitted to each frame's pose and
  rebuilt from it every fourth frame, each character on its own frame: 64 characters of 8,192 triangles cost about
  0.8 ms of GPU time a frame rebuilt every frame in the harness, and about 0.3 ms this way (the fastest frames of a busy,
  shared GPU).
- BepInEx logs a hair shader's colors and values once, for matching the color rays see on hair (grey now: the game colors
  hair in its shader) to the game's.
- The Denoising tooltip is one short line per denoiser.
- Denoising offers None, NRD, Ray Regeneration and Ray Reconstruction, whichever this PC runs: Built-in (the tracer's
  own filters) is no longer a choice, and NRD takes its place wherever the tracer denoises the image itself, also
  while Ray Reconstruction waits for DLSS upscaling or Ray Regeneration couldn't start. A config set to Built-in
  becomes NRD. Denoising starts on Ray Reconstruction, then Ray Regeneration, then NRD.
- The game's fog stays while tracing. The volume that turns HDRP's own indirect light, ambient occlusion, contact
  shadows and screen-space reflections off while tracing overrode every parameter of each of those, with HDRP's
  defaults, and its fog component (there to dim the fog's ambient light) turned the fog off: HDRP's default. The
  night mist over Sagremor Castle was gone and the void below it black. Each override now sets only the parameter it
  names, so transparent surfaces keep the game's screen-space reflections and reflection probes their intensity too;
  the fog isn't touched at all (the dimming answered a haze that HDRP's auto exposure made, which the mod no longer
  uses), and the reflections of what HDRP still shades itself are the game's own, times the light at the camera.
- For GPUs without NVIDIA's extras (a Radeon RX 7900 XTX), from a review of what the tracer assumed of the driver:
  - At start the tracer checks that the driver loads its textures through typed UAVs (the half and single precision
    formats, and R32G32_UINT for the surfaces), and says so when it can't instead of drawing garbage.
  - The D3D11 composite and Shadow Detail write HDRP's image in place only where its format is a floating point one
    that loads through a UAV; anywhere else they draw over it from a copy.
  - The built-in denoiser keeps its moments in 32-bit floats and carries the standard deviation instead of the
    variance: in half precision the variance of a bright spot (a lamp's surround) passed the largest value half
    precision holds, and a dim wall's fell below its normal range.
  - A ray retried past a surface it passes through never starts past its end (undefined in DirectX Raytracing).
  - The boiling filter sums its thread group once, not once per thread; the micromap bake counts texels in integers.
  - The Ray Reconstruction hit-distance texture is released when the resolution changes, like the others.
  - A runtime that failed to create a device is tried again on another GPU or driver, not only after a Windows update
    (read from the registry: loading dxgi.dll that early would bypass a ReShade proxy).
  - The GPU test expects DirectX Raytracing 1.2 exactly where the driver reports it, and runs on any GPU by vendor
    (TGPT_TEST_ADAPTER=1002 for AMD).
- Path tracing turned off gives every light it changed exactly the dimmers the game gave it: one record per light, taken
  the first time either change touched it (darkened as a placed light, or carrying the traced light's gain). With a
  list for each, a light that went from one to the other could be put back at the wrong value, 0 among them, and an
  A/B against the game's own lighting wasn't clean. The log says how many lights got theirs back.
- A Boiling Filter row (on by default, as before), for comparing: off, the bounced light keeps samples far brighter than
  their neighbours' that NVIDIA RTXDI's boiling filter drops. Against an honest reference (the plain path tracer without
  reuse, clamps or filters, 2,048 samples a pixel), ReSTIR's bounced light was 21 % short next to a light 10 cm from a
  wall, as a candle's by a wall is, and within 1.3 % of it without that filter; among 24 lights it matters about 1 %.
  (The first measurement, 1.6 times too bright, compared with a "no reuse" run that still had the clamps and the filter,
  which left it at half the true light.) Test switches for that measurement undo each of the tracer's changes to what
  it estimates: the boiling filters, the radiance clamps, the Jacobian's limits, RTXDI's surface position.
- A Crevice Light row (off by default), for comparing: the gap under a beached boat's canvas lining looked black in
  daylight. Measured in the harness on sand under a canvas 40 cm over it, against a hull and with a 12 cm slit at its
  front, against an honest reference: ReSTIR's bounced light is 14 % short at the gap's mouth, where NVIDIA RTXDI's
  normalization counts the pixels further in for light they can't see through the slit, and 9 % too bright deeper in,
  where a light path's history dropped at 30 frames restarts dark and then counts the next lit path up to three times.
  On, a pixel counts only for light it sees, histories are kept whatever their age, and the boiling filter brings a
  sample far brighter than its neighbours down to their level instead of dropping it (dropped, a quarter of the light
  deep in the gap went once histories last): the mouth comes within 1 % and deeper in 3 to 9 % short. It is noisier
  (15 to 30 % more error after denoising in the harness's room), so it stays off until it's seen in game. No light is
  added.
  Most of the gap's darkness is real: deep under the canvas the sand gets about 1/150 of the sunlit sand's light (7 to
  7.5 stops below it) followed to every bounce, and the 2 bounces traced by default hold 53 to 72 % of that (4: 90 to
  96 %; the mouth, 86 to 89 %). The rest is what closing paths with a radiance cache would add. Shadow Detail and Dark
  Adaptation leave a sunlit view alone by design; the game's own look lights the gap with its ambient probes, which
  don't see the canvas.
- A Shadow Detail row (0 to 100 %, 50 % by default): the dark parts of a dark, uneven view (a room lit by one candle,
  where 42 % of a frame was all but black and the median pixel sat 7 stops under the brightest) are lifted by a bounded
  curve below a mid tone, and everything brighter stays as it is. On its own it acts only where the traced light's
  exposure brightens a view and its brightest fiftieth stands 4.5 to 6.5 stops and more above its median, so a lit hall
  and a sunny street are left alone; it eases in over a second, while the row's value applies at once (0 against 50 %
  compares the two). It works on HDRP's finished image after every denoiser and the upscaler, before its
  post-processing and tonemapper: a curve before Ray Reconstruction would have biased it (the average of a lifted noisy
  pixel is darker than the lifted average). No light is added. The F8 report now also names the game's tonemapper and
  color adjustments.
- NRD, after a review: its motion is NRD's 2D motion (the view depth's change it computed took the camera's move twice:
  HDRP's previous view-projection already takes positions relative to this frame's camera, which AMD Ray Regeneration's
  guides now use too); its bounced light is in units of its own that hold until its history starts over (the tracer's
  units follow the scene's brightest light, and NRD's history mixed them as lights came and went); a frame NRD can't
  denoise records nothing of it and shows the bounced light as traced; and a saved pick of NRD or Ray Regeneration
  stays while the game starts (the GPU's denoisers are known before the device counts as ready). A camera cut starts
  every history over, for every denoiser: another camera, HDRP's own reset, a jump of 20 m or a turn of more than 60
  degrees in one frame (a dialogue cutting between speakers). The package carries the notices of what the DLL compiles
  in (NVIDIA's RTX SDKs license for RTXDI and NRD, MIT for NRD's MathLib and ShaderMake reader), and Ray
  Reconstruction's on-screen warning stays quiet while FSR or XeSS upscales (the built-in denoiser serving is the
  Denoising row's own fallback then).
- The F8 capture's images look as the screen does: scaled down from a screen wider than 1920 pixels (through an sRGB
  render texture), they went through the sRGB curve a second time, which lifted every shadow and washed them out.
- DLSS Ray Reconstruction works again: the reflections' hit distance handed to it last night is one channel, which
  TG Better Upscaling read through a color view that has none, so it refused every frame until Ray Reconstruction
  turned itself off for the session, and the built-in denoiser took over (smeared, blurred, with a trail behind moving
  heads). It is read as a single float channel now, trouble with those optional inputs drops them for the frame
  (never the frame), and should NGX refuse them, Ray Reconstruction runs on without them. The upscaling smoke test
  now hands them to Ray Reconstruction with every frame, and TG Path Tracing says on screen when Ray Reconstruction
  is its Denoising setting but hasn't denoised for 5 seconds of traced frames (not while the game loads: it first
  said so at the main menu, before anything was traced).
- Less boiling with many lights: the lights a pixel's candidates come from are drawn by how much each can matter there
  (its luminance over its squared distance from the light grid's cell), where they were drawn evenly. With lights
  reaching as far as physical lights do, a cell lists dozens (up to 43 in the tavern, 11 on average), and a room's
  chandelier came up as rarely as the faintest candle glow: its light was noise that boiled and that Ray Reconstruction
  smoothed away. In the harness's night scene the direct light's error is 8 % lower (27 % without spatial reuse) and
  the bounced light's 6 %.
- Exposure and Dark Adaptation rows in the Video tab (and FoA Mod Manager). The traced light's exposure starts from the
  game's own and never goes darker: metering sunny exteriors down to a mid tone left them underexposed (at half to four
  fifths of the game's exposure). Dark views are brightened part of the way to a mid tone, 50 % by default (Dark
  Adaptation: fully adapted, a candle-lit tavern looked nearly as bright as day, and at 70 % still a little too
  bright), with the brightest fiftieth of the view at most 1.4 times white (at white, a sunlit floor in view left a
  shaded stall black). Exposure adds up to 3 stops either way; the config's hidden "Auto exposure compensation"
  carries over to it.
- The eyes are HDRP's again, irises included: traced, they took the sclera's texture over the whole eye (the iris is
  its own texture, refracted under the cornea) and came out white; left out of the traced scene, the camera ray
  looking for them went on to the eye socket just behind, and the tracer lit its skin in their place, white again.
  Camera rays stop at the eyes now and leave them to HDRP (a new material flag), which lights them with the lights
  the tracer traces, with its gain, and no longer with the placed lights and the sky's ambient light that made them
  glow.
- Lights' cookies are traced: HDRP's mask on a light's intensity by direction, which the game's candle and torch lights
  carry (PointLight_Cookies), is baked into a cookie atlas (a point light's cubemap as an octahedral map, a spot's
  texture as it is) and shapes the traced light as HDRP's. And a flame's light is traced at most 30 cm across (a fire's
  1 m): HDRP uses a light's radius only for highlights and soft shadows, where the tracer lit surfaces from a sphere of
  that size, and the game gives candles up to a metre and more.
- Hair up close is lit by the tracer: HDRP draws it as an alpha-tested first pass, which the tracer finds and lights,
  and a transparent layer over it, which the tracer can't find, so HDRP lit that with its own lights alone (black
  outside dialogue, white in a dialogue light). While tracing, hair that has both loses the transparent layer (its
  first pass shows); eyebrows, only a transparent layer, keep theirs.
- DLSS Ray Reconstruction gets the traced reflections' hit distance (for reflections as the camera moves) and the color
  before transparent surfaces and particles were drawn (for candle flames, sparks and glass, which carry no traced
  light), both written by the composite as it applies the traced light; TG Better Upscaling passes them on (looked up
  by name, so either mod works with an older copy of the other). Its diffuse guide leans towards the light's color
  where a pixel emits light or is sky, as NVIDIA's RTXPT guides it.
- What HDRP still lights with the sky (transparent surfaces, fog) is scaled by how much sky the camera sees, measured
  alone: the lamps' light the probe's paths found counted too, as if the sky were there.
- Fewer sparkles: a path vertex takes a light's light as if at least 25 cm away (a candle's own holder got I / d^2 from
  centimetres, rare very bright samples among dark neighbours), and what Ray Reconstruction takes is kept below 256
  times white, as NVIDIA's RTXPT clamps its input. ReSTIR's reuse isn't permuted on thin or finely detailed surfaces
  (a neighbour's normal turning more than 35 degrees: strands, slats, blades), as NVIDIA's RTXDI sample does.
- Opt-in, for testing (hidden settings): "Direct light visibility reuse" keeps whether a pixel sees its light in
  ReSTIR DI's reservoirs (RTXDI's initial visibility with its ray-traced bias correction): 7% less error at night in the
  harness, but a 2 cm lamp's shadow edge 4 pixels wide instead of 0.8. "Spatial reuse feeds the next frame", as
  RTXDI's sample does: 6 to 10% more error in the harness, flicker about the same.
- Characters are traced to 45 m (30 before: their shadows popped in as you came closer).
- Alpha-tested textures keep their coverage: each texel of the 256x256 alpha tile holds how much of it passes the
  material's cutoff (16 samples of the finer mip), where averaged alpha thinned strands and a rug's fringe threads
  below the cutoff, so they came and went as the shading points moved. Hair is still cut out where a fifth of a texel
  is covered, now measured as coverage.
- The traced light has its own exposure: the game's exposure (a fixed EV 13 to 14 in a tavern, a physical camera
  outdoors) is set for its own fill lights and baked light, which the tracer leaves out, and with only the real sources
  lit its interiors were nearly black. HDRP's auto exposure on the traced image brightened them (to EV 7 at the least),
  but everything HDRP draws itself is made for the game's exposure and blew out with it: flames, fire, glows, fog lit by
  lamps, hair in a dialogue light, the sky through a door. HDRP keeps the game's exposure now, and only the traced light
  is brightened, adapting as an eye does: its median pixel towards a mid tone, but never so far that its brightest tenth
  passes white, and at most down to the darkest scene EV the config sets (7) from the game's exposure. The config's
  hidden "Auto exposure compensation" and "Auto exposure darkest scene EV" tune it; the 10-second log line and the F8
  report show the gain and what it was metered on.
- Lights left out as placed to fake light no longer light anything HDRP shades itself (hair, eyes, transparent
  surfaces) or its fog: in a tavern lit by them, hair and eyes shone in the dark, and its fill lights (reaching 46 m)
  filled the room with a red haze.
- The indoor clamp on bright light is measured against the brightest light 25 cm away instead of 1 m, so the table
  under a candle and a fireplace's walls feed the bounced light again.
- Streaks along the screen edge on a quick turn: pixels a turn revealed took their light from one traced texel per
  column. They take it from a few picked at random nearby now, which Ray Reconstruction removes as noise.
- Blotchy shadows across faces: characters' scalp and beard masks (stubble and hairlines drawn as a shell on the
  skin), cut out coarsely for rays, shadowed the skin under them. They are left out of the traced scene, as lashes
  and eyebrows are.
- Skin lit like a leaf: HDRP's transmission with subsurface scattering, which the tracer lit through from behind as
  it does grass and leaves, is also what characters' skin uses (for light through the ears). Lit that way, faces had
  hard, dark patches across the cheeks and came out pale. A diffusion profile HDRP transmits through as a thick object,
  or one named as skin, is now lit as a solid surface (the game sets its human skin profile to Thin Object, as it does
  its foliage, so HDRP's mode alone left skin lit like a leaf). The log lists the game's profiles and which are solid,
  and the Surfaces debug view shows such pixels in orange. The harness's check that leaves are lit through had put its
  quad in the back wall's shadow and passed on noise; it's in the sun now, with a check for solid profiles.
- Sparkling light ("Christmas lights") in dark rooms: the boiling filter, which drops a light sample far brighter than
  its neighbours' before ReSTIR spreads it, counted the sample itself in the average it was compared with (as NVIDIA
  RTXDI's own filter does). In a group of 64 pixels with 41 or fewer lit ones (a candle-lit room, bounce rays that
  mostly find darkness) it could never drop anything. It compares a sample with the others now, once at least 4 of them
  are lit.
- Bounced light reused from a neighbouring pixel is weighted as NVIDIA's RTXDI sample weights it: a sample from very
  different geometry is dropped (beyond 10 times either way) and the rest limited to 3 times either way, where up to 8
  times before made sparkles near corners.
- Shadows of alpha-tested fringes and leaves flickered: the alpha test read its 256x256 alpha point by point, so an
  edge stepped from texel to texel as the camera's jitter moved the shading points. It is filtered between texels now,
  and the opacity micromaps are baked from the same filtered alpha (the harness still finds them identical to the
  alpha test).
- DLSS Ray Reconstruction's albedo guides are never both near black (NVIDIA's RTXPT keeps their average above 0.05):
  it divides the light by them.
- The probe key (F8) now captures what the tracer sees: hold still for about 8 seconds while it saves the image, each
  debug view, the image with the sky's light off and with rays not passing through near back faces (the two ways light
  can leak along a seam), and the game's own rendering with path tracing off, with a report of the exposure, every
  light within 40 m (traced or left out, and why) and what the game has under the crosshair. One folder per press in
  BepInEx/TGPathTracing/Captures.
- The dialogue light holds its traced slot while it fades in, so its traced light follows HDRP's from the start.
- Debug view "Surfaces": how HDRP draws each pixel (its material type; for a surface HDRP draws forward, cyan where
  the tracer found it along the camera ray and lights it, magenta where HDRP's own shading stays).
- Hair, beards and other surfaces HDRP draws forward are path traced too: the tracer finds them along the camera ray in
  its own scene and lights them like the rest, and the composite replaces HDRP's own shading there (which gave them
  the sky's ambient light: grey-teal hair and white beards in a candle-lit tavern).
- HDRP's fog is lit only by lights while path tracing: its ambient term (the sky's light, with no walls in between)
  filled the tavern with a bright teal haze once auto exposure adapted.
- Shadows no longer change as you walk up to a statue or a well: within 30 m the tracer traces an object's finest LOD
  whatever the game draws, registers it from 40 m on, and keeps a LOD it has while the one it wants is not ready (the
  object dropped out of the traced scene for a moment at each switch, its shadow vanishing and coming back).
- Indirect Color defaults to 3 and Reflection Boost to 0.2.
- Eyes are path traced: they are in the traced scene for camera rays only (every other ray passes through them, as
  solid surfaces they stopped the rays leaving the face around them), and lit like the face instead of glowing with
  HDRP's own light. So are HDRP's opaque Unlit decals within 60 m (debris on floors shown at their texture's
  brightness whatever the light: they glowed in a dark room).
- What HDRP still lights itself with the sky (transparent surfaces such as glass, smoke and water, and fog) gets as much
  of the sky's light as reaches the camera: the tracer measures the light arriving there against the open sky's along
  the same 256 rays each frame (1 in the open, low indoors) and scales HDRP's ambient light, reflections and fog
  ambient by it, eased over half a second. Glass on a tavern floor glowed with the sky.
- The air above candles and fires no longer boils: HDRP's distortion (their heat shimmer) is off while path tracing is
  on screen. It resampled the traced light, which Ray Reconstruction takes undenoised, and shifting its noise around is
  what NVIDIA's guide warns breaks the denoiser. The camera's own settings come back when path tracing stops.
- Found in two independent reviews of the RTXDI move (Claude Opus and GPT-6-astra), each checked against RTXDI's source
  and the harness:
  - Direct light could go dark near the edge of a lamp's range and blotchy wherever a pixel kept its own light choice:
    RTXDI's pairwise spatial reuse counted empty neighbours (out of every lamp's range) against the pixel's own
    light, and its Basic reuse (RTXDI's default, used now) returns a light only when a neighbour's wins, which the
    shading pass didn't allow for.
  - Temporal reuse weighs a light as it was in the previous frame (a torch that flickered, a light that just came
    into range), and a light slot another light took over is no longer taken for the same light.
  - The boiling filters are the tracer's own now: RTXDI's assume waves of at least 32 lanes and read and write past
    their shared memory on GPUs that run 8 or 16 (Intel's).
  - A bounced-light sample that became infinite or NaN is dropped instead of staying forever and spreading.
  - History starts over after frames that went untraced (path tracing turned off a while, a failed frame).
  - Bounced light no longer borrows samples from whatever surface was at a pixel before when a turn of the camera
    reveals a new one (RTXDI's fallback, which it describes as biased, usually darker).
  - Bounce rays a normal map points into the surface count as finding no light instead of being mirrored out, which
    counted their mirrored directions twice (up to 50% too bright on steep normal maps).
  - "Direct light history" 1 no longer turns off direct light's spatial reuse.
  - Direct light's visibility reuse (dropping a light a pixel doesn't see) stays off: measured with RTXDI's spatial
    reuse, it widened a lamp's shadow edge from under a pixel to 14 pixels, for 13% less noise at night.
- Hard, jagged shadow edges across faces where they turn from the sun: an opaque surface's shadow ray started some
  4 cm inside it wherever its own flat triangle faced away from the light (a rule meant for leaves lit from behind),
  and the jump drew the edge. Opaque surfaces start outside now, and the light fades out towards that edge (Chiang,
  Li and Burley 2019, "Taming the Shadow Terminator"). Shadow rays near a surface also could skip a real blocker
  nearer than something they passed through; there the nearest hit decides now.
- The castle's walls, arches and battlements are traced as the game draws them: they are Entities Graphics
  entities without Drake, whose meshes and materials Entities Graphics registers at runtime, and the tracer now
  reads them from its registry. The shadow-only meshes that stood in for them (coarser copies the game casts
  shadows with) are left out: they cast shadows the drawn walls don't (a battlement's shadow on the walkway with
  blocks that aren't there) and let sunlight leak into corridors where rays passed through them.
- Hard-edged shadows across faces: rays leaving a face into its own flatter triangles where it turns away from
  the sun were stopped by the inside of the head, because the head's object also had alpha-tested hair. Whether
  a back face is a real hit now depends on its material (alpha-tested cards), not on the object. Harness: a slab
  in an object with an alpha-tested material turned 3052 floor pixels black before, 1 now.
- A third mod built on TG Graphics Bridge: ReSTIR path traced lighting on the bridge's Direct3D 12
  device with inline ray tracing (DXR 1.1, Shader Model 6.6). Deferred surfaces are lit by the tracer:
  ReSTIR DI over the lights and the sun with traced shadows, traced bounced light (ReSTIR GI) in place of
  HDRP's indirect diffuse light, and traced reflections. An SVGF-style denoiser, and a composite one frame later through the next frame's motion vectors, so
  Direct3D 11 never waits for Direct3D 12 work of its own frame.
- The traced scene is captured from mesh renderers, Awaken's Drake ECS world, Vegetation Studio Pro's
  trees and large objects, and Unity terrains near the camera; meshes are read back from the game's GPU buffers once and built into acceleration
  structures on the render thread, materials reduced to base color, texture (a shared atlas),
  emission and alpha testing.
- Config section Path Tracing (on or off, bounces, ReSTIR reuse, ray distance, scene radius, debug views);
  Insert turns it on and off, Shift+Insert cycles the debug views; a performance overlay section.
- `tests/pathtracing_gpu_test.cpp` runs the whole path on this machine's GPU with a synthetic HDRP-style
  frame.
- DirectX Raytracing 1.2 where the GPU has it (Shader Model 6.9, raytracing tier 1.2): opacity micromaps
  for alpha-tested meshes, made on the GPU from the same atlas texels the alpha test reads, and Shader
  Execution Reordering for the GI and reflection passes on GPUs that reorder. TG Path Tracing ships
  Microsoft's Agility SDK runtime (D3D12Core.dll 1.619), which TG Graphics Bridge points the game's
  Direct3D 12 at when Windows' own is older; the bridge only takes one from inside the game folder,
  Microsoft-signed, before anything has loaded Direct3D 12.
- Denoisers: DLSS Ray Reconstruction through TG Better Upscaling on NVIDIA GPUs (the tracer writes its
  guides and hands its light over undenoised, with ReSTIR's temporal reuse permuted), AMD FSR Ray
  Regeneration on a Radeon RX 9000 (AMD's signed DLLs ship in `Native/FidelityFX`), the built-in
  SVGF-style denoiser elsewhere. Ray Regeneration is untested: no RX 9000 was available.
- The mods together: TG Better Upscaling starts NGX for Ray Reconstruction only when TG Path Tracing asks
  for it (tracing, Denoising on Ray Reconstruction); RenoDX's DLSS 5 works alongside Ray Reconstruction. A refused Ray
  Reconstruction frame gives TG Path Tracing its own denoiser back (with fresh history), a feature created in
  a list that never ran is created again, and refusals count against successes. With TG Frame Generation, NGX runs as Streamline's session on the shared device, which never
  looked in TG Better Upscaling's folder, so Ray Reconstruction could not start (0xBAD0000B); TG Frame
  Generation now lists that folder after its own when TG Path Tracing is installed. The bridge stops using
  an extension's Agility SDK runtime on the next start if Direct3D 12 could not create a device with it.
- TG Path Tracing's options are Video settings rows (Path Tracing, with its options nested under it), mirrored
  exactly in FoA Mod Manager. Denoising offers None, Built-in, Ray Regeneration and Ray Reconstruction, each
  only where this PC runs it, and starts on the best (Ray Reconstruction where it runs, RenoDX's DLSS 5 loaded
  or not). It replaces the Denoiser setting (Auto, None, Built-in): a pick carries over, Auto becomes the best.
  Indirect Color scales how much color the bounced light keeps, at the same brightness (1 as traced, 2 by default).
- TG Path Tracing's built-in denoiser keeps sharp shadows and leaves less grain, measured outside the game against
  an undenoised converged frame (48 frames of 8 rays a pixel, averaged): the sunlit floor's error fell from 10.6% to
  4.4%, and the box's and the checkered leaf's shadows are sharp again. The direct light (the sun and every lamp)
  is a signal of its own with a short filter, apart from the bounced light, whose heavy filter blurred a chair's or a
  rope net's shadow away; the filters are steered by the noise of the accumulated light, not of a single frame, and
  no longer by a 3x3 spread that took every shadow edge for noise; ReSTIR DI reuses only lights a pixel sees
  (visibility reuse). ReSTIR's temporal neighbour is permuted for the built-in denoiser too, and reflections are
  filtered across normal-map detail by their roughness. The Temporal History option is gone: every denoiser
  averages over time and needs samples that change from frame to frame, and its old default of 20 left two to four
  times the noise of 1 on normal-mapped surfaces. ReSTIR now always reuses a sample for about a frame.
- Characters are in the traced scene: the game's GPU-skinned characters (Kandra) near the camera are posed
  in it every frame, from the game's own skinning output, so they shadow themselves, cast traced shadows and
  contact shadows, and darken the bounced light around them. Tested outside the game with a skinned box in a
  buffer laid out like the game's: it casts a shadow that follows its vertices frame by frame.
- Black patches and stripes on arches, vaults and battlements: the game casts its castle's shadows with
  shadow-only meshes that are mostly the drawn walls' own geometry but coarser on curves, and in places all the
  tracer has of a building. They are traced as stand-ins: a ray leaving a drawn surface passes through a stand-in
  within about half a metre, whichever way it faces, and farther away it shadows and bounces light as before.
  Large static sets drawn by Awaken's Medusa renderer are traced too, from its own instance data; meshes on
  layers the camera does not draw are left out.
- Grass blades and leaves facing away from the sun were black: HDRP lights translucent surfaces through from
  behind. Pixels with HDRP's transmission feature now take half the light arriving from behind them.
- Grass, plants, bushes, ivy and trees drawn by the game's own vegetation renderer (Leshy, which replaced
  Vegetation Studio) are traced: its loaded cells near the camera, at the LOD it draws each instance with
  (grass and plants within 20 m, at most 60,000 instances, nearest cells first; billboards left out), so grass
  shadows itself and the ground under it.
- Only lights that stand for something that emits light are traced: a flame, a candle, a brazier, a lamp, a
  lantern, a magic effect (the light or one of its parents names one). The lights placed to fake the light raster
  rendering lacks are left out, since the bounced light is traced and they lit rooms a second time: fill lights
  (PointLight_Ambience, groups of plain "Point Light"s, unnamed lights up to 25 m across), the light that follows
  the player and light meant for volumetric fog. A fill light inside a lamp's prefab is left out too; Conversation
  Lighting Overhaul's portrait light stays. HDRP still lights fog, transparents and hair with every light.
  The 10 s log line counts them, and F8 says of each light reaching a point whether it is traced.
  The log lists the scene's lights by name and its light-effect meshes once a session.
- Forward-drawn surfaces (eyes, hair: HDRP's Eye and Hair shaders) got no ambient light and were black in
  shade: they take the game's own ambient light again, after the GBuffer is written.
- Hair casts traced shadows: strands a few texels wide averaged out in the 256x256 alpha tile and let light
  through; hair is cut out at a fifth of a texel covered.
- F8 writes to the log what the game has under the crosshair and the lights reaching it.
- Characters' eyes were black: the eye the game draws lies just inside the traced eyeball, so every ray from it
  started inside. Eyes are left out of the traced scene, as is the game's invisible material (TG/NoRender, which
  hides body parts under armour and was traced as a solid surface).
- Characters are traced with their textures: their texture coordinates are read with their triangles, so bounced
  light takes their colors and hair and torn cloth are cut out texel by texel (skin never is).
- Black patches in the shape of the traced triangles on arches, battlements and walls: where the game draws a
  surface a few centimetres inside the mesh the tracer has (an arch drawn as a smoother curve than its triangles),
  every ray from it started inside the traced mesh and found no light. A ray from the drawn surface that first
  meets the inside of a mesh close by now passes out through it. Tested with a traced-only slab around part of
  the floor: none of its pixels turn black (3075 did without the change).
- Lamp shadows are crisp: lights keep the size the game gives them (HDRP's Radius, normally 2.5 cm) instead of
  at least 10 cm, and a pixel weighs every lamp near it once (or eight spread evenly) instead of eight drawn at
  random with repeats. Harness: a 2 cm lamp's shadow edge is 0.8 px wide against 4 px for a 10 cm one.
- Black, flickering patches in the shape of walls, arches and floors: the scene is gathered again half a second
  after the camera has moved 4 m (it was every 2 seconds), so a coarser level of detail no longer stands in for
  the one on screen while walking up to it.
- Characters' hair and transparent parts are left out of the traced scene; they were traced as black, solid
  cards (shadows on faces, dark blotches on clothing).
- Alpha-tested materials (straw, rope nets, leaves) are tested against a 256x256 copy of their texture's alpha
  instead of the 64x64 color tile, which made coarse opaque blocks of them: black, flickering patches under
  straw, and no rope-net shadows. Opacity micromaps are baked from the same texels.
- Reflection Boost: makes every surface glossier and more reflective; 0.15 by default, 1 nearly a
  mirror to judge the traced reflections.
- DLSS Ray Reconstruction: TG Better Upscaling never told TG Path Tracing that it ran (its "no failure yet" start
  overflowed), so TG Path Tracing kept denoising its light itself and Ray Reconstruction denoised it a second time.
  It asks for preset F, the SDK's newest model ("Default model RR2"), in every quality mode.
- While path tracing is on, the game's Video rows it replaces are greyed out with the reason as their
  tooltip: Shadows and their distance, Contact Shadows, Ambient Occlusion, Reflections and Subsurface Scattering.
  They follow the Path Tracing row before Apply, keep the game's own conditions, and apply again once path tracing
  is off; HDRP's ambient occlusion, contact shadows and screen-space reflections are off
  meanwhile, and its shadow maps still cast light shafts in fog. Characters and the player's hands are deferred
  (the game's character shaders draw into the GBuffer): the traced light shades them, though they are not in the
  traced scene.
- TG Frame Generation: with mods fighting over the cursor in a menu (FoA Mod Manager open in game), frame
  generation resumes only once the cursor has stayed locked for half a second, not for single frames, which
  switched it on and off with hitches.
- Lighting fixes from the first test builds in the game: the capture no longer stops at HDRP's
  transparent SSR call, the composite recognises the game's device behind ReShade, a light grid with
  resampled light choice replaces a uniform pick among all lights (the fireflies), geometric normals and
  a spatial variance estimate remove the grain a longer temporal history added, area-light shadow rays
  replace hard point-light shadows, and Rays Per Pixel (1-8) trades GPU time for less noise.

Fixes from the first troubleshooting reports

- TG Better Upscaling's Upscaling row names FSR by the version that runs on this GPU, "FSR 4" on a Radeon RX 9000
  and "FSR 3.1" elsewhere (the major and minor number of AMD's version name), in the Video tab and in FoA Mod
  Manager alike, with the full version (3.1.5, say) in the tooltips of FSR's Quality Mode and Sharpness rows and in
  the log. The config still stores "FSR", so a saved choice stays valid on a GPU that runs another version.
- The game's own Framerate Limit row offers 45, between 40 and 50: 90 frames on screen with 2x frame
  generation, which the player on the RTX 4060 laptop asked for. It is an entry of the game's row, applied
  as the game applies its own limits, and like them it is unavailable while VSync is on; it is also
  unavailable in Exclusive Fullscreen, where the game would ask the display for 45 Hz. The game saves
  the row's choice as a position in its own list, which it builds without 45 at every start, so the
  mod keeps that position as the game counts it and chooses 45 again when the player left it there.
  Without TG Frame Generation, the row is the game's again and falls back to its default.
- The NVIDIA driver's Vertical sync set to On no longer gets an alert telling the player to use the
  3D application setting, and Max Frame Rate no longer gets one telling them to turn it off. A player
  on an RTX 4060 laptop found the game ran worse with the 3D application setting. NVIDIA's DLSS-G
  guide supports VSync from the driver (it overrides the game's), and with G-SYNC that is NVIDIA's own
  advice for frame generation. VSync with frame generation raises latency only where Windows composes
  the frames instead of flipping them straight to the screen, which on a laptop means a screen on the
  integrated GPU, and the "High latency" alert already covers that. A driver frame cap counts generated
  frames too, so one above what frame generation reaches does nothing to it. Vertical sync at 1/2, 1/3
  or 1/4 of the refresh rate still gets an alert, and the framerate check names the driver's VSync or
  Max Frame Rate as the cause only when the frames on screen are at them.
- The troubleshooting report has each test step's Pipeline line from the native log again, with
  Reflex's latency numbers. The filter meant to keep it dropped it every time.
- A moment out of play without a menu, such as an autosave taking its picture with a camera of its
  own, no longer pauses the test run: the step's measurement starts over, and the run pauses only if
  it lasts a second. Pausing for an autosave put the player's settings back and the step on again a
  third of a second later, rebuilding frame generation's swapchain twice.
- The Pipeline line's `d3d11Waits buffer=` count equals the presents in a D3D11 game, which renders
  every frame into one back buffer, not rarely as the 1.2.0 notes below say. The `inputs=` count is
  the one that shows D3D12 falling behind.

Troubleshooting (1.2.0)

- **Create Troubleshooting Report**'s tooltip and start message say what the test tries with the mods
  installed, and for about how long: each upscaler with TG Better Upscaling alone, each frame
  generation and low latency setting with TG Frame Generation alone, both with both. The report names
  the mods that took part.
- **Create Troubleshooting Report**, a button nested under Performance Overlay at the bottom of the
  Video settings, there with the overlay on or off (and a Video entry in FoA Mod Manager). Back in the game it runs a test of about two minutes:
  the player's own settings first, then each upscaler the game's Upscaling row offers with frame
  generation off, then frame generation off with each low latency technology, then each frame
  generation technology with each low latency technology it can run with (Off where that is allowed)
  at 2x, and once more at the highest multiplier where there is one. Each step is applied, left to
  settle and measured for a few seconds: the rendered frame rate and its 1% low, CPU, GPU and video
  memory use, frames on screen, the presenting and latency technology, and where the presentation
  bridge spends its time per frame. Then the player's settings come back and the report is saved as
  one text file in the BepInEx folder, beside LogOutput.log (`TG Troubleshooting Report <date>.txt`,
  about 25 KB), to paste into a bug report on Nexus. It has a summary (system, mods, problems found,
  the test results, what the native side knows), then this session's part of each log, filtered to
  what explains a problem: from BepInEx's log the mods' lines, warnings and errors with their stack
  traces and plugin loading; from the preloader's and native logs this session's lines (found by the
  process ID), with frame generation's 5 s pipeline lines thinned to the first and last few and one
  per test step; errors and warnings from Streamline's and the game's logs; the mods' config values;
  and where each of the mods' files is, with its size, date, version and whether it is a Vortex link.
  Repeated lines are counted, and the Windows user folder is replaced by `%USERPROFILE%`. A test step never writes the
  player's settings: frame generation's steps override what it requests, upscaling's steps select
  the row in the game but leave the mod's config alone, and when the game pauses the player's
  settings come back until play resumes, so the settings screen never shows a test value. If the game
  closes during a test, the next start puts upscaling back from the config. Selecting the button
  again stops the test and saves what was measured; before the test starts, it saves the logs alone.
- The mods' problems are shown on the Video rows they concern, not as rows of their own: the Frame
  Generation row for TG Frame Generation and whatever interferes with frame generation, the game's
  Dynamic Resolution row (the upscaler, which TG Better Upscaling extends) for TG Better Upscaling
  and whatever interferes with upscaling. Such a row gets an amber alert icon after its name and the
  most serious problem as its tooltip, as "Source: Label. What to do" in a sentence or two ("TG Better
  Upscaling: Wrong folder. Its files are in ..."). The full explanation goes in the report. The top of
  the performance overlay lists all of them the same short way, wrapped to under half the screen.
  Each mod tells the bridge when it has started; one that BepInEx loaded but that never started (an
  error while starting, before it could log anything, which this game's missing Unity log hides) gets
  a "Didn't start" alert on its row. When TG Frame Generation has a problem and no row of its own, a
  stand-in Frame Generation row reads Off with the icon and the reason. Tooltips name the game's two versions as the Nexus files do (the
  normal version, and the Mono one whose files end in "mono"), never by runtime names. They cover:
  - where a mod is installed wrong: its archive's BepInEx folder unpacked into BepInEx, into
    BepInEx/plugins (what a mod manager does with a plugin-type mod; with Vortex the fix is the
    Engine Injector type), into a folder of its own, or its contents straight into the game folder;
    a missing preloader or other files; the build for the other game branch; a plugin BepInEx found
    and didn't load; TG Frame Generation 1.0 still installed; a `disable-autoload.txt`;
  - other software that conflicts or limits: OptiScaler (running, or files left over), DLSS Enabler,
    the DLSSG-to-FSR3 mod, FSR 3 mods, Special K, RivaTuner Statistics Server with a frame rate limit
    (read from its profiles), Lossless Scaling running, and from the NVIDIA driver's profile for the
    game: Max Frame Rate, forced VSync, Smooth Motion (ID from NVIDIA Profile Inspector, not yet
    checked against a driver with it on) and a DLSS Frame Generation override that forces it off.
- When frame generation turns on and the framerate on screen doesn't rise by 15% over the rate just
  before, a message says so during play and a row in the Video settings explains it with what this
  session shows: the game's Framerate Limit, VSync (at the display's refresh rate or every second
  refresh), a limit outside the game, video memory nearly full, rendering that fell much more than
  frame generation costs (with Anti-Lag 2 on, hybrid graphics, or AMD Fluid Motion Frames as things
  to try), or nothing known, with the troubleshooting report as the next step. Dynamic mode that
  reaches its target counts as a gain.
- DLSS Frame Generation with VSync on, while the display is connected to another GPU than the one
  the game renders on (a laptop's panel on its integrated GPU), shows a "High latency" alert: NVIDIA's
  DLSS-G guide says high latency is expected with VSync on such a system. The fix it gives is VSync
  off, or the laptop's setting that makes the dedicated GPU drive the display.
- Frame generation's row reads Off, greyed out, with the alert icon when it can't run, and its tooltip gives the first thing
  that went wrong with what to do: the install problem, OptiScaler, a preloader that didn't run,
  another Streamline, and for a Direct3D 12 device Windows refused, why. 0x887E0003 names the Agility
  SDK versions, the Windows build, and whether the bridge corrected the game's request or something
  loaded Direct3D 12 before it could, but only when the bridge saw that mismatch. Otherwise it says
  the game's D3D12Core.dll is missing or doesn't match, and to verify the game's files. The row's
  own value stays one word, and the tooltip is the short form. When DLSS Frame Generation isn't offered on an NVIDIA GPU, the
  tooltip says why from Streamline's own answer: Hardware-accelerated GPU scheduling off, an old
  driver or Windows, or a GPU before the RTX 40 series.
- The native logs have more to go on: the Windows build, the game's adapter, driver version and video
  memory, which GPU drives the display (a laptop's hybrid graphics shows as the display on the other
  GPU), the NVIDIA driver settings above, and in frame generation's 5 s pipeline line, where the
  presenting thread's time goes per frame (waiting for the GPU, the SDK's Present, handing the frame
  back to D3D11) and the frame interval with the frames over 50 ms. New optional exports carry these
  to the plugins: `TGGraphicsBridge_ReadDiagnostics`, `TGGraphicsBridge_LogNote`,
  `TGFG_ReadDiagnostics`, `TGFG_ReadTimings`, `TGFG_LogNote`. The ABIs are unchanged.
- The bridge is 1.2.0, so a copy bundled with a 1.1.0 mod stays idle beside it.

Frame generation and compatibility fixes (1.2.0), from a review of hardware the mods haven't been
tested on (only an RTX 5080 on Windows 11 has been)

- The game's next frame no longer waits for frame generation to finish the current one. After
  every Present, D3D11 used to wait on the GPU until D3D12 had finished the whole present, frame
  generation included, before any of the next frame's work could run. With DLSS-G (which blocks the
  presenting queue until its work is done) and FSR (whose generation runs on that queue) the game's
  rendering and frame generation took turns instead of overlapping. That fits the reports of frame
  generation lowering the framerate on slower GPUs. D3D11 now waits only for what it is about to overwrite: a
  back buffer until D3D12 copied it, and an input texture until the present that used it three
  frames earlier retired. Usually neither has to wait; the pipeline line counts the times one did.
  Each direction has its own shared fence now (D3D11 signals the producer fence, only the D3D12 queue
  the consumer fence): a fence Signal sets its value, and with D3D11 no longer waiting for every
  retirement, a single fence signalled by both could have moved backwards.
- Reflex, XeLL and Anti-Lag 2 now wait in Unity's PlayerLoop right after
  `TimeUpdate.WaitForLastPresentationAndUpdateTime`: after Unity's own wait for the previous frame's
  presentation (Intel's XeLL guide wants other waits before XeLL's) and before any input is read,
  Unity's or Rewired's. Their SDKs ask for the wait just before input is sampled; it ran in a
  MonoBehaviour's Update, after the input it was meant to precede, which made the input each frame
  used older by the wait. If another mod replaces
  the PlayerLoop, frames begin in Update as before. The native frame ID is now a counter that rises
  once per frame instead of `Time.frameCount`, which can still hold the previous frame's number there.
- VSync is turned off for DLSS Frame Generation when the screen is connected to another GPU than the
  one the game renders on: a laptop's screen on its integrated GPU (the default hybrid mode of most
  gaming laptops), or a monitor in the motherboard's port. NVIDIA's DLSS-G guide expects high input
  delay with VSync and frame generation where Windows composes the frames instead of flipping them
  straight to the screen, and says it is better not to enable VSync there. Frames copied to another
  GPU's screen are usually composed, but not always: hybrid laptops can flip them straight to the screen
  too (the two-copy hybrid path, Windows 11's cross-adapter scan-out), where VSync off would tear. So
  while VSync is held off, the bridge presents without the tearing flag (`TGFG_SetNoTearing`, a new
  optional export): the newest frame shows at the next refresh, with no VSync wait and no tear either
  way. The mod turns the game's own
  VSync toggle off once, as a player would, and sets Target FPS to Unlimited: the game shares that row
  between VSync rates and frame limits and would otherwise move it to the nearest limit, a cap the
  player never chose. It greys the toggle out with a tooltip saying why and shows a short message; a
  limit the player picks afterwards stays. Choosing FSR or XeSS, or a screen on the rendering GPU, offers the toggle again; VSync
  stays off until the player turns it on. VSync the NVIDIA driver forces on can't be changed from the
  game: the "High latency" alert then says where to change it in the NVIDIA App. An advanced setting
  turns this off. Which GPU the screen is connected to is checked again every 5 s, so moving the
  window or a laptop switching its screen between GPUs is noticed, and Streamline's measured copy time
  between the GPUs is in the pipeline line (`crossAdapterCopy`).
- Alerts for memory while frame generation runs: "VRAM full" when the game stays over the video memory
  Windows gives it (with how much frame generation added), and "RAM full" when less than 1 GB of system
  memory is free or 95% is in use. Both make the game stutter as memory moves out and back. System
  memory also counts as a cause when frame generation gives no framerate gain, and the report lists
  free memory.
- DLSS-G's UI recomposition is off. It was on whenever a HUD-less image was supplied, but NVIDIA's
  guide allows it only when a UI buffer is tagged as well, and the bridge tags none. DLSS-G still
  gets the verified HUD-less image, and it no longer spends recomposition's memory and GPU time.
- DLSS-G below its minimum output size (`minWidthOrHeight`) is off until the resolution changes,
  as NVIDIA's guide requires, with the reason "resolution too low". The runtime's
  resolution-too-low status used to end frame generation for the session.
- NVIDIA Reflex is offered in Low Latency only when Streamline reports it available
  (`lowLatencyAvailable`: an NVIDIA GPU from Maxwell on with driver 456 or later). NVIDIA's guide
  requires this for the Reflex choice; DLSS-G still runs Reflex whatever the choice. A query that
  fails is asked again before the plugin reads the capabilities, and without an answer Reflex isn't
  offered.
- XeSS Frame Generation generates only from frames XeLL slept for, and only while XeLL's low latency
  mode is on. Intel requires XeLL with every XeSS-FG frame; the first frame after switching to XeSS
  began before XeLL existed. A sleep mode XeLL refused is not asked for again every frame (each
  attempt costs a GPU drain).
- The Base FPS limit with XeSS Frame Generation capped the frames on screen, not the rendered ones:
  XeLL's limit counts every Present. XeLL now gets it multiplied by the presents per rendered frame.
- XeSS Frame Generation and XeLL are loaded only where Intel supports them: Windows 10 21H1 (build
  19043) or later and the Visual C++ runtime 14.40.33810 or later, checked on the copy they would
  bind to (one already loaded, one next to them or the game, or System32's). Elsewhere the Frame
  Generation row's tooltip says which one is missing.
- The three native DLLs link the C and C++ runtimes statically. The game ships no `msvcp140.dll`, so
  they used whatever System32 had. Without the Visual C++ Redistributable they didn't load at all,
  and with one older than 14.40 the first `std::mutex` lock crashed the game: MSVC 14.40 and later
  construct `std::mutex` in a way older runtimes can't lock. They no longer call
  `DisableThreadLibraryCalls`, which Microsoft forbids with the static runtime, and their extension
  factories catch everything before it reaches the bridge's runtime. NVIDIA's, AMD's and Intel's DLLs
  still need the redistributable, so the bridge records the copy they would use (`windows.vcruntime`,
  in the report), a "Visual C++ runtime: Missing" alert appears when there is none in Windows or the game
  folder, and a Streamline that failed to load with Windows error 126 or 127 says which it was.
- The troubleshooting report is written whole and then renamed, so a full disk or the game closing
  leaves no half report. A log longer than the 8 MB the report reads says the session may have started
  before them, instead of that its native part didn't start.

Error handling (1.2.0), from a final review of every failure path

- No C++ exception leaves a native entry point: Unity's render-thread callbacks, the exports the
  plugins call every frame, the performance sampler's thread (which now starts over after an error)
  and the bridge's exports catch everything. The three native logs never throw, since they are called
  from catch handlers, some in functions that must not throw.
- A removed device signals its fences to their maximum, which also woke the CPU fence waits: they
  now check again after waking, so device removal is reported as such instead of passing as a
  finished frame. A bridge whose GPU work was never confirmed finished keeps its GPU objects for the
  rest of the session instead of freeing them under running work; the upscaler does the same on a new
  game device.
- A settings file that can't be written (read-only, locked by an antivirus scan, a full disk) no longer
  stops a mod: settings bind with BepInEx's saving off and every save goes through one place, which
  keeps the values for the session and shows a "Can't save" alert on the mod's row. Each plugin's
  start is guarded and logs why it failed, and the preloader starts the bridge even if it can't create
  its log folder.
- The troubleshooting test run starts only once its recovery file is written, keeps that file until
  TG Better Upscaling has put the upscaler back, reports a step that couldn't be applied as such
  instead of measuring it, says which mod couldn't put your settings back, and puts them back if the
  report's own code fails mid-run.
- After FSR or XeSS stops with an error, STP really takes over at the chosen render scale. The game's
  upscaling controller knows no FSR or XeSS, so handing it that choice turned upscaling off and
  rendered at full resolution. Video memory has to stay over 95% of the budget on two 10 s checks in
  a row before FSR and XeSS give way, not on one sample over 90%.
- TG Better Upscaling extends the upscaling row for DLSS's quality modes even where FSR and XeSS are
  unavailable. Its "Add upscalers to Video settings" setting says that the game applies its upscaler
  through that row.
- The game builds its display and VSync rows again when the window moves to another monitor; the
  Exclusive Fullscreen restriction and the VSync hold follow the new rows. An Apply press in the Video
  tab counts once, so a later Restore Defaults can't apply display rows the player is still editing.
- The display check follows the game's window when the swapchain is replaced on the same device.
  Anti-Lag 2's and XeLL's per-frame errors are counted and logged. GPU usage is the busiest engine of
  the game's GPU, as Task Manager counts it, not engines of one type added together.
- A disable-autoload.txt in the bridge's folder shows on both mods' rows. RivaTuner's frame limit
  and Lossless Scaling are checked again every 30 s, so their alerts clear once they are changed or
  closed.

Two mods and a shared bridge (1.1.0)

- The repository now builds two mods on a shared part. **TG Graphics Bridge** is the DX11-to-DX12
  bridge and its BepInEx preloader, taken out of TG Frame Generation: it owns the graphics hooks,
  observes the game's device, creates one D3D12 device inside the game's swapchain creation, and
  loads the native part of each mod built on it (`BepInEx/<mod>/graphics-bridge-extension.json`).
  **TG Frame Generation** is now one of those extensions. **TG Better Upscaling** is new.
- The bridge is bundled with each mod, not a download of its own, under the mod's own folders
  (`BepInEx/patchers/<mod>.Autoload.dll`, `BepInEx/plugins/<mod>/TGGraphicsBridge.dll`,
  `BepInEx/<mod>/Native/TGGraphicsBridge.Native.dll`), so installing both deploys no file twice and
  Vortex reports no conflict. With both installed, the preloader bundled with the newest bridge
  starts it once for both, and BepInEx loads the newest copy of its plugin; BepInEx logs that it
  skips the other copy.
- TG Better Upscaling adds AMD FSR (FSR 4 on Radeon RX 9000, FSR 3.1 elsewhere) and Intel XeSS to the
  game's own Upscaling row, after None, STP and DLSS, with the row's quality slider as the render
  scale, and an FSR Sharpness row under FSR. They run where HDRP runs STP, on HDRP's
  own color, depth and motion vectors, on the bridge's D3D12 device, and return the upscaled image in
  STP's output texture, so post-processing, RenoDX, ReShade and frame generation see an upscaled frame
  as they would after STP. A refused frame is enlarged instead of left black; five in a row turn that
  technology off for the session and STP takes over.
- FSR has no version row: it runs AMD's default for the GPU, which is FSR 4 on a Radeon RX 9000 and
  the newest FSR 3.1 elsewhere (3.1.5 on an RTX 5080, checked). AMD asks that a version is either
  left to that default or chosen by the player, never picked by the application; the native log names
  the version AMD chose and the newest it offers.
- A performance overlay, switched on in the last row of the Video settings (Performance Overlay, with
  Overlay Position and Overlay Size below it), FoA Mod Manager or TG Graphics Bridge's config: frame
  rate and frame times, render and output resolution, VSync and the frame cap, CPU, GPU and video
  memory use, and a section from each mod (the upscaler, its mode and version, the DLSS model and DLL;
  frame generation's technology, mode, multiplier and frame rate on screen). The DLSS model comes from
  the NVIDIA driver's profile for the game when the NVIDIA App overrides it (preset, performance mode,
  render scale, DLAA), and is the DLL's default for the mode otherwise.
- DLSS, FSR and XeSS get their own quality modes in the Video tab (Quality Mode) instead of the game's
  render-scale slider, which STP keeps. Only the modes each runtime reports are offered, at the ratio
  it reports: NGX's recommended size per DLSS mode through Unity's NVIDIA module (DLAA only when NGX
  sizes it), FSR's per-mode ratio from AMD's runtime, XeSS's seven presets from Intel's. DLSS gets the
  mode as its NGX quality level, so NVIDIA's driver picks that mode's model; the game had given DLSS a
  level derived from the slider's position. XeSS initializes with the chosen preset. A config without
  a mode starts each upscaler on the highest-quality mode at or below the game's render scale (never
  more pixels than before), and the game's graphics presets set the modes by the same rule. The
  upscaling ABI is 1.2 (`TGUP_GetModes`, `TGUP_Frame.mode`).
- FSR gets HDRP's exposure, as HDRP's own DLSS pass takes it (a readback requested every frame,
  limited to 0.2..2): as its pre-exposure, which it divides out so that its history holds while the
  game's exposure adapts, and as its exposure texture, so it still works on the color HDRP tonemaps.
  It had a fixed pre-exposure of 1, so every exposure change looked like a change in brightness to
  its history. XeSS, which has no pre-exposure input, keeps working on the pre-exposed color.
- With FSR, textures get AMD's recommended mip bias, `log2(render / output width) - 1`: HDRP applies
  `log2(render / output width)` wherever STP runs (Intel's guidance for XeSS), and FSR adds the
  extra -1 through the camera's material mip bias while it is selected.
- TG Better Upscaling brings NVIDIA's DLSS 310.9.1 DLL (`Native/DLSS/nvngx_dlss.dll`, from the DLSS
  SDK) for the game's own DLSS, which ships 3.8.10. When NVIDIA's runtime loads an older
  `nvngx_dlss.dll`, it gets the bundled one; an equal or newer file, such as a DLSS mod's, is kept.
  Nothing in the game folder is replaced. A DLSS mod's own set of DLLs is kept whole as well: with
  DLSS neural rendering (`nvngx_dlssnr.dll`, DLSS 5) or frame generation or ray reconstruction of the
  same release beside the game folder's `nvngx_dlss.dll`, that file stays, since the RenoDX DLSS 5
  add-on's frame rate stopped following the render resolution with ours in its place.
  Stage-Runtime.ps1 takes `-DlssRoot`.
- Only FSR gets a sharpness row, at 50% by default. DLSS has no sharpening any more (NVIDIA's headers mark it "not
  supported" and the game's DLSS 310 ignores it) and XeSS never had one.
- The bridge hooks the swapchain methods of DXGI factories up front, on a factory of its own created
  past any hook already on `dxgi.dll`'s exports, before any extension loads. Up to 1.0.x frame
  generation saw the game's swapchain only because something else created a DXGI factory where its
  hooks saw it, which happened with NVIDIA's runtimes loaded; with ReShade installed as `dxgi.dll`,
  which forwards the game's factory calls past later hooks, TG Better Upscaling alone never saw it, and
  frame generation on AMD and Intel GPUs most likely didn't either. Installed first, the bridge's
  hook also runs inside ReShade's, so ReShade leaves the bridge's D3D12 device and frame generation's
  swapchain alone, and the RenoDX DLSS add-on still gets a device of its own.
- When frame generation's native side reports nothing (it didn't load or start, another mod already
  loaded NVIDIA Streamline, or it couldn't take over the game's presentation), the Frame Generation row
  offers Off only, greyed out, and its tooltip says why. Up to 1.0.x it showed DLSS Frame Generation
  with multi-frame choices on any GPU in that case, which did nothing. A report that arrives later
  replaces those rows.
- With TG Frame Generation 1.0.x still installed, which runs a graphics bridge of its own, the bridge
  does not start and says to update Frame Generation to 1.1.0; two bridges cannot share the game's
  device.
- Frame generation and upscaling work on Windows 10 and older Windows 11 builds. The game's exe asks
  Windows for Direct3D 12 Agility SDK 610 from its `D3D12` folder, which holds D3D12Core.dll 611.
  Where Windows' own runtime is newer (Windows 11 24H2 and later), Windows uses its own and never looks
  there; where it is older, Windows loads the game's file and refuses every D3D12 device over the
  mismatch (0x887E0003, D3D12_ERROR_INVALID_REDIST), so up to 1.0.x frame generation never started
  there. Before any runtime loads `d3d12.dll`, which reads the request as it loads, the bridge sets the
  request to the version the game ships, and logs the three versions it found. Reproduced and checked
  on Windows 11 with an exe asking for 618 beside a D3D12Core.dll 619: 0x887E0003 before, a device
  from that file after.

Frame generation quality

- DLSS Frame Generation and Intel XeSS-FG receive depth, motion vectors and a camera in the same
  orientation as the frame they interpolate. HDRP renders Y-inverted, so both were reprojecting from
  mirrored buffers and visibly warping during camera movement; all three backends now look alike.
- The HUD-less image is HDRP's colour at `AfterPostProcess`, taken in the frame it belongs to, instead
  of a swapchain capture that was one frame late.
- First-person hands no longer flicker: Kandra's previous skinned positions survive the extra
  camera-rendering contexts that used to overwrite them within a frame.
- The HUD stays out of frame generation with ReShade and RenoDX, by replaying RenoDX's recognised
  final-output shader on the capture, and by accepting ReShade's wrapped D3D11 device.
- FSR is given the resolution the game renders at rather than the display resolution, and near and far
  swapped for inverted depth, as AMD's own integrations do.
- FSR resets its history when the HUD-less source changes, and rebuilds its context when input flags do.
- DLSS-G is told to allocate its UI recomposition path only while a verified HUD-less image is supplied.
- A still image - a menu, a pause, a loading screen - no longer counts against the HUD-less capture.

Display settings

- Turning frame generation on moves the game's own display setting from Exclusive Fullscreen to
  Borderless Fullscreen, and stops offering Exclusive while it is on. Frame generation cannot run in
  Exclusive Fullscreen: DXGI will not give its swapchain a window that already holds the display.
  `Use borderless for frame generation` in the advanced settings turns this off.
- Low Latency can be Off while FSR frame generation runs; AMD pairs FSR with Anti-Lag 2, which NVIDIA
  hardware does not have.

Stability

- Frame generation survives a display-mode change. Unity replaces the swapchain rather than resizing
  it, and the replacement was refused because a bridge already existed, which left the game presenting
  through its own swapchain for the rest of the session.
- Fixed a crash when the game kept using a bridge it had replaced. Twenty-two swapchain methods
  forwarded to a presentation swapchain that was no longer there, three of them as tail calls through
  its vtable.
- Frame generation starts on an sRGB back buffer instead of failing to create the bridge. The view that
  failed exists only to verify the HUD-less capture, so the bridge now continues without it.
- The HUD-less capture has to earn its verification again whenever the buffers change; the previous
  result outlived the buffers it was measured on.
- A `ResizeBuffers` asking for the configuration already in effect is skipped. FoA issues seven in a
  minute of Options use, each costing a GPU drain, a buffer rebuild and a frame over 100 ms.
- Fixed the IL2CPP crash when opening Options with upscaling active.

DLSS-G correctness

- `slDLSSGGetState` is no longer given an options pointer, which it applies rather than reads.
- The capabilities query stays at startup; deferring it to the present thread broke the Video tab twice.
- The pipeline line reports DLSS-G's own presented-frame count, and prints `unreported` for FSR rather
  than a zero that read as failure.

Diagnostics

- Reflex and XeLL are measured on one comparable figure, `simToPresent`, alongside each technology's
  own. Every average divides by the frames behind it, which `renderLatency` did not.
- A refused bridge says what the game asked for - size, format, buffers, swap effect, flags and whether
  the request was windowed or exclusive fullscreen - on all three creation paths.
- The log records whether the game protects its immediate context, and says so once if the bridge ever
  uses that context from a second thread.
- FSR debug flag 32 saves three source colour, depth and motion frames with their metadata, once.
- Added a D3D11 WARP regression test for the HUD-less colour conversion shader, and portable tests for
  the clip-space orientation change.
- New optional exports `TGFG_SetHudlessCapture`, `TGFG_SetHudless` and `TGFG_SetDebugFlags`.

Packaging

- The Nexus downloads carry `vortex_override_instructions.json`, which sets the mod type Vortex calls
  "Engine Injector". Vortex was installing the BepInEx folder under its own mod directory instead of
  the game root, so nothing loaded. Manual installs and other mod managers are unaffected.

# v0.5.0 - AMD FSR and Intel XeSS frame generation, one Low Latency selector

- The plugin is now "TG Frame Generation" with the ID com.wessberg.tgframegeneration. Settings
  live in BepInEx/config/com.wessberg.tgframegeneration.cfg; the old
  tgfoa.experimental.dlss-frame-generation.cfg and BepInEx/TGFrameGeneration/VideoSettings.cfg
  are migrated on first start and renamed to *.migrated.
- FoA Mod Manager lists exactly the Video tab's six rows, with the same names, choices, labels,
  order and visibility rules. Choices follow what this GPU reports, and a change made there
  shows up in the Video tab once the Options screen is closed. The other options sit in a hidden
  [Advanced] section.
- Avalon Mod Manager's MODS tab no longer lists TGFG: TGFG adds itself to that manager's
  blacklist for the session, without saving its config. Its Apply writes back the values staged
  when the Options screen opened, which undid Video-tab changes.
- Low Latency can be Off only while frame generation is Off; FSR now runs with Reflex or Anti-Lag
  2. The row is greyed out when only one choice remains (XeSS with XeLL).
- Frame generation no longer needs DLSS upscaling. Without DLSS, a custom pass before
  post-processing hands the native side HDRP's own depth and motion vectors (TGFG_SetHdrpInputs,
  render event 3), with HDRP's TAA jitter and the motion vector scale HDRP gives DLSS. DLSS's
  inputs take precedence while it runs. FSR and XeSS frame generation therefore work on AMD and
  Intel GPUs. If the custom pass can't be installed, the rows are greyed out unless DLSS is the
  selected upscaler.
- The standard version (IL2CPP, BepInEx 6) has the same Video rows and HDRP input path as the Mono
  build, from the same sources. TGFG's subclasses of the game's Setting and DependentOption and of
  HDRP's CustomPass are registered with Il2CppInterop, which routes the base class's virtual calls
  to them. Il2CppInterop allocates those objects without running the IL2CPP base constructor, so
  TGFG runs it (Il2CppTypes.Construct).
- Packages include NVIDIA's license notices next to the Streamline runtime
  (LICENSE-Streamline.txt, LICENSE-NVIDIA-RTX-SDKs.txt, LICENSE-NVIDIA-Reflex.txt), staged from
  the SDK release by Stage-Runtime.ps1.
- Packaging writes zip entries with forward slashes (Compress-Archive in Windows PowerShell used
  backslashes) and copies each package to artifacts/Nexus as "TGFrameGeneration - mono.zip" or
  "TGFrameGeneration.zip". Build paths are mapped, so the DLLs no longer name local folders.
  A package holds the BepInEx folder and TGFG-Package-manifest.json; TGFG-PLAYER-INSTALL.txt is
  no longer included.
- Removed leftovers of the 0.3 patch workflow: APPLY.md, integrate-autoload.patch,
  source-manifest.json, the validation logs, docs/VALIDATION.md, the retired Launch-Game.ps1 and
  the 0.3 example config.
- Video tab: Frame Generation [Off | DLSS | FSR | XeSS]. A technology is listed only when its
  runtime is installed and reports support on the GPU. FSR runs through AMD's FSR API (FSR SDK
  2.3.0: FSR 4 ML frame generation on Radeon RX 9000, FSR 3.1 analytic frame generation elsewhere)
  and AMD's frame generation swapchain. XeSS runs through Intel's XeSS-FG swapchain (XeSS SDK
  3.0.2). All three use the same inputs (see the HDRP input path above).
- Frame multipliers come from the runtimes: DLSS from Streamline's numFramesToGenerateMax, XeSS
  from xefgSwapChainGetProperties (up to 4x on Intel Arc, 2x elsewhere). The FSR API has no
  multi-frame control, so FSR is 2x. Frame Multiplier offers up to the highest reported maximum,
  blocks values above the selected technology's maximum and clamps the value when switching
  technology; the native side clamps the request as well. Neither FSR nor XeSS has dynamic frame
  generation, so Dynamic stays DLSS-only. The 6x cap is gone (the ABI bound is 16).
- Switching technology recreates only the private presentation swapchain, between two frames: the
  Streamline swapchain for Off/DLSS, the SDK's own swapchain for FSR/XeSS. The game's D3D11
  buffers are unchanged. An SDK that fails is disabled for the session and presentation falls
  back to the standard swapchain.
- Low Latency [Off | NVIDIA Reflex | NVIDIA Reflex + Boost | Intel XeLL | AMD Anti-Lag 2] replaces
  the NVIDIA Reflex row and lists what the GPU supports. DLSS Frame Generation requires Reflex and
  XeSS Frame Generation runs with XeLL only (Intel's requirement), so the row forbids the other
  choices for those. XeLL on its own needs an Intel GPU; Anti-Lag 2 needs an AMD GPU. The 0.4.0
  "NVIDIA Reflex" value migrates to "Low latency"; the advanced base FPS limit feeds whichever
  technology is active.
- The bridge no longer requires DLSS-G: Streamline binds when DLSS-G or Reflex is supported (RTX
  20/30 get FSR/XeSS with Reflex), other GPUs get a plain D3D12 device, and a Streamline start
  failure no longer stops the other technologies.
- A game waiting on the swapchain's frame-latency object gets a bridge-owned semaphore that
  survives swapchain recreation; the private swapchain no longer carries that flag. HDR metadata
  is re-applied to a recreated swapchain.
- Native ABI v5 (0x00050000): the request gains backend and latencyMode (was reflexMode) and
  baseFpsLimit (was reflexBaseFpsLimit); the status reports available backends, per-backend
  maxima, latency technologies, the active output and latency technology, the GPU vendor and
  backend failures; the camera packet carries the world-to-view matrix XeSS needs.
- Packages add Native/FidelityFX (AMD-signed loader and frame generation DLLs, license notice)
  and Native/XeSS (Intel-signed libxess_fg.dll and libxell.dll, license notice).
  Stage-Runtime.ps1 takes -FidelityFXRoot and -XeSSRoot; Build-Native.ps1 takes the FSR, XeSS and
  Anti-Lag 2 SDK roots, defaulting to folders next to the script.
- Managed and native versions are 0.5.0.

# v0.4.0 - native Video settings rows, Reflex modes, startup crash fix

- Video tab: Frame Generation [Off | DLSS], Frame Generation Mode [Fixed | Dynamic] (shown only
  when the runtime reports Multi Frame Generation and Dynamic support), Frame Multiplier
  [2x .. max reported] (Fixed), Max Frame Multiplier [Up to 2x .. max] and Target Framerate
  [Auto | 60 .. display maximum] (Dynamic; the max is sent as numFramesToGenerate, which the
  Streamline 2.14.1 guide documents as ignored in Dynamic mode, so it is best-effort) and
  NVIDIA Reflex [Off | On | On + Boost] are real game Setting/PrefOption rows registered through
  SettingsMaster.AddGraphicSetting, placed after Dynamic Resolution, with the game's Apply
  Changes / Undo / Restore Defaults. Reflex Off is forbidden while Frame Generation is on.
- Video settings persist in BepInEx/TGFrameGeneration/VideoSettings.cfg, outside the plugin's
  main config, so generic settings managers no longer re-apply stale copies over them. The
  old UGUI adapter and its Mode/Fixed multiplier/target entries in the main config are gone.
- Native ABI v4 (0x00040000): TGFG_Request gains reflexMode (Off/On/On + Boost). Reflex
  follows the user setting and is forced to at least On while FG is requested.
- Managed and native versions are 0.4.0.
- DLSS 5 (RenoDX DLSS addon) compatibility: NVIDIA's sl.interposer.dll is staged and installed,
  byte-identical and still NVIDIA-signed, as `Native/Streamline/TGFG.Streamline.dll`. The addon
  looks Streamline up by module name, hooks the interposer's exports and DXGI factories, and
  releases (E_UNEXPECTED) every swapchain ReShade did not observe, including TGFG's private
  output. Deployment backs up and removes the old `Native/Streamline/sl.interposer.dll`. The
  native log reports another mod's Streamline if one is loaded later.
- ReShade compatibility: NVIDIA's Unity plugin evaluates DLSS on ReShade's proxy context, not the
  real immediate context. The bridge tags the real context with private data (which ReShade
  forwards) and matches on it, so depth/motion capture works under ReShade + RenoDX.
- Video tab: Frame Multiplier is shown only in Fixed mode; Dynamic mode shows Max Frame Multiplier
  and Target Framerate instead. The pipeline log line includes Reflex's own latency reports.
- Fixes found in the first in-game FG runs: the end-of-camera marker never executed under HDRP
  (endCameraRendering fires after the last Submit); FoA's camera rect is not bit-exact full screen;
  the R32G32_FLOAT motion copy is not NT-handle shareable on NVIDIA D3D11 (now R16G16_FLOAT).
  A "Pipeline 5s:" line in TGFG.Native.log shows where frames drop out.

- Native: removed 64 KB stack buffers (GetModuleFileNameW/GetSystemDirectoryW) from the
  GetProcAddress hook and bootstrap paths. The hook runs on every thread in the process,
  including NVIDIA driver worker threads with small stacks, and the 64 KB frame probe caused
  a STATUS_STACK_OVERFLOW at the first Streamline Present. Paths are now heap-grown.
- Build-Native.ps1: passes -DCMAKE_BUILD_TYPE=Release for single-config generators; the
  NMake build previously linked the debug CRT.

# v0.3.1 - repository-integrated Steam autoload

- Adds src/TGFG.Autoload in the existing solution and gameplay build graph.
- Same root Build.Local.props, two runtime-specific managed pairs.
- Build.ps1 -Deploy stages/installs complete plugin/preloader/native/runtime package.
- Package.ps1 produces a launcher-free player ZIP from this repo's artifacts.
- Test-Setup.ps1 checks the installed Steam-autoload layout, not developer launcher files.
- Replaces stale launcher advice and adds an autoload log tail to gameplay diagnostics.
- Retains all native v0.3.0 source, SDK build scripts and ABI unchanged.
- No Windows compilation, PowerShell execution or in-game validation claimed.

# Changelog

## 0.3.0 — DX11 rendering / private DX12 Streamline presentation

Replaced the D3D12-source backend with a direct D3D11-to-D3D12 bridge. Added a
D3D11-facing swapchain, shared textures/fences, typed D3D11 NGX capture, protected
input conversion, HDR output conversion, expanded diagnostics and local setup
checks. Kept the Mono/IL2CPP managed targets and existing in-game menu adapter.

Changed the early launcher to require `-force-d3d11` and rendezvous after Windows
loader initialization at the restored PE entrypoint. ABI changed from v2 to v3.
Added Windows HLSL compilation tests and portable interop policy checks. No
OptiScaler/FidelityFX rendering backend is included. Windows build/game testing
has not been performed in the preparation environment.

The HUD-less capture export is implemented, but no verified FoA/RenoDX HUD-less
stage is hooked. Indexed source buffers, nonblocking presentation, full device
recovery and zero-overhead Off are not claimed.

## 0.2.0

Earlier direct Streamline prototype, requiring actual D3D12 game rendering.
Not compatible with this version's native ABI.

## 0.1.0

Earlier architecture was superseded. It is not a dependency of this package.
