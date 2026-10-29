# Brief: context-condense - `docs/PROJECT-CONTEXT.md` back under its budget, nothing lost (a docs job: no build, no GPU)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-12 ~01:30). Follow `AGENTS.md` in your copy: its
"How to talk to the owner" is also how these files are written (short sentences, plain words, numbers and file paths
welcome, no slogans), and "This harness is the only memory". Report: `docs/agents/context-condense.md` IN YOUR COPY, short:
what moved where, what was rewritten, what you were not sure of.

## Why

`AGENTS.md`: "PROJECT-CONTEXT carries the ~700-line budget: condense or archive in the same edit that adds." It has 975
lines. Every agent reads its sections at the start of a session: what is stale there misleads, what is long costs.

## What to do

1. MOVE, word for word, what a session on TG Path Tracing does not need: the sections on the released mods (5 TG Frame
   Generation, 6 TG Better Upscaling, 7 IL2CPP, 8 the troubleshooting report and player reports) into a new file
   `docs/PROJECT-CONTEXT-MODS.md`, leaving under each old heading two lines that say what is there and where it went.
   (Section 4, the bridge, stays: the tracer uses it.) Add the new file to the table "Where things live" in `AGENTS.md`
   and to its "START HERE" step 2 in a few words ("the released mods' sections: `docs/PROJECT-CONTEXT-MODS.md`").
2. CONDENSE section 9 (TG Path Tracing) to what is TRUE TODAY, by the code and the newest notes: `docs/rounds/round-20.md`
   (its "State at ..." blocks from the top, newest first), `docs/PT-REQUESTS.md` (PT-109, PT-110, PT-112, PT-114, PT-118,
   PT-127, PT-130 to PT-133), `CHANGELOG.md`'s first entries, and the sources where a note names a function or a
   constant (grep it: a note that names something that no longer exists is stale). Known to be stale or superseded:
   9.6 Denoisers (NRD runs as NVIDIA's README sets it up since 2026-10-11: RELAX on all the light, demodulated, real
   hit distances, history in seconds, history confidence the rule, mirrors by primary surface replacement; the old
   route that handed NRD the bounced light alone is gone), 9.13 Performance (its numbers predate rounds 19 and 20), 9.12
   (keep it exact: it was corrected on 2026-10-11 and 10-12; check every bit against `native/pathtracing/include/tgpt_api.h`
   and `src/TGPathTracing/NativeTracer.cs`), 9.1 where it describes passes or their order (the stages today: scene, sky,
   caustics, prepare, bounced light, its spatial reuse, direct light, reflections, radiance cache, denoising, the fog's
   four, composite: `tracer.cpp` `kStageNames`). What is superseded but explains WHY something is as it is moves, word
   for word, to `docs/PT-DETAILS.md` under a heading that says what it is and of which date; what is simply wrong now
   goes, with one line in your report saying so.
3. DEAD ENDS (section 10) and Method traps (11) stay whole unless an entry is contradicted by a later one: then say so
   in your report and keep both until the coordinator rules. Add the dead ends of rounds 19 and 20 if they are missing
   (each one line with its run or report): the water's own pass and regrouping in the water's loop (round 19), RTX Mega
   Geometry's clusters slower per ray and NVIDIA's layout no faster (megageo-2 to megageo-6, PT-109), the characters'
   micromaps (PT-110 M4), caustcut's exact cut of the caustics' shadow rays (PT-114), setting the game's clock back in
   the harness (it rolls the date), "the camera is under water at the camp" (PT-131).
4. THE TARGET: about 700 lines for `docs/PROJECT-CONTEXT.md`, every line true today. Count lines with PowerShell
   (`(Get-Content <file>).Count`); the files are CRLF: keep them so (never `sed -i`; write through your editor or a script
   that keeps line ends).

Touch ONLY: `docs/PROJECT-CONTEXT.md`, the new `docs/PROJECT-CONTEXT-MODS.md`, `docs/PT-DETAILS.md`, `AGENTS.md` (the two
places named in 1), your report. Not `BACKLOG.md`, not `docs/PT-REQUESTS.md`, not `docs/rounds/`, no source, no test.
Run `python tests/source_checks.py` at the end (some checks read these docs): it must pass, or say which check wants
which words and leave those words in.

## Working rules
- No build, no GPU test, no game, no deploy, no commit, no write outside your copy.
- RTXDI and NRD sources are never copied or quoted. The word the source checks ban for the mods stays banned.
- Final message: five lines at most: the line counts before and after, what moved where, what you were unsure of, the
  report's path.
