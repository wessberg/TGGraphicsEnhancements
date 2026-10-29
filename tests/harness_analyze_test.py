#!/usr/bin/env python3
"""Checks tools/harness/analyze.py on made-up runs, with no game: a picture that boils must score as boiling, a game
picture that did not come back after tracing must fail, a light that kept the wrong dimmer must fail, and a flame that
flickers by itself must not. Each check has its opposite beside it, so none can pass by never failing.

  python tests/harness_analyze_test.py
"""
import importlib.util
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("analyze", ROOT / "tools" / "harness" / "analyze.py")
analyze = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analyze)

W, H, FRAMES = 320, 192, 8


def scene():
    """A still picture: a gradient with a bright window and fine stripes (the "hair")."""
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    base = 40 + 60 * x / W + 20 * y / H
    base[40:90, 200:260] += 90
    base[100:160, 120:200] += 25 * ((x[100:160, 120:200] // 2) % 2)
    return np.repeat(base[:, :, None], 3, axis=2)


def write_step(folder, name, picture, rng, pixel_noise=0.0, block_noise=0.0, traced=False, audit=None, audit_later=None, perf=None):
    step = folder / name
    step.mkdir(parents=True)
    frames = []
    for i in range(FRAMES):
        frame = picture.copy()
        if pixel_noise:
            frame += rng.normal(0, pixel_noise, frame.shape[:2])[:, :, None]
        if block_noise:
            blocks = rng.normal(0, block_noise, (H // 16, W // 16))
            frame += np.kron(blocks, np.ones((16, 16), dtype=np.float32))[:, :, None]
        rgba = np.concatenate([np.clip(frame, 0, 255).astype(np.uint8), np.full((H, W, 1), 255, np.uint8)], axis=2)
        rgba[::-1].tofile(step / f"f{i:02d}.raw")  # rows start at the image's bottom, as the plugin writes them
        frames.append({"file": f"f{i:02d}.raw", "frame": 1000 + i})
    data = {"name": name, "error": "", "applied": [], "expectedTraced": traced, "traced": traced, "rayReconstruction": traced, "nrd": False,
            "rayRegeneration": False, "width": W, "height": H, "scale": 1, "format": "RGBA32", "frozen": True, "cameraMoved": False,
            "frames": frames, "audits": [], "framesTraced": 0, "perf": perf or {"measured": False}}
    if audit is not None:
        (step / "audit-a.txt").write_text("\n".join(f"{k}\t{v}" for k, v in sorted(audit.items())) + "\n", encoding="utf-8")
        (step / "audit-b.txt").write_text("\n".join(f"{k}\t{v}" for k, v in sorted((audit_later or audit).items())) + "\n", encoding="utf-8")
        data["audits"] = ["audit-a.txt", "audit-b.txt"]
    (step / "step.json").write_text(json.dumps(data), encoding="utf-8")
    return {"name": name, "set": [{"key": "Mode", "value": "On" if traced else "Off"}], "frames": FRAMES}


class AnalyzeRun(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp(prefix="tgpt-harness-test-"))
        self.rng = np.random.default_rng(7)

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def run_case(self, after_picture=None, after_audit=None, traced_boil=4.0, fps=70.0, baseline_later=None, flame=False):
        picture = scene()

        def lit(base, level):
            """The picture with a flame's patch that much brighter: it flickers by itself from take to take."""
            if not flame:
                return base
            out = base.copy()
            out[100:148, 30:78] += level
            return out
        audit = {"light|Room/Candle#12|lightDimmer": "1", "light|Room/Candle#12|intensity": "25000", "stack|Fog.meanFreePath": "40",
                 "light|Room/Torch#13|intensity": "900.5", "material|Hair#77": "shader=TG/Character/RealHair; queue=2450; keywords=A B; off=; _AlphaCutoff=0.1",
                 "info|lights|count": "2"}
        scenario = self.dir / "room"
        scenario.mkdir()
        steps = [
            write_step(scenario, "1-game", lit(picture, 0), self.rng, pixel_noise=0.6, audit=audit, audit_later=baseline_later),
            write_step(scenario, "2-traced-rr", picture * 1.1, self.rng, pixel_noise=1.0, block_noise=traced_boil, traced=True),
            write_step(scenario, "3-game-after-rr", lit(picture if after_picture is None else after_picture, 4), self.rng, pixel_noise=0.6,
                       audit=audit if after_audit is None else after_audit),
        ]
        if flame:
            # A second take after tracing, as the full recipe has it, with the flame at another brightness again.
            steps.append(write_step(scenario, "5-game-after-toggles", lit(picture if after_picture is None else after_picture, -4), self.rng, pixel_noise=0.6,
                                    audit=audit if after_audit is None else after_audit))
        steps += [
            write_step(scenario, "7-perf-traced-rr", picture, self.rng, traced=True,
                       perf={"measured": True, "fps": fps, "lowFps": fps * 0.8, "frameMs": 1000 / fps, "medianMs": 1000 / fps, "p99Ms": 1250 / fps, "worstMs": 30,
                             "tracerGpuMs": 8.0, "tracerRenderThreadMs": 0.4, "unityGpuMs": 0, "unityCpuMs": 0, "stages": "", "vSyncCount": 0, "targetFrameRate": -1, "frames": 500}),
        ]
        (scenario / "scenario.json").write_text(json.dumps({"key": "room", "save": "PT room", "error": "", "slot": "Slot_1", "slotName": "PT room", "location": "A room",
                                                             "scenes": "Room", "px": 0, "py": 0, "pz": 0, "rx": 0, "ry": 0, "weatherTime": "noon", "loadSeconds": 20}), encoding="utf-8")
        (self.dir / "request.json").write_text(json.dumps({"run": "test", "scenarios": [{"key": "room", "save": "PT room", "steps": steps}]}), encoding="utf-8")
        (self.dir / "done.json").write_text(json.dumps({"run": "test", "error": "", "ok": True, "scenarios": 1, "failed": 0, "savesBlocked": 0, "seconds": 60}), encoding="utf-8")
        (self.dir / "saves-check.json").write_text(json.dumps({"files": 3, "untouched": True, "changes": []}), encoding="utf-8")
        (self.dir / "gpu.json").write_text(json.dumps({"waitedSeconds": 90, "before": "", "during": {}, "samples": 4, "readable": True}), encoding="utf-8")
        plan = self.dir / "plan.json"
        plan.write_text(json.dumps({"scenarios": [{"key": "room", "save": "PT room", "tests": "a test room", "roi": [0.3, 0.4, 0.7, 0.9]}]}), encoding="utf-8")
        argv = sys.argv
        sys.argv = ["analyze.py", str(self.dir), "--plan", str(plan), "--targets", str(ROOT / "tests" / "ingame" / "targets.json")]
        try:
            code = analyze.main()
        finally:
            sys.argv = argv
        return code, json.loads((self.dir / "summary.json").read_text(encoding="utf-8"))

    def step(self, summary, name):
        return next(s for s in summary["scenarios"][0]["steps"] if s["name"] == name)

    def test_a_clean_run_has_no_problems_and_writes_its_pictures(self):
        code, summary = self.run_case(traced_boil=0.0)
        self.assertEqual(summary["problems"], [])
        self.assertEqual(code, 0)
        for name in ["room/overview.jpg", "room/side-by-side.jpg", "room/game-vs-traced.jpg", "room/1-game/atlas.jpg", "room/1-game/frame.jpg", "room/restore-3-game-after-rr.jpg", "report.md"]:
            self.assertTrue((self.dir / name).exists(), name)
        # The frames are deleted once they are measured (unless asked to keep them).
        self.assertFalse((self.dir / "room" / "1-game" / "f00.raw").exists())
        restore = summary["scenarios"][0]["restore"][0]
        self.assertTrue(restore["same_picture"])
        self.assertEqual(restore["state"]["changed_count"], 0)
        # The traced picture is 1.1 times as bright, and says so.
        self.assertAlmostEqual(self.step(summary, "2-traced-rr")["metrics"]["luma_vs_game"], 1.1, delta=0.02)

    def test_boiling_is_measured_and_flagged(self):
        _, quiet = self.run_case(traced_boil=0.0)
        calm = self.step(quiet, "2-traced-rr")["metrics"]
        self.setUp()
        _, loud = self.run_case(traced_boil=6.0)
        boiling = self.step(loud, "2-traced-rr")["metrics"]
        self.assertGreater(boiling["boil"], 4 * calm["boil"])
        # Single-pixel noise averages out of the 8x8 blocks: the game's picture has it too and does not count as boiling.
        game = self.step(loud, "1-game")["metrics"]
        self.assertGreater(game["flicker"], 3 * game["boil"])
        self.assertTrue(any("boils" in p for p in loud["problems"]), loud["problems"])
        self.assertFalse(any("boils" in p for p in quiet["problems"]), quiet["problems"])

    def test_a_game_picture_that_is_not_back_fails(self):
        dimmer = scene()
        dimmer[20:84, 30:94] *= 0.93  # one light came back seven percent too dim, in a 64-pixel patch
        code, summary = self.run_case(after_picture=dimmer)
        self.assertEqual(code, 1)
        self.assertFalse(summary["scenarios"][0]["restore"][0]["same_picture"])
        self.assertTrue(any("picture is not back" in p for p in summary["problems"]), summary["problems"])

    def test_a_value_that_is_not_back_fails_and_a_flicker_does_not(self):
        audit = {"light|Room/Candle#12|lightDimmer": "0", "light|Room/Candle#12|intensity": "25000", "stack|Fog.meanFreePath": "40",
                 "light|Room/Torch#13|intensity": "900.5", "material|Hair#77": "shader=TG/Character/RealHair; queue=2450; keywords=A B; off=; _AlphaCutoff=0.5",
                 "info|lights|count": "3"}
        code, summary = self.run_case(after_audit=audit)
        self.assertEqual(code, 1)
        state = summary["scenarios"][0]["restore"][0]["state"]
        self.assertEqual(sorted(c["key"] for c in state["changed"]), ["light|Room/Candle#12|lightDimmer", "material|Hair#77"])
        self.assertIn("_AlphaCutoff: 0.1 -> 0.5", next(c["what"] for c in state["changed"] if c["key"] == "material|Hair#77"))
        self.assertTrue(any("state is not back" in p for p in summary["problems"]), summary["problems"])
        # A torch whose intensity differs between the two audits of one step changes by itself: not the tracer's doing.
        self.setUp()
        flicker = {"light|Room/Candle#12|lightDimmer": "1", "light|Room/Candle#12|intensity": "25000", "stack|Fog.meanFreePath": "40",
                   "light|Room/Torch#13|intensity": "870.25", "material|Hair#77": "shader=TG/Character/RealHair; queue=2450; keywords=A B; off=; _AlphaCutoff=0.1",
                   "info|lights|count": "2"}
        code, summary = self.run_case(after_audit=flicker, baseline_later=flicker, traced_boil=0.0)
        self.assertEqual(summary["scenarios"][0]["restore"][0]["state"]["changed_count"], 0)
        self.assertEqual(summary["scenarios"][0]["restore"][0]["state"]["volatile"], 1)
        self.assertEqual(code, 0, summary["problems"])

    def test_what_moves_by_itself_is_not_a_leftover_and_a_leftover_beside_it_still_is(self):
        # A flame 4 levels brighter in one take after tracing and 4 darker in the next: it moves by itself (the two takes
        # after tracing differ there), so the picture counts as back.
        code, summary = self.run_case(traced_boil=0.0, flame=True)
        checks = summary["scenarios"][0]["restore"]
        self.assertEqual(len(checks), 2)
        self.assertTrue(all(c["same_picture"] for c in checks), checks)
        self.assertEqual(code, 0, summary["problems"])
        # The same flame, and a patch elsewhere that came back seven percent too dim in both takes: that one is a leftover
        # (the two takes agree on it), and the flame beside it must not excuse it.
        self.setUp()
        dimmer = scene()
        dimmer[20:84, 200:264] *= 0.93
        code, summary = self.run_case(traced_boil=0.0, flame=True, after_picture=dimmer)
        checks = summary["scenarios"][0]["restore"]
        self.assertFalse(any(c["same_picture"] for c in checks), checks)
        self.assertAlmostEqual(checks[0]["worst_at"][0], 232 / W, delta=0.08)
        self.assertEqual(code, 1)

    def test_a_slow_frame_rate_misses_the_target(self):
        _, fast = self.run_case(traced_boil=0.0, fps=70.0)
        self.assertFalse(any("fps, target" in p for p in fast["problems"]), fast["problems"])
        self.setUp()
        _, slow = self.run_case(traced_boil=0.0, fps=24.0)
        self.assertTrue(any("fps, target" in p for p in slow["problems"]), slow["problems"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
