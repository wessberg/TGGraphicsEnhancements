#!/usr/bin/env python3
"""Compile the real policy tests against isolated broken headers and require each break to fail."""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    original = (ROOT / "native/neural/include/tgnr_policy.hpp").read_text()
    cases = {
        "baseline": None,
        "wrong-camera": ("guides.camera==color.camera &&", ""),
        "stale-guides": ("guides.frame==color.frame &&", ""),
        "missing-depth": ("f.depth && f.motion", "f.motion"),
        "history-gap": ("current.frame!=previous.frame+1 ||", ""),
        "jitter-mismatch": ("guides.jitterX==color.jitterX &&", ""),
        "depth-range-reset": ("current.nearPlane!=previous.nearPlane ||", ""),
        "model-reset": ("current.model!=previous.model ||", ""),
        "passes-reset": ("current.passes!=previous.passes ||", ""),
        "passes-range": ("f.passes>=1 && f.passes<=TGNR_MAX_PASSES &&", ""),
        "intensity-reset": ("return !havePrevious ||", "return current.strength!=previous.strength || !havePrevious ||"),
    }
    for name, mutation in cases.items():
        folder = (args.output / name).resolve()
        folder.mkdir(parents=True, exist_ok=True)
        source = original
        if mutation:
            assert source.count(mutation[0]) == 1
            source = source.replace(*mutation)
        (folder / "tgnr_policy.hpp").write_text(source)
        shutil.copyfile(ROOT / "native/neural/include/tgnr_api.h", folder / "tgnr_api.h")
        exe = folder / "policy.exe"
        subprocess.run(["cl.exe", "/nologo", "/EHsc", "/std:c++17", "/MT", "/I" + str(folder),
                        str(ROOT / "tests/neural_policy_tests.cpp"), "/Fe:" + str(exe), "/Fo:" + str(folder / "policy.obj")],
                       cwd=folder, check=True, capture_output=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        (folder / "result.txt").write_text(result.stdout)
        if (result.returncode == 0) != (mutation is None):
            raise AssertionError(f"{name}: unexpected exit {result.returncode}\n{result.stdout}")
        print(f"PASS {name}: exit {result.returncode}")

if __name__ == "__main__":
    main()
