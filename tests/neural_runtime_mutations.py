"""Require the CPU runtime checks to detect an unsupported API and a replaceable DLL.

Run in the VS x64 environment. Only scratch headers are changed; no model or game is loaded.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("adapter", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--fixture-probe", type=Path)
    args = parser.parse_args()
    cases = {
        "accept-incompatible-api": (
            "native/neural/include/tgnr_runtime_profile.hpp",
            "apiVersion==0x13u", "true", "Unsupported direct API accepted"),
        "allow-file-writes": (
            "native/neural/src/runtime_identity.hpp", "FILE_SHARE_READ,nullptr",
            "FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr", "Verified file remained writable"),
    }
    for name, (header, old, new, expected) in cases.items():
        folder = (args.output / name).resolve()
        folder.mkdir(parents=True, exist_ok=True)
        original = (ROOT / header).read_text(encoding="utf-8")
        assert original.count(old) == 1
        (folder / Path(header).name).write_text(original.replace(old, new), encoding="utf-8")
        exe = folder / "runtime-tests.exe"
        command = ["cl.exe", "/nologo", "/MT", "/O2", "/EHsc", "/std:c++17",
                   "/I" + str(folder), "/I" + str(ROOT / "native/neural/include"),
                   "/I" + str(ROOT / "native/neural/src"), "/I" + str(ROOT / "DLSS-SDK-310.9.16/include"),
                   str(ROOT / "tests/neural_runtime_tests.cpp"), "/Fo" + str(folder / "test.obj"),
                   "/Fe" + str(exe), "/link", "bcrypt.lib"]
        build = subprocess.run(command, capture_output=True, text=True)
        (folder / "build.log").write_text(build.stdout + build.stderr)
        build.check_returncode()
        result = subprocess.run([str(exe), str(folder / "files"), str(args.adapter.resolve())],
                                capture_output=True, text=True)
        (folder / "result.log").write_text(result.stdout + result.stderr)
        if result.returncode != 1 or expected not in result.stdout:
            raise AssertionError(f"{name}: expected rejection absent: {result.returncode}\n{result.stdout}")
        print(f"PASS: {name} rejected ({expected})", flush=True)

    if args.fixture and args.fixture_probe:
        adapter_source = (ROOT / "native/neural/src/community_adapter.cpp").read_text(encoding="utf-8")
        adapter_cases = {
            "ignore-support-refusal": ("if(info->supportFlags)", "if(false)", "unsupported", "Incompatible runtime was initialized"),
            "ignore-selected-identity": ("if(!locked->matches(selectedSha256))", "if(false)", "changed", "Replaced file executed"),
            "ignore-handle-release": ("handles.erase(handle)", "(void)handle", "success", "Released handle dispatched twice"),
        }
        for name, (old, new, mode, expected) in adapter_cases.items():
            folder = (args.output / name).resolve()
            folder.mkdir(parents=True, exist_ok=True)
            assert adapter_source.count(old) == 1
            source = folder / "adapter.cpp"
            source.write_text(adapter_source.replace(old, new), encoding="utf-8")
            dll = folder / "TGNeuralRendering.nvngx.dll"
            command = ["cl.exe", "/nologo", "/LD", "/MT", "/O2", "/EHsc", "/std:c++17",
                       "/I" + str(ROOT / "native/neural/include"), "/I" + str(ROOT / "native/neural/src"),
                       "/I" + str(ROOT / "DLSS-SDK-310.9.16/include"), str(source),
                       "/Fo" + str(folder / "adapter.obj"), "/link", "/OUT:" + str(dll),
                       "/IMPLIB:" + str(folder / "adapter.lib"), "bcrypt.lib"]
            build = subprocess.run(command, capture_output=True, text=True)
            (folder / "build.log").write_text(build.stdout + build.stderr)
            build.check_returncode()
            result = subprocess.run([str(args.fixture_probe.resolve()), str(dll), str(args.fixture.resolve()), mode],
                                    capture_output=True, text=True)
            (folder / "result.log").write_text(result.stdout + result.stderr)
            if result.returncode != 1 or expected not in result.stdout:
                raise AssertionError(f"{name}: expected rejection absent: {result.returncode}\n{result.stdout}")
            print(f"PASS: {name} rejected ({expected})", flush=True)


if __name__ == "__main__":
    main()
