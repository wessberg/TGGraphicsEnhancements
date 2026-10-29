# SPDX-License-Identifier: MIT
"""Puts back what Windows swept out of worker copies under %TEMP% (docs/AGENT-WORKERS.md, "When Windows sweeps the copies").

usage: python tools/agents/repair_copies.py [--dry] [--root=<folder>] <copy or base> [<copy or base> ...]

2026-10-12 03:28: with the disk 98 % full, Windows' Storage Sense deleted every file in the temp folder that had not been
written for about six days. A copied file keeps its source's write time, so every copy and every pristine base lost its
old files (138 a tree), the SDK junctions and most of artifacts\\Native (the Agility runtime among them: an offline GPU
test without D3D12Core.dll runs with no DirectX Raytracing 1.2 and proves nothing).

For each named tree this puts back:
  - every file of the main tree (tracked, or untracked and not ignored) that the tree lacks, IF the main tree's file was
    last written before the tree was made: then it is the very file the tree had. A file the main tree made or changed
    later is left out and counted (it was never the tree's);
  - the SDK junctions (never for a base, which has none) and the missing files of artifacts\\Native.
Nothing that exists in a tree is touched. A file put back gets a write time of a day ago: the next sweep leaves it, and
the compiled shader headers of a build made from the tree stay newer than their sources.
New-TreeCopies.ps1 gives a new copy's old files that write time from the start, since the same day.
"""
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SDK_MARKS = ("-SDK-", "Streamline-", "RTXDI-Library-", "DXC-")


def git(*args):
    out = subprocess.run(["git", "-C", str(REPO), *args], capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    return [f for f in out.split("\0") if f]


def main():
    names = [a for a in sys.argv[1:] if not a.startswith("--")]
    options = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    dry = "--dry" in sys.argv
    if not names:
        print(__doc__)
        return 2
    root = Path(options.get("root") or Path(os.environ["TEMP"]) / "tg-agents") / "wt"
    files = git("ls-files", "-z") + git("ls-files", "-z", "-o", "--exclude-standard")
    sdks = [d.name for d in REPO.iterdir() if d.is_dir() and any(d.name.startswith(m) or m in d.name for m in SDK_MARKS)]
    stamp = time.time() - 86400
    for name in names:
        tree = root / name
        if not tree.is_dir():
            print(f"{name}: no such tree under {root}")
            continue
        made = tree.stat().st_ctime
        back = newer = 0
        for f in files:
            if f.split("/")[0] in ("artifacts", "build"):
                continue
            src, dst = REPO / f, tree / f
            if not src.is_file() or dst.exists():
                continue
            if src.stat().st_mtime > made:
                newer += 1
                continue
            back += 1
            if not dry:
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(src, dst)
                os.utime(dst, (stamp, stamp))
        links = runtime = 0
        is_copy = (tree / "artifacts").is_dir()  # a base has neither junctions nor runtime files
        if is_copy:
            for s in sdks:
                if not (tree / s).exists():
                    links += 1
                    if not dry:
                        subprocess.run(["cmd", "/c", "mklink", "/J", str(tree / s), str(REPO / s)], capture_output=True)
            native = REPO / "artifacts" / "Native"
            for src in native.rglob("*"):
                dst = tree / "artifacts" / "Native" / src.relative_to(native)
                if src.is_file() and not dst.exists():
                    runtime += 1
                    if not dry:
                        dst.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(src, dst)
                        os.utime(dst, (stamp, stamp))
        verb = "would be put back" if dry else "put back"
        print(f"{name} (made {time.strftime('%m-%d %H:%M', time.localtime(made))}): {back} files, {links} SDK links, {runtime} runtime files {verb}; "
              f"{newer} files of the main tree are newer than the tree and were left out")
    return 0


if __name__ == "__main__":
    sys.exit(main())
