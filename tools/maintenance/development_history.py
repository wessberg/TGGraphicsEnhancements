"""Verify the exact local development archive without changing it."""
from pathlib import Path
import argparse
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]

def verify():
    history = ROOT / ".development/history"
    current = (history / "CURRENT").read_text(encoding="utf-8").strip()
    archive = (history / current).resolve()
    if not archive.is_relative_to(history.resolve()):
        raise ValueError("Invalid archive location")
    manifest = json.loads((archive / "manifest.json").read_text(encoding="utf-8"))
    failures = []
    for entry in manifest["files"]:
        path = (archive / "files" / entry["path"]).resolve()
        if not path.is_relative_to(archive / "files"):
            raise ValueError("Invalid archived path")
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            failures.append(entry["path"])
    for name in failures:
        print("FAIL:", name)
    print(f"{len(manifest['files'])} archived files; {len(failures)} mismatches.")
    return bool(failures)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["verify"])
    parser.parse_args()
    try:
        raise SystemExit(verify())
    except (OSError, ValueError) as error:
        print("Archive verification failed:", error)
        raise SystemExit(1)
