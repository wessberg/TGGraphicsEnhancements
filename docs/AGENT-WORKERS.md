# Workers: how to run several agents on this repo at once

For the coordinator (the agent the owner talks to). The rules are in `AGENTS.md` "Subagents and reviews"; this page is
the how. Tools: `tools/agents/`. Learned on 2026-10-06 and 10-07 with up to six workers on TG Path Tracing.

## The two kinds of worker

| | Claude subagent | GPT-6-astra through Codex |
|---|---|---|
| Started with | the coordinator's Agent tool | `tools\agents\Start-CodexWorker.ps1` (in the background) |
| Costs | the owner's Claude tokens | the owner's Codex plan (the owner, 2026-10-07: "so you don't waste claude tokens") |
| At once | at most 3 (the owner's rule) | this page's reading: 6 workers in all, of which at most 3 Claude (four Codex ran alone after Claude's limit hit) |
| Can be told something while it runs | yes, a short message | no: everything is in its brief; to change course, stop and restart |
| Permissions | the session's own | never asks; code jobs run without a sandbox (the owner's choice) |
| Reasoning | the agent type's | xhigh, always (the owner: "I want them at xhigh reasoning") |

Use Codex workers for jobs that would otherwise wait for a Claude slot, for merges onto a newer tree, and for designs
and research (read-only). A reviewer is another thing (`AGENTS.md`: `codex exec -s read-only ... -o review.md`).

## The routine

1. **One job per worker, written as a brief** (a Markdown file; the template is below). If you cannot write it
   clearly, the job is not understood well enough to hand off.
2. **A copy of the tree per code worker:**
   `.\tools\agents\New-TreeCopies.ps1 -Names water,glass -BaseName base01 -Root <folder>`.
   It makes `<Root>\wt\water`, `<Root>\wt\glass` and the pristine `<Root>\wt\base01` the merge compares against. The
   SDK folders are junctions, nothing of an SDK is copied. `-Root` is any folder outside the repo (default
   `%TEMP%\tg-agents`); a session's scratch folder works, and is gone when the session's files are cleaned up.
   Builds go to `%TEMP%\tgb\<name>-...`, never `build/` or `artifacts/`.
   **When Windows sweeps the copies** (2026-10-12 03:28): the owner's PC has Storage Sense on, and with the disk nearly
   full (98 % that night) it deletes every file under `%TEMP%` that was not written for about six days. A copied
   file keeps its source's write time, so every copy and every pristine base lost its old files (138 a tree), its
   SDK junctions and most of `artifacts\Native` (the Agility runtime: an offline GPU test without `D3D12Core.dll`
   runs with no DirectX Raytracing 1.2 and proves nothing). The main tree and the SDK folders were whole. Since that
   day `New-TreeCopies.ps1` gives a copy's old files a write time of a day ago, and
   `python tools\agents\repair_copies.py [--dry] <copy or base> ...` puts back what a sweep took (only files the
   main tree has not changed since the copy was made). Signs of a sweep: a configure or build that misses a file
   nobody touched; a copy without its SDK folders; a merge preview with files "new" that are old. A worker's own
   folders under `%TEMP%\tgb` (a test stage's runtime files, package caches) are swept the same way: copy again.
   `%TEMP%\tgb` held 145 GB of finished workers' builds that morning (1,523 folders since 10-06). The owner,
   2026-10-12: "delete all the old ones, and make sure our Clear-PtHarnessRuns.ps1 tool also does that". So
   `tools\harness\Clear-PtHarnessRuns.ps1 -Apply` deletes what nothing was written to for 2 days there
   (`-WorkerDays`), and a worker's evidence that must outlive that goes into its report or the main tree's docs,
   never only into `%TEMP%\tgb`. The tree copies (`wt`) stay: `New-TreeCopies.ps1 -Remove` takes one away.
3. **Start it.**
   - Codex, a code job: `.\tools\agents\Start-CodexWorker.ps1 -Name water -Brief <brief.md> -Base base01 -Root <folder>`
     as a background command (it returns when the worker ends, hours later).
   - Codex, a design or research job: add `-ReadOnly`. It reads the main tree, writes nothing, and its last message
     (`<Root>\codex\<name>\last.md`) is the document: file it under `docs/agents/`.
   - Claude: the Agent tool with a short prompt that points at the brief file and repeats the hard rules.
4. **Check the start** (30 seconds in): the first lines of `<Root>\codex\<name>\stdout.log` name the model, the sandbox
   and the reasoning effort. Expect `model: gpt-6-astra`, `sandbox: danger-full-access` (a code job) or `read-only`,
   `approval: never`, `reasoning effort: xhigh`.
5. **While it runs:** its report (the file the brief names, inside its copy) is how you see where it is. Read that, not
   the console. Never read a Claude subagent's transcript file. Tell the owner what each worker does in one line each.
6. **When it ends:** read the last message and the report. Then
   `python tools/agents/merge_copy.py <name> --base=<base> --root=<folder> --dry` lists what it changed; without
   `--dry` it merges into the working tree (three-way, line endings evened out, deletions moved to
   `<Root>\removed-by-merge`). Resolve conflicts so both sides stay; numbers pinned in several files (the ABI, struct
   sizes, test lists) by hand. Read each merged diff. Then source checks, build, GPU test, deploy, one in-game scene.
7. **One merge at a time,** never during a queue's build or GPU test (PROJECT-CONTEXT 13). When a merge would meet
   many conflicts (20 and more happened), give the merge itself to a fresh worker: a new copy of today's tree, the
   finished copy and its base to port from, build and test there.

## Permissions (Codex)

- `codex exec` runs with `approval: never`. It never prompts. What its sandbox forbids simply fails.
- The `workspace-write` sandbox cannot build this repo: it refused to run CMake, which is installed in the user's
  profile ("Access is denied"), so no native build and no GPU test (2026-10-07 18:05; the C# plugin did compile, and
  `Run-PtHarness.ps1 -GpuCheck` did run). The owner chose "Full access" (18:12): code workers run with
  `-s danger-full-access`, as Claude subagents already run. Never `--dangerously-bypass-approvals-and-sandbox`.
- So the rules a sandbox would enforce are in the prompt the launcher writes: write only in the copy and its build
  folders; never the main tree, the game folder, the game, a deploy, a commit, an install; never stop another's process.
  Check after a job that the main tree holds nothing you did not merge (`git status` against what you know).
- Proof it works: a full-access worker configured and built `TGPathTracing.Native` and `tgpt_gpu_test` two minutes
  after its start (2026-10-07 18:14, `%TEMP%\tgb\glass-build`).
- To check the launcher itself (5 seconds): a brief that says "Read no file and run no command. Reply with exactly:
  WORKER LAUNCHER OK", started with `-ReadOnly -Effort low`; `last.md` then holds that line (done 2026-10-07 18:18).
- Removing a copy: `New-TreeCopies.ps1 -Remove <names>`, never a plain recursive delete (a copy's SDK folders are
  junctions into the real ones).

## What a brief holds (the template)

```markdown
# Brief: <the job in one line> (<request IDs>) - one agent, one job

Copy: `<Root>\wt\COPYNAME` (pristine base: `<Root>\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings"
first (<name the three or four that bear on this job>). Report: `docs/agents/<name>.md`; write it early (the design
before the code) and keep it current.

## Why
<the owner's words that ask for it; what is wrong today, with file names and numbers; where the evidence is>

## What to do
<numbered; what is NOT this worker's (other workers' files and jobs)>

## How it is judged
<the offline checks to add (judged with no denoiser); the ONE in-game scene and recipe per part that the coordinator
will run, and what its picture and log must show. While the migration runs (AGENTS.md "test what catches defects and
nothing more"): build and run the job's own new checks (a fixture or an `..._ONLY` mode, minutes). NEVER the whole
GPU test or the whole of ctest in a worker (the owner, 2026-10-08: "full test suite never in subagents"): that is
the coordinator's, in the main tree, once the thing looks right in the game. No mutation checks until then>

## Clean as you go
<what the job makes dead is deleted in the job; the report lists every workaround met, removed or not>

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `<main tree>\artifacts\harness\RUNNING` is absent or its process gone, and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one background
  script, never by polling. Build into `%TEMP%\tgb\COPYNAME-...`.
- Files of other sessions and workers that must not be touched: <list>. The coordinator's own files are not yours
  either: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`. Everything you
  learned goes into your report; the coordinator files it.
- Keep tokens down: grep and read ranges of the big files, never whole. Stop when done; ideas go in the report.
- Bits and numbers: <which option, material or instance bits are this worker's; "say if an export or struct changes">.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
```

COPYNAME and BASENAME are filled in by `Start-CodexWorker.ps1`. Today's briefs are the examples: `docs/agents/briefs/`.

## What went wrong, so it does not again

- **A worker with a long history is never given a second job** (each call pays for its whole history): a fresh one,
  with the last one's report. After a usage-limit stop, stop the old agent before a finisher starts in its copy.
- **Workers waited on each other for the GPU**: `-GpuCheck` lets 4 offline tests run side by side, and refuses while
  the game itself runs.
- **Two workers moved the same code into a file of the same name** (the wave functions into `pt_waves.hlsli`): when a
  second worker will touch what an unmerged one moved, point it at the first one's copy and tell it to use the same
  file and names.
- **A merge tool that does not handle deletions leaves dead files behind** (`pt_hair.hlsl`): `merge_copy.py` does.
- **Copies and the tree differ in line endings** for some files: compare and merge with them evened out
  (`merge_copy.py`), never with a plain diff.
- **Many copies in flight mean big merges.** Order the merges, and keep each job's files apart in the briefs.
- **A stopped Codex worker leaves unbuilt edits in its copy**: list them with `merge_copy.py --dry` and tell the
  restarted worker (`-Note`) to check them against the base first.
- **A copy's SDK folders are the real ones** (junctions): NRD's configure step writes `Shaders/NRDConfig.hlsli` into
  its SDK folder, so a worker that must not write outside its copy builds with `-DTGPT_WITH_NRD=OFF` and judges with
  Denoising None (two Codex workers found this on their own, 2026-10-07). The NRD path is then tested only in the
  coordinator's merged build: say so in the brief, and run that build's GPU test before installing.
- **Workers write their notes into their report, not into the coordinator's files.** A worker added lines to
  `BACKLOG.md`, the round file, `PT-REQUESTS.md` and `PROJECT-CONTEXT.md` (one deleted an unrelated line there). Before
  merging, put the base's version of those four back into the copy (they then merge as unchanged) and write the
  coordinator's own entry. Briefs say so (template, "Working rules").
- **.NET's first-run banner** in a worker ("Installed an ASP.NET Core HTTPS development certificate") came from a
  redirected `DOTNET_CLI_HOME`; the user's store already held that certificate (since 2026-04-01), so nothing new. The
  launcher now sets `DOTNET_GENERATE_ASPNET_CERTIFICATE=false` and `DOTNET_NOLOGO=1` for workers.
- **Claude's usage limit stops every Claude agent at once, and the coordinator** (2026-10-07 20:15: three agents
  died mid-work, HTTP 429). What survives: every edit (files in the copies), every report written so far, the test
  scripts an agent had already started (they run to their end as plain processes), and all Codex workers. What is
  lost: the agents' final messages and anything only in their heads. So: reports early and current (the brief says
  so; it paid off: two dead agents' work could be merged from their reports), and prefer Codex workers for long jobs
  so that a Claude limit stops only the coordinating. After a stop: read each copy's report and `merge_copy.py
  --dry`, wait for its leftover scripts to end, check no mutant is left applied, then merge or start a finisher.
- **A dead or finished agent leaves waiting shells behind** (`until [ -f ... ]; do sleep 10; done` loops for files
  that never come: seven were found open 17 hours later; the owner saw them as open shells). When an agent ends,
  list the session's `bash.exe` children and stop the wait loops whose agent is gone.
- **A worker that cannot get a GPU slot must keep waiting, not give up**: a Codex worker stopped "partly done" after
  90 minutes without a slot. Briefs say: wait with one background script; never end the job untested for that.
- **Tell every running worker of a new ruling at once** (a Claude subagent by message; a Codex worker only by stopping
  and restarting, so put standing rules in the brief from the start).
- **`merge_copy.py` always writes into the main tree.** `--work=<folder>` is where it keeps its temporary files, not
  another destination (the coordinator took it for one on 2026-10-08 02:55 and merged a copy into main while main
  was being built: the build stopped on the conflict markers, nothing was installed). So: only `--dry` while a build
  of main runs, and a real merge only when no build, deploy or harness build step is reading the sources.
- **A whole GPU run with failed frames is void, not a result** (2026-10-08 01:00 to 01:30: three and four whole runs
  side by side, each compiling its passes, ended with 93, 86 and 43 FAIL lines, among them checks that pass alone).
  The log says so: `totals: frames traced N, failed M` with M above 0, `last error 0x887A0006`, "Interop fence
  timeout" in the native log, and "video memory near this card's budget". Run it again; never read its FAIL lines as
  the code's. Focused checks (a job's own `..._ONLY` mode) are short and survive sharing. Since 2026-10-08 18:00 no
  worker runs the whole test at all (the owner: "full test suite never in subagents"); only the coordinator does,
  with the card to itself.
- **Two workers wrote the same helper two ways** (the test's start-up wait, `TGPT_TEST_STARTUP_SECONDS`, by the candle
  and the materials workers): the second merge conflicts with the first in the test and in its source check. Tell
  workers of a shared annoyance once it is known, and name the one who fixes it.
- **Two jobs took the same spare field of a shared record** (`GeometryRecord`'s third word: decals put a plant group's
  rendering layers there, the glass job its "closed" mark): a three-way merge of text cannot see it, the build does.
  A brief that lets a worker use a record's padding says which word.
- **A native build took an hour** (2026-10-08: NMake compiles the tracer's 43 shader headers one at a time, and three
  trees were building at once). `tools\Build-PtShaders.ps1 -BuildDir <tree> -Throttle 8` first, then `cmake --build`:
  10.5 minutes for the shaders. Every brief says so; a running Claude agent can be told by message.
- **A new native build's first start compiles its passes for minutes** (215 s in the game on 2026-10-08): the harness
  now waits for that; a worker's GPU test that fails only at "the passes were made" runs once more (the second run
  finds them cached).
