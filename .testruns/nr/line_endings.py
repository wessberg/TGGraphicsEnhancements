from pathlib import Path
files = '''.gitignore BACKLOG.md Build.ps1 CMakeLists.txt Package.ps1 README.md CHANGELOG.md ROUNDS.md docs/PROJECT-CONTEXT.md docs/SOURCES.md docs/TESTING.md docs/PT-HARNESS.md docs/DLSS5-IMPLEMENTATION.md docs/rounds/round-08.md native/bridge/include/tgb/ngx_session.hpp native/bridge/src/ngx_session.cpp native/framegen/include/sl_runtime.hpp native/framegen/src/sl_runtime.cpp native/upscaling/src/rr_upscaler.cpp src/TGGraphicsBridge/ScenePostProcess.cs src/TGGraphicsBridge/InstallCheck.cs src/TGGraphicsBridge/GraphicsBridge.cs src/TGGraphicsBridge/Notices.cs src/TGGraphicsBridge/RowAlerts.cs src/TGFrameGeneration/HudlessUi.cs src/TGFrameGeneration/Plugin.cs src/TGPathTracing.Harness/Neural.cs src/TGPathTracing.Harness/Runner.cs src/TGPathTracing.Harness/Request.cs tests/upscaling_gpu_smoke.cpp tests/source_checks.py tests/test_autoload_integration.py tests/package_tests.ps1 tools/Package.Common.ps1 tools/harness/Run-PtHarness.ps1 tools/harness/neural_plan.py tools/harness/neural_analysis.py tools/harness/analyze.py'''.split()
for folder in ('src/TGNeuralRendering', 'src/TGNeuralRendering.Autoload', 'native/neural/include', 'native/neural/src'):
    files += [str(p) for p in Path(folder).iterdir() if p.is_file()]
files += [str(p) for p in Path('tests').glob('neural*') if p.is_file()]
p=Path('ROUNDS.md')
s=p.read_text(encoding='utf-8').splitlines()
line=next(x for x in s if x.startswith("Each round's file"))
s.remove(line)
s.insert(3,line)
p.write_text('\n'.join(s)+'\n',encoding='utf-8')
changed=0
for name in files:
    p=Path(name); before=p.read_bytes(); after=before.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
    if before != after: p.write_bytes(after); changed+=1
print('CRLF normalized in',changed,'owned/edited text files; contents retained.')
