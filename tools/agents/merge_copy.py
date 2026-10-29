# SPDX-License-Identifier: MIT
"""A worker's tree copy merged back into the working tree (docs/AGENT-WORKERS.md).

  python tools/agents/merge_copy.py <copy name> --base=<base name> [--root=<folder>] [--work=<folder>] [--dry]

For every source file the worker changed against its base (line endings ignored: copies and the tree differ in them,
and a line-by-line merge then sees whole files as changed): a three-way merge of the three texts as LF, written into
the working tree with the tree's own line endings (and its BOM, if it has one). New files are copied. Files the worker
deleted are moved out of the tree into <root>/removed-by-merge/<copy> when the tree still holds the base's text, and
named when it does not. Prints what it did and every conflict; exit 1 when there is one.

--dry writes nothing: it lists what a worker left in its copy (also the way to see what a stopped worker did).
The numbers that are pinned in several files (the ABI, struct sizes, test lists) are merged by hand afterwards; read
each merged diff before building.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

MAIN = Path(__file__).resolve().parents[2]
SOURCE_DIRS = ("src", "native", "tests", "tools", "docs")
SOURCE_EXT = {".cs", ".inl", ".hlsl", ".hlsli", ".h", ".hpp", ".cpp", ".in", ".py", ".ps1", ".md", ".json", ".csproj", ".props", ".txt", ".cmake", ".cmd", ".inl", ".inc"}
SKIP_PARTS = {"bin", "obj", "__pycache__"}
ROOT_FILES = ("CHANGELOG.md", "CMakeLists.txt", "BACKLOG.md", "README.md", "AGENTS.md")


def tracked(rel):
    if len(rel.parts) == 1:
        return rel.name in ROOT_FILES
    if rel.parts[0] not in SOURCE_DIRS or any(p in SKIP_PARTS for p in rel.parts):
        return False
    return rel.suffix.lower() in SOURCE_EXT or rel.name == "CMakeLists.txt"


def load(path):
    """(text as LF without BOM, had BOM, uses CRLF)."""
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw[3:].decode("utf-8") if bom else raw.decode("utf-8")
    crlf = text.count("\r\n") > text.count("\n") - text.count("\r\n")
    return text.replace("\r\n", "\n"), bom, crlf


def files_of(tree):
    roots = [tree / d for d in SOURCE_DIRS if (tree / d).is_dir()]
    for f in [p for r in roots for p in r.rglob("*")] + [p for p in tree.iterdir() if p.is_file()]:
        if f.is_file() and tracked(f.relative_to(tree)):
            yield f.relative_to(tree)


def main():
    names = [a for a in sys.argv[1:] if not a.startswith("--")]
    options = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    dry = "--dry" in sys.argv
    if len(names) != 1 or "base" not in options:
        print(__doc__)
        return 2
    agent = names[0]
    root = Path(options.get("root") or Path(os.environ.get("TEMP", tempfile.gettempdir())) / "tg-agents")
    copy, base = root / "wt" / agent, root / "wt" / options["base"]
    work = Path(options["work"]) if "work" in options else None
    if work:
        work.mkdir(parents=True, exist_ok=True)
    for tree in (copy, base):
        if not tree.is_dir():
            print(f"no tree at {tree}")
            return 2
    changed, added = [], []
    for rel in files_of(copy):
        b = base / rel
        if not b.exists():
            added.append(rel)
            continue
        try:
            if load(copy / rel)[0] != load(b)[0]:
                changed.append(rel)
        except UnicodeDecodeError:
            if (copy / rel).read_bytes() != b.read_bytes():
                print(f"BINARY changed, skipped: {rel}")
    conflicts = 0
    for rel in sorted(changed):
        m = MAIN / rel
        if not m.exists():
            print(f"GONE in the tree, skipped: {rel}")
            conflicts += 1
            continue
        mt, bom, crlf = load(m)
        bt, ct = load(base / rel)[0], load(copy / rel)[0]
        if mt == bt:
            result, what, n = ct, "copy  ", 0
        elif mt == ct:
            print(f"same   {rel}")
            continue
        else:
            with tempfile.TemporaryDirectory(dir=work) as d:
                paths = []
                for name, text in (("main", mt), ("base", bt), ("agent", ct)):
                    p = Path(d) / name
                    p.write_bytes(text.encode("utf-8"))
                    paths.append(str(p))
                r = subprocess.run(["git", "merge-file", "-p", "-L", "main", "-L", "base", "-L", agent] + paths, capture_output=True)
            if r.returncode < 0:
                print(f"ERROR  {rel}: {r.stderr.decode(errors='replace').strip()}")
                conflicts += 1
                continue
            result, n = r.stdout.decode("utf-8"), r.returncode
            what = "merged" if n == 0 else f"CONFLICT x{n}"
            conflicts += n
        print(f"{what} {rel}")
        if not dry:
            out = result.replace("\n", "\r\n") if crlf else result
            m.write_bytes((b"\xef\xbb\xbf" if bom else b"") + out.encode("utf-8"))
    for rel in sorted(added):
        m = MAIN / rel
        if m.exists():
            if m.read_bytes() != (copy / rel).read_bytes():
                print(f"NEW but exists differently in the tree, skipped: {rel}")
                conflicts += 1
            continue
        print(f"new    {rel}")
        if not dry:
            m.parent.mkdir(parents=True, exist_ok=True)
            m.write_bytes((copy / rel).read_bytes())
    removed = 0
    for rel in sorted(files_of(base)):
        m = MAIN / rel
        if (copy / rel).exists() or not m.exists():
            continue
        try:
            untouched = load(m)[0] == load(base / rel)[0]
        except UnicodeDecodeError:
            untouched = m.read_bytes() == (base / rel).read_bytes()
        if not untouched:
            print(f"DELETED by {agent} but changed in the tree since, left alone: {rel}")
            conflicts += 1
            continue
        removed += 1
        print(f"delete {rel}")
        if not dry:
            keep = (work or root) / "removed-by-merge" / agent / rel
            keep.parent.mkdir(parents=True, exist_ok=True)
            keep.write_bytes(m.read_bytes())
            m.unlink()
    print(f"{len(changed)} changed, {len(added)} new, {removed} deleted, {conflicts} conflicts" + (" (dry: nothing written)" if dry else ""))
    return 1 if conflicts else 0


if __name__ == "__main__":
    sys.exit(main())
