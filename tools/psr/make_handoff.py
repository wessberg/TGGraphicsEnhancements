"""The worker psr-2's hand-over: every file of the copy that differs from the pristine base, found by comparing the two
trees (no list kept by hand, no Git), as one review patch and a list with sizes and SHA-256.

    python tools/psr/make_handoff.py

Writes %TEMP%/tgb/psr-build/psr2.patch and psr2-source-manifest.json. The first worker's psr-checkpoint.patch beside them
stays as it is: it holds the Ray Reconstruction guide changes this build left out (docs/agents/psr-2.md, section c).
"""
from pathlib import Path
import difflib
import hashlib
import json

root = Path(__file__).resolve().parents[2]
base = root.parent / "base20pc"
out = Path("C:/Users/Frede/AppData/Local/Temp/tgb/psr-build")
# Where sources live. Build outputs, the SDKs' folders and caches are no part of a hand-over.
places = ["CMakeLists.txt", "BACKLOG.md", "CHANGELOG.md", "ROUNDS.md", "AGENTS.md", "README.md", "Build.ps1", "Package.ps1",
          "docs", "native", "src", "tests", "tools"]
skipped = {"bin", "obj", "__pycache__", ".vs", "artifacts", "build"}


def files_under(tree):
    found = set()
    for place in places:
        start = tree / place
        if start.is_file():
            found.add(place)
        elif start.is_dir():
            for path in start.rglob("*"):
                relative = path.relative_to(tree)
                if path.is_file() and not (set(relative.parts) & skipped):
                    found.add(relative.as_posix())
    return found


def text_of(path):
    try:
        return path.read_bytes().decode("utf-8")
    except UnicodeDecodeError:
        return None


ours, theirs = files_under(root), files_under(base)
patch, manifest, removed = [], [], sorted(theirs - ours)
for name in sorted(ours):
    current, prior = root / name, base / name
    data = current.read_bytes()
    if prior.exists() and prior.read_bytes() == data:
        continue
    after = text_of(current)
    before = text_of(prior) if prior.exists() else ""
    if after is not None and before is not None:
        patch.extend(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
                                          fromfile=f"a/{name}" if prior.exists() else "/dev/null", tofile=f"b/{name}"))
    manifest.append({"path": name, "new": not prior.exists(), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
(out / "psr2.patch").write_text("".join(patch), encoding="utf-8", newline="\n")
(out / "psr2-source-manifest.json").write_text(json.dumps({"changed": manifest, "removed": removed}, indent=2) + "\n", encoding="utf-8")
for entry in manifest:
    print(("new      " if entry["new"] else "changed  ") + entry["path"])
for name in removed:
    print("removed  " + name)
print(f"{len(manifest)} files differ from the base, {len(removed)} removed; the patch and the list are in {out}")
