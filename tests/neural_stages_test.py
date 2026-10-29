"""Reject misleading NR stage evidence, including deliberate faults in the analyser."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import numpy as np

SOURCE = Path(__file__).resolve().parents[1] / 'tools/harness/neural_stages.py'
spec = importlib.util.spec_from_file_location('neural_stages', SOURCE)
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class Evidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        (self.folder / 'nr-stages').mkdir()
        self.request = dict(frames=6, motionFrames=1)
        self.frames = [dict(frame=i, neuralFrame=i, px=0, py=0, pz=0, rx=0, ry=0, rz=0, fov=60) for i in range(100, 106)]
        self.rows = [dict(frame=i, camera=7, flags=9, reset=0, exposurePresent=1) for i in range(100, 106)]
        self.values = np.zeros((6, 5, 36, 64, 4), dtype='<f4')
        self.exposures = np.tile([.125, 13, 0, 1], (6, 1)).astype('<f4')

    def replay(self, reset=False, every=False):
        self.request.update(neuralReplay=True, neuralReplayReset=reset, neuralReplayResetEveryFrame=every)
        self.values = np.zeros((6, 6, 36, 64, 4), dtype='<f4')
        for i, row in enumerate(self.rows):
            row.update(replaySourceFrame=102 if i >= 2 else 0, replayReset=int(reset and i >= 2),
                       replayResetEveryFrame=int(every and i >= 2))
            row['reset'] = int(reset and (i >= 2 if every else i == 2))

    def write(self):
        (self.folder / 'step.json').write_text(json.dumps(dict(frames=self.frames, error='', menuOpen=False)))
        with (self.folder / 'nr-stages/frames.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=self.rows[0].keys()); writer.writeheader(); writer.writerows(self.rows)
        for i in range(6):
            count = self.values.shape[1]
            header = np.array([0x3144524e, {5: 1, 6: 2, 7: 3, 9: 4}[count], i+100, 7, 64, 36, count, self.rows[i]['flags']], dtype='<u4')
            (self.folder / f'nr-stages/nr-{i+100}.bin').write_bytes(header.tobytes()+self.values[i].tobytes()+self.exposures[i].tobytes())

    def result(self):
        self.write()
        return analysis.analyze_step(self.folder, self.request)

    def test_effect_is_separate_from_input_and_detects_local_change(self):
        self.values[:, 1, ..., :3] = np.arange(6).reshape(6, 1, 1, 1) / 255
        self.values[:, 2] = self.values[:, 1]
        self.assertEqual(self.result()['model_effect']['hold_max_range'], 0)
        self.values[4:, 2, :, :32, 0] += 10 / 255
        self.values[4:, 2, :, 32:, 0] -= 10 / 255
        r = self.result()
        self.assertAlmostEqual(r['model_effect']['hold_max_range'], 10, places=5)
        self.assertAlmostEqual(max(r['model_effect']['mean_curve']), 0, places=5)

    def test_stale_frame_is_not_same_frame_evidence(self):
        self.frames[-1]['neuralFrame'] -= 1
        with self.assertRaises(ValueError): self.result()

    def test_pixel_ranges_preserve_extrema_and_weight_fractions(self):
        self.values = np.zeros((6, 9, 36, 64, 4), dtype='<f4')
        for row in self.rows: row.update(width=130, height=73)
        self.values[:, 7:, ..., 0] = .02
        self.values[:, 7:, ..., 1] = .9
        self.values[-1, 8, -1, -1] = [-.01, 1.2, 1/9, 2/9]
        result = self.result()
        self.assertAlmostEqual(result['model_input_range']['minimum'], .02)
        output = result['model_output_range']
        self.assertAlmostEqual(output['minimum'], -.01)
        self.assertAlmostEqual(output['maximum'], 1.2)
        # Bottom-right cell is 3x3 at this resolution; averaging cells equally would be wrong.
        self.assertAlmostEqual(output['negative_fraction_max'], 1/(130*73))
        self.assertAlmostEqual(output['fallback_fraction_max'], 2/(130*73))
        self.assertEqual(output['fallback_fraction_curve'][:-1], [0]*5)

    def test_tone_map_comparison_records_mode_and_initial_reset(self):
        self.request['neuralToneMap'] = 1
        for row in self.rows: row['toneMap'] = 1
        self.rows[0]['reset'] = 1
        result = self.result()
        self.assertEqual(result['tone_map'], 1)
        self.assertEqual(result['resets'], 1)

    def test_non_gameplay_frames_are_not_model_evidence(self):
        self.write()
        path = self.folder / 'step.json'
        step = json.loads(path.read_text())
        step['menuOpen'] = True
        path.write_text(json.dumps(step))
        with self.assertRaisesRegex(ValueError, 'Invalid game capture'):
            analysis.analyze_step(self.folder, self.request)

    def test_missing_exposure_is_not_constant_exposure(self):
        self.rows[-1]['exposurePresent'] = 0
        with self.assertRaises(ValueError): self.result()

    def test_invalid_pixel_is_not_a_zero_measurement(self):
        self.values[3, 2, 0, 0, 3] = .01
        with self.assertRaises(ValueError): self.result()

    def test_moving_camera_is_not_stationary_instability(self):
        self.frames[-1]['px'] = .002
        with self.assertRaises(ValueError): self.result()

    def test_exposure_is_measured_independently_of_brightness(self):
        self.exposures[4:, 0] = .25
        self.assertEqual(self.result()['hold_exposure_range'][0], .125)

    def test_input_conditioning_must_match_the_plan(self):
        self.request['neuralInputScale'] = 4
        with self.assertRaises(ValueError): self.result()
        for row in self.rows: row['inputScale'] = 4
        self.assertEqual(self.result()['input_scale'], 4)
        self.rows[-1]['inputScale'] = .25
        with self.assertRaises(ValueError): self.result()

    def test_native_controls_must_match_requested_model_and_placement(self):
        controls = {'Model': 'B', 'Automatic Mask': 'On', 'Strength': '1', 'Local Tone': '1',
                    'Detail': '1', 'Skin Detail': '0.75', 'Placement': 'Before Upscaling'}
        self.request['set'] = [dict(plugin='com.wessberg.tgneuralrendering', key=k, value=v)
                               for k, v in controls.items()]
        for row in self.rows:
            row.update(model=1, mask=1, strength=1, tone=1, detail=1, skin=.75, flags=25)
        self.result()
        # Each fault leaves the request intact, changing only the observed native evaluation.
        for key, value in [('model', 0), ('mask', 0), ('strength', 0), ('tone', 0), ('detail', 0),
                           ('skin', 1), ('flags', 9), ('model', float('nan'))]:
            previous = self.rows[-1][key]
            self.rows[-1][key] = value
            with self.assertRaisesRegex(ValueError, 'differs from the plan'):
                self.result()
            self.rows[-1][key] = previous

    def test_pass_count_must_match_plan(self):
        self.request['set'] = [dict(plugin='com.wessberg.tgneuralrendering', key='Passes', value='2')]
        with self.assertRaisesRegex(ValueError, 'pass count differs'): self.result()
        for row in self.rows: row['passes'] = 2
        self.assertEqual(self.result()['passes'], 2)
        self.rows[-1]['passes'] = 1
        with self.assertRaisesRegex(ValueError, 'pass count differs'): self.result()

    def test_replay_reports_output_drift_with_constant_inputs(self):
        for reset in (False, True):
            self.replay(reset)
            self.values[4:, 2, ..., 0] = 8 / 255
            self.assertAlmostEqual(self.result()['model_effect']['hold_max_range'], 8, places=5)

    def test_replay_requires_constant_colour_and_depth(self):
        for stage in (1, 5):
            self.replay()
            self.values[-1, stage, 0, 0, 0] = .01
            with self.assertRaises(ValueError): self.result()

    def test_replay_requires_zero_motion(self):
        self.replay()
        self.values[-1, 4, 0, 0, 2] = .01
        with self.assertRaises(ValueError): self.result()

    def test_replay_rejects_extra_or_missing_resets(self):
        for reset in (False, True):
            self.replay(reset)
            self.rows[2]['reset'] = int(not reset)
            with self.assertRaises(ValueError): self.result()
        self.replay(True)
        self.rows[-1]['reset'] = 1
        with self.assertRaises(ValueError): self.result()

    def test_replay_rejects_changing_source(self):
        self.replay()
        self.rows[-1]['replaySourceFrame'] += 1
        with self.assertRaises(ValueError): self.result()

    def test_repeated_reset_must_be_requested_and_applied(self):
        self.replay(True, True)
        self.assertTrue(self.result()['replay_reset_every_frame'])
        self.rows[-1]['reset'] = 0
        with self.assertRaises(ValueError): self.result()
        self.replay(True, True)
        self.rows[-1]['replayResetEveryFrame'] = 0
        with self.assertRaises(ValueError): self.result()
        self.replay(True, True)
        self.request['neuralReplayResetEveryFrame'] = False
        with self.assertRaises(ValueError): self.result()

    def test_replay_must_end_for_ordinary_takes(self):
        self.replay()
        self.request['neuralReplay'] = False
        with self.assertRaises(ValueError): self.result()


def mutation_checks():
    import io
    import types
    global analysis
    original = analysis
    mutations = [
        ("int(row.get('passes', 1)) != int(controls.get('Passes', 1))", 'False', 'test_pass_count_must_match_plan'),
        ("step.get('error') or step.get('menuOpen')", "step.get('error')", 'test_non_gameplay_frames_are_not_model_evidence'),
        ('not np.isfinite(actual) or abs(actual - value) > 1e-6', 'False', 'test_native_controls_must_match_requested_model_and_placement'),
        ("bool(int(row['flags']) & 16) != before", 'False', 'test_native_controls_must_match_requested_model_and_placement'),
        ("abs(input_scale - requested.get('neuralInputScale', 1)) > 1e-6", 'False', 'test_input_conditioning_must_match_the_plan'),
        ('values[:, 2] - values[:, 1]', 'values[:, 1] - values[:, 1]', 'test_effect_is_separate_from_input_and_detects_local_change'),
        ("frame['neuralFrame'] != number", 'False', 'test_stale_frame_is_not_same_frame_evidence'),
        ("int(row['exposurePresent']) != 1", 'False', 'test_missing_exposure_is_not_constant_exposure'),
        ('np.any(stages[:7, ..., 3] != 0)', 'False', 'test_invalid_pixel_is_not_a_zero_measurement'),
        ('np.ptp(pose) > tolerance', 'False', 'test_moving_camera_is_not_stationary_instability'),
        ("result['model_in']['hold_max_range'] != 0 or result['depth']['hold_max_range'] != 0", 'False', 'test_replay_requires_constant_colour_and_depth'),
        ("result['motion_hold_max_mean_pixels'] != 0", 'False', 'test_replay_requires_zero_motion'),
        ('resets[hold:] != expected_resets', 'False', 'test_replay_rejects_extra_or_missing_resets'),
        ('actual_every != [0] * hold + [every] * (len(frames)-hold)', 'False', 'test_repeated_reset_must_be_requested_and_applied'),
        ('replay_sources != expected_source', 'False', 'test_replay_rejects_changing_source'),
        ('elif any(replay_sources):', 'elif False:', 'test_replay_must_end_for_ordinary_takes'),
    ]
    try:
        for old, new, name in mutations:
            source = SOURCE.read_text(); assert source.count(old) == 1, old
            analysis = types.ModuleType('mutated_neural_stages')
            exec(compile(source.replace(old, new), str(SOURCE), 'exec'), analysis.__dict__)
            outcome = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.TestSuite([Evidence(name)]))
            assert outcome.failures and not outcome.errors, f'Mutation escaped or failed for the wrong reason: {name}'
    finally:
        analysis = original
    print(f'{len(mutations)} deliberate stage-analysis faults rejected.')


if __name__ == '__main__':
    outcome = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Evidence))
    if not outcome.wasSuccessful(): raise SystemExit(1)
    mutation_checks()
