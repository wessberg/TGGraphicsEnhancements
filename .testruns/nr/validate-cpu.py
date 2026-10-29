import json
from pathlib import Path
import subprocess
import sys
import time

root=Path(__file__).resolve().parents[2]
out=root/'build/nr/validation-r15'
out.mkdir(exist_ok=True)
jobs=[]
for name in ['source_checks','test_autoload_integration','harness_analyze_test','neural_harness_test',
             'neural_chairs_test','neural_stages_test','neural_temporal_test','neural_motion_direction_test']:
    jobs.append((name,[sys.executable,f'tests/{name}.py']))
jobs += [('packages',['powershell','-NoProfile','-File','tests/package_tests.ps1']),
         ('managed-mono',['dotnet','build','src/TGNeuralRendering/TGNeuralRendering.csproj','-c','Release','-p:TargetFrameworks=net472']),
         ('managed-normal',['dotnet','build','src/TGNeuralRendering/TGNeuralRendering.csproj','-c','Release','-p:TargetFrameworks=net6.0',
                            '-p:Il2CppCoreDir=C:/Users/Frede/Downloads/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.755+3fab71a/BepInEx/core']),
         ('install-check',['dotnet','run','--project','tests/InstallCheck.Tests.csproj','-c','Release']),
         ('option-conditions',['dotnet','run','--project','tests/Il2CppOptionConditions.Tests.csproj','-c','Release'])]
results=[]
for name, command in jobs:
    start=time.monotonic()
    with (out/f'{name}.log').open('w',encoding='utf-8') as log:
        proc=subprocess.run(command,cwd=root,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    record=dict(name=name,exit=proc.returncode,seconds=round(time.monotonic()-start,2),command=command)
    results.append(record)
    (out/'cpu-results.json').write_text(json.dumps(results,indent=2)+'\n')
    print(name,proc.returncode,record['seconds'],flush=True)
raise SystemExit(any(r['exit'] for r in results))
