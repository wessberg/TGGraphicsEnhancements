# R23: documentation edit, 2026-11-10

## Changes

Renamed the README project heading to TGGraphicsEnhancements and shortened it from 549 to 57 lines.
Status now covers Frame Generation, Better Upscaling, Neural Rendering, the bridge and paused Path Tracing.
Installation, settings and build details moved to linked guides. The obsolete PT description was replaced by
the current status link.

Edited the public docs to remove filler, promotional wording, repeated qualifications and conversational
asides. Technical names, measurements, issue IDs, code blocks and validation limits were retained. Corrected
the stale DLSS version in the upscaling page and its FSR low-latency description using existing project notes.
The source check for documented limits now includes the linked build guide.

Rendering code, installed game files and player packages were unchanged. PT remains paused. The original
history archive was untouched; the pre-edit docs are saved in `.development/editing/no-ai-slop-r23/`.

## Validation

121 source checks and the public-source audit pass. All 1,185 original archive hashes still match.
Links and anchors resolve across 42 Markdown documents. Command/code blocks in the 32 pre-edit documents
match their saved originals; README details were moved into the new guides. No game or GPU run.

## Next session (R24)

Read the current backlog and PT status before choosing work. Keep the README brief and project-wide;
update the linked guides when details change. Publication and Git-history cleanup remain separate work.
Resume PT implementation only when requested, starting with its Unity compatibility blocker.
