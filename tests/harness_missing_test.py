#!/usr/bin/env python3
"""Offline PT-83 inventory checks, including deliberate corruptions of its evidence."""
import copy
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('missing', ROOT / 'tools/harness/missing.py')
missing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(missing)
BOXES = [{'name': 'window', 'rect': [.2, .1, .8, .9], 'crop': 'window.jpg'}]


def row(ident, status='excluded', rect=None):
    rect = rect or [.3, .2, .7, .8]
    return {'id': ident, 'name': ident, 'path': 'hall/window|' + ident, 'type': 'MeshRenderer',
            'cameraEligible': True, 'drawRule': '', 'rect': rect,
            'overlaps': [{'box': 'window', 'area': missing.intersection(rect, BOXES[0]['rect'])}],
            'triangles': 42, 'mesh': {'id': 9, 'name': 'diamond lead'},
            'materials': [{'slot': 0, 'shader': 'TG/Glass', 'name': 'window', 'queue': 3000,
                           'keywords': ['_SURFACE_TYPE_TRANSPARENT'], 'rule': 'transparent gate'}],
            'trace': {'status': status, 'rule': 'transparent gate' if status == 'excluded' else 'accepted native input',
                      'instances': [{'route': 'SetMoving', 'slot': 2, 'mesh': 19, 'frame': 109}] if status in ('included', 'partial') else []}}


def inventory(traced, rows):
    rows = copy.deepcopy(rows)
    if not traced:
        for r in rows:
            r['trace'] = {'status': 'unknown', 'rule': 'PT is Off', 'instances': []}
    return {'schema': 1, 'scenario': 'room', 'traced': traced, 'expectedTraced': traced, 'stepError': '',
            'frame': 110 if traced else 100, 'timeScale': 0, 'boxes': copy.deepcopy(BOXES),
            'camera': {'position': [1, 2, 3], 'rotation': [0, 10, 0], 'fov': 60, 'width': 3840, 'height': 2160},
            'coverage': [{'source': s, 'ok': True, 'rows': 0} for s in sorted(missing.SOURCES)],
            'submissions': {key + suffix: value for key in ('scene', 'moving', 'skin', 'decal') for suffix, value in [('Frame', 109), ('Result', 0)]},
            'rows': rows}


def fixture(root):
    steps = [{'name': 'off', 'missing': True, 'traced': False}, {'name': 'on', 'missing': True, 'traced': True}]
    request = {'scenarios': [{'key': 'room', 'save': 'PT room', 'missingBoxes': BOXES, 'steps': steps}]}
    (root / 'request.json').write_text(json.dumps(request))
    records = [row('included', 'included', [.2, .1, .8, .9]), row('small', rect=[.3, .2, .4, .3]), row('large'), row('partial', 'partial')]
    for step in steps:
        folder = root / 'room' / step['name']
        folder.mkdir(parents=True)
        (folder / 'missing.json').write_text(json.dumps(inventory(step['traced'], records)))
        (folder / 'step.json').write_text(json.dumps({'traced': step['traced'], 'error': '', 'menuOpen': False}))
        (folder / 'missing-view.jpg').write_bytes(b'fixture presence only; no image decoding in this analysis')
    (root / 'room/scenario.json').write_text(json.dumps({'slotName': 'PT room', 'standIn': False, 'error': ''}))


