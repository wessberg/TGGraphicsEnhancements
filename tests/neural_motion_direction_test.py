"""An independent translated image must establish both vector signs, without NR input data."""
import sys
from pathlib import Path
import unittest
import cv2
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/harness'))
from neural_motion import track, scores


class Direction(unittest.TestCase):
    def test_known_translation_and_wrong_signs(self):
        rng = np.random.default_rng(42)
        previous = cv2.GaussianBlur(rng.integers(0, 256, (240, 320, 3), dtype=np.uint8), (3, 3), 0)
        for dx, dy in ((5, -3), (-4, 6)):
            current = cv2.warpAffine(previous, np.float32([[1, 0, dx], [0, 1, dy]]), (320, 240))
            points, flow = track(current, previous)
            self.assertGreater(len(points), 100)
            expected = np.broadcast_to([-dx, -dy], flow.shape)
            result = scores(expected, flow)
            self.assertLess(result['as_supplied']['median_error_pixels'], .1)
            for fault in ('flip_x', 'flip_y', 'flip_both', 'half_scale', 'double_scale'):
                self.assertGreater(result[fault]['median_error_pixels'], 2, fault)

    def test_missing_or_nonfinite_correspondences_are_not_evidence(self):
        with self.assertRaises(ValueError): scores(np.zeros((99, 2)), np.zeros((99, 2)))
        with self.assertRaises(ValueError): scores(np.full((100, 2), np.nan), np.zeros((100, 2)))


if __name__ == '__main__':
    unittest.main()
