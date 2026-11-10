# Development documentation

[BACKLOG.md](../BACKLOG.md) holds priorities. [PT-STATE.md](PT-STATE.md) separates the implemented renderer
from pending checks and unmerged trials. [PT-REQUESTS.md](PT-REQUESTS.md) preserves issue IDs;
[PROJECT-CONTEXT.md](PROJECT-CONTEXT.md) records integration lessons and failed approaches.
Update these when facts change. Session notes record results and evidence, not conversations.

## Local historical archive

The pre-cleanup working tree was copied to `.development/history/2026-11-10-797a1d3/files/` on 2026-11-10.
All 1,185 files were verified with SHA-256 before originals were removed. The sibling `manifest.json` records
original paths, sizes and hashes. `.development/history/CURRENT` identifies this archive.
The archive is ignored by Git and excluded from public source exports. Back it up separately when moving machines.
It contains complete session notes, request discussions, research reports, scratch source, logs and test images.
SDK downloads and ignored build output are not included.

```powershell
python tools/maintenance/development_history.py verify
rg -n "PT-109|cluster|arena" .development/history/2026-11-10-797a1d3/files/docs
```

Historical paths such as `docs/agents/psr-2.md` in source comments refer to this archive's `files/` directory.
Read current status first: historical reports may propose defaults later rejected or superseded.
The archived `docs/PT-REQUESTS.md` retains every issue's full evidence. `docs/rounds/round-20.md`, section
`State at 12:30`, is the final active PT handoff; round 21 records the later Unity break and tabled status.
Never reactivate work solely from an old starter prompt.

The `megageo-7.md` and `framelate.md` reports were already absent from the repository and their documented
temporary directories at cleanup. Surviving summaries are in round 20 and PT-STATE. Their missing
implementations and game validation must not be assumed recoverable.

Superseded SDKs, two obsolete build trees and extracted downloads are retained separately under
`.development/retired/2026-11-10/`; its `move-manifest.json` records the original paths. These generated
caches do not define current dependencies. The original 1,185-file history archive remains unchanged.

## Public source

See [PUBLIC-SOURCE.md](PUBLIC-SOURCE.md) for export and Git-history limitations. A public clone contains the concise engineering notes; the archive stays local. Keep essential constraints in public notes so new contributors
can work without private history. New issue entries need a symptom/requirement, evidence, status and next action.
