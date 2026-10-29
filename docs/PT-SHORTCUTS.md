# TG Path Tracing: every shortcut, stand-in and limit the coordinator knows of (2026-10-07)

Written because the owner asked: "if there's any other surprises in store, now's the point where you tell me, so i can
redirect you ... I want a full, correct, proper Path Tracer, that looks amazing, without hacks, without shortcuts. Somehow
you have failed to fully respect this." They had found on their own: the arbitrary limits, the 32 sharp texture slots, no
mirror reflections, hair laid on after denoising, no camera rays of the tracer's own.

"Sure" was checked in the code or a log on 2026-10-07. "To check" is the coordinator's memory or an agent's report. This
list is from one agent's knowledge; an outside audit of the whole tracer against it is the owner's to order. Each item
needs the owner's word: fix, keep, or decide later. `docs/PT-REQUESTS.md` carries the work (PT-50, PT-51, PT-59 to PT-61).

## A. How the picture is made

1. Sure. The camera's first surface comes from HDRP's G-buffer, not the tracer's own rays. See-through things (hair,
   glass, water) are bolted on. Ruled out by the owner on 2026-10-07 (PT-59, PT-60).
2. Sure. One ray a pixel (`Rays per pixel` 1), at about half the screen's size (1924x1084 for a 4K screen), then Ray
   Reconstruction or NRD rebuilds and upscales the picture. Without a denoiser the picture is very noisy. Usual for
   real-time path tracing, but everything fine (hair, brows, thin bars) lives or dies by the denoiser.
3. Sure. `Bounces` defaults to 1: light bounces once by rays. What comes after is a cache (the "radiance cache": light
   kept in a coarse grid of the world, cells of 10 cm and up, averaged over 16 frames). Reflections rougher than 0.6
   take the cache's light instead of tracing. The cache holds a cell's light to 2,000 times a reference.
4. Sure. `Indirect Color` defaults to 3: the bounced light's colour is tripled in strength (a red cloth reddens the floor
   three times as much as traced). Not physical, on by default (CHANGELOG line 1482).
5. Sure. `Boiling Filter` on by default throws away about a fifth of a candle-lit room's bounced light (the owner knows
   the row: "jury is out"). Measured by the limits agent: the denoisers keep 0.81 to 0.94 of the undenoised light, and
   the reuse of light samples between pixels and frames (ReSTIR) keeps 0.934.
