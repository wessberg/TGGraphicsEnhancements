"""Validate the live NR lifecycle audit and reject deliberately corrupted evidence."""
import copy
import json
import sys
from pathlib import Path


def validate(records):
    rows = {r['name']: r for r in records}
    assert len(rows) == 18, 'Incomplete lifecycle audit'
    other = next((r['name'].split('-')[1] for r in records if r['name'].startswith('2-') and r['name'].endswith('-begin')), None)
    assert other in ('STP', 'None'), 'No non-DLSS gate check'
    for phase, backend in ((1, 'DLSS'), (2, other), (3, 'DLSS')):
        a, b = [rows[f'{phase}-{backend}-{end}'] for end in ('begin', 'end')]
        active = phase != 2
        for r in (a, b):
            assert r['enabled'] is active and r['interactable'] is active, 'Incorrect NR gate'
            assert r['selected'] == int(active), 'Incorrect Video selection'
            assert r['status']['Failed'] == 0, 'Native NR failures'
        assert (b['evaluated'] > a['evaluated']) if active else (b['evaluated'] == a['evaluated']), 'Incorrect evaluation activity'
        if not active:
            assert 'DLSS' in b['reason'] and 'DLSS' in b['tooltip'], 'Missing reason for disabled row'
        view = rows[f'{phase}-video-row']
        assert view['active'] and view['option'] == 'TGNR.Video.Mode', 'Actual Video row missing'
        assert view['interactable'] is active and abs(view['alpha'] - (1 if active else .4)) < .001, 'Incorrect visible grey state'
    original = rows['4-before-resize']
    for phase in ('5-resized', '6-restored', '7-cut', '8-return'):
        a, b = [rows[f'{phase}-{end}'] for end in ('begin', 'end')]
        assert a['enabled'] and b['enabled'] and b['evaluated'] > a['evaluated'], 'NR did not resume'
        assert a['status']['Failed'] == b['status']['Failed'] == 0, 'NR failed after discontinuity'
        assert a['status']['Resets'] == b['status']['Resets'], 'History keeps resetting while held'
        for r in (a, b):
            assert (r['status']['Width'], r['status']['Height']) == (r['width'], r['height']), 'Stale model viewport'
        if phase == '5-resized':
            assert (b['width'], b['height']) != (original['width'], original['height']), 'Resize did not occur'
        else:
            assert (b['width'], b['height'], b['screenMode']) == (original['width'], original['height'], original['screenMode']), 'Display not restored'
    for old, new in (('4-before-resize', '5-resized-begin'), ('5-resized-end', '6-restored-begin'),
                     ('6-restored-end', '7-cut-begin'), ('7-cut-end', '8-return-begin')):
        assert rows[new]['status']['Resets'] > rows[old]['status']['Resets'], 'Missing discontinuity reset'


def main():
    records = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
    validate(records)
    faults = [
        ('2-', '-end', 'evaluated', lambda r: r['evaluated'] + 100),
        ('2-', '-video-row', 'alpha', lambda r: 1),
        ('3-', '-end', 'enabled', lambda r: False),
        ('5-', '-end', 'status', lambda r: dict(r['status'], Width=1)),
        ('6-', '-end', 'screenMode', lambda r: 'broken'),
        ('7-', '-end', 'status', lambda r: dict(r['status'], Resets=r['status']['Resets'] + 1)),
    ]
    for prefix, suffix, field, value in faults:
        broken = copy.deepcopy(records)
        row = next(r for r in broken if r['name'].startswith(prefix) and r['name'].endswith(suffix))
        row[field] = value(row)
        try:
            validate(broken)
        except AssertionError:
            continue
        raise AssertionError(f'Missed corruption: {prefix}{suffix} {field}')
    print(f'PASS: live upscaler gate, native Video row, resume, resize, cuts. Rejected {len(faults)} corruptions.')


if __name__ == '__main__':
    main()
