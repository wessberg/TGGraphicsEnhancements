"""Build isolated broken NR adapters and require the real GPU image probe to reject each one.

Run in the VS x64 environment after building/installing the normal probe. No production source is edited.
"""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installed_bepinex", type=Path)
    parser.add_argument("probe", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--case", action="append", choices=("black-output", "ignored-strength", "hdr-clipping", "ignored-model"))
    args = parser.parse_args()
    native = args.installed_bepinex.resolve() / "TGNeuralRendering/Native"
    original = (ROOT / "native/neural/src/extension.cpp").read_text(encoding="utf-8")
    cases = {
        "black-output": ("return float4(rgb,before.a);", "return float4(0*rgb,before.a);", "invalid or black"),
        "ignored-strength": ('params->Set("DLSSNR.Intensity",f.strength);', 'params->Set("DLSSNR.Intensity",0.f);', "Full strength did not change the input"),
        "hdr-clipping": ("if(mode&2) v.rgb=compressHdr(max(v.rgb,0)*inputScale);", "", "Input conditioning inverse changed near-neutral HDR"),
        "ignored-model": ('params->Set("DLSSNR.Style",f.model);', 'params->Set("DLSSNR.Style",0u);', "Model selector did not change output"),
    }
    for name, (old, new, message) in cases.items():
        if args.case and name not in args.case: continue
        assert original.count(old) == 1
        folder = (args.output / name).resolve()
        if folder.exists(): raise ValueError(f'Use a fresh output directory: {folder}')
        target = folder / "BepInEx/TGNeuralRendering/Native"
        (target / "DLSS").mkdir(parents=True, exist_ok=True)
        shutil.copyfile(native / "TGGraphicsBridge.Native.dll", target / "TGGraphicsBridge.Native.dll")
        model = target / "DLSS/nvngx_dlssnr.dll"
        if not model.exists():
            shutil.copyfile(native / "DLSS/nvngx_dlssnr.dll", model)
        source = folder / "extension.cpp"
        source.write_text(original.replace(old, new), encoding="utf-8")
        shutil.copyfile(ROOT / "native/neural/src/diagnostics.hpp", folder / "diagnostics.hpp")
        command = ["cl.exe", "/nologo", "/LD", "/MT", "/O2", "/EHsc", "/std:c++17", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN",
                   "/I" + str(ROOT / "native/bridge/include"), "/I" + str(ROOT / "native/neural/include"),
                   "/I" + str(ROOT / "DLSS-SDK-310.9.16/include"), str(source), "/link",
                   "/OUT:" + str(target / "TGNeuralRendering.Native.dll"),
                   str(ROOT / "DLSS-SDK-310.9.16/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib"),
                   "d3d11.lib", "d3d12.lib", "d3dcompiler.lib", "dxguid.lib", "version.lib", "bcrypt.lib", "advapi32.lib", "user32.lib"]
        build = subprocess.run(command, cwd=folder, capture_output=True, text=True)
        (folder / "build.log").write_text(build.stdout + build.stderr)
        build.check_returncode()
        guard = "if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { exit 1 }; if (Test-Path artifacts/harness/RUNNING) { exit 2 }; exit 0"
        subprocess.run(['powershell', '-NoProfile', '-Command', guard], cwd=ROOT, check=True, capture_output=True)
        # The exact neutral-output path bypasses the colour adapter. Diagnostic signal cases
        # also test near-neutral output, so removing HDR conditioning must break that inverse.
        command = [str(args.probe.resolve()), str(folder / "BepInEx")]
        if name == "hdr-clipping": command += ["--diagnostics", str(folder / "stages")]
        result = subprocess.run(command, capture_output=True, text=True)
        (folder / "result.txt").write_text(result.stdout + result.stderr)
        if result.returncode != 1 or message not in result.stdout + result.stderr:
            raise AssertionError(f"{name}: image defect was not detected: exit {result.returncode}\n{result.stdout}")
        print(f"PASS {name}: real GPU probe rejected the broken output", flush=True)


if __name__ == "__main__":
    main()
