# AGENTS.md - TG Frame Generation, TG Better Upscaling, TG Path Tracing and TG Graphics Bridge

Mods for Tainted Grail: The Fall of Avalon (FoA): Unity 6000.0.64f1, HDRP 17, rendering with Direct3D 11. Two game
versions: the Mono branch with BepInEx 5 (players call it the "Mono version") and the standard IL2CPP one with BepInEx 6
(the "normal version"). Repo: github.com/wessberg/TGFrameGeneration, working branch `fg-input-orientation`.

## START HERE, EVERY SESSION - read these, in this order

1. **`BACKLOG.md`** - the goal, the owner's rulings, the open defects in priority order, the queue.
2. **`docs/PROJECT-CONTEXT.md`** - read the section for the mod you will touch and **DEAD ENDS** before you change
   code or ask the owner to test anything. Load other sections only when relevant.
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

| Mod | What it is for | State (2026-09-28) |
|---|---|---|
| TG Frame Generation (TGFG) | Native DLSS, FSR and XeSS frame generation, with Reflex, XeLL or Anti-Lag 2. No OptiScaler, no wrapper. | 1.2.0, released on Nexus |
| TG Better Upscaling (TGBU) | FSR and XeSS in the game's own Upscaling row, vendor quality modes, a newer DLSS DLL. Hosts DLSS Ray Reconstruction for TGPT. | 1.2.0, released on Nexus |
| TG Path Tracing (TGPT) | "Proper ReSTIR path tracing" (owner, 2026-09-25): everything on screen lit by the tracer. Mono only. | Prototype. **The current priority.** |
| TG Graphics Bridge (TGB) | The shared D3D11-to-D3D12 bridge. Bundled inside each mod, never a download of its own. | 1.2.0 |

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
- Keep claims apart: "built", "passes the harness", "seen in game" and "the owner says it's fixed" are four claims.
- Check before you blame. A mod runs only if `BepInEx/LogOutput.log` has its `Loading [` line (Vortex leaves
  config-only folders behind). A theory is true when a number shows it. Two blames had to be taken back (TG Global
  Illumination on 2026-09-26 and RenoDX on 2026-09-27: neither was installed).
- Text for players (Nexus pages, tooltips, README) is the owner's voice: plain, first person. Nexus pages are BBCode,
  which the owner calls "markdown". Run a no-AI-slop pass on README and Nexus text (Claude Code: the `no-ai-slop` skill).
  Details: PROJECT-CONTEXT "Writing for players".

## The owner's rulings - hard rules

Path tracing:
- **Full or nothing** (2026-09-26): "Don't waste energy on anything else than 'Full' mode", "drop the GI option for
  good ... Full or nothing". The row is Off / On. Never a hybrid or GI mode, nor options only such a mode needs.
- **No HDRP shading while tracing** (2026-09-28): "not to keep shading of anything in HDRP when PT is on", and "A
  solution to black eyes or weird shadows on hair isn't to exclude it from path tracing". Hair moved into the tracer on
  2026-09-28; the eyes are next. Hair's transparent layer, eyebrows and lashes, transparents and particles, and water's
  own shading are HDRP-shaded too: raise those with the owner before designing them.
- **No fake lights** (2026-09-26): "no fake lights, no fake bounce lights, no fake light volumes". Only lights that
  stand for something that emits light (a flame, candle, lamp, magic) are traced. Fill lights light no surface.
- **Keep the game's fog** (2026-09-27): "fog shouldn't go away just because people activate PT. It's core to the
  game's art style". Fill lights keep lighting the fog (2026-09-28): "these are just as much artist-placed fog volumes
  as they are artist-placed lights... allow the fog-part of them while preventing the light-part" (Fill Light Fog row).
  Never `Add<T>(true)` on an HDRP Volume: it resets every parameter to HDRP's defaults (a source check scans for it).
- **Denoising** lists None, NRD, Ray Regeneration and Ray Reconstruction, only what the PC runs, starting on the best
  (RR, then RRG, then NRD). "don't suggest the built-in denoiser" (2026-09-27). "Ray Reconstruction is essential for
  good PT, and it IS compatible with DLSS 5" (2026-09-26): never hide or demote RR because RenoDX's DLSS 5 is loaded.
- **Reflection Boost** (default 0.1) is for the world's stone and brick AND for armour and metal (2026-09-28): never
  exclude whole characters. Skin is exempt (a glossy neck mirrored the chin).

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
- **The TGPT harness never runs Ray Reconstruction.** A change on the RR path goes through `tgup_gpu_smoke_with_pt`,
  then the F8 status line in game.
- **A passing test must be mutation-checked**: break the thing on purpose and watch the test fail. Grade noise
  against an **undenoised** reference.
