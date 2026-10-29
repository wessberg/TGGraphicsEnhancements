"""A grade attached to moving geometry is stable; an added colour pulse is not."""
import io
from pathlib import Path
import sys
import types
import unittest
import cv2
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/harness'))
import neural_temporal as analysis


class TrackedColour(unittest.TestCase):
    def fixture(self):
        y, x = np.mgrid[:160, :240].astype(np.float32)
        old = np.stack((x*.7, y*.6, (x+y)*.3), axis=-1)
        now = cv2.warpAffine(old, np.float32([[1, 0, 5], [0, 1, -3]]), (240, 160))
        yy, xx = np.mgrid[30:130:10, 30:210:10].astype(np.float32)
        points = np.column_stack((xx.ravel(), yy.ravel()))
        flow = np.tile([-5, 3], (len(points), 1)).astype(np.float32)
        return now, old, points, flow

    def test_moving_grade_is_not_flicker(self):
        self.assertLess(np.abs(analysis.colour_change(*self.fixture())).max(), .001)

    def test_colour_pulse_is_measured(self):
        now, old, points, flow = self.fixture()
        now += [6, -4, 2]
        np.testing.assert_allclose(analysis.colour_change(now, old, points, flow),
                                   np.tile([6, -4, 2], (len(points), 1)), atol=.001)

    def test_invalid_samples_are_rejected(self):
        now, old, points, flow = self.fixture()
        flow[0] = [500, 0]
        with self.assertRaises(ValueError): analysis.colour_change(now, old, points, flow)
        flow[0] = np.nan
        with self.assertRaises(ValueError): analysis.colour_change(now, old, points, flow)


def mutations():
    global analysis
    original = analysis
    source = Path(original.__file__).read_text(encoding='utf-8')
    cases = [('sample(previous, points + flow)', 'sample(previous, points)', 'test_moving_grade_is_not_flicker'),
             ('return sample(current, points) - sample(previous, points + flow)',
              'return np.zeros((len(points), 3))', 'test_colour_pulse_is_measured')]
    try:
        for before, after, name in cases:
            assert source.count(before) == 1
            analysis = types.ModuleType('mutant')
            exec(compile(source.replace(before, after), original.__file__, 'exec'), analysis.__dict__)
            result = unittest.TextTestRunner(stream=io.StringIO()).run(unittest.TestSuite([TrackedColour(name)]))
            assert result.failures and not result.errors, f'Mutation escaped: {name}'
    finally:
        analysis = original
    print('Two deliberate tracking-analysis faults rejected.')


if __name__ == '__main__':
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(TrackedColour))
    if not result.wasSuccessful(): raise SystemExit(1)
    mutations()
