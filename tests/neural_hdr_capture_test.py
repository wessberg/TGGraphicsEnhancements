"""Require the HDR gain bound in both scenes/placements; reject the recorded inverse-peak regression."""
import json
from pathlib import Path
import sys
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/harness'))
from neural_stages import analyze_step, read_frame
from neural_analysis import counters, matched_motion


def check(root):
    request = json.loads((root / 'request.json').read_text(encoding='utf-8-sig'))
    coverage, worst = set(), 0
    for scene in request['scenarios']:
        baseline = json.loads((root / scene['key'] / scene['steps'][0]['name'] / 'step.json').read_text(encoding='utf-8-sig'))
        for take in scene['steps']:
            if not take.get('neuralStages'): continue
            folder = root / scene['key'] / take['name']
            step = json.loads((folder / 'step.json').read_text(encoding='utf-8-sig'))
            assert not matched_motion(baseline, step), 'Camera route mismatch'
            assert not counters(take, step), 'NR evaluation/order mismatch'
            measurement = analyze_step(folder, take)
            assert not measurement['replay'], 'The bound needs the actual source frame, not retained input'
            controls = {s['key']: s['value'] for s in take['set']}
            assert all(float(controls[k]) == 1 for k in ('Strength', 'Local Tone', 'Detail', 'Skin Detail')), 'NR effect was weakened'
            scale = measurement['input_scale']
            coverage.add((scene['key'], controls['Placement'], scale))
            for frame in step['frames']:
                _, grids, _ = read_frame(folder / 'nr-stages' / f"nr-{frame['frame']}.bin")
                assert frame['frame'] == int(frame['neuralFrame'])
                assert np.all(grids[0, ..., :3] >= 0), 'This regression assumes the game nonnegative HDR format'
                assert np.abs(grids[2, ..., :3] - grids[1, ..., :3]).mean() > 1e-4, 'Model effect absent'
                assert np.abs(grids[3, ..., :3] - grids[0, ..., :3]).max() > 1e-4, 'NR effect absent from HDR output'
                # For each pixel: output <= source_peak + 1/scale, since decoded NR LDR <= 1.
                # For a cell average: mean(peak) <= mean(sum RGB), giving a conservative measurable bound.
                # Four percent allows packed R11G11B10 quantization and float transfer roundoff.
                bound = 1 / scale + grids[0, ..., :3].sum(axis=-1)
                ratio = float(np.max(grids[3, ..., :3] / bound[..., None]))
                worst = max(worst, ratio)
                assert ratio <= 1.04, f'HDR gain bound exceeded: {scene["key"]}/{take["name"]} frame {frame["frame"]}: {ratio:.3f}x'
    expected = {(s, p, scale) for s in ('hair-eyebrows', 'tavern-room')
                for p in ('Before Upscaling', 'After Upscaling') for scale in (.25, 1, 4)}
    assert coverage == expected, f'Incomplete input/scene/placement comparison: {coverage}'
    return worst


if __name__ == '__main__':
    before, after = map(Path, sys.argv[1:])
    try:
        check(before)
    except AssertionError as failure:
        assert str(failure).startswith('HDR gain bound exceeded:'), f'Old capture failed for the wrong reason: {failure}'
        print(f'Rejected old HDR restoration: {failure}')
    else:
        raise AssertionError('The old inverse-peak capture incorrectly passed')
    print(f'PASS: full-strength HDR gain bound at all 12 scene/placement/input cases; maximum bound ratio {check(after):.6f}.')
