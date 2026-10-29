# The reordering study in the game (PT-99, PT-110), 2026-10-10 11:34 to 11:44

> Closed on 2026-10-10 (docs/agents/rule-2.md): the study's build option and its recipes are gone; what it found led
> to the rule written there.

Run `artifacts/harness/20261010-113425`: the study build of `ser1` (`%TEMP%\tgb\ser1`, native D9677EF78AE2, CMake
`TGPT_SER_STUDY` ON, no NRD), scene `tavern-room`, recipe `ser-study`, no denoiser, 16 frames a step, 1924 x 1084,
the card free. Read by `<scratch>\measure_read.py` (the medians of the tracer's 10 s lines inside each step). A
first try at 11:17 (`20261010-111610`) ran out of its time while the driver made the ten big passes (over 600 s);
this one found them cached (145 s of waiting). The main tree's own native went back afterwards (9D134423AA82).

## The numbers

| Step | `Speed trial` | a frame, ms | the bounced light's pass | the reflections' pass |
|---|---|---|---|---|
| as it ships | 0 | 73.9 | 50.9 | 9.3 |
| the plain compute passes (the yardstick) | 8 | 40.3 | 14.2 | 12.6 |
| **the ray generation passes with NO regrouping call compiled in** (both passes) | 16 | **34.8** | **11.4** | 10.0 |
| the regrouping where it is, today's key, one call | 32 | 78.3 | 54.8 | 9.7 |
| the regrouping where it is, NO hint bits | 32, mode 4 | 52.5 | 29.5 | 9.1 |
| the regrouping where it is, by what comes next (2 bits) | 32, mode 1 | 64.2 | 41.3 | 9.4 |
| the regrouping right after the ray, "little carried", today's key | 48 | 70.1 | 47.4 | 9.2 |
| right after the ray, by what comes next | 48, mode 1 | 57.2 | 34.4 | 9.2 |
| right after the ray, by a hit object from the ray query | 64 | 76.1 | 53.1 | 9.4 |
| right after the ray, and again by the screen before the reuse | 80 | 94.9 | 71.3 | 9.7 |
| as it ships, again | 0 | 73.4 | 50.5 | 9.3 |

The picture is the same in every step (`<scratch>\same_picture.py`: each against the first, 2.24 levels in all of
them, the second take of "as it ships" too; the brightness within 0.04 levels).

The driver's own word on what a thread carries across a regrouping (the study's log lines, "the driver's stack
for its ray generation shader"): the bounced light's pass as it ships **2,560 bytes** a thread; the reflections'
pass **128**; with no regrouping call 0; EVERY rearranged variant of the bounced light's pass still 2,560 to
2,592 (so "little carried, 205 bytes" as counted in the listing is not what the driver keeps).

## What it says

1. **The ray generation form is not the cost.** With no regrouping call the bounced light's pass is 11.4 ms: under
   the plain compute pass's 14.2. So `ser1`'s step 3 (rays by `HitObject::TraceRay` with hit shaders) is not
   needed for speed in a room.
2. **In a room every regrouping of the bounced light's pass costs, whatever the key or the place**: 18 ms with no
   hint bits at all (the pure cost of keeping and fetching 2,560 bytes a thread, three calls compiled), 23 to 30
   by "what comes next", 36 to 43 by the material's number, 42 by a hit object, 60 when regrouped twice. None of
   the keys tried buys it back there: the threads of a room do nearly the same thing.
3. **The reflections' pass shows what the feature wants**: it carries 128 bytes, and there the regrouping is free
   or better (9.3 ms with it, 10.0 without, 12.6 as a plain pass; at the beach 140 with it against 318 plain).
4. So the fault is the 2,560 bytes, and a regrouping asked of threads that have nothing to gain from it.

## What follows for the rebuild (`ser2`)

- Step 1 (the reuse across frames and all screen-order work out of the sorted pass) is the way to the small
  stack: what is alive across the regrouping today is what the code AFTER the shading still needs (the surface,
  the reservoir, the candidates' sums, the cache's feed). The measure of success is the driver's own line: the
  bounced light's pass at or near the reflections' 128 bytes, not the listing's count.
- The regrouping is for threads that diverge: where the ray met water, glass or a walk, and what comes next is
  long and unlike its neighbours'. A thread that met a plain surface gains nothing from it in this run. Whether a
  thread may skip the call by a condition (and what the driver makes of a conditional call: still 2,560 bytes
  kept for all, or only for those that call) is the first thing to find out from Microsoft's specification and
  NVIDIA's guidance, and then by this same study's log line.
- The target stands: with reordering on, the tavern's bounced light at or under 14 ms (11.4 is shown possible in
  this form), a town's under 28, the beach's no slower than 42.6.