6. Sure. Picture-shaping rows that are not physics: `Dark Adaptation` 50, `Shadow Detail` 50, `Fog Adaptation`,
   `Reflection Boost` 0.1 (the owner's), `Exposure`. The traced light has an automatic gain of its own, apart from the
   game's exposure.
7. Sure. The game still renders its whole own lighting every frame (lights, shadows, reflections, ambient occlusion),
   and most of it is thrown away while tracing: wasted frame time (PT-60 item 9).

## B. What is not traced at all

8. Sure. Fire, smoke, rain, dust, magic effects, every particle and see-through effect: drawn by the game over the
   picture, not lit by traced light (PT-29 to PT-31). The noisy "gas" rectangle above candles is one of them (PT-06).
9. Sure. Fog: the game's own, lit by the game's lights (the fill lights too, the owner's ruling for the fog), not by
   traced light: no traced shadows or shafts in it (PT-37).
10. Sure. The sky is the game's sky picture. Clouds cast no shadows while tracing (PT-36).
11. Sure. Decals (blood, dirt, wet patches, glowing runes) exist only in the game's picture: a ray does not see them, so
    a mirror does not show them.
12. Sure. Surface relief on walls (parallax) is ignored (PT-28).
13. Sure. Nothing farther than 250 m from the camera is in the traced scene (`Scene Radius`); a ray ends at 500 m.
14. Sure. Objects Unity has statically batched are left out of the traced scene (logged once: "N statically batched
    renderers near the camera are left out"): they cast no shadow and show in no reflection.
15. Sure. Grass and leaves are traced STANDING STILL while the game sways them in the wind (`SceneCapture.cs` 1162); rays
    are told not to trust such geometry near where they start (`MASK_SWAYS`). Cloth is traced in its swayed pose; plants
    are not. Against the owner's ruling "What moves is traced as it moves".
16. Agent's report (PT-61). The tracer traces coarser copies of rocks than the game draws (level of detail).
17. Sure (log). The eye's cornea is left out of the trace; eyes are partly shaded by the game with traced shadows.
18. Sure. With the camera under water the game's own picture is shown.
19. Sure. The hero's own body has a stand-in head for reflections; later bounce rays still meet the inside of the head.

## C. Lights

20. Sure. A traced light is the game's light object, kept or dropped by rules (its names, what glows near it), not the
    flame itself. Its size is a guess by kind with a cap (a candle at most 1.5 cm); its strength is the game's number.
21. Review, not tested. A lamp's light and its shadow use different shapes; the shadow rule for lamps moved to their
    flames is right for direct light and wrong for bounced light and hair.
22. Review, arithmetic. With the raised limit of 65,536 lights, rounding gives many faint lights zero chance of being
    chosen: they light nothing. Excess lamps are still dropped outright in one place.
23. Sure. Glowing surfaces count as lights up to 262,144 triangles.

## D. Materials

24. Sure. The tracer does not run the game's shaders. It re-reads a handful of them its own way (standard, skin, hair,
    eye, cloth, plants) from their textures and numbers. Whatever else a shader does is lost to rays.
25. Sure (log). Textures under 64 KB (285 of 815 in the tavern) are still read as 64x64 copies.
26. Sure. Fixed numbers for every material of a kind: leaves pass 50 % of the light, thin cloth 30 %, every glass bends
    light as 1.5.
27. Sure. Skin's scattering is a screen-space blur in the build in the game (the traced one is built, not merged, PT-58).
28. Sure. A ray that hits something which is also on screen borrows the game's on-screen material for it
    (`hdrp_material_at`, `pt_specular.hlsl`): the same thing can look different in a reflection when it leaves the screen.
29. Sure. Glass is a special case with a capped number of crossings (64).
29b. Added 2026-10-08 (PT-78), told to the owner the same morning. A texture the engine makes before the tracer's
    texture hook can exist (today one: the water's foam mask, 5.4 MB) cannot be opened, so the plugin makes it once
    more, an exact copy at full size, and the tracer reads that. Against "Don't do copies" until the hook is in from the
    game's start.

## E. Water and caustics

30. Sure. To rays water is a flat sheet; its waves never reached the tracer until today's fix (not in the game yet).
31. Review, confirmed by an agent. The water's body colour is a white surface standing in for light scattered in water;
    the bed through water is 1.78 times too bright; absorption is applied on averaged lengths. At most 4 waters.
32. Sure. Caustics: light lands on surfaces it never hit, shows only at the camera's first surface, and costs the frame
    rate (5 to 6 fps in the cave lake). Light off wet floors that are not water is not built.

## F. Hair

33. Sure. Hair is a layer laid on after the denoiser (ruled out, in rework). Its bounced light comes from the cache's
    cell under the scalp. Brows and lashes: brows a layer, lashes held opaque in the game's buffers.

## G. How it was tested and reported

34. Sure. The offline test uses simple scenes of its own; "passes offline" never meant "looks right in the game". The
    waves' handoff was never tested at all.
35. Sure. The frame-rate numbers the coordinator quoted left out the water passes and everything outside the tracer.
36. Sure. Path tracing needs TG Better Upscaling for Ray Reconstruction, and runs on the Mono version only.

## The owner's word, 2026-10-07 15:15 (rulings in AGENTS.md "The owner's word on the shortcuts list")

| Item | The owner | What follows |
|---|---|---|
| 4 Indirect Color 3 | "Let's keep it at 1 for now" | Done in the tree 15:17: default 1, a saved 3 moved to 1 once. Not in the game yet. |
| 15 plants stand still | "of course our shadows and reflections should move alongside the grass and leaves swaying in the wind" | PT-63 the whole world traced |
| 14 batched objects left out | "that sounds like a big problem ... Fix that" | PT-63 |
| 13 nothing past 250 m | "Why? Do that" | PT-63 |
| 11 decals | "they should! Of course they should" | PT-63 |
| 3 one bounce, then a cache | "You mean Neural Radiance Caching takes care of the remaining bounces, right? And we can always increase the number of bounces, right? As far as I recall, I saw no difference between 1 and higher values" | Answered: it is NOT NVIDIA's neural cache, it is a plain grid of averaged light of the mod's own; Bounces goes to 4. PT-66 |
| 2 one ray a pixel at half size | "Do full resolution traces. Ray Reconstruction is not a given. We can expose as a config option to do half res or quarter res rays" | PT-65 |
| 25 small textures as 64x64 copies | "why?! ... Don't do copies" | PT-64 |
| 28 a reflection borrows the screen | "Well, don't" | PT-64, with PT-59 |
| 24 not the game's shaders | "sounds like a big problem, fix that" | PT-64 |
| 26 fixed numbers | "how can we fix that? Big problem too" | PT-64: each material's own values |
| 20 a lamp is the game's light object | "sounds like a problem too" | PT-67 with particles: the flame itself emits |
| 8 particles | "particles need to be traced. This is in the list of requests already, I hope!" | It was (PT-29 to PT-31), as "to look at"; now PT-67, a job |
| 7 the game's own lighting rendered | "super wasteful, don't do that" | PT-60 item 9, after the camera rays |
