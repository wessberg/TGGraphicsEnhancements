"""NR harness evidence checks; mutations must reject false positive reports."""
import importlib.util
from pathlib import Path
import unittest

PATH = Path(__file__).resolve().parents[1] / "tools/harness/neural_analysis.py"
spec = importlib.util.spec_from_file_location("neural_analysis", PATH)
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class Evidence(unittest.TestCase):
    def test_local_colour_detects_cancelling_patches_and_ignores_fixed_grade(self):
        import numpy as np
        baseline = np.full((6, 2, 2, 3), 100, dtype=float)
        fixed = baseline + 10
        result, spread = analysis.local_temporal(baseline, fixed, 2)
        self.assertEqual(result["max_step"], 0)
        self.assertEqual(result["hold_max_range"], 0)
        changed = fixed.copy()
        changed[4:, 0, :, 0] += 20
        changed[4:, 1, :, 0] -= 20
        self.assertEqual(float(changed[0].mean()), float(changed[-1].mean()))
        result, spread = analysis.local_temporal(baseline, changed, 2)
        self.assertEqual(result["hold_max_range"], 20)
        self.assertEqual(result["max_step"], 20)
        vanished = baseline.copy(); vanished[:3] += 10
        result, _ = analysis.local_temporal(baseline, vanished, 2)
        self.assertEqual(result["changed_area_range"], 100)
        with self.assertRaises(ValueError): analysis.local_temporal(baseline, changed[:5], 2)

    def test_motion_requires_the_same_route(self):
        frame = dict(px=0, py=2, pz=1, rx=0, ry=359.99, rz=0, fov=60)
        before = dict(frames=[frame])
        self.assertEqual(analysis.matched_motion(before, before), "")
        self.assertTrue(analysis.matched_motion(before, dict(frames=[])))
        for field, value in [("px", .002), ("ry", 1), ("fov", 59), ("px", float("nan")), ("fov", float("nan"))]:
            self.assertTrue(analysis.matched_motion(before, dict(frames=[dict(frame, **{field: value})])))

    def test_colour_changes_are_relative_to_off_at_each_pose(self):
        reference = [[10, 20, 30], [90, 100, 110], [10, 20, 30]]
        constant_grade = [[v + 5 for v in rgb] for rgb in reference]
        measured = analysis.colour_motion(reference, constant_grade)
        self.assertAlmostEqual(measured["luma_shift_range"], 0)
        self.assertAlmostEqual(measured["mean_luma_shift"], 5)
        constant_grade[1] = [v + 20 for v in constant_grade[1]]
        self.assertAlmostEqual(analysis.colour_motion(reference, constant_grade)["luma_shift_range"], 20)

    def test_evaluation_evidence_is_required(self):
        self.assertEqual(analysis.counters({"neural": True}, {"neuralEvaluations": 8}), "")
        self.assertTrue(analysis.counters({"neural": True}, {"neuralEvaluations": 0}))
        self.assertTrue(analysis.counters({"neural": True}, {}))
        self.assertTrue(analysis.counters({"neural": False}, {"neuralEvaluations": 8}))
        self.assertEqual(analysis.counters({"neural": False}, {"neuralEvaluations": 0}), "")
        self.assertEqual(analysis.counters({}, {}), "")

    def test_off_and_neutral_are_compared_with_pt_still_on(self):
        class Picture:
            ok = True

        steps = []
        for name, on, strength, count in [("before", False, 1, 0), ("neutral", True, 0, 8), ("after", False, 1, 0)]:
            request = dict(name=name, neural=on, set=[dict(plugin=analysis.NR, key="Strength", value=strength)])
            result = dict(neuralEvaluations=count, frozen=True, traced=True,
                          frames=[dict(px=0, py=0, pz=0, rx=0, ry=0, rz=0, fov=60)])
            steps.append((request, result, Path(name), Picture()))
        for same in (True, False):
            checked = []

            def restore(before, after, limits, output):
                checked.append(output.name)
                return dict(same_picture=same, difference=0 if same else 5, block_max=0 if same else 9)

            records, problems = analysis.analyze(steps, Path("."), {}, restore)
            self.assertEqual(len(records), 3)
            self.assertEqual(checked, ["nr-restore-neutral.jpg", "nr-restore-after.jpg"])
            self.assertEqual(len(problems), 0 if same else 2)

    def test_every_frame_runs_before_lens_effects(self):
        frames = [dict(frame=i, neuralFrame=i, neuralEvaluated=i+50, neuralBeforeLensEffects=True) for i in range(3)]
        result = dict(neuralEvaluations=3, frames=frames)
        self.assertEqual(analysis.counters(dict(neural=True), result), "")
        for key, value in [("neuralFrame", 0), ("neuralBeforeLensEffects", False), ("neuralEvaluated", 99)]:
            broken = dict(result, frames=[dict(frame) for frame in frames])
            broken["frames"][1][key] = value
            self.assertTrue(analysis.counters(dict(neural=True), broken), key)

    def test_restoration_rejects_a_changed_camera(self):
        class Picture:
            ok = True

        pose = dict(px=0, py=0, pz=0, rx=0, ry=0, rz=0, fov=60)
        steps = []
        for name, frame in [("before", pose), ("after", dict(pose, rx=.37))]:
            steps.append((dict(name=name, neural=False),
                          dict(neuralEvaluations=0, frozen=True, traced=False, frames=[frame]),
                          Path(name), Picture()))
        checked = []

        def restore(*args):
            checked.append(True)
            return dict(same_picture=True)

        records, problems = analysis.analyze(steps, Path("."), {}, restore)
        self.assertEqual(checked, [])
        self.assertEqual(len(problems), 1)
        self.assertIn("static pose differs", records[1]["restoration_error"])
        self.assertNotIn("restoration", records[1])

    def test_blur_comparisons_have_their_own_off_reference(self):
        class Picture:
            ok = True
            def __init__(self, value): self.frame_rgb = [[value, value, value]]

        pose = dict(px=0, py=0, pz=0, rx=0, ry=0, rz=0, fov=60)
        steps = []
        for name, suppress, on, value in [("off", False, False, 10), ("on", False, True, 11),
                                           ("sharp-off", True, False, 100), ("sharp-on", True, True, 102)]:
            request = dict(name=name, neural=on, motionFrames=1, slide=.65, disableMotionBlur=suppress)
            result = dict(neuralEvaluations=int(on), frozen=True, traced=False, frames=[pose])
            steps.append((request, result, Path(name), Picture(value)))
        records, problems = analysis.analyze(steps, Path("."), {}, None)
        self.assertEqual(problems, [])
        self.assertAlmostEqual(records[1]["motion_colour"]["mean_luma_shift"], 1)
        self.assertAlmostEqual(records[3]["motion_colour"]["mean_luma_shift"], 2)

    def test_mutations_are_detected(self):
        source = PATH.read_text(encoding="utf-8")
        for old, new, check in [
            ('spread = np.ptp(held, axis=0).max(axis=-1)', 'spread = np.zeros(held.shape[1:3])', 'test_local_colour_detects_cancelling_patches_and_ignores_fixed_grade'),
            ("position > .001", "position > 100", "test_motion_requires_the_same_route"),
            ("np.asarray(after, dtype=float) - np.asarray(before, dtype=float)", "np.asarray(after, dtype=float)", "test_colour_changes_are_relative_to_off_at_each_pose"),
            ("if expected and evaluations == 0:", "if False:", "test_evaluation_evidence_is_required"),
            ("if not check[\"same_picture\"]:", "if False:", "test_off_and_neutral_are_compared_with_pt_still_on"),
            ('mismatch = matched_motion(baseline[0], result, "static")', 'mismatch = ""', "test_restoration_rejects_a_changed_camera"),
            ('bool(request.get("disableMotionBlur", False))', 'False', "test_blur_comparisons_have_their_own_off_reference"),
            ('frame["neuralBeforeLensEffects"] is not (not late)', 'False', "test_every_frame_runs_before_lens_effects"),
            ('frame["neuralFrame"] != frame.get("frame")', 'False', "test_every_frame_runs_before_lens_effects"),
            ('current["neuralEvaluated"] - previous["neuralEvaluated"] != current["frame"] - previous["frame"]', 'False', "test_every_frame_runs_before_lens_effects"),
        ]:
            self.assertEqual(source.count(old), 1)
            saved = analysis.__dict__.copy()
            try:
                exec(compile(source.replace(old, new), str(PATH), "exec"), analysis.__dict__)
                with self.assertRaises(AssertionError):
                    getattr(self, check)()
            finally:
                analysis.__dict__.update(saved)


if __name__ == "__main__":
    unittest.main()
