from pathlib import Path

p=Path('ROUNDS.md')
data=p.read_bytes()
marker=b'## [Round 10]'
assert data.count(marker)==1
entry=('## [Round 11](docs/rounds/round-11.md) - 2026-10-08 - NR: actual NVIDIA Style Default/A/B/C labels, '
       'saved values preserved. Signed DLL profile checks distinguish Style from unverified preset hints. '
       'Owner video keeps stability a release blocker. Exact guide captures at1.2m/s agree with camera/depth '
       'motion within0.008..0.016 guide pixels median, including stairs and shields. Actual game-grading '
       'comparison valid but weakens the measured effect as it reduces variation, no default change or '
       'stability fix. NR-only pair deployed16:10, both managed builds compile. Five valid takes,117 saves '
       'unchanged/configs restored. No game or waiter at final check. Parallel PT preserved.\r\n\r\n').encode()
if b'## [Round 11]' not in data:
    p.write_bytes(data.replace(marker,entry+marker,1))

p=Path('docs/DLSS5-FEEDBACK.md')
data=p.read_text(encoding='utf-8')
marker='NR-25 (owner,10-08):'
entry=('R11 input audit161014: two128-frame Quality takes at1.2m/s, exact native guide captures, '
       'all12 frames match DLSS after documented conversion. Independent camera/depth motion error '
       '0.008..0.016 guide pixels median, edgep95<=0.027. Inspected depth outlines match stairs and '
       'furniture. Wrong signs/scales give6.59..25.13pixels. This supports the SR guide contract on '
       'static tavern geometry, not all animated objects or PT/RR.\n'
       'Run161628 compares actual game grading: three valid takes, exact late neutral, steady exposure. '
       'Variation decreases but mean model effect falls4.813 to2.249 encodedRGB levels. No same-look '
       'stability improvement established. Keep defaults and release blocker. Details and limitations '
       'in round-11. Earlier155904/160609 captures failed the new guide reader and are excluded.\n\n')
assert data.count(marker)==1
if 'R11 input audit161014:' not in data:
    p.write_text(data.replace(marker,entry+marker,1),encoding='utf-8',newline='\r\n')

# Normalize only the files touched by this NR work, preserving concurrent PT content.
paths=['native/neural/src/diagnostics.hpp','native/neural/src/extension.cpp','native/neural/include/tgnr_api.h',
       'native/neural/include/tgnr_policy.hpp','tests/neural_gpu_smoke.cpp','tests/neural_policy_tests.cpp',
       'src/TGNeuralRendering/Session.cs','src/TGNeuralRendering/VideoRows.cs','src/TGNeuralRendering/NativeNeural.cs',
       'src/TGNeuralRendering/UpscalePlacement.cs','src/TGNeuralRendering/SceneCapture.cs',
       'src/TGPathTracing.Harness/CameraSlide.cs','src/TGPathTracing.Harness/Runner.cs','src/TGPathTracing.Harness/Request.cs',
       'src/TGPathTracing.Harness/Neural.cs','src/TGPathTracing.Harness/NeuralMotion.cs',
       'tools/harness/neural_guides.py','tools/harness/neural_stages.py','tools/harness/neural_analysis.py',
       'tools/harness/neural_chairs.py','tests/neural_chairs_test.py',
       'docs/rounds/round-11.md','docs/DLSS5-FEEDBACK.md','docs/DLSS5-TUNING.md','docs/PT-HARNESS.md',
       'docs/PROJECT-CONTEXT.md','BACKLOG.md','ROUNDS.md','CHANGELOG.md','README.md']
for name in paths:
    p=Path(name)
    data=p.read_bytes()
    normalized=data.replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
    if normalized!=data: p.write_bytes(normalized)
print('NR round and feedback recorded. Backlog bytes:',Path('BACKLOG.md').stat().st_size)
