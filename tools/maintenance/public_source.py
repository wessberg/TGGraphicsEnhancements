"""Audit/export the current source tree without private files or Git history."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
ROOT_FILES = {
    ".editorconfig", ".gitignore", ".gitattributes", "AGENTS.md", "BACKLOG.md",
    "Build-Native.ps1", "Build.Local.props.example", "Build.ps1", "CHANGELOG.md",
    "CLAUDE.md", "CMakeLists.txt", "CONTRIBUTING.md", "LICENSE", "Package.ps1",
    "README.md", "ROUNDS.md", "Stage-Runtime.ps1", "Test-Setup.ps1", "TGFrameGeneration.sln",
}
ROOT_DIRS = {"docs", "native", "src", "tests", "tools", "LICENSES"}
PRIVATE_PARTS = {".git", ".development", ".testruns", "tgpt-images", "__pycache__", "bin", "obj", ".vs"}
BINARY_SUFFIXES = {".dll", ".exe", ".lib", ".pdb", ".f32", ".pyc", ".zip", ".log", ".patch"}
CONVERSATION = re.compile(r"(?i)the owner(?:'s)?|owner says|owner,\s*20\d\d|claude\.ai/artifact|^\s*(?:user|assistant):", re.M)

def allowed(name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or any(part in PRIVATE_PARTS for part in path.parts):
        return False
    if len(path.parts) == 1:
        return name in ROOT_FILES
    return path.parts[0] in ROOT_DIRS and path.suffix.lower() not in BINARY_SUFFIXES

def source_files(root=ROOT):
    output = subprocess.check_output(["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard", "-z"])
    names = sorted(set(output.decode("utf-8").split("\0")) - {""})
    files = []
    for name in names:
        if not allowed(name):
            continue
        path = root / name
        if path.is_symlink():
            raise ValueError("Source symlink requires explicit review: " + name)
        if path.is_file():
            if not path.resolve().is_relative_to(root.resolve()):
                raise ValueError("Source outside workspace: " + name)
            files.append((name, path))
    return files

def audit(files):
    failures = []
    names = {name for name, _ in files}
    for required in ("LICENSES/MIT.txt", "LICENSES/THIRD_PARTY.md", "README.md", "AGENTS.md", "docs/PT-STATE.md"):
        if required not in names:
            failures.append("Missing required source document: " + required)
    for name, path in files:
        if path.suffix.lower() not in {".md", ".txt", ".py", ".ps1", ".cmd", ".cs", ".cpp", ".hpp", ".h", ".json", ".props"}:
            continue
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        if "-----BEGIN " + "PRIVATE KEY-----" in text:
            failures.append("Private key material: " + name)
        if path.suffix.lower() == ".md":
            if CONVERSATION.search(text):
                failures.append("Conversational documentation: " + name)
            if re.search(r"(?i)[A-Z]:[/\\]Users[/\\](?!<|%|\{)[A-Za-z0-9_-]+", text):
                failures.append("Machine-specific user path: " + name)
    return failures

def export(output, files):
    output = output.resolve()
    if not output.is_relative_to((ROOT / "artifacts/public-source").resolve()):
        raise ValueError("Export destination must be under artifacts/public-source")
    if output.exists():
        raise ValueError("Export already exists; choose a new output filename")
    output.parent.mkdir(parents=True, exist_ok=True)
    entries = []
    # Exclusive creation prevents replacement of an earlier reviewed export.
    with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, path in files:
            data = path.read_bytes()
            archive.writestr(name, data)
            entries.append(dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
        archive.writestr("SOURCE-MANIFEST.json", json.dumps(dict(schema=1, files=entries), indent=2) + "\n")
    print(f"Exported {len(entries)} files: {output.relative_to(ROOT)}")
    print("No Git history, SDK downloads, private archive or build output included.")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Audit without writing an archive")
    parser.add_argument("--output", type=Path, default=ROOT/"artifacts/public-source/TGGraphicsEnhancements-source.zip")
    args = parser.parse_args()
    files = source_files()
    failures = audit(files)
    for failure in failures:
        print("FAIL:", failure)
    if failures:
        return 1
    print(f"Source audit passed: {len(files)} files.")
    if not args.check:
        export(args.output, files)
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print("Source preparation failed:", error)
        raise SystemExit(1)
