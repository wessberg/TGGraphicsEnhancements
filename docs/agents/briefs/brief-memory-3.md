# Brief: the 2 GB that come with the tracer and are in none of its figures (PT-87) - a fresh worker continues `memory2` in ITS copy

Copy (already holds the stopped worker's edits; work there): `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there.
READ FIRST, in the copy: `docs/agents/memory-2.md` (the stopped worker's report, all of it: it is short), then
`docs/agents/briefs/brief-memory-2.md` (its Rules hold for you word for word). Keep writing into that same report,
in a new section "memory3", early and after every step: it is what survives if you are stopped (the worker before
you was cut off by a usage limit in the middle of an edit of `scene_water.cpp`: CHECK THAT FILE AND `scene.cpp`,
`tracer.cpp` against the base for a half-made change before you build anything, and say what you found).

## The owner

"I very, very frequently get this [Path tracing paused: 129 textures unavailable], so I'm barely able to test PT",
"constantly exceeding our VRAM budget" (2026-10-09). "The priority is getting this under control" (2026-10-10).

## What the stopped worker found (its section 1), and why it changes the order

- Windows counts a texture of the game's a second time when the tracer opens it (measured: 5.38 MB for a 5.38 MB
  texture, 274 times of 274; the card itself holds it once). So the game's textures the tracer holds open (703 MB
  in a tavern, 776 at the beach, 1,316 in a town) take that much of Windows' budget for nothing on the card.
- NEW AND LARGER: with the tracer ready and NOTHING traced, Windows counts about 2,089 MB (1.8 to 2.3 GB) more
  than the game alone plus everything in the tracer's own memory line. What it is, is NOT MEASURED. Its guesses:
  the driver's memory for the 44 passes and the two devices; the tracer's own shared textures counted a second
  time (436 MB with nothing traced, 616 while tracing); the heaps the SDKs make.
- Together that is up to 3.4 GB of Windows' budget (13.0 to 15.2 GB on this PC) that no figure of ours explained.
  Over the budget a traced frame takes 1.5 s or tracing pauses itself. THIS is the gap to open first.

## The job, in this order

1. **What the 2 GB is, measured** (C++ only, a log line, and your own offline check): Windows' count for the
   process (the same query the memory line uses) read at each step of the tracer's start and written to the log
   with the difference a step made: before the tracer's first D3D12 object; after the device's own setup; after
   each group of passes is made (the 44 compute and ray tracing pipelines; the two reordering passes apart); after
   the tracer's own textures are made and shared; after RTXDI's, NRD's and Ray Regeneration's own memory; after
   the first traced frame. One line each, "PT memory at start: <step> +N MB (Windows counts M)". Run it offline in
   your check (the test's `TGPT_TEST_NO_AGILITY=1` mode under the 10 GB ceiling; say what that mode leaves out)
   and hand the same build to the coordinator for the game (HAND-OVER 1, within your first hour: the recipe is any
   scene's first start, the lines are the result).
2. **What can be given back of it, with the same picture**, by what step 1 shows. For each part: can it be made
   smaller, later, or not at all (pipelines made only when their pass is used: a denoiser's, a debug view's, the
   reordering passes' plain twins that never run on this card; the tracer's own shared textures made on one
   device where only one reads them; a second count avoided by closing the other device's handle once a texture
   is filled). Build what is certain, behind nothing if it cannot change the picture (say why it cannot), behind a
   hidden switch if its direction needs the game.
3. **The second count of the game's own textures** (0.7 to 1.3 GB): is there a way of holding the game's textures
   that Windows counts once, with no copy and at the game's own size (the owner's rulings: "Don't do copies",
   "Textures at the game's own size")? Read what the bridge and `texture_share.cpp` do, and Microsoft's own
   documents on shared resources and the video memory budget. Write the ways up with their cost; build nothing
   large. If the only way is the game's own device made on Direct3D 12, say so plainly, with what it would save.
4. The stopped worker's cut 1 (the chunks' free room: 20 to 113 MB): finish it only if what is in the copy is
   nearly done; else leave it clean (out, or complete) and say which. Its cuts 2 and 3 wait.
5. STOP when 1 and 2 are handed over and 3 is written.

## Rules

As `brief-memory-2.md`'s, word for word (C++ only, no shader change; never the game, a deploy, a commit, the main
tree or the game folder; the picture does not change; your own check only, under the 10 GB ceiling you never
raise; big files by grep and ranges; CRLF; the report early). And: this session's usage limit was reached twice
tonight with three workers running: write "State when stopped" into the report whenever you finish a step, so
that a cut-off loses nothing.

Final message: eight lines at most: what the 2 GB is made of (MB by part, measured where), what is given back and
how it is shown in the game, the ways for the second count with their cost, what is left in the copy of cut 1,
that it compiles and the checks pass, the report's path.
