"""Check the Unity motion-marker correction in the recorded frozen tavern/face scenes."""
import argparse
import json
from pathlib import Path


def check_fixed(run):
    data = json.loads((run / 'nr-stages.json').read_text())
    assert not data['errors'], data['errors']
    results = data['results']
    assert len(results) == 12, 'Need both six-take NR stage sequences'
    for key, value in results.items():
        assert value['frames'] == 144 and value['resets'] == 0, key
        assert value['motion_hold_max_mean_pixels'] < .01, f'{key}: no-motion marker still interpreted as movement'
        assert value['hold_exposure_range'] == [0, 0, 0, 0], f'{key}: exposure changed; do not compare blindly'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    args = parser.parse_args()
    check_fixed(args.after)
    try: check_fixed(args.before)
    except AssertionError as error:
        assert 'no-motion marker still interpreted' in str(error), error
    else: raise AssertionError('The check accepted the captured pre-fix defect')
    print('PASS: all 12 corrected takes have <0.01 pixel held motion and constant exposure; pre-fix captures fail the motion check.')