- **After a scripted edit, check every hunk landed.** A script with two blocks for one file dropped the first, and
  Spatial Samples above 8 never reached `tracer.cpp` for a day.
- **BepInEx config section and key names may not contain `= \n \t \ " ' [ ]`.** One apostrophe killed TGFG with no log
  line (source check `test_config_keys_are_valid_for_bepinex`).
- **Never touch a game Setting type with a localizing static constructor (UpScaling) at plugin load.** It hung the
  Mono game and broke loading under IL2CPP. Get the game's own instance first.
- **Hooks on global functions run on driver threads with tiny stacks**: no large stack arrays (a 64 KB one crashed).
- **One signalling queue per fence** (a fence both D3D11 and D3D12 signalled ran backwards).
- **The Bash tool eats backslashes**: write code with backslashes through Write/Edit or a script file. Sources are
  CRLF in the working tree: Git Bash `sed -i` rewrites them as LF; count bytes with PowerShell.

## Build, deploy, test

The SDKs sit gitignored in the repo root (`README.md` "Building" lists them). Close the game before deploying.

- **Native**, in the VS 2022 x64 environment:
  `"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"`.
  `build/native` is an NMake Release tree (its configure line: PROJECT-CONTEXT "Build recipes"). Run
  `cmake --build build\native`, then `cmake --install build\native --prefix artifacts\Native --component TGNative`,
  then `ctest --test-dir build\native -C Release --output-on-failure` (16 tests; `-R tgpt_gpu_test` for the tracer,
  its PNGs in `build/native/tgpt-images`). After a manual build, run `tools\Write-NativeManifest.ps1` before
  packaging. The scripted route: `Build-Native.ps1` (README).
- **Source checks:** `python tests/source_checks.py` (71 checks). Run them before any hand-off.
- **Managed, package, deploy:** `.\Build.ps1 -Runtime Mono -Deploy` (`-Mod TGPathTracing` for one mod; TGPT is Mono
  only; `-Runtime IL2CPP` for the normal version). It backs up to `BepInEx/<Mod>/Backups` and writes the Nexus zips
  to `artifacts/Nexus`.
- **When to deploy:** after each round of fixes the owner will test ("Can you please deploy the files manually to the
  game dir every time you have a fresh build?", 2026-09-26). Not while they edit the tree in parallel ("Don't build and
  deploy, no, we're currently iterating on the files concurrently", 2026-09-27): then only on request, and compile
  checks go to `bin/` and a scratch install prefix, never `artifacts/`. After a deploy, hash-check, and tell them
  Vortex will call the files changed and must not revert them.
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
| `ROUNDS.md` + `docs/rounds/round-NN.md` | one row per round; the narratives and starter prompts |
| `README.md` | what each mod does, its rows, building, diagnostics |
| `CHANGELOG.md` | every change with its reason and harness numbers: the "why" behind a setting |
| `docs/ARCHITECTURE.md` | native ownership, the GPU handoff, frame generation and upscaling internals |
| `docs/TESTING.md` | offline checks and the in-game test sequence for each mod |
| `docs/SOURCES.md` | primary sources, SDK versions and their license terms |
| `docs/AUTOLOAD-INTEGRATION.md` | the preloader election and the installed layout |
| `docs/FSR-FLICKER-INVESTIGATION.md` | the resolved HUD and hand flicker; read it before touching FG inputs |
| `docs/nexus.md`, `docs/nexus-upscaling.md` | Nexus page drafts (BBCode) |
| `tests/source_checks.py` | the textual rules the build enforces |

PROJECT-CONTEXT carries the ~700-line budget: condense or archive in the same edit that adds. `CLAUDE.md` only imports
this file (Codex reads `AGENTS.md` itself). Never put a rule in `CLAUDE.md`.

## Subagents and reviews

- **Subagents only when the owner asks** (2026-09-28; earlier that day: "don't parallelize into subagents this time").
  When they ask, use PROJECT-CONTEXT "Parallel agents": scratch copies, preassigned option and material bits, merges
  against a base snapshot. Brief each agent to follow this file and to write its findings to a tracked file. GPU
  timings mean nothing while agents run GPU tests.
- **Reviews, when the owner asks for one:** GPT-6-astra through Codex (`codex exec -m gpt-6-astra -s read-only -c
  model_reasoning_effort=xhigh [-i image] -o review.md - < prompt.md`), or Claude Code's `pt-max-reviewer` agent (Opus,
  effort max, read-only). Reviews have found real bugs (a fence that ran backwards; dark hair and eyes). Test a finding
  before you accept it.
- A caustics research agent, started 2026-09-28, writes `docs/CAUSTICS-RESEARCH.md`.
