from pathlib import Path
import json

paths = '''BACKLOG.md CHANGELOG.md README.md ROUNDS.md docs/PROJECT-CONTEXT.md docs/DLSS5-IMPLEMENTATION.md docs/DLSS5-FEEDBACK.md docs/DLSS5-TUNING.md docs/TESTING.md docs/PT-HARNESS.md docs/rounds/round-08.md src/TGPathTracing.Harness/Neural.cs src/TGPathTracing.Harness/Runner.cs src/TGPathTracing.Harness/Request.cs src/TGPathTracing.Harness/CameraSlide.cs tools/harness/Run-PtHarness.ps1 tools/harness/neural_plan.py tools/harness/neural_analysis.py tools/harness/analyze.py tests/neural_gpu_smoke.cpp tests/neural_gpu_mutations.py tests/neural_policy_tests.cpp tests/neural_policy_mutations.py tests/neural_harness_test.py'''.split()
for folder in ('native/neural/include', 'native/neural/src', 'src/TGNeuralRendering'):
    paths += [str(p) for p in Path(folder).iterdir() if p.is_file()]
changed = []
for name in paths:
    p = Path(name)
    old = p.read_bytes()
    new = old.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
    if old != new:
        p.write_bytes(new)
        changed.append(name)
plan = json.loads(Path('build/nr/tuning/compare.json').read_text())
steps = plan['recipes']['neural']
assert len(steps) == 23
assert all(s['slide'] == .65 and s['turn'] == 25 and s['motionFrames'] == 48 for s in steps if s['name'].find('motion-') >= 0)
assert len(Path('BACKLOG.md').read_bytes()) < 12288
print('All 23 comparison takes and strong-motion settings verified. CRLF normalized:', len(changed))
