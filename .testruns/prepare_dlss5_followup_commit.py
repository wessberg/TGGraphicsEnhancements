"""Stage-only patch for this inquiry; exclude the owner's ongoing PT edits."""
import difflib
from pathlib import Path
import subprocess

changes = {}
for name in ("BACKLOG.md", "ROUNDS.md", "docs/PROJECT-CONTEXT.md"):
    base = subprocess.check_output(["git", "show", f"HEAD:{name}"]).decode("utf-8")
    current = Path(name).read_text(encoding="utf-8")
    if name == "BACKLOG.md":
        old = "  Owner assigns implementation elsewhere. SDK access, D3D12 contract and runtime release rights unconfirmed."
        new = "  Owner accepts a player-supplied NR DLL; finish research here. Public NGX; caller restrictions unresolved."
    elif name == "ROUNDS.md":
        old = next(line for line in base.splitlines() if line.startswith("## [Round 7]"))
        new = next(line for line in current.splitlines() if line.startswith("## [Round 7]"))
    else:
        old = (
            "  310.9.1 lacks a public NR API; NVIDIA's sample requires a beta NGX SDK. Direct bridge NGX spec: `docs/DLSS5-SPEC.md`.\n"
            "  SDK access, D3D12 contract and NR release rights unconfirmed. First NGX paths and post-NR FG capture need coordination."
        )
        new = (
            "  Public NGX can build an NR adapter without beta helpers; owner accepts a supplied model DLL. `docs/DLSS5-SPEC.md`.\n"
            "  `docs/DLSS5-RESEARCH.md`: community caller workarounds not cleared; normal NGX unmeasured. First paths/FG capture matter."
        )
    assert base.count(old) == 1, (name, "base mismatch")
    assert new in current, (name, "worktree mismatch")
    changes[name] = (base, base.replace(old, new, 1))

patch = "".join(
    "".join(difflib.unified_diff(old.splitlines(keepends=True), new.splitlines(keepends=True), fromfile=f"a/{name}", tofile=f"b/{name}"))
    for name, (old, new) in changes.items()
)
Path(".testruns/dlss5-followup-docs.patch").write_bytes(patch.encode("utf-8"))
print(patch)
