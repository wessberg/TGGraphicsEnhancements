"""Read-only inventory of pinned SDKs, local alternatives and CMake cache references."""
from pathlib import Path
import argparse
import json
import re

ROOT = Path(__file__).resolve().parents[2]
CURRENT = {
    "Streamline": ("Streamline-2.14.16", "Maintained native headers"),
    "Streamline runtime": ("Streamline-2.14.16-bin", "Signed staging input"),
    "DLSS": ("DLSS-SDK-310.9.16", "NGX headers/static library and SR/RR runtimes"),
    "FidelityFX": ("FidelityFX-SDK-2.4.0", "FSR headers, FG/SR/RRG staging"),
    "XeSS": ("XeSS-SDK-3.0.2", "XeSS SR/FG and XeLL"),
    "Anti-Lag 2": ("AntiLag2-SDK-2.0.0a", "Native FG header"),
    "DXC": ("DXC-1.9.2607", "PT SM6.9 shader compiler"),
    "Agility": ("Agility-SDK-1.619.6", "PT DXR1.2 runtime"),
    "RTXDI": ("RTXDI-Library-3.1", "PT native source and shader headers"),
    "NRD": ("NRD-SDK-4.17.3", "PT denoiser source and embedded shaders"),
    "NVAPI": ("NVAPI-R615", "PT Mega Geometry"),
    "Neural runtime": ("DLSS-NR-Runtime", "Signed model staging; not SDK source"),
}
PREFIXES = ("Streamline-", "DLSS-SDK-", "FidelityFX-SDK-", "XeSS-SDK-", "AntiLag2-SDK-", "DXC-", "Agility-SDK-", "RTXDI-Library-", "NRD-SDK-", "NVAPI-")

def inventory(root=ROOT):
    caches = {}
    for path in (root / "build").rglob("CMakeCache.txt"):
        caches[path.relative_to(root).as_posix()] = path.read_text(encoding="utf-8", errors="replace").replace("\\", "/")
    active = [dict(component=name, folder=folder, purpose=purpose, present=(root/folder).is_dir())
              for name, (folder, purpose) in CURRENT.items()]
    pinned = {entry["folder"] for entry in active}
    alternatives = []
    for path in sorted(root.iterdir()):
        if not path.is_dir() or path.name in pinned or not path.name.startswith(PREFIXES):
            continue
        references = [name for name, content in caches.items() if re.search(re.escape(path.name) + r"(?=/|\s|$)", content)]
        alternatives.append(dict(folder=path.name, cache_references=references,
                                 disposition="unlisted local SDK; check active source before retirement"))
    return dict(baseline="2026-10-30 / 1.2.4", active=active, alternatives=alternatives,
                note="Read-only inventory. Generated cache references are informational, not reasons to retain an obsolete SDK.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    report = inventory()
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        for entry in report["active"]:
            print(("PRESENT " if entry["present"] else "MISSING ") + entry["folder"] + ": " + entry["purpose"])
        for entry in report["alternatives"]:
            print(entry["folder"] + ": " + entry["disposition"])
            for reference in entry["cache_references"]:
                print("  " + reference)
