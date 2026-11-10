"""Verify real NR chaining against independent single-pass evaluations and deliberate faults.

VS x64 environment required. Builds copies in scratch; never modifies or deploys production binaries.
The test-only export copies raw model output, avoiding colour-conversion rounding in the oracle.
"""
import argparse
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
extern "C" __declspec(dllexport) void TGNR_TestCopyModelOutput(ID3D11Texture2D* destination) {
    auto& r=tgnr::runtime();
    auto& result=r.previous.passes%2 ? r.result:r.intermediate;
    tgnr::host->game_context()->CopyResource(destination,result.d11.Get());
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('installed_bepinex', type=Path)
    parser.add_argument('probe', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    native = args.installed_bepinex.resolve() / 'TGNeuralRendering/Native'
    original = (ROOT / 'native/neural/src/extension.cpp').read_text()
    cases = {
        'baseline': None,
        'original-input-each-pass': ('passInput=passOutput;', 'passInput=&color;', 'Multipass did not match'),
        'intermediate-output': ('finalOutput=passOutput;', 'finalOutput=&result;', 'Multipass did not match'),
        'one-evaluation': ('for(uint32_t i=0;i<f.passes;++i) {\n            auto* passOutput=',
                           'for(uint32_t i=0;i<1;++i) {\n            auto* passOutput=', 'evaluation count mismatch'),
    }
    for name, mutation in cases.items():
        folder = (args.output / name).resolve()
        target = folder / 'BepInEx/TGNeuralRendering/Native'
        (target / 'DLSS').mkdir(parents=True, exist_ok=True)
        for file in ('TGGraphicsBridge.Native.dll', 'DLSS/nvngx_dlssnr.dll'):
            shutil.copyfile(native / file, target / file)
        helper = native / 'TGNeuralRendering.nvngx.dll'
        if helper.exists():
            shutil.copyfile(helper, target / helper.name)
        source = original
        if mutation:
            old, new, _ = mutation
            assert source.count(old) == 1, name
            source = source.replace(old, new)
        (folder / 'extension.cpp').write_text(source + FIXTURE)
        shutil.copyfile(ROOT / 'native/neural/src/diagnostics.hpp', folder / 'diagnostics.hpp')
        command = ['cl.exe', '/nologo', '/LD', '/MT', '/O2', '/EHsc', '/std:c++17', '/utf-8', '/DUNICODE', '/D_UNICODE',
                   '/D_WIN32_WINNT=0x0A00', '/I'+str(ROOT/'native/bridge/include'), '/I'+str(ROOT/'native/neural/include'),
                   '/I'+str(ROOT/'DLSS-SDK-310.9.16/include'), str(folder/'extension.cpp'), '/link',
                   '/OUT:'+str(target/'TGNeuralRendering.Native.dll'),
                   str(ROOT/'DLSS-SDK-310.9.16/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib'),
                   'd3d11.lib', 'd3d12.lib', 'd3dcompiler.lib', 'dxguid.lib', 'version.lib', 'bcrypt.lib',
                   'advapi32.lib', 'user32.lib', 'dxgi.lib', 'ole32.lib']
        build = subprocess.run(command, cwd=folder, capture_output=True, text=True)
        (folder/'build.log').write_text(build.stdout+build.stderr)
        build.check_returncode()
        # Offline probes follow the shared GPU guard. A reserved harness lock alone is not GPU use.
        subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                        str(ROOT / 'tools/harness/Run-PtHarness.ps1'), '-GpuCheck'],
                       cwd=ROOT, check=True, capture_output=True)
        result = subprocess.run([str(args.probe.resolve()), str(folder/'BepInEx'), '--multipass'], capture_output=True, text=True)
        (folder/'result.txt').write_text(result.stdout+result.stderr)
        if mutation is None:
            if result.returncode != 0: raise AssertionError(result.stdout+result.stderr)
        elif result.returncode != 1 or mutation[2] not in result.stdout:
            raise AssertionError(f'{name}: mutation escaped or failed for another reason:\n{result.stdout}{result.stderr}')
        print(f'PASS {name}: exit {result.returncode}', flush=True)


if __name__ == '__main__':
    main()
