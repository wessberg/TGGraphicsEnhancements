"""Require exact neutral scene grids in game, and reject the pre-fix capture."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/harness'))
from neural_stages import analyze_step, read_frame


def check(root):
    request = json.loads((root / 'request.json').read_text(encoding='utf-8-sig'))
    placements, count = set(), 0
    for scene in request['scenarios']:
        for take in scene['steps']:
            settings = {p['key']: p['value'] for p in take.get('set', [])
                        if p.get('plugin') == 'com.wessberg.tgneuralrendering'}
            if not take.get('neuralStages') or settings.get('Strength') != '0':
                continue
            assert take['neural'] and not take.get('neuralReplay'), 'Ordinary live-input neutral capture required'
            folder = root / scene['key'] / take['name']
            result = analyze_step(folder, take)
            for frame in result['frame_ids']:
                _, stages, _ = read_frame(folder / 'nr-stages' / f'nr-{frame}.bin')
                assert np.array_equal(stages[0, ..., :3], stages[3, ..., :3]), 'Neutral scene colour changed'
            placements.add(settings['Placement'])
            count += 1
    assert placements == {'After Upscaling', 'Before Upscaling'}, 'Both placements required'
    return count


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    args = parser.parse_args()
    count = check(args.after)
    try:
        check(args.before)
    except AssertionError as ex:
        assert str(ex) == 'Neutral scene colour changed', 'Pre-fix capture failed for an unrelated reason'
    else:
        raise AssertionError('Pre-fix colour error was not detected')
    print(f'PASS: {count} neutral takes preserve exact scene grids at both placements; pre-fix capture rejected.')
