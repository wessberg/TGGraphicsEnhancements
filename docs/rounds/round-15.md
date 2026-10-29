# Round15 - 2026-10-08 - Remaining NR validation

Owner: "OK, do all the other validation testing then". NR-28 and DLSS5-VALIDATION.md track the matrix.
No extra temporal filter, no retuning Default0/Quality/full/one-pass/maskOff/exposure1. No release
approval, package, commit or push. NR-14 walking-light instability remains a release blocker.

Both managed builds and isolated native build complete. Initial source95 passes. An intermediate shared
tree had95/96, concurrent PT NRD assertion source_checks.py1933. Latest source96 passes after PT updates.
CPU policies plus10 faults, install/autoload12/package98, normal option routing and diagnostic analyses pass.
Two stale mutation search strings corrected. GPU mutation cases refreshed for diagnostics header,
current HDR conditioning and exact-neutral bypass. All4 broken adapters rejected. Real-runtime neutral,
alpha/input/stage/resize/Off/bad-region checks pass. Four signed-motion corruptions rejected.
Multipass1..4 at strength0.5/1 exactly matches independent calls,3 broken chains fail. Shared Streamline,
PT module, RR32/NR30 synthetic coexistence passes. Style3 evaluates but equals2 on this fixture, not proof
of separate weights for every number. Current analyses accept archived fixed motion/neutral/HDR captures
and reject the corresponding pre-fix defects. Full commands/logs/setup failures: DLSS5-VALIDATION.md.

FoA Mod Manager0.6.63 installed and Loading line confirmed. Actual metadata-reader tests pass. New
optional harness audit opens NR, reads actual catalogue, captures menu, checks rows/labels/visibility,
restores selection/open state. Hidden entries are omitted, which the inspector initially missed.
Passive FG status and bridge memory snapshots added to NR capture metadata. No rendering effect.

Live attempt192702 never starts because Steam controller-config syncfailed. Owner resolves it.
Retry194932 captures all5 menus, inspector fails on hidden rows. Fixed inspector and expanded Video.
Final195305 passes all5 states,0 report problems,117 saves/configs restored. Expanded menu inspected.
Manager warning: missing OPTIONAL Avalon manager, not an NR error. No need to install Avalon for NR.

Tested NR pair installed19:49, only NR files replaced. Build/install/game hashes checked:
native83C681B8F6309D08DBB3C8D378013F309A106436A20C1A6BAF50C4823A9590EA,
MonoDDBE0B1415E676762CFDE60B13EA4E3A6C05B1ECC75D0F7A538F2DFD467BBE39.
Backup BepInEx/TGNeuralRendering/Backups/20261008-174932-619-tuning. No shared/PT deployment.
Owner told to retain Vortex changes. No defaults changed.

## Live checks and remaining gaps

Standalone .testruns/nr/validate-standalone.ps1 holds the shared lock while temporarily moving other
mods' plugin, bundled managed bridge and autoloader, then restores/hash-checks all in finally.
Journal build/nr/validation-r15/standalone-files.json. Normal harness has opt-in KeepLock for this.
Standalone195951 passes4 takes,0 report problems,117 saves/configs preserved. Actual plugins and native
PID39900 confirm only NR/bridge among our graphics mods. NR evaluations0/5/5/0. Overview/full atlas inspected.
All9 staged files restored and hash-checked, lock released afterward.
FG200317 passes8 takes. DLSS+NR14 input/28 vendor output frames, XeSS16/32. FSR14 successful dispatches,
active backend2 and no errors. FSR has no measured output count in this API. NR evaluations0/16/16/0/16/16/16/0.
Six corrupt capture records rejected by the new FG evidence check. Overview inspected,117 saves/configs restored.
Cost201117:15-second4K tavern intervals83.7/32.4/86.1fps Off/On/Off, process VRAM5.13/6.80/5.19GiB.
No other GPU workload, PT/FG Off. GPU eval sample11.32ms is only part of the19ms added real-frame cost.
Lifecycle201643 passes two scenarios. Actual Video grey state1/.4/1 on DLSS/STP/DLSS, NR stops/resumes.
Output1440p/4K,3m camera cut/return, one reset per change and none during held intervals. Reload resumes4K NR.
Six corrupt records rejected, screenshots inspected,117 saves/configs preserved. Screen mode restored too.
Harness audit restores in finally and Runner.Finish, optional and never part of normal PT recipes.
Final-combinations202304 passes11 takes: real FSR/XeSS upscaling with NR Off, FoA Off-only/explanation,
automatic DLSS return, full-resolution DLAA guides. FG with NR real fps35.86DLSS/15.73FSR/33.60XeSS.
DLSS FG without NR88.97fps. FSR performance concern recorded immediately as NR-29.
Focused202741 FSR FG with NR Off/On/Off:18.78/15.99/18.48fps. Most cost exists while NR is disabled,
broader FG issue, not proved with NR plugin absent. Zero errors/competing GPU work. Both runs preserve117
saves/configs and quit. Final NR hashes match tested build/install/game, no remaining owned process/lock.
Plans: build/nr/validation-r15. No new rendering/default code.
Starts only through Run-PtHarness and its GPU gate. PT agents repeatedly own the shared slot, respect it.
Normal-version game installation absent and ReShade owner-disabled, those runs remain setup-dependent.
Asked for normal-version path and owner-enabled ReShade/RenoDX HDR with its own NR Off. No reply yet.
PT-72 own-geometry guides remain a contract conflict, so current PT scene combinations wait for that fix.

## ▶ Starter prompt (R16)

Continue NR-28 from docs/DLSS5-VALIDATION.md. Completed available offline/Mono checks, no owned job remains,
all standalone staging restored. Remaining normal runtime/ReShade setup questions are pending. PT scenes
need PT-72 first. NR-29 is slow FSR FG even with NR Off, not a proven NR-specific regression. Do not repeat
completed suites. Keep approved settings, no temporal filter, no release claim while NR-14 remains open.
