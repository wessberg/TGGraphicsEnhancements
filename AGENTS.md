# AGENTS.md - TG Frame Generation, TG Better Upscaling, TG Path Tracing and TG Graphics Bridge

Mods for Tainted Grail: The Fall of Avalon (FoA): Unity 6000.0.64f1, HDRP 17, rendering with Direct3D 11. Two game
versions: the Mono branch with BepInEx 5 (players call it the "Mono version") and the standard IL2CPP one with BepInEx 6
(the "normal version"). Repo: github.com/wessberg/TGFrameGeneration, working branch `fg-input-orientation`.

## START HERE, EVERY SESSION - read these, in this order

1. **`BACKLOG.md`** - the goal, the owner's rulings, the open defects in priority order, the queue. For path tracing
   its Open list is one line per entry of **`docs/PT-REQUESTS.md`**: every request of the owner's and every defect
   found, with the owner's words, the evidence and what comes next. **Add an entry there, and its line in BACKLOG, the
   moment a request is made or a defect is seen** (owner, 2026-10-06: "maintain a list of all the requests I have, and
   track them in the backlog so they're not forgotten ... If you discover other visual defects or areas of improvement
   for better compatibility with PT, please add them to the backlog too").
2. **`docs/PROJECT-CONTEXT.md`** - read the section for the mod you will touch and **DEAD ENDS** before you change
   code or ask the owner to test anything. Load other sections only when relevant; the released mods' sections:
   `docs/PROJECT-CONTEXT-MODS.md`.
3. **`ROUNDS.md`**'s newest row, then its **`docs/rounds/round-NN.md`** - where the last session stopped, and the
   "▶ Starter prompt" at its end.

The owner's word in the current conversation outranks a handoff's order.

**This harness is the only memory (owner, 2026-09-28).** The owner switches between agents (Claude Code, Codex, ...).
Every rule, learning, insight and constraint goes into these files, never into one agent's private memory: a note only
one agent can read is lost to the next. At session end update them all: BACKLOG (state, rulings, open list),
PROJECT-CONTEXT (learnings, dead ends), a ROUNDS.md row and `docs/rounds/round-NN.md` (the narrative, ending with
"▶ Starter prompt (R<next>)"). A round is one working session or day; R1, R2, ... in files `round-01.md`, `round-02.md`.

### The one case where you skip step 2
For an explicit "cold read" or "unbiased review", skip `docs/PROJECT-CONTEXT.md` and say so. That is for review and
analysis only. Never skip it when changing code: its DEAD ENDS stop re-attacks on things already measured not to work.
A subagent is not cold: it inherits this file.

---

## The goal

| Mod | What it is for | State (2026-10-09) |
|---|---|---|
| TG Frame Generation (TGFG) | Native DLSS, FSR and XeSS frame generation, with Reflex, XeLL or Anti-Lag 2. No OptiScaler, no wrapper. | 1.2.3, on Nexus since 2026-10-09 (1.2.0 before it) |
| TG Better Upscaling (TGBU) | FSR 3.1 / 4 and XeSS in the game's own Upscaling row (the game itself has FSR 2 since its 2026-10-06 update), vendor quality modes, a newer DLSS DLL. Hosts DLSS Ray Reconstruction for TGPT. | 1.2.3, on Nexus since 2026-10-09 (1.2.0 was broken by that update) |
| TG Path Tracing (TGPT) | "Proper ReSTIR path tracing" (owner, 2026-09-25): everything on screen lit by the tracer. Mono only. | Prototype. **The current priority.** |
| TG Neural Rendering (TGNR) | The DLSS 5 mod (`docs/DLSS5-*.md`, `docs/nexus-neural-rendering.md`). | 1.2.3, done (owner, 2026-10-09: "the mod is done"); on Nexus since 2026-10-09 |
| TG Graphics Bridge (TGB) | The shared D3D11-to-D3D12 bridge. Bundled inside each mod, never a download of its own. | 1.2.3 |

One tree, one session (owner, 2026-10-09 14:35: "I am about to release Neural REndering, the mod is done, so you can
just have that as part of your build setup, we no longer build multiple things in parallel"): TG Neural Rendering is
built and packaged with the others, and no second session edits this tree any more. Every csproj, plugin constant,
the CMake project and the six zips of 2026-10-09 say 1.2.3 (checked that day when the owner asked; "1.2.1" was left
only in notes). Those zips were built in the other session's own tree (`build\nr`); since 14:44 that day
`build\native` and `artifacts\Native` hold all six native DLLs too, so `Build.ps1 -Package` works for every mod.
Two cautions: a packaging run REPLACES that mod's zip in `artifacts/Nexus` with new, untested binaries (the released
six are kept in `artifacts\Nexus-1.2.3-backup`); and a TG Path Tracing package built here carries another build of
bridge 1.2.3 than the released mods in the game folder, so while iterating install the tracer with
`tools\harness\Deploy-PtBuild.ps1` (plugin and native only) and keep its two bridge copies the released ones.
"No second session" stopped being true the same evening: a session of round 18 (Windows Defender flags the released
TG Frame Generation 1.2.3 DLL; `docs/rounds/round-18.md`, BACKLOG's "URGENT") edits this tree beside the path tracing
work. So: before writing a shared file (BACKLOG, ROUNDS, this file) read it again, change only your own lines, and
never revert lines you did not write.

The acceptance test is what the player sees in game, and the owner is the judge of how it looks.
Players are not technical (owner, 2026-09-25): the mods fix things for them, they don't hand them chores.

## How to talk to the owner

> "You talk in riddles and really weird academic language. I haven't understood anything you've written all day."
> "treat me like I have ADHD, and please ELI5" (the owner, in their other project; it holds here)

- Short sentences. Plain words. Lead with the answer, then what you did, then what you need from them.
- Numbers and file paths are welcome. Explain a technical word in plain words the first time you use it.
- No bold slogans, no arrows, no saying one thing three ways. If a reply grows long, write it in a file and give a
  three-line summary.
- Ask about goals and priorities while you work, not at the end. Before a long detour, say in one line which thing the
  player will see it serves. If you can't, stop and ask.
- **Choices go on a page with pictures** (owner, 2026-10-12, of twelve questions in a report: "please present me a
  page with visualizations so I can easily pick betweeen them"): one card a choice, the versions side by side as
  pictures of the game (a table or a drawing where no picture exists), what each costs in a line, the pick marked
  that the agent would take. A choice about MOTION is shown "in shots where you grab frames during motion with motion
  blur disabled so I can pick from shots instead of feel, and so you can also look at it too". Where the owner writes
  "I didn't have enough context" beside a pick, the choice comes back with real screenshots. Claude Code: the Artifact
  tool with the page's own store for the picks; that day's page: https://claude.ai/artifact/JbWmoPqCgAS7em2nN1aSFt
  In the harness a moving step takes `"disableMotionBlur": true` (without it the game's own motion blur is ON and
  smears every frame, a denoiser or none: the first run of the recipe `nrd-motion` was thrown away for it); each of
  its frames then says `motionBlurActive: false`. Pictures the owner must judge closely are given at the game's own
  pixels, with their file paths ("The images are too low-res to judge", the same day, of a half-size sheet).
- Keep claims apart: "built", "passes the harness", "seen in game" and "the owner says it's fixed" are four claims.
- Check before you blame. A mod runs only if `BepInEx/LogOutput.log` has its `Loading [` line (Vortex leaves
  config-only folders behind). A theory is true when a number shows it. Two blames had to be taken back (TG Global
  Illumination on 2026-09-26 and RenoDX on 2026-09-27: neither was installed).
- Text for players (Nexus pages, tooltips, README) is the owner's voice: plain, first person. Nexus pages are BBCode,
  which the owner calls "markdown". Run a no-AI-slop pass on README and Nexus text (Claude Code: the `no-ai-slop` skill).
  Details: PROJECT-CONTEXT "Writing for players".

## The owner's rulings - hard rules

Path tracing:
- **No limits that change the picture** (2026-10-07, of the hair layer's fixed count of 8 cards a camera ray walks
  through: "There is no budgets or limits there, this is PT, whats that about?"). This file's reading: a budget may cost
  noise or speed, never change what the picture converges to; a cap that cuts light, coverage or detail (a layer
  count, a fixed number of lights or swaying cloths, tiles that run out) is a bug to replace with an unbiased way.
  **No shortcuts** (2026-10-07, the same morning: "dont do shortcuts, we want realism and correctness, and then we
  will optimize from there"): build the correct, realistic thing first; speed comes after, without changing the result.
  And of skin's screen-space scattering blur, described as "imitated": "No, I said we don't want shortcuts or hacks - we
  want proper path tracing, and everything should be path traced". Never describe an imitation as done; name it as a
  gap (PT-51 lists the ones left). And again at 11:07, when the lamp's light off water turned out to be a flat mirror
  times HDRP's ready-made ripple picture, a glow with no caustic lines in it: "I'm getting tired of telling you this.
  Do. not. fake. stuff. Real path traced caustics". So: never build, keep polishing or propose an effect that stands in
  for traced light (a ready-made pattern texture, a flat stand-in for a surface that has waves, a screen-space blur for
  scattering). When a real effect needs a piece the tracer lacks (the water's real waves for its rays), build that
  piece. A stand-in already in the code is a defect with an entry of its own, to be replaced, and never shown to the
  owner as the feature.
- **Speed is no longer left for last** (2026-10-09 10:45, told that the tracer runs at 8 frames a second in a
  town square because speed work had waited for correctness: "Correctness first, sure, to a point, but I mean
  right now we're at like 8 FPS. It can barely render"). So speed work runs beside the correctness work from
  now on (PT-99): first what costs nothing in the picture; what would change the picture (fewer rays, shorter
  paths, a coarser pass) is shown to the owner as a choice with its numbers, never taken silently. The rule of
  2026-10-07 below ("realism and correctness, and then we will optimize from there") still says what a
  shortcut is; it no longer says speed must wait.
- **Shader Execution Reordering stays on; the tracer is designed for it** (2026-10-10 10:30, told that the bounced
  light's pass ran in 14 ms as a plain compute pass and 56 ms as the ray generation pass with reordering, the same
  picture, and offered a chooser that picks the faster: "Do not turn that off. If it makes it slower with that on,
  then it's either not due to that, or due to us not designing our tech with it in mind"; and before: "It's the
  feature that makes PT viable to accelerate on modern GPUs"). So: never ship, default to or propose a path that
  switches reordering off or picks a pass without it; the plain compute passes are a YARDSTICK for tests only (a
  reordered pass that is slower than its plain twin has a design fault of ours: find it by measuring, then
  redesign that pass for reordering: what a thread carries across the regrouping, what it is regrouped by, hit
  objects and hit shaders where they fit). The same reading holds for the other things that make path tracing
  viable on this hardware (opacity micromaps, the denoisers): when one of them looks like the cost, the cause is
  looked for in how the tracer uses it, and the owner is told the measurement, not a plan to switch it off.
- **The order of work** (2026-10-10 01:05, told that RTX Mega Geometry's first piece saves 0.03 ms and all of it
  about 3 ms and 280 MB, and asked whether to build its next part now):
  "RTX Mega Geometry is important, but not as a first step - that'll be a later optimization. The priority is
  getting this under control, and then fix the remaining rendering issues and missing features".
  So: (1) "under control": a frame rate one can play at and video memory that fits (PT-99, PT-87), with what
  keeps the picture; (2) the remaining rendering issues and missing features (`docs/PT-REQUESTS.md`); (3) later
  optimizations, RTX Mega Geometry among them (PT-109: its first piece stays in the tree, switched off). A
  worker slot goes to (1) before (2), and a game run to (1) before (2), unless the owner asks for a thing by name.
  Later is not never (the same day, 10:40: "Also don't forget about RTX Mega Geometry, it might end up giving us a
  bigger boost than we think"): it stays an open entry, and its gain is reckoned again each time the frame gets
  faster (what building the structures costs is a fixed few milliseconds: a small share of a slow frame, a large
  one of a fast frame).
  **RTX Mega Geometry is ON and stays on; where it is slower, the fault is ours** (2026-10-12 11:00, told in a
  status that it no longer hangs, is still about 6 % slower than the plain structures and sits in the code switched
  off: "I still want us to enable RTX Mega Geometry. Do not turn it off. I still insist there must be something
  we've done wrong if it's not making things faster. Othewise, what's the point?"). This overrules "a later
  optimization" above and "switched off". So: it is enabled by default where the driver reports it (the plain
  structures stay as the path for cards and drivers without it, and as a YARDSTICK for tests, never as the choice
  when it is slower); never ship, default to or propose a build with it off; when it costs more than the plain
  way, the cause is looked for in how the tracer uses it (what is clustered and how large, what is built and
  refitted a frame and in how many calls, the flags, the micromaps under it, the top level), by measuring, with
  NVIDIA's documentation and sample read in full first (the rule on vendors' guides below), and the owner is told
  the measurement and the fix, not a plan to switch it off. PT-109 is the entry.
- **Everything on screen is part of the main traced picture** (2026-10-07, told that traced hair is laid over the
  picture after the denoiser, because the tracer starts from HDRP's opaque G-buffer and see-through things never enter
  it: "wtf, why? That sounds like a terrible idea. Of course it needs to be inside the frame ... This sounds like the
  wrong design entirely"). Hair, and by the same reasoning glass and water, are surfaces of the tracer's own camera
  rays, with their depth, motion, normals and albedo handed to the denoiser, never a layer added after it. Asked why not
  real strands with physics: the coordinator recommended fixing this design first, strands as a decision for later.
  **The tracer fires its own camera rays, for every pixel** (2026-10-07, told the tracer starts from HDRP's G-buffer:
  "yes, of course it should be like that. Why isn't it? We want full, proper path tracing. Should go without saying ...
  Everything should be in the main picture. I can't believe this wasn't obvious"). HDRP's G-buffer is no longer the
  camera's first surface; it is the yardstick the tracer's own first hit is checked against, pixel by pixel. What the
  tracer cannot yet read of a surface (decals, terrain blending, wet patches) is a gap to close, not a reason to start
  from HDRP's picture. It comes before every other path tracing job (15:22: "I think number one thing to do before
  anything else is make sure WE trace the world, not the game"): build nothing new on HDRP's picture as the start.
  **Finish the migration first; the picture may get worse on the way** (2026-10-07 17:50, told that outdoors the
  camera-ray build looks worse than the one before it, the tracer's first hit matching the game's on 64 % of a forest's
  screen: "well, isn't that expected until we've fully migrated to our own renderer?", then: "I expect visual
  regressions until the renderer is fully migrated. Focus on finishing the migration to our own renderer, then we can
  A/B with vanilla and ensure we render everything"). So: until everything on screen is the tracer's (the list:
  PT-71), a picture worse than the old route's is no reason to stop, to go back to HDRP's picture or to polish the old
  route; report it as a number in the status, not as an alarm. Looks (hair's fineness, candles, caustics' shape) wait
  unless they are part of the migration. When the list is done: the A/B against the game's own picture, scene by scene.
  **Remove the workarounds as we go** (2026-10-07 17:55: "Also remove all the 'hacks' and workarounds as we go, all the
  stuff we did as workarounds for not controlling the tracing from the camera or whatever. So we have a clean base to
  work from"). Whatever exists only because the tracer started from HDRP's picture (layers added after the denoiser,
  special cases for glass, water, eyes and forward-drawn things, surface offsets guessed from HDRP's depth, borrowed
  motion and materials, passes that patch HDRP's shading, the switch back to the old route once nothing needs it) is
  deleted in the same job that makes it dead, with its settings, tests and docs: not switched off, not kept "for an
  A/B". Every job's report lists the workarounds it met, removed or not (PT-60 is the list).
- **The owner's word on the shortcuts list** (2026-10-07 15:15, `docs/PT-SHORTCUTS.md`; the owner had found the limits,
  the texture copies, the missing mirrors, hair after the denoiser and the missing camera rays on their own: "I want a
  full, correct, proper Path Tracer, that looks amazing, without hacks, without shortcuts"; tell the owner of every
  shortcut BEFORE they find it):
  - Everything the game draws is in the traced world: no object left out ("that sounds like a big problem, if things
    suddenly disappear when PT is enabled. Fix that"), no distance past which nothing is traced ("Why? Do that"), decals
    seen by rays ("they should! Of course they should"), particles traced ("particles need to be traced").
  - Grass and leaves sway in the tracer as in the game ("of course our shadows and reflections should move alongside the
    grass and leaves swaying in the wind. Otherwise what's the point of realtime ray tracing").
  - Rays at the screen's full resolution by default ("Do full resolution traces. Ray Reconstruction is not a given"); a
    row offers half and quarter resolution rays.
  - No copies of textures ("Don't do copies"): every texture a ray reads is the game's own.
  - A reflection never borrows the game's on-screen picture ("Well, don't").
  - The tracer reproduces what every one of the game's shaders shows ("sounds like a big problem, fix that"), and each
    material's own values replace fixed numbers (leaves' 50 %, cloth's 30 %, glass at 1.5: "Big problem too").
  - A light is the thing that emits it, its real shape, not the game's light object with a guessed size ("sounds like a
    problem too").
  - The game's own lighting is not rendered while tracing ("super wasteful, don't do that"). Nor anything else twice
    (2026-10-07 22:00, told the game's effect particles would be found by watching its draw calls: "but we don't want
    to render the game twice?"): the game simulates (its effects, its water, its wind) once and the tracer draws once.
    What the tracer needs of the game is taken where the game's simulation leaves it, never from a draw the game must
    keep doing for our sake; a draw may be watched only to learn where that data is. And at 22:05: "Yeah, exactly,
    off-screen effects must be included, no screen space stuff": a flame behind the camera lights the room and shows
    in a mirror; nothing the tracer shows or is lit by may depend on what happens to be on screen.
  - Indirect Color defaults to 1, as traced ("Let's keep it at 1 for now").
- **Path traced, always: never ask whether to keep HDRP's or a faked way** (2026-10-12, asked whether far hills
  should keep the game's haze or get real haze: "I didn't have enough context to pick between these two, but whenever
  you ask me if we should use HDRP stuff or fake stuff instead of Path Traced stuff, I pick Path Traced stuff"). So:
  between the game's rasterized or faked way and the traced, physical one, take the traced one and say that it was
  taken; ask only where two TRACED ways differ, or where the answer costs speed. What stays the game's is its content:
  the fog's density, colour and volumes, the lights' strengths, the materials.
  **The choices of that day** (the page above; each in its entry of `docs/PT-REQUESTS.md`): the sun's and the moon's
  light come from the sun and moon one sees, not from the game's hidden light held 30 degrees up; far land gets real
  haze; clouds cast moving shadows; glass and water under NRD: way B on glass first, then A; NRD's filter RELAX
  (NVIDIA's for RTXDI), both to be shown in motion, the lost detail to be had back NVIDIA's way; RTXDI's three
  settings: NVIDIA's defaults tried in the game beside ours, shown; the traced light one frame late: built behind a
  hidden switch and shown in frames grabbed during motion; a flame's steady reach: yes, to be looked at again with
  screenshots; a measuring run that takes the screen: "Ask me each time"; offline GPU tests beside the owner's
  games: "Never"; the owner makes the saves at Sagremor's gate and "PT walk", "PT walk town".
- **Full or nothing** (2026-09-26): "Don't waste energy on anything else than 'Full' mode", "drop the GI option for
  good ... Full or nothing". The row is Off / On. Never a hybrid or GI mode, nor options only such a mode needs.
- **No HDRP shading while tracing** (2026-09-28): "not to keep shading of anything in HDRP when PT is on", and "A
  solution to black eyes or weird shadows on hair isn't to exclude it from path tracing". Hair moved into the tracer on
  2026-09-28; the eyes are next. Hair's transparent layer, eyebrows and lashes, transparents and particles, and water's
  own shading are HDRP-shaded too: raise those with the owner before designing them. **Water is the tracer's**
  (2026-10-07, asked whether the tracer should take over the water's own look: "of course. Full Path Tracing means full
  path tracing"). Read that answer for the rest of the list too: what is still HDRP-shaded is a gap to close, not a
  choice to keep (the owner's words are about water; the rest is this file's reading of them: confirm a large design
  with the owner, don't ask whether to do it).
  **Our water need not look like the game's; it must look amazing** (2026-10-12 11:00, of a report line "the beach's
  water still looks nothing like the game's": "that doesn't matter, the game's water doesn't look good in the first
  place. It's fine that ours look different, so long as it looks amazing"). So the game's own picture is NOT the
  yardstick for water: never report "unlike the game's" as a water defect, and never tune the traced water toward
  the game's brightness, colour or foam. What is a defect there is what is wrong by physics or ugly on its own: light
  that is missing where it must arrive (a dark slab of sea bed under clear shallow water), rare bright samples
  (white dots, blobs under a denoiser), noise, a surface without its waves. How it looks is the owner's to judge
  from the traced picture itself. (This file's reading: the same holds wherever the game's own look is a rasterizer's
  shortcut; where the owner has named the game's look as the art to keep, the fog above all, it stays the yardstick.)
- **No fake lights** (2026-09-26): "no fake lights, no fake bounce lights, no fake light volumes". Only lights that
  stand for something that emits light (a flame, candle, lamp, magic) are traced. Fill lights light no surface.
  **A light placed at something that glows is that thing's light** (2026-10-06, asked whether a blue light sitting at a
  glowing crystal counts as the crystal's light: "yes, and there may be more instances of these. Like magic, for
  example, also emits light. Orbs and stuff. Fireflies. I don't know - we may be dropping stuff we don't want to drop
  still!"): glowing crystals, magic, orbs, fireflies and the like, by what is at the light, not only by its name.
  The owner's meaning, said plainly the same night: "make sure we only drop those fake area lights and stuff from the
  game that artists place as fake area lights or fake bounce lights and stuff like that ... But emissive things, like
  crystals in a cave, or magic, things like that, of course they should still emit light and appear in reflections".
  So: drop only artists' fill, fake area and fake bounce lights; everything that glows lights the world and shows in
  reflections. **A traced light has the size of what emits it** (2026-10-06, asked whether flames should get their real
  size for reflections only or shadows too: "whatever is most realistic"): a candle's flame, a torch's, a crystal's
  glowing surface, never the HDRP light radius an artist set for soft shadows, for highlights and shadows alike.
- **Keep the game's fog** (2026-09-27): "fog shouldn't go away just because people activate PT. It's core to the
  game's art style". Fill lights keep lighting the fog (2026-09-28): "these are just as much artist-placed fog volumes
  as they are artist-placed lights... allow the fog-part of them while preventing the light-part" (Fill Light Fog row).
  Never `Add<T>(true)` on an HDRP Volume: it resets every parameter to HDRP's defaults (a source check scans for it).
  **The tracer should light that fog too** (2026-10-06, asked "shadows and light shafts in the mist?": "yes"): today
  HDRP lights it; a feature to design (PT-37), the game's fog itself stays.
  **Anything can light the mist** (2026-10-12 11:06, on the choices page, shown the night by a torch three ways and
  told that outdoors the game's own Fog volume says "directional lights only": picked "Lamps light the mist" and
  wrote "Anything can light the mist - not just the sun and moon."). So every lamp and everything that glows lights
  the traced fog, outdoors too, whatever the game's "directional lights only" says; never a switch or a default that
  takes lamps out of the fog. **And the fog's light scatters in every order** (the same page, the thick fog shown
  with one bounce and with all: picked "All bounces"): the physically full fog is the rule, though it is brighter
  than the game's in thick white fog; "light scattered once only" stays a hidden test switch and nothing more.
  **The lit mist around lamps is a look the owner confirmed in game** (2026-10-12 ~11:40 and 12:00, watching the
  harness's runs of the town and the forest over the stream: "Loved the look of the lit fog you had", then: "Looks
  amazing with the lit fog around the lights, also in the forest scene - so pretty"). A settled look: nothing that
  dims a lamp's glow in the mist is taken without the owner seeing both. Know what was on screen when judging it:
  the town's lamps are bright or nearly off BY THE GAME'S OWN STATE, which depends on what was loaded before in the
  same session (PT-147; `docs/PT-HARNESS.md` "A scene depends on what was loaded before it").
- **A mirror must show the world as the camera sees it** (2026-10-06: "In principle, we need our tracer to be able to
  perfectly render the world in a mirror if a scene has one"). What a ray hits needs the textures, bumps, shine, hair,
  eyes and skin that the same thing has on screen. Today it does not (PT-46, PT-45): never call reflections done, or
  design a surface for the screen alone, without saying what a ray sees of it. **Textures at the game's own size**
  (2026-10-06: "We want way more sharp slots, and we want them at the same res as the res the game renders at"): what
  the tracer reads of a texture is as sharp as what the game draws with, no fixed handful of sharp copies (PT-45).
- **What moves is traced as it moves** (2026-10-06, of a canopy the game sways in the wind and the tracer holds still:
  "Obviously reflections and shadows of moving objects, including cloth, must be accurately represented by our path
  tracer. It's one of the reasons why realtime path tracing is a nice feature - that it is reactive to scene changes.
  But yeah, we really need to make sure we detect cloth correctly. That also includes clothes hanging on a drier and
  stuff"). Swaying cloth, laundry, banners and the like are traced in the pose the game draws them, and every cloth is
  found as cloth.
- **Grass casts shadows, soft and partial** (2026-10-06: "We want grass shadows, but not those horrible looking ones"):
  never the game's own "no shadows from grass", never hard black ones.
- **Follow NVIDIA's and AMD's guides to the letter** (2026-10-11 ~14:50, told that NRD had been handed the bounced
  light alone since round 2, where NVIDIA's guide hands it all the light, demodulated, with real hit distances and the
  shading normal: "Obviously we want to 100% follow NVIDIA's and AMD's implementation guidelines for these
  technologies"; before it: "make sure NRD is set up to perfection, then, I no longer trust anything I look at which
  use NRD"). NRD, RTXDI, DLSS Ray Reconstruction, Shader Execution Reordering, opacity micromaps, RTX Mega Geometry and
  AMD's Ray Regeneration take their inputs, settings and passes as the vendor's documentation and sample set them up;
  read that documentation in full (the SDKs sit in the repo root) before changing an integration. A departure is a
  defect with an entry in `docs/PT-REQUESTS.md` (PT-127 lists the audit), unless the owner rules otherwise. When an
  offline number seems to favour a departure, look for a wrong input of ours first: round 2 kept the direct light from
  NRD because RELAX blurred it, while RELAX was told a hit distance of 1 and the triangle's normal.
- **Denoising** lists None, NRD, Ray Regeneration and Ray Reconstruction, only what the PC runs, starting on the best
  (RR, then RRG, then NRD). "don't suggest the built-in denoiser" (2026-09-27). "Ray Reconstruction is essential for
  good PT, and it IS compatible with DLSS 5" (2026-09-26): never hide or demote RR because RenoDX's DLSS 5 is loaded.
- **Reflection Boost** (default 0.1) is for the world's stone and brick AND for armour and metal (2026-09-28): never
  exclude whole characters. Skin is exempt (a glossy neck mirrored the chin), and grass and leaves. **Metal gets more
  of it than stone and brick** (2026-10-06: "we want metal to reflect more than stone and brick"): the boost's mirror
  share goes to metal alone, everything else only gets a little glossier (it used to double what stone mirrors and
  leave metal as it was).

Settings and players (reasons and the recipe: PROJECT-CONTEXT "Settings design"):
- Take capabilities from what the runtime reports, never from tables. Clamp the value when switching technology.
  Never offer a technology the runtime didn't report. One selector per concept (one Low Latency row).
- Every option is a row in the game's Video tab, and FoA Mod Manager shows exactly the same rows: names, choices,
  order, visibility. Grey out what doesn't apply, with a tooltip that says why.
- A vendor-documented conflict with one safe fix: apply it to the game's own setting and grey that out. No buttons, no
  tips for the player to act on, no frame cap of the mod's own.
- Problems show as an amber alert icon on the row they concern, never as rows named after a mod.
- Each upscaler shows only its own options ("we want the native sharpening for the upscaler of choice": FSR only).
  No FSR version row (AMD's default). STP keeps the game's render-scale slider.
- Player text says "normal version" / "Mono version", never "IL2CPP". Names are "TG <Name>", GUIDs
  `com.wessberg.tg<name>`.
- Never describe a mod, row, log line or doc with the word the source checks ban (e-x-p-e-r-i-m-e-n-t-a-l; see
  `test_no_third_party_wrapper` and `test_testing_limits_disclosed`). Say "prototype" or "in testing".

Releases:
- One version for all mods, matching Nexus ("keep version numbers consistent so they match with nexus"). Bump the
  bridge's version on any bridge change; the bridge's managed API only grows.
- A release zip holds the BepInEx folder, `<mod>-Package-manifest.json` and `vortex_override_instructions.json`
  (Vortex's "Engine Injector" type). No loose docs; `TGFG-PLAYER-INSTALL.txt` never comes back. Ask before adding or
  removing anything in a zip.
- All mods share one D3D12 device on purpose (the owner rejected a second device, 2026-09-23). Don't propose one.

Licenses:
- **RTXDI and NRD sources** (`RTXDI-Library-3.1/`, `NRD-SDK-4.17.3/`, gitignored in the repo root) are **never
  committed, copied or quoted into the repo**. The NVIDIA RTX SDKs License allows object code only. Keep the notice
  "This software contains source code provided by NVIDIA Corporation." DLSS and NGX are for NVIDIA GPUs; NRD runs on any.
- AMD's FidelityFX DLL license forbids reverse engineering: never disassemble those DLLs.
- Kuan-Mi/UnityPathTracing has no license: read it, copy nothing.

## Rules that cost a round when they were missed

- **Settled decisions need in-game evidence.** Before you change a constant or a behaviour, grep its comments,
  `CHANGELOG.md` and `tests/source_checks.py` for why it is so. A choice the owner confirmed in game stays until the
  owner reopens it; a harness number is not enough. (TemporalCap 1 -> 20 on 2026-09-26: "It scares me a little you
  don't remember or can't see it in our notes".) Look for a new symptom's cause in what changed since the last good build.
- **Read the F8 capture yourself** (`BepInEx/TGPathTracing/Captures/<time>/`: report.txt and the JPGs) instead of
  asking the owner to describe the picture. The verdict on how it looks stays the owner's.
- **Anything that removes light from the image moves the exposure.** Read the exposure and gain log line before
  calling such a change done.
- **Hash the deploy.** `build/native`, `artifacts/Native` and the game folder must match. Two matching stale copies look
  like a good deploy. The game folder is Vortex-managed: replace files, never write through a symlink.
- **Build, install, then test.** ctest's GPU tests load the DLLs from `artifacts/Native`, which only `cmake --install`
  updates.
- **Never start the game while another program uses the GPU** (owner, 2026-10-06: "you spawned the game while
  Starfield was running. Whenever you want to test, check if another process is using the GPU, and if it is, monitor
  it, and launch once it settles ... any process ... Make it a general thing"). The owner plays other games on this
  PC, streamed with Sunshine. Start the game only through `tools\harness\Run-PtHarness.ps1`: it watches every
  process's 3D and compute use (video encoding, as by Sunshine, does not count: the owner's choice) and starts the
  game once the GPU has been free for 5 s ("90 quiet seconds are too long. Just 5 quiet seconds"; `-GpuCheck` only
  says who uses it). Never start the exe or the Steam URL
  by hand. **A game of the owner's that is OPEN holds every start back, whatever it uses just then** (2026-10-09
  15:44: a run began beside Starfield, which sat nearly idle at 7.5 % and 1.3 GB, under the guard's old limits of
  10 % and 2 GB; the owner then played, and the run went on beside it for 8 minutes). Since then the harness
  refuses while a listed game is open (`$ownerGames` in `Run-PtHarness.ps1`: add a name when the owner plays
  something else), counts a program as using the GPU from 4 % or 800 MB, and ENDS A RUN BY ITSELF, closing our game,
  when a listed game opens or another program holds the card for two readings in a row. Frame times of a run with
  "Other programs used the GPU" in its last lines mean nothing. (Sunshine alone no longer ENDS a run since 2026-10-11
  22:45: it copies every frame our own game shows, 4 to 5 % of the 3D engines, and twice over the limit it ended a
  look-only run; the owner that day: "Sunshine practically always runs". It is still written down and the report still
  names it.) **The stream stays on; speed is measured with it** (owner, 2026-10-12 10:55, asked for ten minutes
  without the stream so that the new build's speed could be measured: "I would prefer if I can stay on the
  SUnshine/moonlight stream"). Never ask the owner to stop the stream. This file's reading of it: the owner plays over
  the stream, so the speed WITH the stream is the speed that counts. A speed number is taken with the stream on and
  says so; a decision between two builds or two settings is an A/B/A within ONE run (the stream's load is then the
  same on every step; a stream slows the two reordering passes by 2 to 3 ms each in a forest and the reflections by
  13 ms at a beach, so a number with the stream is never held against one from a quiet card, and the baselines of
  before 2026-10-12 08:26, taken without it, are not the yardstick any more). Another program than the stream on the
  card still spoils a run's speed, as before. A run that is only for LOOKING need not wait for the workers' offline GPU tests (owner, 2026-10-08 00:20,
  with a comparison queued behind eight workers: "Just run it now, we'll live with the poor framerate"):
  `Run-PtHarness.ps1 ... -AllowGpuContention` starts beside them; its frame rates and GPU times then mean nothing. It
  still never starts beside another game or program of the owner's. Since 2026-10-08 it can also show NOTHING: with
  three tests on the card Windows cut the game's video memory budget from 14.85 to 6.3 GB, textures could not be
  opened and tracing paused itself (the owner watched "Path tracing paused" and closed the game). When the owner is
  waiting for a picture, make the card free first (let the tests end; start none), then run.
- **Frame times count only from stretches where the game was in front; the game is FORCED to the front only in runs
  that measure frame times** (owner, 2026-10-10 15:40: "let's make sure the game always has the foreground and only
  respect our frametimes when we know it was"; narrowed 2026-10-11 10:35: "Please only force the game to the
  foreground in runs where you want to exactly measure frametimes"). `Run-PtHarness.ps1` checks every two seconds
  which window has the foreground and writes the stretches the game was not in front into `gpu.json` (`notInFront`;
  `analyze.py` lists them as a problem); with `-Measure` it also brings the game back at once (restored, in front).
  A run for looking at a picture goes without `-Measure` and leaves the owner's screen alone; a run whose frame rates
  will be quoted goes with it. Never quote a frame rate or a stage time from a step in a not-in-front stretch, nor from
  a run before 2026-10-10 without saying that the window's state is not known. A `-Measure` run takes the owner's
  screen while it lasts: say so before one starts while the owner is at the PC.
  And a run whose report says "Other programs used the GPU during the run" is not quoted for speed either, whatever
  the program (2026-10-10: runs with the owner's streaming at 4 to 5 % of the 3D engines read the beach 35 to 50 %
  slower than a clean run of the same build; the coordinator had quoted them). Not even step against step: another
  program can slow one variant and not another (round 19: Sunshine at 4 % slowed the reflections' sorted pass by 18 ms
  at the beach and the unsorted one not at all, and two rounds took "no hint wins at the water" from such runs).
- **The offline TGPT harness (ctest) never runs Ray Reconstruction.** A change on the RR path goes through
  `tgup_gpu_smoke_with_pt`, then the in-game harness (below), which runs RR and NRD in the real game.
- **A passing test must be mutation-checked**: break the thing on purpose and watch the test fail. Grade noise
  against an **undenoised** reference.
- **Judge the picture without a denoiser first** (owner, 2026-10-07: "let's make sure we test less with Ray
  Reconstruction and more without denoising so we better identify the problem areas, so we can rely less on Ray
  Reconstruction to clean it up and serve it a more truthful base frame"). The harness's default recipe takes the
  undenoised frames first (`2-traced-none`, 16 frames; their mean in a frozen world is what the picture converges to),
  then Ray Reconstruction and NRD. ReSTIR's own reuse of samples stays on for this ("it's fine with the reuse
  internally in ReSTIR and stuff - I only mean for Ray Reconstruction, because it does such a good job of hiding
  stuff"). A defect seen with Denoising "None" is the tracer's; never tune a denoiser to hide it.
  **The order of judging, and Ray Reconstruction last** (owner, 2026-10-07 23:10, after a session of the merged build:
  "I really think we need to make this look pretty good before testing with ray reconstruction. The NRD denoiser should
  look relatively noise-free, and the denoiser-less presentation should reveal the problem areas, like spots with a lot
  of shadow flickering, the weird rectangle gas spots over candle lights and stuff"). So: (1) no denoiser shows where
  the problems are (`tools/harness/flicker.py` cuts out what changes most between frames): fix those in the tracer;
  (2) NRD is the denoiser the picture is judged with until then, and it must look nearly free of noise and must not
  boil (23:20: "That also means we need to catch boiling with NRD and prevent that too"; `flicker.py --boil` measures
  how far 16-pixel blocks wander over the step's frames, against the game's own picture); (3) Ray
  Reconstruction is tested only when 1 and 2 look good. The harness's default recipes take no denoiser, then NRD;
  Ray Reconstruction is in the recipe `with-rr`, for when the owner says it is time.
- **After a scripted edit, check every hunk landed.** A script with two blocks for one file dropped the first, and
  Spatial Samples above 8 never reached `tracer.cpp` for a day.
- **BepInEx config section and key names may not contain `= \n \t \ " ' [ ]`.** One apostrophe killed TGFG with no log
  line (source check `test_config_keys_are_valid_for_bepinex`).
- **Never touch a game Setting type with a localizing static constructor (UpScaling) at plugin load.** It hung the
  Mono game and broke loading under IL2CPP. Get the game's own instance first.
- **Hooks on global functions run on driver threads with tiny stacks**: no large stack arrays (a 64 KB one crashed).
- **One signalling queue per fence** (a fence both D3D11 and D3D12 signalled ran backwards).
- **Several workers compiling at once run the PC out of memory, and the game then starts crippled** (2026-10-09:
  five workers, 16 dxc at up to 6 GB each plus three GPU tests: 47 of 62 GB; the game's Direct3D 12 device failed
  with 0x8007000E, the bridge noted it and every later start used Windows' older runtime: no DirectX Raytracing
  1.2, a frame of 2 s, PT-96). `tools\Build-PtShaders.ps1` now holds a limit for the whole PC (`-MachineLimit`,
  `-FreeGb`): every worker's copy must carry that version. Before a game run, look at free memory (24 GB) and at
  the tracer's log line "DirectX Raytracing 1.2 (opacity micromaps) available"; if it says "no DirectX
  Raytracing 1.2", delete `BepInEx\TGGraphicsBridge\agility-extension-failed.txt` and start again.
- **Code that is switched off still costs frame time in the big passes** (2026-10-09: PT-85's lamp trial, both
  its settings 0, doubled the tavern's bounced light by the tracer's own logs; and after a day of merged trial
  switches the reflections' pass ran 6 ms slower with every one of them off, PT-99). The driver's code for a
  pass grows with what is compiled into it, run or not. So: a trial that adds more than a few lines to
  `pt_gi`, `pt_specular`, `pt_prepare` or the walk goes behind a COMPILE switch (as CMake's `TGPT_LAMP_AROUND`),
  or lives in a worker's copy until the game has to answer; and once the game has answered, the trial comes out
  or becomes the rule THE SAME DAY. Never read a speed number off a build that carries undecided trials, nor
  off a run with another program on the card.
- **The Bash tool eats backslashes**: write code with backslashes through Write/Edit or a script file. Sources are
  CRLF in the working tree: Git Bash `sed -i` rewrites them as LF; count bytes with PowerShell.

## Build, deploy, test

The SDKs sit gitignored in the repo root (`README.md` "Building" lists them). Close the game before deploying.

- **Native**, in the VS 2022 x64 environment:
  `"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"`.
  `build/native` is an NMake Release tree (its configure line: PROJECT-CONTEXT "Build recipes"). Run
  `cmake --build build\native`, then `cmake --install build\native --prefix artifacts\Native --component TGNative`,
  then `ctest --test-dir build\native -C Release --output-on-failure` (17 tests; `-R tgpt_gpu_test` for the tracer,
  its PNGs in `build/native/tgpt-images`). After a manual build, run `tools\Write-NativeManifest.ps1` before
  packaging. The scripted route: `Build-Native.ps1` (README).
  **Before `cmake --build`, run `tools\Build-PtShaders.ps1 [-BuildDir <tree>]`**: it compiles the tracer's shader
  passes side by side (NMake does one at a time; on 2026-10-08 the passes had grown until a build took about an hour,
  the reflections' pass alone 10 minutes of one core; side by side: 10.5 minutes for all 43). It runs the commands CMake
  itself wrote, only for headers that are out of date. Never while an NMake build of the same tree is running, and
  never merge into the tree while either runs (`merge_copy.py` always writes into the main tree).
- **Source checks:** `python tests/source_checks.py` (76 checks). Run them before any hand-off.
- **The in-game harness** (owner, 2026-10-06; all of it: `docs/PT-HARNESS.md`): `.\tools\harness\Run-PtHarness.ps1`
  starts the game, loads the saves of `tests/ingame/scenarios.json` by name, freezes the world, and for each takes
  frames of the game's own picture, the traced one with Ray Reconstruction and with NRD, and the game's again after
  tracing was turned off; `tools/harness/analyze.py` writes pictures and `report.md` into `artifacts/harness/<run>/`.
  It tests whatever build is in the game folder. **Test runs are of path tracing alone** (owner, 2026-10-08 12:50:
  "Reshade was enabled, Ive turned it off again now", "Also I believe FG has been turned off", "And make sure to also
  keep NR turned off during testing"): no ReShade (the harness refuses to start while it is in the game folder; the
  owner turns it on and off, never an agent), Frame Generation Off (the harness sets it for the run), TG Neural
  Rendering Off (it starts Off; no recipe of the path tracing runs may set a step's `neural`). Read its
  `overview.jpg` and `atlas.jpg` yourself. `-Inventory` lists
  the saves with their pictures; `-Scenario a,b` runs some. Nothing is saved during a run, and the owner's configs
  come back after it. Its analysis has checks of its own: `python tests/harness_analyze_test.py`.
  **When the disk fills up:** `.\tools\harness\Clear-PtHarnessRuns.ps1` (owner, 2026-10-08: "Our artifacts/harness
  folder is +300GB large right now ... clean up older runs so we reclaim some disk space"). Without `-Apply` it only
  says what it would free. The newest 10 runs stay whole; older ones lose their raw frames and keep their pictures,
  reports and logs; `-Keep 'nr-*'` leaves runs alone; it refuses while a harness run is going (`docs/PT-HARNESS.md`).
  It also deletes the old packages of `artifacts/Packages` (owner, 2026-10-08: "Add it to the script. Sounds like it
  is completely dead weight"), all but the newest of each mod, version and game version; never `artifacts/Nexus`.
  And the workers' old builds in `%TEMP%\tgb` (owner, 2026-10-12, with 8 GB left on the disk and 145 GB of them
  there: "Yes, delete all the old ones, and make sure our Clear-PtHarnessRuns.ps1 tool also does that"): a folder
  or file there in which nothing was written for 2 days goes (`-WorkerDays`); never the tree copies in
  `%TEMP%\tg-agents\wt`. This file's reading, not the owner's words: the tool is the owner's standing word for
  those three folders, so an agent looks at the free space before a run or a worker's start, runs the tool with
  `-Apply` when the disk runs low, and says afterwards what it freed. Windows' page file (114 GB that day, grown
  while several trees compiled at once) stays as it is (owner: "Leave it as it is").
- **Managed, package, deploy:** `.\Build.ps1 -Runtime Mono -Deploy` (`-Mod TGPathTracing` for one mod; TGPT is Mono
  only; `-Runtime IL2CPP` for the normal version). It backs up to `BepInEx/<Mod>/Backups` and writes the Nexus zips
  to `artifacts/Nexus`.
- **When to deploy:** after each round of fixes the owner will test ("Can you please deploy the files manually to the
  game dir every time you have a fresh build?", 2026-09-26). Not while they edit the tree in parallel ("Don't build and
  deploy, no, we're currently iterating on the files concurrently", 2026-09-27): then only on request, and compile
  checks go to `bin/` and a scratch install prefix, never `artifacts/`. After a deploy, hash-check, and tell them
  Vortex will call the files changed and must not revert them.
- **While the owner iterates on a look in game, deploy without the test suites** ("skip all those tests in-between while
  we iterate... when I think it looks good, then you validate and run tests", 2026-09-28): build, install, deploy,
  hash-check. When the owner says it looks good: ctest, source checks, mutation checks, docs.
- **While the migration runs, test what catches defects and nothing more** (owner, 2026-10-07 22:20, told four merges
  with the whole GPU test and mutants after each would take another hour and a half: "That's a long time, man. Do we
  really need to run so many tests right now, while we're still not even sure if it will look right?"; then, told the
  tests had caught real defects that day (a device removed by a misaligned buffer, a job breaking another's glass check,
  optics checked against physics): "Then they may add value", and: "Yeah, just run tests at the end of the merge,
  instead of several times"). So for workers and the coordinator alike: a job builds
  and runs its own new checks (minutes); the whole GPU test runs once at the end of a job or a merge chain, its FAIL
  lines written down; never the whole suite after every step, and no mutation checks until the owner says a thing
  looks right. Then: the whole suite, the mutants, the docs. Briefs say so.
- **One thing at a time, in a short loop; no hour-long runs** (owner, 2026-10-08 14:05, after a one-hour run of every
  scene and with four whole GPU tests of the workers' on the card: "I'd like for you to iterate more precisely on
  individual things at a time instead of running these hour-long test runs? Wasteful to run a ton of [tests] for stuff
  that doesn't look right in-game anyway"; minutes before: "Can you stop using the GPU so much for tests in the
  background right now? I need some more GPU free"). So: take ONE defect, change it, build, deploy, run the one scene
  that shows it (`Run-PtHarness.ps1 -Scenario <one>`: a few minutes), look at the crop, show the owner; then the next.
  The whole GPU test, a run of every scene and whole runs of several builds side by side wait until the thing looks
  right in the game, or the owner asks (this narrows the rule above: not even once at the end of a merge chain while
  the picture is wrong). When the owner asks for the card, every background GPU test stops at once (stop the
  runner scripts first: they start the next test) and none starts until the owner says the card is free.
- **The game is the first test, not the last** (owner, 2026-10-08 23:20: "I just generally want our iteration time
  to be faster, with less waiting on tests and stuff like that, because often we find out in-game that the direction
  of a fix was wrong"; an hour before: "Man, it takes forever". That day a worker spent two hours on a fix its own
  fixture liked and the game did not). So, for a fix whose direction is not proven yet: (1) the SMALLEST change that
  could show the direction, behind a hidden switch where it can be (an `Advanced` setting or an option bit), so that
  ONE run of one scene shows it on and off (recipes `shift`, `fireflies`, `room-light` in
  `tests/ingame/scenarios.json` are the pattern; a plugin-only switch needs no shader compile); (2) that scene in
  the game at once; (3) only when the game says the direction is right: the offline check, the clean-up, the docs.
  A worker hands the coordinator a trial build for the game within its first half hour, or says why it cannot, and
  builds no fixture before the game has answered. Runs stay short: the one scene, the fewest steps and frames that
  answer the question. Counts that decide something take 16 frames (a 4-frame count misled on 2026-10-08).
- **The full test suite never runs in a subagent** (owner, 2026-10-08 18:00: "full test suite never in
  subagents"; the same day, 17:55, the owner allowed workers' small checks again). A worker of any kind (Claude
  subagent, Codex worker) runs the one small check of its own job (its own fixture or `..._ONLY` mode, minutes) and
  nothing more: never the whole `tgpt_gpu_test`, never `ctest` as a whole, never a whole run "once at the end" or
  of a base to compare against. The whole suite is the coordinator's, in the main tree, once a thing looks right
  in the game or the owner asks. Every brief says so; a brief that asks a worker for a whole run is wrong.
- **Commit and push only when the owner asks.**
- **In game:** `C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA` (Mono, Vortex-managed). Insert turns
  path tracing on and off, Shift+Insert cycles debug views, F8 takes the TGPT capture (hold still about 22 s). The
  owner's MacBook keyboard has no Insert key. F9 is the game's QuickLoad. The game writes no Unity Player.log: read
  `BepInEx/LogOutput.log` and each mod's native log (README "Diagnostics", PROJECT-CONTEXT 9.10).
- The in-game test plan per mod: `docs/TESTING.md`.

## Where things live

| File | Holds |
|---|---|
| `BACKLOG.md` | the goal, the rulings, the open defects, the queue (under 12 KB) |
| `docs/PROJECT-CONTEXT.md` | the engine picture, each mod's learnings and recipes, DEAD ENDS, method traps (~700 lines at most) |
| `docs/PROJECT-CONTEXT-MODS.md` | released mods: Frame Generation, Better Upscaling, normal-version interop and player reports (sections 5-8) |
| `ROUNDS.md` + `docs/rounds/round-NN.md` | one row per round; the narratives and starter prompts |
| `README.md` | what each mod does, its rows, building, diagnostics |
| `CHANGELOG.md` | every change with its reason and harness numbers: the "why" behind a setting |
| `docs/ARCHITECTURE.md` | native ownership, the GPU handoff, frame generation and upscaling internals |
| `docs/TESTING.md` | offline checks and the in-game test sequence for each mod |
| `docs/SOURCES.md` | primary sources, SDK versions and their license terms |
| `docs/AUTOLOAD-INTEGRATION.md` | the preloader election and the installed layout |
| `docs/FSR-FLICKER-INVESTIGATION.md` | the resolved HUD and hand flicker; read it before touching FG inputs |
| `docs/PT-DETAILS.md` | TG Path Tracing's recipes and decodes behind PROJECT-CONTEXT section 9: the game's hair and eye shaders, hair's highlights, glass and water caustics, measurements |
| `docs/PT-REQUESTS.md` | path tracing: every request of the owner's and every defect found, one entry each with its state (BACKLOG's Open list carries the same IDs) |
| `docs/PT-HARNESS.md` | the in-game harness: how to run it, what each scenario's save shows, what the numbers mean, the performance target |
| `docs/AGENT-WORKERS.md`, `tools/agents/` | running several workers at once (Claude subagents, GPT-6-astra workers through Codex): tree copies, the launcher, permissions, the brief template, merging back; example briefs in `docs/agents/briefs/` |
| `tests/ingame/scenarios.json`, `targets.json` | the scenarios (save name, what it tests, the screen region) and what a run is judged against |
| `docs/nexus.md`, `docs/nexus-upscaling.md` | Nexus page drafts (BBCode) |
| `tests/source_checks.py` | the textual rules the build enforces |

PROJECT-CONTEXT carries the ~700-line budget: condense or archive in the same edit that adds. `CLAUDE.md` only imports
this file (Codex reads `AGENTS.md` itself). Never put a rule in `CLAUDE.md`.

## Subagents and reviews

- **Subagents only when the owner asks** (2026-09-28; earlier that day: "don't parallelize into subagents this time").
  For the path tracing work the owner allowed up to 2 at a time on 2026-10-06 ("You can parallelize these things with
  up to 2 subagents at a time"), then asked for more ("parallelize some of the requests across a few subagents to get
  some more work done", 17:00): three code agents, one request each, each in its own copy of the tree (PROJECT-CONTEXT
  13). Only one game runs at a time: an agent never starts the game or deploys; it runs GPU tests only while the game is
  closed and `Run-PtHarness.ps1 -GpuCheck` says the GPU is free, and writes its findings to a file. That check lets up
  to 4 of the tracer's offline GPU tests run side by side (owner, 2026-10-07: "Would be nice if we could speed up the
  testing of those background agents so they won't have to do full regression test suites one by one"; one test keeps
  one CPU thread busy and the GPU at 13 to 36 %): never add a rule of your own that waits for other tests to end, and
  run a mutant's test only as far as the check it is about (PROJECT-CONTEXT 13). The offline tests also run while
  the owner plays another game (2026-10-07, asked "While you play Starfield, may the agents' offline tests run? They
  never start our game, but they would take GPU from Starfield": "Yes, up to 4"). Starting OUR game stays forbidden
  while any other program uses the GPU.
  **NOT ANY MORE, SINCE 2026-10-12 00:20: no offline GPU test while a game of the owner's is open.** That night a
  worker's offline test hung the GPU beside Starfield (one run 600 times slower than every other from its first
  frame), Windows reset the driver (LiveKernelEvent 141, nvlddmkm 153) and the owner's game went down; the owner:
  "Starfield was playing while you were doing your test, and the GPU couldnt handle it". `Run-PtHarness.ps1 -GpuCheck`
  now exits 1 while a listed game is open (`$ownerGames`): every worker obeys it before EVERY test start and never
  takes its own wait for it out (the worker had). Compiling is not a GPU test. The owner may allow tests beside a
  game again; until then this holds, and every brief says it. A test that is ALREADY RUNNING when the game opens is
  ended too (04:51 that night the owner opened Starfield a minute after three workers' tests had started; one ran on
  beside it): the coordinator keeps `tools\harness\Stop-TestsBesideGame.ps1` running in the background while workers
  may test (it ends every `tgpt_gpu_test` the moment a listed game opens), and a worker's own script does the same.
  When they ask, use PROJECT-CONTEXT "Parallel agents": scratch copies, preassigned option and material bits, merges
  against a base snapshot. Brief each agent to follow this file and to write its findings to a tracked file. GPU
  timings mean nothing while agents run GPU tests.
- **The full test suite never runs in a subagent** (owner, 2026-10-08: "full test suite never in subagents"): its
  own small check only; the rule is under "Build, deploy, test".
- **At most 3 agents at a time, of any kind** (owner, 2026-10-07, when four and five ran: "Max 3 agents at a time as a
  rule. Don't abort any of the current ones though, but remember it in the future"). Count research and finishing
  agents too; a new one starts only when fewer than 3 run.
  **Claude subagents: up to 3 at a time, Opus 5.5 at xhigh effort** (owner, 2026-10-11 ~21:40, after a stretch of one
  Claude subagent at a time: "you can up to 3 Opus 5.5 xhigh subagents at a time again, the old restriction has been
  lifted"). Start them with the Agent tool's model `opus` and effort `xhigh`. Codex workers run beside them as below.
  Claude's weekly limit was hit again that evening (19:15, one agent running; the owner bought usage): every
  subagent writes its report early, and a job that can wait for Codex goes to Codex.
- **Never send a follow-up job to an agent that has already done a long one** (owner, 2026-10-06: "When you re-enable
  an old agent that was already at almost a million tokens spent, that burns really fast through my tokens", "their
  context window is likely already almost fully spent"). Each of its calls pays for its whole history again. Start a
  fresh agent with a short brief instead: the goal, the copy to work in, the last agent's report
  (`docs/agents/<name>.md`) and the evidence folders. So every agent writes its report early and keeps it current,
  never reads the big files whole (`tests/pathtracing_gpu_test.cpp`, `pt_common.hlsli`, `tests/source_checks.py`:
  grep, then read a range), waits for the GPU with one background script instead of polling, and stops when its one
  job is done.
- **GPT-6-astra through Codex may do jobs, not only reviews** (owner, 2026-10-07 18:00: "You can get gpt-6-astra to work
  on some of the other tasks in parallel via codex, so you don't waste claude tokens"). A Codex worker is briefed like
  any agent (one job, its own copy of the tree, a report in `docs/agents/`, the same rules: never the game, never a
  deploy, never a commit). **The how, for every kind of worker, is `docs/AGENT-WORKERS.md`; the tools are in
  `tools/agents/`** (owner, 18:15: "save it to your harness, so you know how to spawn codex workers (not reviewers, -
  workers) in the future, when we want to parallelize even more"): `New-TreeCopies.ps1` makes the copies,
  `Start-CodexWorker.ps1 -Name <copy> -Brief <file> -Base <base>` starts a worker (`codex exec -m gpt-6-astra -s
  danger-full-access -c model_reasoning_effort=xhigh -C <copy> --skip-git-repo-check -o last.md -`; `-ReadOnly`,
  sandbox `read-only`, for a design or research job whose last message is the document), `merge_copy.py` merges a
  copy back; briefs to copy from: `docs/agents/briefs/`.
  Always at xhigh reasoning (owner, 18:12: "I want them at xhigh reasoning"). `codex exec` never asks for permission
  (approval: never): what a sandbox forbids just fails. Code workers run WITHOUT a sandbox (the owner's choice, 18:12,
  "Full access", after the `workspace-write` sandbox refused to run CMake from the user's profile, so no native build and
  no GPU test), as the Claude agents do: the launcher's prompt carries the rules the sandbox would have enforced (write
  only in the copy and its build folders, never the main tree, the game folder, the game, a deploy, a commit).
  Prefer it for jobs that would otherwise wait for a Claude slot, and for merges onto a newer tree. This file's reading,
  not the owner's words: the cap of 3 was set for Claude's tokens, so at most 3 Claude agents, with Codex workers beside
  them (owner, 2026-10-07 23:40, with five Codex workers running and no Claude agent: "So why don't you spin up some
  claude subagents so we can get some more work done? It can't always be astra": three Claude agents were added, eight
  workers in all that night); the 4 offline GPU tests at once hold for all workers together. **"A few" Codex
  workers means two** (owner, 2026-10-09 09:05, offered "a few gpt-6-astra agents" and seeing three started: "Maybe
  3x codex is a bit much, I said a few, a few means two"): start the number the owner names, and two when the
  word is "a few". Claude's weekly limit was hit on 2026-10-07
  20:15 with three Claude agents running: long jobs go to Codex workers first. Test a worker's result as any agent's.
  **Codex can be out too** (owner, 2026-10-09 23:47: "codex is out of tokens I'm afraid"; its own message: no use
  until 2026-10-16 09:04). A Codex worker that ends within seconds has hit that limit: read the last lines of
  `<Root>\codex\<name>\stdout.log` before anything else. Until that date every worker is a Claude agent, three at
  most, and Claude's own limit was reached on 2026-10-09 20:28 with three running: keep briefs narrow, and tell a
  worker to write "State when stopped" into its report the moment a limit shows.
- **Reviews, when the owner asks for one:** GPT-6-astra through Codex (`codex exec -m gpt-6-astra -s read-only -c
  model_reasoning_effort=xhigh [-i image] -o review.md - < prompt.md`), or Claude Code's `pt-max-reviewer` agent (Opus,
  effort max, read-only). Reviews have found real bugs (a fence that ran backwards; dark hair and eyes). Test a finding
  before you accept it.
- A caustics research agent, started 2026-09-28, writes `docs/CAUSTICS-RESEARCH.md`.