## The town and the beach (run `20261010-115539`, 11:56 to 12:08, the same build and recipe)

A caution first: another program used the card at some moment of this run (the harness: "pid 53424 up to 14.7 %";
gone when looked for). The two takes of "as it ships" agree to 3 ms in each scene, and the tavern's run was clean.

ms: a frame / the bounced light's pass / the reflections' pass:

| Step | `Speed trial` | a town (`metal`) | the beach (`water-day`) |
|---|---|---|---|
| as it ships | 0 | 100.9 / 57.4 / 24.6 | 165.0 / 55.0 / 95.4 |
| the plain compute passes (the yardstick) | 8 | 75.1 / 27.8 / 28.2 | 272.1 / 63.6 / 193.8 |
| no regrouping call compiled in (both passes) | 16 | 78.1 / 27.9 / 31.1 | 191.6 / 51.7 / 125.4 |
| the regrouping where it is, today's key, one call | 32 | 102.7 / 59.4 / 24.4 | 164.9 / 50.9 / 99.6 |
| **the regrouping where it is, NO hint bits** | 32, mode 4 | **77.3 / 35.1** / 23.0 | **147.8 / 32.2** / 100.8 |
| the regrouping where it is, by what comes next | 32, mode 1 | 101.0 / 57.9 / 24.2 | 163.3 / 50.6 / 98.0 |
| right after the ray, "little carried", today's key | 48 | 96.2 / 53.1 / 24.1 | 159.7 / 48.1 / 97.0 |
| right after the ray, by what comes next | 48, mode 1 | 94.4 / 51.6 / 23.8 | 158.3 / 45.1 / 98.7 |
| right after the ray, by a hit object | 64 | 97.4 / 54.2 / 24.3 | 160.1 / 47.2 / 98.3 |
| right after the ray, and again by the screen | 80 | 115.1 / 72.8 / 22.8 | 169.8 / 58.2 / 97.1 |
| as it ships, again | 0 | 101.5 / 58.6 / 24.0 | 161.4 / 51.7 / 95.0 |

The picture is the same in every step of both scenes (each against the first: 1.55 to 1.56 levels in the town,
2.08 to 2.16 at the beach, the second take of "as it ships" as large as any).

### What the three scenes say together

1. **The hint is the largest single cost, everywhere.** The bounced light's pass, by the material's number in 16
   bits against a regrouping with no hint at all: the tavern 50.9 / 29.5, the town 57.4 / 35.1, the beach 55.0 /
   32.2. Twenty milliseconds and more in each, the same picture. MADE THE RULE AT ONCE (12:15 on 10-10,
   `pt_gi.hlsl`: `dx::MaybeReorderThread(0u, 0)`; a source check holds it; the reflections' pass keeps its key).
2. **The regrouping itself pays at the water and costs in a room, for this pass as it is built**: with no call at
   all the pass is 11.4 ms in the tavern (29.5 with the hint-less call), 27.9 in the town (35.1), but 51.7 at the
   beach (32.2). So the call must stay, and the 18 ms it costs where threads do not diverge is the 2,560 bytes.
3. **The reflections' pass is the model**: 128 bytes carried, and its regrouping pays in every scene: 9.3 ms
   against 10.0 without in the tavern, 24.6 against 31.1 in the town, 95.4 against 125.4 at the beach.
4. A hit object made from the ray query, a key by what comes next, and a second regrouping back to screen order
   bought nothing in any scene with the stack as it is.

### For the rebuild (`ser2`)

- Today's pass in the main tree now has the hint-less call: take that as "as it ships" (your merge will meet the
  line near `pt_gi.hlsl` 89: keep the hint-less call in the path that ships).
- The target moves: with the call kept, the bounced light's pass must come from 29.5 / 35.1 / 32.2 ms toward what
  it costs with nothing carried: 11.4 in the tavern, 27.9 in the town, and under 32 at the beach. The measure is
  the driver's own line (2,560 bytes today; the reflections' 128).
- Whether the reflections' pass also does better with fewer hint bits was not asked: one more study mode, cheap.
