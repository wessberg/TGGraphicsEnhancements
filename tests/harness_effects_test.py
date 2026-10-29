#!/usr/bin/env python3
"""Known effects records: preserve counts, identity, unknowns, phase and freeze evidence."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("effects", ROOT / "tools/harness/effects.py")
effects = importlib.util.module_from_spec(spec)
spec.loader.exec_module(effects)


def samples(changes=False, live=False):
    return [{"frame": 100 + i * 60, "realtime": 10 + i, "timeScale": 1 if live else 0,
             "shaderTimes": {"_Time": [0, i if changes else 4, 0, 0], "_TimeParameters": [4, 0, 0, 0], "_FrameCount": 100 + i * 60},
             "particles": [{"identity": {"id": 20}, "data": {"systemTime": 4,
                            "current": {"liveCount": 2, "firstParticles": [{"seed": 1, "position": [i if changes else 0, 2, 3]}]}}}],
             "vfx": [{"identity": {"id": 21}, "data": {"aliveParticleCount": 9}}]} for i in range(3)]


def record(ident, shader, count, renderer="ParticleSystemRenderer"):
    return {"identity": {"id": ident, "scene": "test", "path": f"root[{ident}]", "cameraDistance": 900, "activeInHierarchy": False},
            "rendererType": renderer, "materials": [{"shader": shader}, {"shader": shader}], "liveCount": count,
            "lights": {"attached": [{"id": 55}], "nearby": [{"id": 56}]},
            "particleSystem": {"lights": {"enabled": True}},
            "tracer": {"geometry": "not captured", "glowMaterials": [{"cachedGlowDetection": {"Item1": True}}]}}


class EffectsTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT / "build/effects-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(dir=scratch)
        self.root = Path(self.tmp.name)
        self.assertEqual(self.root.resolve().parent, scratch.resolve())

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, scenario="candle", phase="Off", records=None):
        step = "1-game-effects" if phase == "Off" else "2-traced-effects"
        folder = self.root / scenario / step
        folder.mkdir(parents=True, exist_ok=True)
        data = {"schemaVersion": 1, "scenario": scenario, "traced": phase == "On",
                "effects": records if records is not None else [record(1, "flame", 7), record(2, "flame", 3)],
                "freezeSamples": samples(), "lights": [{"identity": {"id": 55}, "data": {"tracer": {"status": "selected" if phase == "On" else "off"}}},
                                                           {"identity": {"id": 56}, "data": {"tracer": {"status": "dropped"}}}],
                "fog": {"blendedCameraFog": "unknown: no camera", "localVolumes": []}}
        (folder / "effects.json").write_text(json.dumps(data), encoding="utf-8")
        (folder.parent / "effects-live.json").write_text(json.dumps({"samples": samples(True, True)}), encoding="utf-8")
        return folder / "effects.json"

    def test_counts_unknowns_and_multiple_material_slots(self):
        self.write(records=[record(1, "flame", 7), record(2, "flame", "unknown: GPU count unavailable"), record(3, "rain", 11, "VFXRenderer")])
        report = effects.analyze(self.root)
        rows = report["censuses"][0]["families"]
        self.assertEqual((rows[0]["systems"], rows[0]["knownLiveParticles"], rows[0]["unknownLiveSystems"]), (2, 7, 1))
        self.assertEqual(rows[0]["withParticleLightModule"], 2)
        self.assertEqual(rows[0]["instances"][0]["cameraDistance"], 900)  # off-camera/inactive remains present
        self.assertEqual(len(report["censuses"][0]["unknowns"]), 2)
        self.assertEqual(report["issues"], [])

    def test_phase_totals_and_light_ownership_do_not_merge(self):
        self.write(); self.write(phase="On"); self.write(scenario="fog")
        report = effects.analyze(self.root)
        rows = {r["phase"]: r for r in report["allScenarios"]}
        self.assertEqual(rows["Off"]["systems"], 4)
        self.assertEqual(rows["Off"]["knownLiveParticles"], 20)
        self.assertEqual(rows["On"]["knownLiveParticles"], 10)
        self.assertEqual(rows["On"]["withSelectedAttachedLights"], 2)
        self.assertEqual(rows["On"]["withSelectedNearbyLights"], 0)
        self.assertEqual(rows["Off"]["withSelectedAttachedLights"], 0)
        self.assertEqual(rows["On"]["glowRecognized"], 2)
        self.assertEqual(rows["On"]["geometryRoutes"], {"not captured": 2})

    def test_unreadable_light_data_and_scan_errors_are_not_zero_or_systems(self):
        r = record(1, "flame", 7)
        r["lights"] = "unknown: scan failed"
        r["tracer"] = "unknown: plugin absent"
        self.write(records=[r, {"error": "unknown: VFX type absent"}])
        c = effects.analyze(self.root)["censuses"][0]
        self.assertEqual(c["unidentifiedRecords"], 1)
        self.assertEqual(len(c["families"]), 1)
        self.assertEqual(c["families"][0]["systems"], 1)
        self.assertEqual(c["families"][0]["unknownLightSystems"], 1)
        self.assertEqual(c["families"][0]["unknownGlowSystems"], 1)

    def test_frozen_probe_catches_particle_and_shader_movement(self):
        for moving in (False, True):
            result = effects.freeze(samples(moving))
            self.assertTrue(result["valid"])
            self.assertTrue(result["frozen"])
            self.assertIs(result["particleStateChanged"], moving)
            self.assertIs(result["shaderClockChanged"], moving)
            self.assertFalse(result["vfxCountChanged"])
        self.assertFalse(effects.freeze(samples(live=True))["frozen"])

    def test_birth_death_count_and_unknown_are_detected(self):
        seq = samples()
        seq[1]["particles"][0]["data"]["current"]["liveCount"] = 19
        seq[1]["vfx"][0]["data"]["aliveParticleCount"] = 0
        self.assertTrue(effects.freeze(seq)["particleStateChanged"])
        self.assertTrue(effects.freeze(seq)["vfxCountChanged"])
        seq = samples()
        seq[1]["particles"].append({"identity": {"id": 99}, "data": "unknown: failed read"})
        seq[1]["shaderTimes"] = "unknown: no pipeline"
        self.assertIn("unknown:", effects.freeze(seq)["particleStateChanged"])
        self.assertIn("unknown:", effects.freeze(seq)["shaderClockChanged"])
        seq = samples(); seq[1]["particles"] = []
        self.assertTrue(effects.freeze(seq)["particleStateChanged"])

    def test_spacing_missing_samples_and_frame_duplicates_are_invalid(self):
        self.assertFalse(effects.freeze(samples()[:2])["valid"])
        for field in ("realtime", "frame"):
            seq = samples(); seq[1][field] = seq[0][field]
            self.assertFalse(effects.freeze(seq)["valid"])

    def test_missing_inventory_and_bad_json_are_incomplete(self):
        path = self.write()
        request = {"scenarios": [{"key": "candle", "steps": [{"name": "1-game-effects", "effects": True}, {"name": "2-traced-effects", "effects": True}]}]}
        (self.root / "request.json").write_text(json.dumps(request))
        self.assertIn("missing inventory: candle/2-traced-effects", effects.analyze(self.root)["issues"])
        path.write_text("{")
        self.assertGreater(len(effects.analyze(self.root)["issues"]), 2)

    def test_failed_pt_activation_is_reported_without_double_counting_off(self):
        self.write()
        path = self.write(phase="On")
        data = json.loads(path.read_text())
        data["expectedTraced"], data["traced"] = True, False
        path.write_text(json.dumps(data))
        report = effects.analyze(self.root)
        self.assertTrue(any("requested PT On, compositing=False" in s for s in report["issues"]))
        self.assertEqual({r["phase"]: r["systems"] for r in report["allScenarios"]}, {"Off": 2, "On": 2})

    def test_cli_writes_readable_and_machine_reports(self):
        self.write()
        proc = subprocess.run([sys.executable, str(ROOT / "tools/harness/effects.py"), str(self.root)], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(json.loads((self.root / "effects-summary.json").read_text())["allScenarios"][0]["knownLiveParticles"], 10)
        text = (self.root / "effects-report.md").read_text()
        self.assertIn("ParticleSystemRenderer / flame | 2 | 10 | 0", text)
        self.assertIn("shader clocks changed=False", text)


if __name__ == "__main__":
    unittest.main()