class MissingAnalysis(unittest.TestCase):
    def test_pair_order_and_keep_unknown(self):
        off = inventory(False, [row('left-out'), row('disappeared'), row('present', 'included')])
        on = inventory(True, [row('left-out'), row('present', 'included')])
        rows = missing.pair_rows(off['rows'], on['rows'], BOXES)
        self.assertEqual([r['id'] for r in rows], ['left-out', 'disappeared', 'present'])
        self.assertEqual(rows[1]['trace']['status'], 'unknown')
        self.assertIn('not proof', rows[1]['trace']['rule'])

    def test_game_hidden_goes_after_visible(self):
        a = row('hidden'); a['cameraEligible'] = False; a['drawRule'] = 'inactive'
        b = row('visible', 'included')
        rows = missing.pair_rows(inventory(False, [a, b])['rows'], [a, b], BOXES)
        self.assertEqual(rows[0]['id'], 'visible')

    def test_clean_evidence_and_deliberate_corruptions(self):
        clean = inventory(True, [row('a'), row('b', 'included')])
        self.assertEqual(missing.validate(clean, True, BOXES), [])
        mutations = {
            'wrong schema': lambda d: d.update(schema=9),
            'failed PT activation': lambda d: d.update(traced=False),
            'not frozen': lambda d: d.update(timeScale=1),
            'missing source': lambda d: d['coverage'].pop(),
            'hidden source exception': lambda d: d['coverage'][0].update(ok=False, error='unreadable pointer'),
            'wrong boxes': lambda d: d['boxes'][0].update(rect=[0, 0, 1, 1]),
            'stale moving input': lambda d: d['submissions'].update(movingFrame=80),
            'native refusal': lambda d: d['submissions'].update(sceneResult=-2),
            'duplicate identity': lambda d: d['rows'].append(copy.deepcopy(d['rows'][0])),
            'wrong area': lambda d: d['rows'][0]['overlaps'][0].update(area=.99),
            'missing overlap': lambda d: d['rows'][0].update(overlaps=[]),
            'NaN rectangle': lambda d: d['rows'][0]['rect'].__setitem__(0, float('nan')),
            'negative triangles': lambda d: d['rows'][0].update(triangles=-1),
            'lost material shader': lambda d: d['rows'][0]['materials'][0].pop('shader'),
            'invented membership': lambda d: d['rows'][1]['trace'].update(instances=[]),
            'invented slot': lambda d: d['rows'][1]['trace']['instances'][0].update(slot=-1),
            'lost reason': lambda d: d['rows'][0]['trace'].update(rule=''),
            'future instance': lambda d: d['rows'][1]['trace']['instances'][0].update(frame=111),
            'null camera': lambda d: d.update(camera=None),
            'null submissions': lambda d: d.update(submissions=None),
            'null overlaps': lambda d: d['rows'][0].update(overlaps=None),
            'string frame': lambda d: d.update(frame='bad'),
        }
        for name, corrupt in mutations.items():
            with self.subTest(name=name):
                damaged = copy.deepcopy(clean)
                corrupt(damaged)
                self.assertTrue(missing.validate(damaged, True, BOXES), name)

    def test_off_cannot_claim_inclusion(self):
        doc = inventory(False, [row('a')]); doc['rows'][0]['trace'] = row('a', 'included')['trace']
        self.assertTrue(any('Off census' in x for x in missing.validate(doc, False, BOXES)))

    def test_source_failure_keeps_readable_evidence(self):
        with tempfile.TemporaryDirectory(prefix='missing-analysis-') as temporary:
            root = Path(temporary); fixture(root)
            path = root / 'room/on/missing.json'; doc = missing.read(path)
            doc['coverage'][0].update(ok=False, error='unreadable runtime data')
            path.write_text(json.dumps(doc))
            result = missing.analyze(root)
            self.assertFalse(result['ok'])
            self.assertEqual(len(result['scenarios'][0]['rows']), 4)

    def test_reports_and_sort_largest_first(self):
        with tempfile.TemporaryDirectory(prefix='missing-analysis-') as temporary:
            root = Path(temporary); fixture(root)
            result = missing.analyze(root)
            self.assertTrue(result['ok'], result['issues'])
            rows = result['scenarios'][0]['rows']
            self.assertEqual([r['id'] for r in rows], ['large', 'small', 'partial', 'included'])
            report = (root / 'room/missing-report.md').read_text(encoding='utf-8')
            self.assertIn('SetMoving[2] mesh 19 frame 109', report)
            self.assertIn('TG/Glass; q3000; _SURFACE_TYPE_TRANSPARENT', report)
            self.assertIn('hall/window\\|large', report)
            self.assertIn('missing-view.jpg', report)
            self.assertTrue((root / 'missing-summary.json').is_file())
            self.assertTrue((root / 'missing-report.md').is_file())

    def test_run_missing_or_bad_evidence_is_non_success(self):
        cases = ('missing on', 'malformed json', 'menu', 'camera moved', 'camera zoom', 'wrong scenario', 'no screenshot', 'On before Off')
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='missing-analysis-') as temporary:
                root = Path(temporary); fixture(root); path = root / 'room/on/missing.json'
                if case == 'missing on':
                    path.unlink()
                elif case == 'malformed json':
                    path.write_text('{broken')
                elif case == 'menu':
                    (path.parent / 'step.json').write_text(json.dumps({'traced': True, 'menuOpen': True}))
                elif case == 'no screenshot':
                    (path.parent / 'missing-view.jpg').unlink()
                else:
                    doc = missing.read(path)
                    if case == 'camera moved': doc['camera']['position'][0] += 1
                    if case == 'camera zoom': doc['camera']['fov'] = 70
                    if case == 'wrong scenario': doc['scenario'] = 'another'
                    if case == 'On before Off': doc['frame'] = 90
                    path.write_text(json.dumps(doc))
                result = missing.analyze(root)
                self.assertFalse(result['ok'], case)
                self.assertIn('INCOMPLETE', (root / 'missing-report.md').read_text())

    def test_all_five_scenes_have_boxes_and_safe_recipe(self):
        plan = missing.read(ROOT / 'tests/ingame/scenarios.json')
        recipe = plan['recipes']['missing']
        self.assertEqual([s['traced'] for s in recipe], [False, True, False])
        self.assertEqual([s.get('missing', False) for s in recipe], [True, True, False])
        self.assertTrue(all('neural' not in s and s['frames'] == 0 for s in recipe))
        self.assertIn({'key': 'Denoising', 'value': 'None'}, recipe[1]['set'])
        scenes = {s['key']: s for s in plan['scenarios']}
        for key in ('vista', 'npc-day', 'npc-candle', 'stairwell', 'forest'):
            self.assertTrue(scenes[key]['missingBoxes'])
            for box in scenes[key]['missingBoxes']:
                self.assertTrue(missing.rectangle(box['rect']))
                self.assertTrue(box['inspectionRows'])


if __name__ == '__main__':
    unittest.main()
