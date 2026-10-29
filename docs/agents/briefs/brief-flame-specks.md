# Brief: WHY do lamps placed in their flames make speckles? Three suspects, a switch each (PT-85, PT-40) - one agent, a trial first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\flame`
(pristine base: `...\scratchpad\wt\base60`). Follow `AGENTS.md` (read "The game is the first test, not the last") and
`docs/AGENT-WORKERS.md` there. Report: `docs/agents/flame-specks.md`; write it early.

## The owner's question (2026-10-09 01:00), the top priority

"I'm really interested in why the flames being lit causes NRD speckles when placing the lights as bulbs and not inside
the [flame] doesn't. That makes me think the way we light the flame has problems, if that could cause such speckles?"
And the ruling that stands: the fire IS the emitter ("No, of course I don't want to go back to the game's own light
position - the fire is the emitter"). The lamps stay in their flames, at their flames' size. This job finds what in OUR
lighting from a small emitter makes specks, and fixes that.

## What is measured (the MAIN tree's `artifacts\harness`, read only)

Run `20261009-004410`, `tavern-room`, NRD, 16 frames a step, the hidden setting `Advanced / Lights sized as their
emitters` true (as shipped: each lamp at its flame's centre, its flame's radius) then false (HDRP's place and radius).
A sensitive count (a pixel at least 8 levels AND 1.4 times over its own median; spots = 24-pixel cells holding one):

| | lamps at their flames | lamps at the game's place and size | the game |
|---|---|---|---|
| the picture: spots a frame (places in 16 frames) | 28.2 (156) | 14.8 (71) | 9 (11) |
| Specular view: spots a frame (places) | 67.7 (317) | 23.5 (89) | |
| the picture: 16-pixel blocks' wander | 0.062 | 0.037 | |
| pixels over 240 round a wall candle (its wax) | 388 | 6 | 0 |

That test moved PLACE and SIZE together, so it cannot say which of these it is:

1. **The wax.** `light_incident` (`pt_common.hlsli`) is a point at the flame with `1 / max(d^2, radius^2 + 1e-4)`: the wax
   1 to 3 cm away takes thousands of times the wall's light and is burnt white (sheet `wall-candle-emitters.jpg` in the
   run). A tiny blinding surface that reflection and bounce rays meet by chance.
2. **The lamp's own reflection.** A lamp is reflected as the sphere it is traced as (`reflected_as_sphere`, the code near
   `sum += e * max(dist * dist, l.radius * l.radius + 1e-4) / (PI * l.radius * l.radius)`, and `pt_specular`'s
   reflected lamps): radiance I / (pi r^2), met only when a lobe's ray happens to hit a ball of 1 cm. Small r: rare and huge.
3. **The highlight of a pinpoint lamp.** The lights' samples give a glossy surface its highlight; how the lamp's radius
   enters that (a sphere light's widened lobe, a floor on the roughness) decides whether a 1 cm lamp makes a
   highlight a pixel can hold or one that is caught in some frames and missed in others.
(4. Anything else you find that depends on `LIGHT_AT_EMITTER` or on a lamp's radius: list it.)

## What to do: THE TRIAL FIRST (hand it over within 30 minutes; no fixture before the game has answered)

1. One build with a SEPARATE switch for each suspect, all default off (off = today's behaviour), carried the way the
   job before did it (a hidden `Advanced` integer or spare option bits; `docs/agents/reuse-shift.md` shows how
   `Reuse follows camera shift` travels without an ABI change; say plainly what you used):
   - A: the near field alone: where a lamp at its emitter lights a surface closer than a few of its radii, the light
     that surface takes is held to what it would take at, say, 10 cm (ONLY to see whether the burnt wax is the
     specks' source; this is a diagnostic, not a fix to ship).
   - B: the reflected sphere alone: lamps are reflected with the radius HDRP gave them (or 5 times the flame's)
     while everything else keeps the flame's radius.
   - C: the highlight alone: the lights' samples evaluate their highlight as for a lamp of HDRP's radius (or 5 times).
   The plugin has both radii for a lamp (`src/TGPathTracing/SceneLights.cs`, where `LightsAsEmitters` is read): say
   how the second one reaches the shader.
2. Write into the report: the switch's name and its values, the files changed. Then STOP and say so in your final
   message: the coordinator merges, builds, and runs `tavern-room` once with each switch alone and sends the counts.
3. After the game's answer (a second job or a message): the PROPER fix for the suspect that is guilty, keeping the
   flame as the emitter: for 2 and 3 the standard way is to sample the lamp's sphere on purpose (by its solid angle)
   and weigh that against the lobe's own ray, so a small lamp's reflection is found every frame at its right
   brightness and not by luck; for 1 see PT-40 and say what you would do (the owner will not accept the lamp moved
   out of the flame, nor a special case that fakes the wax).

## Rules

- Game first: no offline fixture, no long study, before the hand-over. Compile, source checks, hand over.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"); a small GPU check only if you
  truly need one, through the `-GpuCheck` gate.
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\flame` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`.
- Do not touch filters or denoiser settings. RTXDI and NRD sources never copied or quoted.
- Never read `pt_common.hlsli`, `tracer.cpp`, `pt_specular.hlsl`, `tests/pathtracing_gpu_test.cpp` whole: grep, then a range.
- Final message: five lines at most: the switches (name, values), what each does in one line, the files changed, that
  it compiles and the source checks pass, the report's path.
