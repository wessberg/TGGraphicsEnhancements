"""Guarded, isolated real-runtime checks. Does not change the game installation."""
from pathlib import Path
import json
import shutil
import subprocess

root = Path.cwd()
out = root / 'build/nr/validation-r15'
native = root / 'build/nr/probe/BepInEx/TGNeuralRendering/Native'
probe = out / 'probe.exe'
guard = "if (Get-Process -Name 'Fall of Avalon','Starfield' -ErrorAction SilentlyContinue) { exit 1 }; if (Test-Path artifacts/harness/RUNNING) { exit 2 }; exit 0"
jobs = [('styles', [str(probe), str(native.parents[1])]),
        ('bad-viewport', [str(probe), str(native.parents[1]), '--bad-viewport'])]
for name, state in [('missing', 1), ('rejected', 2)]:
    folder = out / name / 'BepInEx/TGNeuralRendering/Native'
    folder.mkdir(parents=True, exist_ok=True)
    for file in ['TGGraphicsBridge.Native.dll', 'TGNeuralRendering.Native.dll']:
        shutil.copyfile(native / file, folder / file)
    if name == 'rejected':
        (folder / 'DLSS').mkdir(exist_ok=True)
        (folder / 'DLSS/nvngx_dlssnr.dll').write_bytes(b'Intentional invalid NR runtime for fallback validation.\n')
    jobs.append((name, [str(probe), str(folder.parents[1]), str(state)]))
jobs.append(('coexistence', [str(root / 'build/nr/tgup_gpu_smoke.exe'), str(root / 'build/nr/install'),
                             '--factory-first', '--with-fg', '--with-pt', '--with-nr', '--installed']))
results = []
for name, command in jobs:
    subprocess.run(['powershell', '-NoProfile', '-Command', guard], check=True)
    result = subprocess.run(command, capture_output=True, text=True)
    (out / (name + '.log')).write_text(result.stdout + result.stderr)
    results.append(dict(name=name, command=command, exit=result.returncode))
    (out / 'fallback-results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'{name}: exit {result.returncode}', flush=True)
    if result.returncode != 0:
        print(result.stdout[-3500:] + result.stderr[-1000:], flush=True)
        raise SystemExit(result.returncode)
