# campfix: the camp in the forest at 3 frames a second (PT-131)

Worker: Claude (Opus), started 2026-10-12 02:15, fresh, after campspeed (its reading: `docs/agents/campspeed.md`).
Copy `%TEMP%\tg-agents\wt\campspeed`, base `base20f`. Brief: `docs/agents/briefs/brief-campfix.md`.
Build folder: `%TEMP%\tgb\campfix-build` (a build for players: no `TGPT_SPEED_MEASURE`). Scripts: `%TEMP%\tgb\campfix-scripts`.

## In short (kept current)

- READ THIS FIRST (found 08:15): THE OFFLINE CHECK RAN AND THE MEAN MOVED. With the rule on, the sun's share of the
  fixture's picture is 0.6 to 1.1 % ABOVE the reference at a sun 57 degrees up, and the picture's pattern changes
  (one band of it 1.8 % brighter). THE CAUSE IS MY LOW-SUN CORRECTION ("each corner its own facet's sun"), NOT the
  coarser cell: the same coarse cells summed the plain way (campspeed's trial, as it was before I touched it) are
  within 0.05 % of the reference, whole picture and band by band. The correction was built on two CPU reckonings
  that had steeper waves and no sideways move of the water; the fixture's sea has no facet turned from the sun
  even at 8 degrees, and there the GPU finds the plain sum right (0.2 to 1.4 % off in two runs: that sun's noise).
  PUT RIGHT IN THIS COPY'S SOURCES (made ready 08:25; the coordinator's word, 08:30: "(a). Make the plain sum the
  rule in YOUR COPY now, and keep it small"): the three uses of `ownSun` are out of `pt_water_sun.hlsli`, a coarser cell
  is summed as every other, which is campspeed's trial as it was. Same cells, same speed. MAIN STILL HOLDS THE RULE
  WITH THE CORRECTION (merged at about 05:50, before this was known) until the coordinator merges this again; until
  then `Speed trial` 64 turns the whole rule off. The numbers: "The offline check".
  BUILT AND RUN OFFLINE (08:51 built, DLL 20428DB08F3F...; 09:31 "PASS sea checks"): with the plain sum the sun's
  share is within 0.16 % of the rule off at 57 degrees at every ray width, every band of the picture within 0.5 %;
  at 8 degrees within 1.9 %, which is that sun's noise. 34.3 ms to 11.1 in the fixture. NOT SEEN IN THE GAME: no
  game run has had this build ("The offline check, again").

- THE "CAMP" IS THE HOLLOW UNDER THE LAND (found 03:05 by reading the harness; SEEN at 03:24, run `20261012-032137`,
  `forest\3-traced-150-m-ahead\frame.jpg` and `2-game-150-m-ahead\frame.jpg`: the sea from under the land, the
  rocks' undersides overhead, in the tracer's picture and in the game's own).
  The harness's walk moved the camera alone, level, in a straight line: no ground under it, nothing stopped it. A
  step's picture (`frame.jpg`) is taken where the walk STARTS. The slow frames were 150 m further, where nobody had
  looked: the ground rises 30 m ahead of the save, the camera flew into it, and from under a one-sided ground the
  camera sees the sea that runs under the whole land. 40 % of the screen were the sea's own pixels there.
- So: NO LEAK in the tracer's rays, and no place a player can stand in at 3 frames a second. PT-131's numbers are of
  a view from under the ground. (The coordinator has since made the walk keep the camera over the ground.)
- What is real in it: water that is really in view, far off, pays the same price a pixel. THE FIX IS ONE RULE of the
  sun's patch made at a point: THE CELL BY THE RAY'S WIDTH, with a correction for a low sun. In every build, one
  hidden switch (`Speed trial` 64 turns it off).
- IN THE GAME (the coordinator's run `20261012-041726`, the world running, no denoiser, a clean card): the beach
  (`water-day`) 121 ms to 73 ms of the tracer, a frame 141 to 92 ms (7.1 to 10.8 frames a second); the evening beach
  120 to 66 ms; the tavern (no water) 35.8 ms against main's 36.0: no cost where there is no water. THE PICTURE,
  the world frozen (run `20261012-044312`, 64 frames a step): the luma with the rule on equals the luma with it
  off at both beaches (56.3 and 56.3; 80.0 and 80.0), flicker and boil the same.
- MERGED INTO MAIN AND BUILT THERE (the coordinator, about 06:00: main's DLL 4EA43DDDBC19, 57 passes, source checks
  119 OK). The merged sources in the game (run `20261012-055244`, `water-day@sun-cell`): the rule on 73.3 and
  71.5 ms of the tracer (11.2 and 11.4 frames a second), off (64) 116.4 ms (7.6); the tavern 35.6 ms. So the
  sources without the shadow first do what the handed DLL did with it switched off.
- A SECOND RULE, "THE SHADOW FIRST", WAS BUILT, MEASURED AND TAKEN OUT of the sources the same day: it gained nothing
  in those two views (72.6 ms with it off, 75.5 and 71.2 with it on). The handed DLL (7BB5795C...) still holds it
  (`Speed trial` 512 turns it off there); the copy's sources do not. Its design and where its lines are kept: below.
- campspeed's candidate (the cell) was checked ON THE CPU and found 11 to 18 % too dark at a sun 8 degrees up; I
  added a correction for that. THE GPU HAS SINCE SAID THE OPPOSITE FOR THE FIXTURE'S SEA (the first point above):
  campspeed's sum was right there and my correction is what moves the mean. What the reckonings do show still
  holds for a steep sea under a very low sun (a triangle with a corner turned from the sun is left out), and is
  entered as a gap, not cured ("The offline check", "What is left").
- For the merge: which of campspeed's measuring code stays and which goes, and what comes out with the switch 64:
  the section "For the merge".

## Where the paths come from: not a leak, a camera under the ground

### What was asked (the coordinator, 02:20)

A million paths a frame enter the water's path at the camp; no water is in the picture; do the rays reach the sea
through the ground (a leak) or does the land open to it?

### The answer

Neither. The picture is not of the place.

1. THE WALK HAS NO GROUND. `src/TGPathTracing.Harness/CameraSlide.cs` (main tree): the walk sets
   `camera.position = home + forwardDirection * (walkDistance * progress)`, `forwardDirection` the view's direction
   laid on the level plane. The camera alone moves (the hero stays), at its starting height, straight on, through
   whatever is there.
2. THE PICTURE IS TAKEN AT THE WALK'S START. `Runner.cs` `Burst`: the walk begins with the frames (`BeginCycle`), and
   the two frames of a step are its first two. Run `20261012-021107`, `forest\2-everything\step.json`: both frames at
   x -1354.86, y 58.15, z -3019.09: the save's own place. The walk's end is 150 m on at yaw 46.6 degrees:
   x -1245.8, z -2916.1 (the "camera about -1246, 58, -2916" of PT-131, and where the tracer's log has its water
   sample at the hold: "at x -1247.63 z -2921.20"). Every "camp" picture so far (`2-camp-as-it-is`, `2-plain-walk`,
   `2-everything`) is the save's view: palisades, torches, a pine. None shows the hold.
3. THE GROUND RISES AHEAD. In the save's view the camera looks level (pitch 0.5 degrees), so eye height is the
   picture's middle row. The grassy rise in the middle of the picture stands ABOVE that row 25 to 35 m ahead, and the
   rock under the column and the arch behind it. A level flight at eye height goes into it.
4. THE COUNTERS SAY WHEN (the same run, a line a second while the camera flies at 10 m/s):

   | into the walk | water paths a frame, bounced light / reflections | their starts from the camera | points the sun reached |
   |---|---|---|---|
   | 0 to 2 s (the save's view) | 330 / 350 | 850 m (the open sea, far off) | 105 / 130 of 430 / 460 (a quarter) |
   | 3 s (30 m) | 275,000 / 385,000 | 35 / 41 m | 3 / 8 |
   | 4 s | 436,000 / 550,000 | 36 / 38 m | 0 / 4 |
   | 6 s (60 m) | 9,000 / 12,800 | 33 m | 0 / 0 |
   | 7 to 14 s | 128,000 to 333,000 / 456,000 to 862,000 | 38 to 59 / 51 to 111 m | 0 to 800 / 100 to 30,000 |
   | the hold (150 m) | 216,400 / 824,800 | 59 / 111 m | 830 / 30,000 of 342,000 / 1,210,000 |

   From hundreds to hundreds of thousands within one second, 30 m into the walk, and the sun reaches none of them:
   that is the camera going under the ground, not a forest opening to a coast.
5. AT THE HOLD THE 825,000 "REFLECTION" PATHS ARE THE SEA'S OWN PIXELS, the camera's own rays:
   - `pt_specular` gives a water pixel its light by starting the water's path where the camera's ray meets the
     surface (`trace_water`, then `shade_seen`). The counters count that as a water path, one a pixel, not "from a
     point". (825 thousand pixels, 825 thousand paths, 0 "from a point", 0 glass walks.)
   - Their starts are where a camera 23.4 m over a level sea sees it: 23.4 m / sin(angle under the horizon). Over
     the lower half of the screen that is 105 m on average (measured 111), and none nearer than 47 m.
   - "The path's ray there 10 m wide" is no reflection's lobe. It is the camera ray's own 11 cm plus the far
     water's rough facets (HDRP's smoothness is 0 far out: a lobe of about a radian) over the path's flight to its
     first scatter point.
   - The stages at the hold: direct light 2.5 ms (6.2 at the save), prepare 1.4 (2.3): far fewer solid pixels.
   - The sun reaches 2.5 % of the scatter points, and three in four of the paths that leave the surface upwards
     meet something solid: there is a roof over that sea. It is the land.
6. WHY THE CAMERA SEES THE SEA FROM IN THERE: the camera's own ray passes the BACK of a one-sided surface
   (`pt_primary.hlsli` `primary_walk`: HDRP culls it, so the camera never draws it). From under the ground the
   ground is not there for the camera, and the first thing under the horizon is the sea's sheet, which runs under
   the whole land (PT-80).
7. THE BOUNCED LIGHT'S 216,000: the rest of that screen is what a camera inside a hill sees (the outsides of
   buried rock, the sky through the ground's back). Their bounce rays go down to the same sea with nothing between.

What reading rules out, for the record: a level ground's reflection ray cannot go down (`fetch_hit` turns every
triangle's normal to the ray that met it, `above_surface` keeps the ray over the triangle, its start is 2 cm over
it); the ground is plain `TG/Environment/MeshTerrain` (not see-through, not cut out, not lit through); no mask hides
it from any ray; a plain still thing is solid to every ray but one rule: `passes_through` lets a ray from a surface
pass a BACK face within 15 cm plus a hundredth of the pixel's distance. That rule does not ask whether the material
is drawn from both sides (the camera's own ray does): a small gap, written down below, not this.

### The coordinator's question (03:25): do bounce and reflection rays that start ABOVE ground ever reach the sheet under the land?

No, by the code and by the counters. A ray from a surface or along a path asks for the nearest thing with every
triangle's both sides in the structure (only the camera's own walk skips a back), so ground met from above stops it;
and at the save, on the ground, 330 bounce and 350 reflection paths a frame reach water, 0.03 % of the screen's
rays, starting 850 to 950 m off with a quarter of their scatter points in the sun: the open sea seen far off over
the land's edge, none of it under the land. What could still get under: a ray that comes down where the ground has
no triangles at all (a hole the game hides with a rock), and the one rule that passes a back face near a ray's start
(`passes_through`, "A gap found on the way" below); neither shows in these numbers.

### Confirmed: one short run of main's own build (`20261012-032137`)

The recipe `camp-look` (in this copy's `tests/ingame/scenarios.json` until 05:00, taken out since: its job is
done; main ran its own copy of it) PUTS the camera ahead and takes
the pictures there (`cameraForward`: the harness's own step field; the world frozen; no walk):

| step | what it shows |
|---|---|
| `2-game-150-m-ahead` | the game's own picture from the walk's end |
| `3-traced-150-m-ahead` | the tracer's picture from there (Denoising None, the Caustics row on) |
| `4-`, `5-`, `6-traced-30/60/100-m-ahead` | the way there |

`Run-PtHarness.ps1 -Scenario forest@camp-look`. (`cameraForward` goes along the view itself, 0.5 degrees down: 1.2 m
lower at 150 m than the level walk.)
WHAT IT SHOWED (the coordinator's run, 03:22): 150 m ahead the tracer's picture is the sea under a black roof, lit
only through a far gap to the open sky on the left, rocks' undersides hanging overhead; the game's own picture from
there is the same hollow, its rocks grey with HDRP's ambient light and fog. The sea fills the lower part of the
screen. That is the place PT-131 measured.

### What it means

- PT-131 is no defect of a place in the game. It is how a walk of the harness reads when it leaves the ground.
- A walk that should say something about speed must stay on the ground. DONE in the main tree by the coordinator
  (03:25: "The harness's walk now keeps the camera over the ground", `CameraSlide.cs`).
- The speed numbers of `forest@mega-walk`, `forest@camp*` and `forest@camp-measure*` taken before that, past the
  first 2 s of the walk, are of the hollow under the land. A step's `frame.jpg` is still its walk's START.
- What stays true and is worth the fix: a sea pixel in shade, or far, costs what these did (the beach's 133 to
  151 ms of water path were known before). 1.5 scatter points a pixel, each asking the sun through the waves.

### A gap found on the way (read, not measured; not this defect)

`passes_through` (pt_common.hlsli) passes a near back face unless the material is a cut-out. Its own comment says
"on an alpha-tested (two-sided) material it is a real hit": it means two-sided, and tests cut-out. A plain material
drawn from both sides (`MATERIAL_DOUBLE_SIDED`, not a cut-out) is passed from behind within 15 cm plus a hundredth
of the distance, where the camera's own ray meets it. One line would close it
(`& (MATERIAL_ALPHA_TEST | MATERIAL_DOUBLE_SIDED)`). Not put in: nothing measured shows it, and with the tracer's own
camera rays the whole rule (made for HDRP's surfaces off the traced mesh) is one of the workarounds to take out.

## The 9 ms of the brief's (b)

There is no such thing. The "700 paths a frame" were the first seconds of the step, at the save. At the hold a
MILLION paths a frame enter the water's path, three million turns: 9 ms for them (the water's path run, the sun
not asked: 40 against 31 ms) is 3 ns a turn of the whole GPU. Nothing waits for nothing there.

## What the candidate rule does at the hold (the measuring build's counters, run `20261012-021107`)

Step 7 (the trial on) against step 2 (off): the reflections 95 against 230 ms, the bounced light 19 against 66,
the tracer 106 against 259.

- The patches' corners only HALVE (47.6 against 95.4 million a frame; 75 against 152 a patch; patches of 256
  corners and more: 29,400 against 72,300), because the scatter points lie 3.5 m under the surface on average: a
  cell may be no more than half that way (1.8 m), the waves shift sideways by up to 0.7 m, and so the patch still
  grows over two or three turns, made anew at each.
- The kept tiles' work is the same in both (36.5 against 36.8 million corners): the rule does not touch them.
- With no patch made at all the tracer is 68 ms there, with no sun asked 40, with no water path 31.

What would take the rest, exactly (NOT BUILT: large. The game has answered since: with the rule on the beach still
spends 38 to 46 ms in the reflections' stage and 73 ms in the tracer, so these are the candidates for what is left
there; none is measured):

1. ONE TURN A PATCH. A slot that keeps corners has a record of what every corner's ray can do (the whole grid's
   `SunRecord`: how far a ray can stray, how far the waves move sideways). A point off the grid can size its patch
   from that record at once, in place of growing it: one turn of about 20 corners where 75 are made now, and every
   thread of a group doing the same small work (what the tiles were built for). The patch is then as large as the
   record allows, never smaller than what the growing finds, so no light is lost.
2. KEPT CORNERS FARTHER OUT, COARSER: a second and third grid of the same 1,024 corners with cells 4 and 16 times
   as large, for points whose ray is that wide. 567 thousand of the hold's 626 thousand patches are made only
   because their point is "off the grid" (the grid reaches 40 m round the camera).
3. THE PATCH-MAKERS REGROUPED (perf-1's `TGPT_REGROUP_WATER`, number 4: built, never run in the game). A point the
   kept tiles answer makes no patch, but a group of 32 threads still runs the patch's code as soon as one of them
   does; regrouped before the patch, the ones that skip do not wait.

## The fix: the cell by the ray's width (and a second rule, tried and taken out)

The rule that stays is in every build (no measuring switch), keeps what the picture converges to, and leaves the
kept tiles as they were. The second rule ("the shadow first") was in the build the game ran; it gained nothing
there and is out of the sources (section 2 says what it was, for the day a scene asks for it).

### In the game (the coordinator's run `20261012-041726`: the handed DLL, the world running, no denoiser, a clean card)

Milliseconds a frame: the tracer's GPU time / the reflections' stage / the bounced light's stage.

| step (`Speed trial`) | `water-day` | `water-day-evening` (the tracer) |
|---|---|---|
| both rules on (0) | 75.5 / 46.4 / 9.7 | 65.8 |
| both off (576) | 120.8 / 72.4 / 26.0 | 120.2 |
| the cell off (64) | 121.8 / 67.8 / 25.9 | 114.4 |
| the shadow first off (512): THE CELL ALONE, what the sources now are | 72.6 / 38.2 / 9.5 | 68.1 |
| both on again (0) | 71.2 / 41.2 / 10.2 | 67.9 |

- The cell by the ray's width is the whole gain: the beach 121 ms to 73 ms of the tracer, a frame 141 to 92 ms (7.1
  to 10.8 frames a second).
- The shadow first gains nothing in these two views (within the noise; the on steps 75.5 and 71.2 stand on both
  sides of the off step's 72.6).
- The tavern, one step with this DLL: 35.82 ms (reflections 11.20, bounced light 8.10) against main's 36.0. The
  path's two floats cost nothing where there is no water.
- The picture is not judged by this run: the world ran, the game's clock moved and the sun rose over the five steps
  (luma 51, 56, 62, 67, 70). It is judged by the next one.

THE PICTURE, FROZEN (the coordinator's run `20261012-044312`: the handed DLL, the world frozen, 64 frames a step, no
denoiser; recipe `sun-cell-still` of main: rules on, off, on again). Luma of the whole picture (of the scene's
region):

| | on | off | on again |
|---|---|---|---|
| `water-day` | 56.1 (37.9) | 56.3 (38.6) | 56.3 (38.7) |
| `water-day-evening` | 79.9 (73.7) | 80.0 (74.0) | 80.0 (74.0) |

THE LIGHT HOLDS: the on-again step equals the off step; the first step's 0.2 lower is the order of the steps, not
the rule. Flicker and boil are the same in all three (2.0 / 0.7 and 2.2 / 0.8). (These are numbers of the pictures;
how it looks is the owner's to say.)

### 1. The cell by the ray's width

- `water_path` carries the width of its own ray: at its start the width of the ray that met the water (the hit's
  ray cone), growing along each stretch by the cone's spread; a facet that mirrors the path adds its lobe, one that
  lets it through adds the lobe times the indices' difference, and into the water Snell's law narrows the spread by
  the index; a solid's highlight adds its lobe, its diffuse light and a scatter point make the spread a bounce's.
  Kept on the narrow side throughout.
- `water_sun` cuts a patch made at a point into cells no finer than half that width (and, as before, never more
  than half the way to the surface).
- A patch whose cell is thereby coarser than the waves' takes each corner's own share of the sun ("The claim,
  checked", below). THIS IS THE PART THE OFFLINE CHECK FOUND WRONG (it moves the mean by 0.6 to 1.1 % and changes
  the pattern; "The offline check"): it should come out, and such a patch be summed as every other.
  A patch whose cell is the waves' own is summed as before, line for line.
- Who is left as fine as today: every ray narrower than two of the waves' cells at its scatter point (16 cm for the
  sea's ripples): the camera's own ray up to about 160 m out, a mirror's as far as the camera pixel's width
  carries, WHERE THE WATER'S OWN SURFACE IS SMOOTH (HDRP's smoothness near the camera, 0.947 for the sea).
- Who is not, and should not be: the camera's own ray through water whose surface HDRP makes rough (the sea far
  out: smoothness falling to 0). The rough facets spread the ray inside the water (a quarter of the lobe's width
  through the surface), so the pixel's scatter points lie metres apart and its light is the pattern's mean over
  them in any case; the cell follows that width. The hold's 10 m in campspeed's counters were this (its trial
  added the whole lobe; the rule adds the part Snell's law leaves: about 2.5 m there).

### 2. The shadow first: built, measured in the game, TAKEN OUT (04:40)

(The coordinator, 02:20 and 03:25: "an exact order that asks the cheap thing first"; 04:34, with the game's
numbers: "a rule that gains nothing in the game is taken out by the project's rule for trials".)

THE RULING ASKED FOR: IT COMES OUT. It is out of the copy's sources: no function, no switch, no line of it in a
pass (a source check holds that). Why not keep it switched off: "Code that is switched off still costs frame time
in the big passes" (AGENTS.md), and a trial the game has answered comes out the same day.
- What scene would show its gain: water IN VIEW that lies in the LAND'S shade (a cliff, a hill, a building between
  it and the sun) and beyond the kept corners' reach (40 m from the camera), so that its points make their own
  patches: a cove under a cliff at a low sun, a river in a gorge. None of the harness's scenes is that by name. The
  hold under the land was (nine questions in ten there brought nothing), and that was no place a player stands in.
- If such a scene turns up: its lines are in `%TEMP%\tgb\campfix-scripts\handed-7BB5795C\` (the nine source files
  exactly as built into the handed DLL; `pt_water_sun.hlsli` there has `sun_shadow_first`, `sun_ground_shade`,
  `SUN_DARK` and the gate). `%TEMP%` gets swept: the paragraphs below say enough to write it again in an hour.
- Its offline numbers (under half a roof and under a whole one): the built test exe still has that part; they are in
  "The offline check" when it has run.

WHAT IT WAS. The patch's two shadow rays come last, so water the sun cannot reach paid for every patch and then got nothing
(nine questions in ten at the hold). A point that must make its own patch first sent ONE SMALL RAY, from the
place over it (where its own bent ray met the waves) to the sun, against the ground, rock and buildings alone
(`sun_ground_shade`: `MASK_GROUND`, every triangle solid, the first one met ends it; no texture read, nothing
shaded: the ray `water_medium` asks the ground with).
- Where nothing of those stands in the way: the patch as before.
- Where something does: the patch is made with the chance 1 in 4 (`SUN_DARK`) and its light counts four times.
The mean is the same whatever that ray says (it is a gate, not the answer: the patch's own two shadow rays, with
leaves, cut-outs and glass, still decide the light): a point whose place over it is in the land's shadow while a
part of its patch is lit gets that light one time in four, four times as strong. The price is noise along a
shadow's edge on the water's body, and one small ray for every such point. Three patches in four of water in the
land's shade are not made. A tree's shade is not seen by the gate: there the patch is made as before.
WHY A SMALL RAY AND NOT A SHADOW RAY: the tracer's shadow ray (`transmittance`) is a large piece of code (alpha
tests, the leaves' and the glass's walks), and every place it is asked from is one more copy of it in every pass
that shades a path. "Code that is switched off still costs frame time in the big passes" (AGENTS.md). The first
look as first written was such a copy; it was changed before the build (03:40). The ground-only ray is a few
lines. (Seen in the game since: the tavern is as fast with the handed DLL as with main's.)
What it is not: campspeed's measuring switch of the same name (third page 1024) ended the question for good where
its two first rays were dark, which moved a shadow's edge. That switch still stands in a measuring build.
What was expected of it: the biased form made the hold's reflections 68 ms from 230; this one skipped three in
four of what that skipped, and a group of 32 threads still waited for the one that makes a patch (above, 3).
What the game said: nothing gained at the two beaches, where the far water lies in the sun.

Its lines, as they were in `pt_water_sun.hlsli` of the handed build (kept here because `%TEMP%` gets swept):

```hlsl
bool sun_shadow_first() { return (speed_trial() & 512u) == 0u; }   // a measuring build: !speed_measure3(SPEED_M3_SUN_FOOTPRINT)
static const float SUN_DARK = 0.25;       // the chance a patch is made with where the first look at the sun found none

// Does ground, rock or a building stand between a place on the water and the sun's middle? One ray against the plain
// still things alone (MASK_GROUND), every triangle solid, the first one met ends it: no texture read, nothing shaded.
bool sun_ground_shade(float3 from) {
    RayDesc ray; ray.Origin = from; ray.Direction = g.sunDir.xyz; ray.TMin = 0.0; ray.TMax = g.params.y;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) ground;
    ground.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_GROUND, ray);
    ground.Proceed();
    return ground.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// in water_sun, after the kept tiles were asked (made: they answered) and before the patch is grown:
    float sure = 1.0;
    if (!made && sun_shadow_first() && sun_ground_shade(p - t0 * s0 + l * 0.005)) {
        if (rng.next() >= SUN_DARK) return 0.0;
        sure = SUN_DARK;
    }
// and at the patch's end:
    float3 light = sum / (kept * sure);   // (now: sum / kept)
```

### The hidden switch (for one run's A/B; it comes out the day the owner has seen the picture)

| `Speed trial` | what is off | in which build |
|---|---|---|
| 64 | the cell by the ray's width (a patch's cell by the waves alone, as before 2026-10-12) | the sources, and the handed DLL |
| 512 | the shadow first (every patch made, as before) | THE HANDED DLL ALONE (7BB5795C...); in the sources 512 does nothing |
| 576 | both | the handed DLL alone |

The tracer's log says which. In a measuring build the third page's 8192 turns the cell's rule off (it was that
page's trial of it). ONE switch, as the brief asked.

### A test's two knobs (the tracer reads them each frame, as `TGPT_TEST_SUN_PICK`; a player's game sets neither)

- `TGPT_TEST_SUN_WIDTH=<centimetres>`: where the sun is asked for, every path's ray is taken as that wide.
- `TGPT_TEST_SUN_PLAIN=1`: a cell made coarser is summed by the plain mean of what its three corners let through
  (campspeed's trial, before the correction): to say by how much a low sun comes out too dark that way.
They ride in `FrameConstants.waterGeometry.w` (bits 8 to 30 and the top bit; the low byte stays the test's count of
tiles).

### What is added (every switch, export, record, row)

| file | what |
|---|---|
| `native/pathtracing/shaders/pt_water_sun.hlsli` | the rule's paragraph at the top (and a note of the one taken out); `sun_cell_by_width()`; `water_sun` takes `width`; the cell; a coarser cell's corner (its own sun, its landing place when its facet faces away) and triangle (its level area); the test's numbers in `waterGeometry.w` |
| `native/pathtracing/shaders/pt_water_path.hlsli` | `water_path` takes `startWidth` and carries `rayWidth`, `raySpread` (campspeed's measuring-only width lines are gone: the rule's are in every build) |
| `native/pathtracing/shaders/pt_common.hlsli` | `shade_path` hands the hit's ray cone to `water_path`; `speed_trial`'s 64 written down; the third page's 8192 now means "the cell's rule off"; `g_speedStartWidth` and `g_speedSunFootprint` are gone |
| `native/pathtracing/src/tracer.cpp` | the log's words for 64 (the "do nothing" mask is `0xFF80u`: main's `0xFFC0u` less the 64); the measuring build's words for 8192; `TGPT_TEST_SUN_WIDTH`, `TGPT_TEST_SUN_PLAIN` |
| `src/TGPathTracing/Settings.cs` | the hidden setting's own text names 64 (the plugin was not rebuilt for the trial: main's served) |
| `tests/source_checks.py` | the changed lines' checks, eight lines for the rule, and one that no line of the shadow first is left |
| `tests/water_sea_gpu.inl`, `tests/pathtracing_gpu_test.cpp` | the sea fixture's part `width` (below); the deep camera and its frames moved out of the body's part for both; `sunDir` no longer `const` (the width part alone moves it and puts it back) |
| `tests/ingame/scenarios.json` | the recipe `sun-cell` in; the base's `camp` and `camp-measure` out (and `camp-look`, `camp-fix`, which this job had added) |

No export, no record, no FrameConstants row, no option or material bit. ONE `Speed trial` number (64). Two more
uses of `waterGeometry.w` (a test's). Of campspeed's measuring code the third page's 1024 and its counter are out
(the coordinator's word, 04:57; "For the merge", below); the rest stands where it was.

WHAT THE SOURCES ARE AGAINST THE HANDED DLL: the DLL was built at 04:09 with both rules; the shadow first was taken
out of the sources at 04:40, after the game's answer. The sources as they are now were checked by the source
checks (116 OK), by the shader compiler's front end for every pass that reads the water's sun (as a build for
players, a measuring build and with the regrouping define) and by the C++ compiler's syntax pass for `tracer.cpp`
and the test. THEY WERE NOT BUILT INTO A DLL. What they do is what the handed DLL does with `Speed trial` 512 (the
run's step 5).

### The recipes

- `sun-cell` (for a scene with real water in view: `water-day@sun-cell`): the rule on, off (64), on again; each a
  live step of 20 s with 16 frames. Look at: the tracer's GPU time and the reflections' stage in each step's
  lines. (In main, and as run on the handed DLL, it has five steps: 0, 576, 64, 512, 0. For a build of the merged
  sources main's `sun-cell` and `sun-cell-still` want their 576 turned into 64 and their 512 step taken out.)
- `camp-look` and `camp-fix`, which this job added, are out of the copy again with the base's `camp` and
  `camp-measure` (05:00): all four went 150 m ahead of the forest's save, into the hill. They were never in the
  base, so THE MERGE DOES NOT TAKE THEM OUT OF MAIN: where main holds its own copy of one (`camp-look` ran there),
  it is deleted there by hand.

### The offline check (the sea fixture's part `width`: `TGPT_TEST_SEA_PART=width`; `campfix-scripts\sea_width.ps1`)

The deep sea of part 7, every patch made at its point (`TGPT_TEST_SUN_GRID=0`), the sun's share of the picture
(the sun on, less the sun off) over 256 frames.
- THE CELL, at the fixture's sun (57 degrees up) and at a sun 8 degrees up: the rule off, what the others are held
  against; the rule on as the game runs it (this camera's rays are narrower than the waves' cell: within a
  hundredth); every ray taken as 1 m wide and as 10 km wide (the MEAN the same within 2 %; the time falls); as
  10 km wide by the plain mean (said, held to nothing: the "before" of the correction).
- In the BUILT exe (04:09, the one that runs) the reference is `Speed trial` 576 and a second part follows: THE
  SHADOW FIRST under a roof whose shadow's edge runs through the picture (both rules off, the shadow first alone,
  both: the mean the same within 2 %), and under a whole roof the time with and without. In the SOURCES the
  reference is 64 and the roof's part is gone with the rule.

RUN 2026-10-12 07:56 TO 08:04 (the tenth start: nine were ended when Starfield or a game run of the coordinator's
appeared). The handed DLL 7BB5795C (both rules; DirectX Raytracing 1.2 and Shader Execution Reordering on), 640 x
360, 256 frames a run. Raw lines: `%TEMP%\tgb\campfix-runs\run-width-handed-0804.txt` (the mean pictures were
read band by band, below, and deleted for the disk's sake at 08:27).

The sun's share of the picture against the same with both rules off (red / green / blue), and the GPU's time:

| run | sun 57 degrees up | ms | sun 8 degrees up | ms |
|---|---|---|---|---|
| both rules off (the reference): the share itself | 0.03772 / 0.13807 / 0.08783 | 34.60 | 0.00428 / 0.01551 / 0.00998 | 75.49 |
| rules on, the camera's own rays | x1.0105 / 1.0067 / 1.0078 | 11.04 | x1.0384 / 1.0150 / 1.0232 | 19.43 |
| every ray taken as 1 m wide | x1.0116 / 1.0071 / 1.0088 | 15.06 | x1.0152 / 1.0078 / 1.0099 | 33.38 |
| every ray taken as 10 km wide | x1.0110 / 1.0059 / 1.0073 | 9.56 | x1.0124 / 1.0042 / 1.0025 | 10.41 |
| 10 km wide, THE PLAIN SUM (no correction) | x0.9996 / 0.9999 / 0.9995 | (not printed) | x1.0044 / 0.9984 / 0.9980 | (not printed) |
| the sun off | | 4.08 | | 4.12 |

(The whole picture is 0.10150 / 0.25552 / 0.23373 at 57 degrees and 0.06803 / 0.13290 / 0.15579 at 8; with the
sun off 0.064 / 0.117 / 0.146. At 8 degrees the share is a tenth of the picture, so a ratio there carries about
ten times the picture's own noise: 0.4 % or so. The reflections' stage: 27.9 ms off and 3.5 ms at 10 km at 57
degrees; 70.3 and 4.1 at 8 degrees.)

WHAT IT SAYS.
- THE MEAN MOVED WITH THE RULE AS BUILT: plus 0.6 to 1.1 % of the sun's share at 57 degrees, at every width (0.3 to
  0.4 % of the whole picture's mean); at 8 degrees plus 0.3 to 1.5 % at the two forced widths and plus 1.5 to 3.8 %
  with the camera's own rays.
- IT IS THE CORRECTION, NOT THE CELL: the same coarsest cells summed the plain way are x0.9996 to 0.9999 at 57
  degrees, and x0.998 to 1.004 at 8 (inside the noise there).
- IT IS A CHANGE OF THE PATTERN, not an even lift (`campfix-scripts\png_compare.py`, the saved means in eight bands
  each way; these are the saved pictures' 8-bit values, after the test's own curve, so the sizes are rough): with
  the correction one upright band of the picture is 1.6 to 1.8 % brighter in green, the same band in all three
  runs, its neighbours 0.1 to 0.4 % darker, and three level bands 0.4 to 0.7 % brighter. With the plain sum every
  one of the sixteen bands is within 0.05 % of the reference (0.9995 to 1.0004).
- THE TEST FAILED where I had written its expectation too tightly AND wrongly: "the camera's own rays within a
  hundredth" (red x1.0105 at 57 degrees, x1.038 at 8). I had reasoned that this camera's rays are narrower than
  the waves' cell, so the rule would do nothing. It does act (34.6 ms to 11.0): after a path's first scatter point
  its ray is as wide as a bounce's, and every later point of the path asks with a coarse cell.
- ONE VERY LARGE SAMPLE with the rules on at the low sun, the camera's own rays: 1552 times the picture's mean
  (20 with the rules off, 40 at 1 m, 18 at 10 km). Seen once in 59 million samples; not followed up. A coarse
  triangle whose three corners' rays happen to land together puts all its light in one place, which fine cells
  cannot do to that size: to watch for (bright single dots in the water at a low sun).
- The speed is the cell's (it does not depend on which sum): 2.3 to 7 times faster in this fixture (2.3 at 1 m,
  3.1 and 3.9 with the camera's own rays, 3.6 and 7.3 at 10 km).

WHY THE CORRECTION IS WRONG (reckoned on the CPU, `campfix-scripts\chop_sim.py`, `chop_sim2.py`: the fixture's own
waves with their sideways move; NOT proven on the GPU beyond the table above).
- The plain sum takes a triangle's sun from the area of its three MOVED corners across the light. That is exactly
  the sun through that piece of water, whatever the waves do inside it: each triangle keeps its own light, so a
  coarser cell only blurs the pattern.
- The correction took, in its place, the three corners' own slopes times the grid's area. That leaves out how far
  the waves' sideways move (HDRP's choppiness, 2.25) squeezes the water together at a crest and pulls it apart in
  a trough: the area ratio goes down to 0.56 in the fixture's sea. Over a whole sea the error evens out (the
  reckoning: 0.1 to 0.4 % over 500 m of it), in one picture of 10 m of sea it does not. Corner by corner times
  that area ratio, the reckoning's own-sun sum agrees with the plain one to 0.06 % at 57 degrees (at 8 degrees
  the two stay 0.4 to 1 % apart: that reckoning's noise is of that size there).
- It gets worse fast on a steeper sea (the reckoning, normals as HDRP writes them, 6,000 points a row, so 1 to 2 %
  of noise at the low sun): every wave twice as high, a sun 8 degrees up: 4 to 5 % too bright (the plain sum
  between 1.8 % dark and 1.4 % bright); three times as high, where the crests fold over: 1.3 to 1.7 times too
  much light at a sun 20 degrees up and 2.3 to 2.6 times at 8 (the plain sum within 4 % throughout). The factor
  `facing / dot(normal, up)` has no bound where the water stands upright.
- What I had reckoned before (`bias.py`, `patch_sim.py`: "The claim, checked", below) was a sea with slopes twice
  the fixture's and NO sideways move. Its finding is real but narrow: where corners are turned from the sun, the
  plain sum leaves their triangles out and comes out dark. The fixture's sea has no such corner even at 8 degrees
  (0.00 % of three million), so nothing of it showed, and the cure I built for it does harm everywhere else.

THE SHADOW FIRST (taken out since; the built exe still ran its part), under half a roof at 57 degrees: the sun's
share 0.01839 / 0.07192 / 0.04488 with both rules off; with the shadow first alone x0.9990 / 0.9997 / 0.9998; with
both x1.0009 / 1.0003 / 1.0005. GPU 34.67 ms off, 30.45 with the shadow first alone, 10.37 with both; under a whole
roof 33.51 against 7.51. So that rule kept the mean, and gained 12 % by itself where half the water is in shade.
(Why "both" is so near 1 here while the same rules are 0.7 % up in the open sea: not looked into. Half this
picture is in shade, so another half of the pattern counts.)

### The offline check, again: the plain sum as the rule (2026-10-12 09:23 to 09:31) - PASS

The copy's sources as they are, built 08:51 (DLL 20428DB08F3F...), the changed part `width` (its fifth start: four
were ended when a game run of the coordinator's began). DirectX Raytracing 1.2 and Shader Execution Reordering on,
640 x 360, 256 frames a run. "The rule off" is every ray taken as 1 cm wide (no cell made coarser): at 57 degrees
its share is this morning's "both rules off" to the fifth digit (0.03772 / 0.13807 / 0.08783), so the two are the
same patch. Raw lines: `%TEMP%\tgb\campfix-runs\run-plain-20428DB0-0931.txt`.

The sun's share of the picture against the rule off (red / green / blue), the band of the picture farthest from
its own share (16 bands, green), the largest single sample against the picture's mean, and the GPU's time:

| run | sun 57 degrees up | worst band | largest | ms | sun 8 degrees up | worst band | largest | ms |
|---|---|---|---|---|---|---|---|---|
| the rule off: the share itself | 0.03772 / 0.13807 / 0.08783 | | 17.7 | 34.32 | 0.00429 / 0.01551 / 0.00996 | | 227.6 | 74.27 |
| the rule on, the camera's own rays | x0.9984 / 0.9996 / 0.9988 | 0.47 % | 16.2 | 11.06 | x0.9915 / 0.9936 / 0.9910 | 2.99 % | 923.8 | 19.50 |
| every ray taken as 1 m wide | x0.9995 / 1.0000 / 0.9997 | 0.28 % | 21.0 | 15.00 | x1.0186 / 1.0027 / 1.0075 | 2.77 % | 1551.4 | 43.84 |
| every ray taken as 10 km wide | x0.9995 / 0.9996 / 0.9991 | 0.20 % | 17.3 | 9.60 | x0.9919 / 0.9901 / 0.9858 | 2.51 % | 39.9 | 12.66 |

- AT THE HIGH SUN THE RULE KEEPS THE PICTURE: the share within 0.16 % at every width (this morning with the
  correction: 0.6 to 1.1 % up), every band within 0.5 % (held to 1 %; this morning one band was 1.8 % of the whole
  picture off, which is some 3 % of its share). The reflections' stage 27.6 ms to 3.5 at the coarsest cells.
- AT THE LOW SUN: within 1.9 % (held to 2 %), and that is the size of the noise there, not a finding: the same
  10 km run came out x1.0044 / 0.9984 / 0.9980 this morning and x0.9919 / 0.9901 / 0.9858 now, with other frame
  numbers. (I had written 0.4 % for that noise above; two runs of one thing say about 1 %.) The bands there, 2.5 to
  3 %, are printed and not held for the same reason. A low sun's share wants more frames to be held any tighter.
- LARGE SINGLE SAMPLES AT THE LOW SUN, with the rule and without it: 228 times the picture's mean with the rule
  off, 924 and 1551 with it on (this morning: 20 off, 1552 on). So they are not the rule's alone, but the two
  largest are with coarser cells. One such sample is a bright dot in one frame without a denoiser. NOT FOLLOWED UP:
  for the coordinator to enter (the frozen A/B in the game found flicker the same on and off at the evening beach).
- The times of the last two runs at the low sun (43.8 and 12.7 ms; this morning 33.4 and 10.4) carry another
  program on the card (the caustics' stage is 10.4 ms there against 5.2). Which one was not looked up: another
  worker's offline test was running when this one ended (no game run was). The pictures do not depend on that;
  those two times are not to be quoted.
- MUTATION, by the numbers already measured: this morning's build (the correction in) gives x1.0059 to x1.0116 at
  57 degrees, every one outside the 0.5 % this part now holds, so the part as it stands fails on that build. Not
  run again on it (the card).

### What is left (after the offline check)

1. THE RULE IN MAIN CARRIES THE CORRECTION until this copy is merged again. DONE IN THE COPY (08:25; the
   coordinator, 08:30: "(a) ... keep it small"), four files:
   - `native/pathtracing/shaders/pt_water_sun.hlsli`: the three uses of `ownSun` are out (the corner's factor
     `facing / max(dot(normal, up), 1e-3)`, the `else if (ownSun && ...)` branch that landed a turned corner, and
     `across = ownSun ? 0.5 * cell * cell * levelShare : ...`), with `ownSun` itself, `levelShare`, the test's top
     bit of `waterGeometry.w`, and the paragraph at the file's top (it now says what the sum is and that the
     correction was tried and taken out). The patch's code differs from the base's by the cell's lines alone
     (`coarse`, `byWidth`, the `width` it is handed) and the measuring build's counters.
   - `native/pathtracing/src/tracer.cpp`: `TGPT_TEST_SUN_PLAIN` is out (two comment lines and its one line).
   - `tests/water_sea_gpu.inl`: the width part runs four pictures a sun now (the rule off, the camera's own rays,
     1 m, 10 km: no fifth, the plain sum IS the rule). "THE RULE OFF" IS NO LONGER `Speed trial` 64 THERE: it is
     every ray taken as 1 cm wide (`TGPT_TEST_SUN_WIDTH=1`), with which no cell is made coarser: the same patch,
     by the code's own lines. Why: once the switch is out at the merge, a reference asked for with 64 would be the
     rule itself, and the part would pass by comparing the rule with the rule. It holds the rule to tighter numbers: the share of the whole
     picture within 0.5 % at 57 degrees and 2 % at 8 (there the share is a tenth of the picture and carries ten
     times its noise), AND each of the picture's 16 bands (8 upright, 8 level) within 1 % at 57 degrees (printed
     for 8, not held). The old expectation "the camera's own rays within a hundredth" is gone: it was reasoned
     wrongly (the rule does act on this camera's paths, after their first scatter point). The band check is the
     one that would have caught the correction (its band was 1.8 % of the picture off).
   - `tests/source_checks.py`: the correction's three lines out; in: the plain `across` in both places, no
     `ownSun` / `levelShare` / top bit left, no `TGPT_TEST_SUN_PLAIN`, the band check present.
   NOT TOUCHED, as told: the switch `Speed trial` 64 (the coordinator takes it out at the merge), the recipes, any
   doc but this report.
2. ITS PROOF: done offline, 09:31 ("The offline check, again": the camera's own rays, 1 m and 10 km, both suns,
   PASS). Not in the game: the coordinator's merged build is the first that can be.
3. THE GAP THE PLAIN SUM LEAVES (for the coordinator to enter in `docs/PT-REQUESTS.md`; reckoned, not measured in
   the game): at a very
   low sun over a steep sea a coarse triangle with a corner turned from the sun is left out, and the water's glow
   goes darker there (the fine cells lose only the triangles along the line where the water turns from the sun).
   How much, by the two reckonings: 11 to 18 % at 8 degrees on the first one's sea (slopes' spread 0.12, no
   sideways move); 1.8 % at the coarser cell on a sea twice as steep as the fixture's with its sideways move (3 %
   of its corners turned), which is inside that reckoning's noise. So: real in principle, its size in a real sea
   not known. The cure that fits the plain sum: keep such a triangle, with its own area across the light, the
   turned corner landing where still water's ray would, the share that of the corners that face the sun
   (`chop_sim2.py`'s "mixed": within 4 % of the fine cells on every sea tried, the reckoning's noise). Not built.
4. The fixture cannot show item 3: its sea is too gentle. A second sea for it (the same waves, twice as high) would.

## The claim, checked (reading and reckoning; nothing here is measured on the GPU yet)

SUPERSEDED IN ITS CONCLUSION (08:30, "The offline check", above). What follows is as written before the GPU had
run. Its two reckonings are of a sea with slopes' spread 0.10 to 0.12 and with NO sideways move of the water.
They are right about what they model (the plain sum leaves out a triangle with a corner turned from the sun) and
wrong as a guide to the fixture's sea and to the cure: the GPU finds the plain sum right there, and the cure built
on this section ("each corner its own facet's sun") moves the mean and the pattern. Read it for the mechanism, not
for the verdict.

What a patch made at a point does (`pt_water_sun.hlsli`): the surface round the place over the point is cut into
triangles; each triangle takes the sun that falls on it (`across`: its area across the light), lets through a share
(`through`: the mean of its three corners' shares, Fresnel and the facets' hiding), and that light is spread evenly
over the triangle its three corners' bent rays land on, on the plane through the point; the point takes what lands
on it.

- WHERE the light lands: any cell conserves the light (each triangle's light lands once, somewhere). A coarser cell
  moves light by at most about a cell: the pattern is coarser, the mean over a region a few cells wide is the same.
  A path's ray that is two cells wide or more averages over that region anyway (the scatter points of a pixel's
  paths are spread over the ray's width). HOLDS.
- How much sun ARRIVES: the triangles' `across` add up to the area of the patch's outline across the light, whatever
  the cell (the sun through a sheet depends on its edge alone). HOLDS.
- How much the surface LETS THROUGH: with cells as fine as the waves a triangle's three corners stand on one facet
  of the waves, and `across` is that facet's own area across the light: a facet turned to the sun takes more sun AND
  lets more of it through, and the product is right. With a cell coarser than the waves the triangle is a flat sheet
  over many facets: `across` is the sheet's, and `through` is the plain mean of three facets drawn by chance, not
  the mean weighted by how much sun each facet takes. A facet turned away from a low sun takes little sun and
  mirrors most of it; counted by the plain mean it pulls the share down. And a corner whose facet faces away from
  the sun ends its six triangles, sun and all. DOES NOT HOLD as the trial was written.

  The reckoning (`campfix-scripts\bias.py`: a height field, slopes drawn from a bell curve cut at the game's 0.479,
  Fresnel alone; the share let through against the true one):

  | sun up by | slopes' spread | the trial's plain mean | corrected (below), a triangle with a facet facing away still given up |
  |---|---|---|---|
  | 60 deg | 0.05 to 0.2 | 1.000 | 1.000 |
  | 40 deg | 0.05 to 0.2 | 0.993 to 1.000 | 1.000 |
  | 30 deg | 0.05 to 0.2 | 0.970 to 0.999 | 1.000 |
  | 20 deg | 0.05 / 0.1 / 0.2 | 0.994 / 0.974 / 0.821 | 1.000 / 1.000 / 0.937 |
  | 15 deg | 0.05 / 0.1 / 0.2 | 0.986 / 0.929 / 0.656 | 1.000 / 0.993 / 0.852 |
  | 10 deg | 0.05 / 0.1 / 0.2 | 0.955 / 0.774 / 0.442 | 1.000 / 0.932 / 0.733 |

  THE CORRECTION (in the fix): in a patch whose cell was made coarser than the waves', a corner carries the sun its
  OWN facet takes for each square metre of level water (the facet's cosine to the sun over its cosine to the level
  normal) times what it lets through, and a triangle's light is its level area times the mean of its three corners'.
  Averaged over the grid's chance shift every corner is an evenly drawn point of the surface, so the sum is the
  true sun let through for any cell (a sum of point samples, with no ratio in it). A corner whose facet faces away
  from the sun carries none and is given the landing place still water's ray has, so that its triangles stay (the
  last column of the table is what is left if they are given up: with them kept it is 1.000 in every row).
  Not in it: the waves' sideways squeeze of the surface (HDRP's choppiness) is in `across` and not in a corner's
  own sun; its mean is 1 over a wave and it does not lean with the facets' tilt.

  A SECOND RECKONING, OF THE PATCH ITSELF (`campfix-scripts\patch_sim.py`: the patch's own sums on the CPU for a
  point sun and a height field of six waves, 0.6 to 7 m long, slopes' spread about 0.12; a point 3 m deep; the
  mean over 30,000 points and shifts of the grid, against what the surface itself lets through a square metre):

  | sun up by | cells of 5 cm, as today (5,000 points: noisy) | the trial's plain mean, cells 0.5 m / 1.5 m | a corner's own sun (the fix), 0.5 m / 1.5 m |
  |---|---|---|---|
  | 57 deg | 1.03 | 1.000 / 0.999 | 1.000 / 0.999 |
  | 20 deg | 0.97 | 1.045 / 0.989 | 1.003 / 1.004 |
  | 12 deg | 1.01 | 0.969 / 0.949 | 1.010 / 1.021 |
  | 8 deg | 1.12 | 0.893 / 0.817 | 1.028 / 1.028 |

  So the fix's coarse cell keeps the surface's own mean within 3 % down to a sun 8 degrees up, where the plain
  mean loses 11 to 18 %. (The fine cells' own column wanders by its noise: a sample of theirs is a caustic's
  spike.) The GPU's check of the same is the sea fixture's part `width`.
- The shadow rays and the foam of "the chosen cell speaks for all": chosen inside a coarser cell, so a shadow's edge
  on the water's body light is as fine as the cell: half the ray's width. HOLDS for the same reason as the pattern.
- WHERE THE WIDTH ITSELF IS WRONG (the claim's edge): the ray cone knows no focusing. A path through something that
  magnifies (a curved mirror, a glass ball in front of water) has a wider cone than it should; the tracer's textures
  are read by the same cone there (PT-46). And the width is one pixel's for the camera's own ray: where the camera
  does not shift its rays inside the pixel from frame to frame, a pixel today shows the pattern at its middle, and
  with the rule the pattern's mean over half a pixel.
- FOUND BY READING, NOT THE RULE'S: a patch that outgrows 32 cells a side makes its cells coarser than the waves
  today already ("past SUN_GRID cells a side the cells grow instead"), with the plain mean. So a deep point under a
  low sun is a little too dark today. The rule leaves that patch as it is (its cell was not made coarser by a ray's
  width); the same correction would cure it. An entry of its own, if the coordinator wants one.
  (SINCE 08:15: NO. The plain sum is the right one, and "a little too dark" holds only where corners are turned
  from the sun, which is "What is left", item 3. The correction cures nothing here.)

## For the merge: what of campspeed's measuring code stays, and what goes

(The coordinator, 04:50: "say in the report which of campspeed's measuring code ... should stay in the tree and
which should go, now that PT-131 is no place; my reading is that the counters "Special branches" stay as a
measuring build's tool and the camp recipes go". Mine is the same, with two more things to take out. 04:57:
"agreed on all of it ... do the clean-up IN YOUR COPY now so the merge brings the final state". DONE AT 05:00:
what is under GOES is out of the copy; the switch 64 stays until the offline check of the mean has run.)

All of it sits between `#ifdef PT_SPEED_MEASURE` and its end (CMake's `TGPT_SPEED_MEASURE`): a build for players
holds none of it, so nothing here costs a player a frame. The question is only what a measuring build is still
asked.

STAYS (a measuring build's tool for water and glass; the beach still spends 38 to 46 ms in the reflections' stage):
- THE COUNTERS, "Special branches" (`SPEED_COUNT_LIST`, `speed_count_flush`, the tracer's small buffer, its
  read-back and log line). They are what showed that a million paths a frame ran the water's path and why each
  point made its own patch. The next candidates for the beach (one turn a patch, kept corners farther out, the
  patch-makers regrouped: "What would take the rest", above) are judged by exactly these numbers: patches made,
  their turns and corners, the seven reasons. One of them now counts the rule: "made coarser by the ray's width".
- THE THIRD PAGE'S SWITCHES THAT TAKE A PART OF THE WATER'S OR THE GLASS'S WORK OUT: 2 (glass not followed), 4 (the
  water's path not run), 16 (no sun through the waves), 32 (no patch at a point), 64 (the body scatters nothing),
  512 (the path ends after its first turn), 2048 (every ray ends after 60 m), 4096 (no landed light), 16384 (the
  glass walk ends at its first interface). They say what a part costs, in any scene.
- 8192 (`SPEED_M3_SUN_FOOTPRINT`): it is the measuring build's way to turn the cell's rule off, the twin of the
  players' 64. It stays as long as 64 does and goes with it (below).
- THE SEA FIXTURE'S COST ARRANGEMENT "the same sea under a roof 30 m up" (`tests/water_sea_gpu.inl`, the cost part):
  the one offline stand-in for water in view that the sun cannot reach. It runs only when its part is asked for.

GOES (OUT OF THE COPY SINCE 05:00):
- THE THIRD PAGE'S 1024 (`SPEED_M3_SUN_SHADOW_FIRST`, in `pt_water_sun.hlsli`, with its counter `g_scSunFirstDark`,
  "stopped by the shadow rays first", and its words in `tracer.cpp`'s log). Its question is answered: at the hold it
  said the cost was at points no sun reaches; the exact form of it was built and gained nothing where a player
  stands. And it was one more inlined copy of the whole shadow-ray code (`sun_rays`) in every pass that has the
  water's sun, which made a measuring build's passes larger than the players' for a question nobody asks any more.
  What was done: `pt_common.hlsli` the constant is a note now, and the counters' list has a free place at 43 (the
  others keep their numbers, so the tracer's indices stand); `pt_water_sun.hlsli` its six lines are gone (the file
  names `sun_rays` three times now: its definition, the row's off, the chosen cell's; a source check holds that);
  `tracer.cpp` the counters' line no longer says "stopped by the shadow rays first", and a third-page 1024 is
  logged as doing nothing.
- THE RECIPES OF THE "CAMP": `camp` and `camp-measure` (the base's; campspeed had rewritten the second), `camp-look`
  and `camp-fix` (mine). All four walked or moved the camera 150 m ahead of the forest's save, which was the flight
  into the hill. `camp-look` has done its one job (run `20261012-032137`). `camp-measure`'s step 7 had also changed
  its meaning: its 40961 (8192 on the third page) now turns the rule OFF, where it once turned the trial on.
  (Main's own `camp-measure-b` and `camp-walks` are the coordinator's: `camp-walks` stays as PT-125's crash walk.)
- PT-131's numbers as a scene's target, wherever a file holds them (the 366 ms were of a view from under the land):
  NOT MINE TO TOUCH (`docs/PT-REQUESTS.md`, `tests/ingame/targets.json` are the coordinator's; this copy's
  `docs/PT-REQUESTS.md` is the base's, old PT-131 and all: it does not differ from the base, so main's rewritten
  entry stands in a three-way merge).

READY TO MERGE (05:05). The copy's files that differ from its base `base20f` (compared by content,
`campfix-scripts\changed.py`), campspeed's changes and mine together:

| file | whose | what |
|---|---|---|
| `native/pathtracing/shaders/pt_water_sun.hlsli` | both | the cell by the ray's width with its correction and switch 64; the test's knobs; campspeed's counters and part-switches (measuring build) |
| `native/pathtracing/shaders/pt_water_path.hlsli` | both | `water_path` takes `startWidth`, carries `rayWidth`, `raySpread`; counters and part-switches (measuring build) |
| `native/pathtracing/shaders/pt_common.hlsli` | both | `shade_path` hands the hit's ray cone on; `speed_trial`'s 64; the third page, `speed_measure3`, the counters' list and `speed_count_flush` (measuring build) |
| `native/pathtracing/shaders/pt_dielectric.hlsli` | campspeed | counters and the third page's 16384 (measuring build only) |
| `native/pathtracing/shaders/pt_gi.hlsl`, `pt_specular.hlsl` | campspeed | one `speed_count_flush` call each (measuring build only) |
| `native/pathtracing/src/tracer.cpp` | both | the log's words for 64, mask `0xFF80u`; `TGPT_TEST_SUN_WIDTH`, `TGPT_TEST_SUN_PLAIN`; the third page, the counters' buffer, read-back and log line (measuring build) |
| `src/TGPathTracing/Settings.cs` | campfix | the hidden setting's text names 64 |
| `tests/source_checks.py` | both | the checks of the lines above |
| `tests/water_sea_gpu.inl` | both | the part `width`; the deep camera and `frames` hoisted; campspeed's cost arrangement under a roof |
| `tests/pathtracing_gpu_test.cpp` | campfix | `sunDir` no longer `const` |
| `tests/ingame/scenarios.json` | both | `sun-cell` in (on, off 64, on again); `camp`, `camp-measure` out |
| `docs/agents/campfix.md`, `docs/agents/campspeed.md` | new | the two reports |

Not to merge: `Build.Local.props` (the copy's own). Where the merge will ask: `scenarios.json` (main added its own
`sun-cell` with five steps where the copy has three: take the copy's, and make `sun-cell-still` the same three
numbers: 0, 64, 0; with the merged sources 576 acts as 64 and 512 does nothing); `tracer.cpp`'s mask line (main
`0xFFC0u`, the copy `0xFF80u`: the copy's while 64 lives) and the lines round main's new marks; `pt_common.hlsli`
round main's one-line guard in `fetch_hit`.
Checked on the copy as it is now: source checks 116 OK; the shader compiler's front end for `pt_specular`, `pt_gi`
(players' reordering and compute forms, the measuring build, the regrouping define), `pt_walk`, `pt_caustics`: 0
failed; `tracer.cpp` as a players' and as a measuring build, and the test's source, through the C++ compiler's
syntax pass: clean. NOT BUILT INTO A DLL, no frame traced with these sources: the first real build of them is the
coordinator's after the merge. The measuring build's log line lost one number and its format one `%.0f`: counted
by a script (`campfix-scripts\fmt_count.py`: 47 and 47), not run.

WHEN THE SWITCH 64 COMES OUT (the game has answered for speed and for the frozen picture; it waits only for the
owner's look, or for the coordinator's word that it need not): `pt_water_sun.hlsli` `sun_cell_by_width()` (both
forms) and the `if (sun_cell_by_width())` round the cell's lines (the lines stay); `pt_common.hlsli` the 64 line of
`speed_trial`'s list and the third page's 8192 (`SPEED_M3_SUN_FOOTPRINT`); `tracer.cpp` the `trial&64u` words (the
"do nothing" mask becomes `0xFFC0u`) and the `third&8192u` words; `Settings.cs` the 64 clause; `source_checks.py`
the lines that name them; the recipes `sun-cell`, `sun-cell-still`; and in `tests/water_sea_gpu.inl` the width
part's reference becomes its run with the camera's own rays (the rule leaves those as they were: the offline
check's "own" number says by how little they differ), since there is no "off" to hold against any more.

## On the way: a measuring build that was not needed

Before the harness was read, a "mark" was built into the measuring tree (`pt_specular.hlsl`, `pt_gi.hlsl`: a pixel
whose ray ended in the water's path asked two plain rays against the ground). Its six passes were stopped at four
(03:09) and its lines taken out of both files again. `%TEMP%\tgb\campspeed-measure` now holds campspeed's DLL
(01:09, SHA B386C2E7..., also kept as `TGPathTracing.Native.campspeed-B386C2E7.dll`) beside four pass headers of
03:00 that no DLL holds: a build in that tree makes them again. The script is `campfix-scripts\mark_edit.py`.

## Rules met on the way

- Windows' Storage Sense swept `%TEMP%` at 03:28 (the coordinator, 03:50). Checked at 03:50
  (`campfix-scripts\files_check.py`): all 317 files the players' build names in its makefiles exist; the test's
  stage is copied anew from the MAIN tree's `artifacts\Native` at each run (`sea_width.ps1`), whose
  `TGPathTracing\D3D12\D3D12Core.dll` is there; the script now refuses to run a test without it and prints the
  tracer's "DirectX Raytracing" line after a run.
- The Bash tool halves doubled backslashes in a here-document: a path written that way into this report came out
  with a tab in it (seen and put right). Scripts and text with backslashes go through the Write tool.
- A build's passes read the tree's shader files when each compile STARTS, and `cmake --build` makes every pass again
  whose file is newer than its header: nothing under `native/pathtracing/shaders` may change between the first
  compile's start and the DLL's link. The players' build was stopped and started again twice for that (03:21 to add
  the shadow first, 03:38 to make its first look a ground-only ray), each time before a pass of its own had ended.

## State when stopped

09:40, DONE: handed back. In four claims: BUILT (the copy's sources, 08:51, DLL 20428DB08F3F...); PASSES ITS OWN
OFFLINE CHECK (09:31, the sea fixture's part `width` alone); NOT SEEN IN THE GAME (the game runs of this morning
were of the handed DLL and of main's merge, both with the correction that has since come out); THE OWNER HAS NOT
LOOKED. Source checks 116 OK on the copy as handed.
THE FILES TO MERGE AGAIN (what changed in the copy since the merge of about 05:50):
`native/pathtracing/shaders/pt_water_sun.hlsli`, `native/pathtracing/src/tracer.cpp`, `tests/water_sea_gpu.inl`,
`tests/source_checks.py`, `docs/agents/campfix.md`. Nothing else: the switch 64, the recipes and every other doc
are as they were merged.
FOR THE ONE WHO TAKES THE SWITCH 64 OUT: the width part no longer asks for it (its reference is a ray 1 cm wide),
so it keeps testing. `tests/source_checks.py` still pins the switch's line in the shader, the log's words and the
mask `0xFF80u`: those go with it ("For the merge", "WHEN THE SWITCH 64 COMES OUT").
LEFT OPEN, for entries: (1) the plain sum leaves out a coarse triangle with a corner turned from the sun (a very
low sun over a steep sea; "What is left", 3); (2) single samples of up to 1551 times the picture's mean at a sun 8
degrees up, the largest with coarser cells ("The offline check, again"); (3) `passes_through` does not ask
`MATERIAL_DOUBLE_SIDED` ("A gap found on the way"); (4) a patch that outgrows 32 cells a side makes its cells
coarser already, without the rule (it is summed the plain way, so it is right by today's finding).
How it went before that:
08:35. THE OFFLINE CHECK HAS RUN (07:56 to 08:04) AND FOUND THE RULE'S CORRECTION WRONG: see the top and
"The offline check". Told to the coordinator at 08:15; its answer, 08:30: take the correction out in this copy,
keep it small, hand over the sources when the offline check has passed on the built copy, and "if the card is not
free within an hour of your build, hand over on this morning's TGPT_TEST_SUN_PLAIN numbers and say plainly that
the final sources were built but not run".
- THE CORRECTION IS OUT OF THE COPY'S SOURCES (08:25: "What is left", item 1). Source checks 116 OK; the passes'
  front ends compile in the three kinds of build; `tracer.cpp` and the test's source pass the C++ syntax check.
- BUILT, 08:28 TO 08:51 (`campfix-scripts\rebuild2.cmd`, into the same build folder: no new tree): 57 passes in
  1387 s, 0 failed, then the DLL and the test: `%TEMP%\tgb\campfix-build\TGPathTracing.Native.dll`, 33,259,008
  bytes, SHA-256 `20428DB08F3F0416775F1E06FB07D7EB0A0EAE10A951CFD116F85A1CEF44D60B` (the copy's sources as they
  are: the cell by the ray's width with the plain sum, switch 64, no shadow first). The handed DLL and its test are
  kept beside them as `TGPathTracing.Native.handed-7BB5795C.dll` and `tgpt_gpu_test.handed-7BB5795C.exe`.
  (The copy's links to the SDK folders were gone at 07:08, every worker copy's: the build's one path through them,
  the shader compiler's, now points at the main tree's own, run in place, read only. The main tree's newer
  `Build-PtShaders.ps1` is the one run: its compilers run below normal priority.)
- THE CHANGED WIDTH PART ON THE NEW BUILD (`sea_width.ps1 -Part width -Tag plain`): started 08:52 (`-GpuCheck`
  said the card was free). Its numbers: "The offline check, again", below the first one's, when it has ended.
What was true before this (kept; the four claims apart):
- BUILT: the players' build of 04:09 (57 passes in 28 minutes, 0 failed):
  `%TEMP%\tgb\campfix-build\TGPathTracing.Native.dll`, 33,265,664 bytes, SHA-256
  `7BB5795C0EB8C77A2F8B95987884ED4B1D745A4A61ACB6E52F3AF8F56C154CD2`, with BOTH rules in it. Handed to the
  coordinator at 04:10 with the recipe `sun-cell`. (The first link failed: the build tree's empty object folders
  had gone with Windows' sweep of `%TEMP%`. CMake wrote the tree out again, `campfix-scripts\regen_build.cmd`; the
  57 pass headers were kept.)
- SEEN IN THE GAME (the coordinator's runs `20261012-041726` and `20261012-044312`, both of the handed DLL): the
  speed, and the frozen picture's luma (on = off): the tables under "In the game". The owner has not looked yet.
- THE SOURCES SINCE 04:40: the shadow first is out. SINCE 05:00 (the coordinator's clean-up before the merge): the
  measuring build's third-page 1024 and its counter are out, and the four camp recipes. READY TO MERGE, said to the
  coordinator at 05:07 with the list of files ("For the merge"). Source checks 116 OK; the passes' front ends
  compile (`campfix-scripts\syntax_fix.ps1 -Tree`: every pass with the water's sun in it, three kinds of build, 0
  failed); `tracer.cpp` (players' and measuring) and the test's source pass the C++ compiler's syntax check. NOT
  BUILT INTO A DLL: the handed one is the only DLL, and it is older than these sources by both removals. The
  coordinator merges on the game's frozen A/B and builds main (04:57: "no need for a DLL of your own"); the
  offline proof of the mean is owed (PT-99's entry says so) and the switch 64 stays until it has run.
- PASSES THE HARNESS: NO. The offline check of the mean (`campfix-scripts\sea_width.ps1`, part `width`, on the
  built exe and the handed DLL) ran at its tenth start, 07:56 to 08:04 (nine starts from 04:49 on were ended by
  the script when Starfield or a game run appeared), and its result is "FAIL sea checks": the test's own
  expectation for the camera's rays failed at both suns, and the mean is 0.6 to 1.1 % up with the rule as built.
  The two expectations for the forced widths (within 2 %) passed; they were too loose to catch what the table shows.
  Its output: `%TEMP%\tgb\campfix-runs\run-width-handed-0804.txt` (lines "SEA WIDTH ...").
  To run it by hand later: `powershell -File %TEMP%\tgb\campfix-scripts\sea_width.ps1 -Part width -Tag width`
  (it stages main's `artifacts\Native` with the built DLL over it; never the whole `tgpt_gpu_test`). After the
  merge the same part of main's own build is `TGPT_TEST_WATER_SURFACE=1`, `TGPT_TEST_WATER_SEA=1`,
  `TGPT_TEST_SEA_PART=width`.
