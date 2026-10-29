# Design job: particles, transparents and fog in the tracer (PT-71 items 7 and 8) - a document, no code

Read `AGENTS.md` first ("The owner's rulings": "The tracer fires its own camera rays, for every pixel", "Finish the
migration first", "Do. not. fake. stuff.", "No limits that change the picture", "Keep the game's fog", "No fake
lights", "A mirror must show the world as the camera sees it"), then `docs/PT-REQUESTS.md` entries PT-71, PT-67, PT-29
to PT-31, PT-06, PT-37, PT-41 (grep the headings, read those entries only), `docs/agents/hair-6.md` ("The camera-ray
design"), and the fog and light parts of `docs/PROJECT-CONTEXT.md` section 9 (grep "fog", "Fill Light", "particle").

## The situation

TG Path Tracing is a path tracer inside the Unity 6 / HDRP 17 game Tainted Grail: The Fall of Avalon (Direct3D 11 game,
the tracer on Direct3D 12 through a bridge). Since today every pixel's first surface is the tracer's own camera ray's
hit; HDRP's G-buffer is only compared against. The owner wants everything on screen rendered by the tracer ("I expect
visual regressions until the renderer is fully migrated. Focus on finishing the migration to our own renderer, then we
can A/B with vanilla and ensure we render everything"). Two things HDRP still draws over the traced picture:

1. **Particles and other transparents**: smoke, fire and flames, sparks, rain, dust, glow sprites around candles (the
   owner sees "a rectangle full of noise above candlelights" without a denoiser), magic, heat haze (the owner: it
   "should bend the picture through the tracer"). The owner: "particles need to be traced." And: "A light is the thing
   that emits it" (a flame is the emitter, not the game's light object with a guessed size).
2. **The game's fog** (HDRP's volumetric fog and its local fog volumes). The owner's rulings: the game's fog stays, it
   is the game's art style; artists' fill lights keep lighting the fog though they light no surface; and the tracer
   should light the fog too ("shadows and light shafts in the mist?": "yes"). Today HDRP lights it with its own
   lights and shadow maps, composited over the traced picture.

## What to deliver: one design document

For each of the two, a design the next agent can build from, with no stand-in for traced light anywhere:

- **What the game has.** From the code in this tree (`src/TGPathTracing/*.cs`: what it already finds of the game's
  particle systems, visual effects, lights and fog; grep "Particle", "VisualEffect", "VFX", "Fog", "LocalVolumetricFog"),
  from the notes, and from Unity's and HDRP's public documentation as you know it: which kinds of particle renderers
  and shaders a Unity 6 HDRP game uses (ParticleSystem billboards and meshes, VFX Graph outputs, lit and unlit,
  soft particles, distortion), and how HDRP's volumetric fog is built (its froxel grid, density volumes, how lights and
  shadows enter it). Say what is known and what must be found out in the running game; for the unknowns, specify the
  inventory the plugin should log in one harness run (per system: renderer type, shader, blend mode, count, bounds,
  whether it has a light) so the coordinator can run it.
- **What a particle is to a ray.** The physically right model for each kind (an emitting, absorbing, scattering medium
  for smoke, fire and dust; small emitters for sparks; drops for rain; a refracting medium for heat haze), how the
  game's own data for it (positions, sizes, colours, textures, lifetimes, as the game simulates them each frame) reaches
  the tracer every frame (the plugin reads the particle buffers? VFX Graph's GPU buffers?), how a camera ray, a shadow
  ray, a mirror ray and a bounce each meet it, how a flame becomes the emitter that lights the room (replacing the
  game's light object at it), and what the denoisers' guides get. No limit that changes the picture; where a budget is
  needed, it may cost noise only.
- **How the tracer lights the fog.** The game's fog density stays HDRP's (the same volumes, the same look at rest);
  the light in it becomes the tracer's: the sun's and lamps' light with traced shadows, light shafts, fill lights still
  lighting it (the ruling), the fog seen in mirrors and through glass and water. Options (the tracer marching the
  game's density with its own lights and shadow rays; feeding traced visibility into HDRP's own fog lighting), their
  costs, what each needs from HDRP (how to read its density volumes), and your recommendation.
- **Order of work and checks.** Steps small enough for one agent each, each with the offline check (the tracer's GPU
  test: `tests/pathtracing_gpu_test.cpp`, judged with no denoiser) and the one in-game scene that proves it
  (`tests/ingame/scenarios.json` lists the scenes: `candle`, `campfire`, `fog`, `night`, `tavern-room`).
- **What it removes** (the owner: "remove all the 'hacks' and workarounds as we go"): which passes, settings and special
  cases of today's code die with each step (grep the plugin and `native/pathtracing` for what exists around fog,
  particles and transparents: `FogPass`, `EventFog`, Fill Light Fog, the candle glow handling).
- **Open questions for the owner**, in plain words, only those that are truly theirs (looks and priorities).

Plain words, short sentences, numbers where you have them. Mark every statement about the game that you did not verify
in this tree as an assumption. Do not read the NVIDIA RTXDI or NRD sources (`RTXDI-Library-3.1/`, `NRD-SDK-4.17.3/`),
and quote nothing from any SDK. 3 to 6 pages.
