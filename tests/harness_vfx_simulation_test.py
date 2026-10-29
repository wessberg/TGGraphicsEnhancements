# SPDX-License-Identifier: MIT
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "harness"))
import vfx_simulation as capture


class SimulationEvidence(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        (self.folder / "simulation" / "programs").mkdir(parents=True)
        code = b"independently generated test program identity"
        digest = hashlib.sha256(code).hexdigest()
        (self.folder / "simulation" / "programs" / (digest + ".dxbc")).write_bytes(code)
        values = struct.pack("<6I", 999, 999, 3, 2, 1, 555)
        (self.folder / "args.bin").write_bytes(values)
        binding = {"role": "indirect", "slot": 0, "resource": {"id": 27, "dimension": 1, "bytes": len(values)},
                   "bytes": len(values), "file": "args.bin", "sha256": hashlib.sha256(values).hexdigest()}
        self.doc = {"schema": 1, "frame": 8, "previousBoundary": 7, "particleCaptureComplete": False,
                    "faults": 0, "submittedThisSample": 1, "packets": [{"serial": 12, "submittedAfter": 7,
                    "cs": digest, "executions": 1, "bindingsComplete": True, "groups": [0, 0, 0],
                    "indirect": True, "indirectOffset": 8, "buffers": [binding]}]}
        self.image = {"frame": 8, "nativeCompletedAtScreenshot": 8, "grabbed": True, "primer": False}

    def result(self, doc=None):
        return capture.validate(self.folder, self.doc if doc is None else doc, self.image)

    def test_indirect_nonzero_offset_and_retained_empty_frame(self):
        result = self.result()
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["packets"][0]["groups"], [3, 2, 1])
        self.doc["submittedThisSample"] = 0
        self.doc["packets"][0]["submittedAfter"] = 4
        result = self.result()
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["submitted"], 0)
        self.assertTrue(result["packets"][0]["retained"])

    def test_corruption_count_and_frame_mutations(self):
        for name, change in [
            ("count", lambda d: d.update(submittedThisSample=2)),
            ("frame", lambda d: d.update(frame=9)),
            ("ownership", lambda d: d["packets"][0].update(submittedAfter=8)),
            ("range", lambda d: d["packets"][0].update(indirectOffset=20)),
            ("bytes", lambda d: d["packets"][0]["buffers"][0].update(bytes=4)),
            ("values", lambda d: d["packets"][0]["buffers"][0].update(sha256="wrong")),
            ("shader", lambda d: d["packets"][0].update(cs="wrong")),
            ("replay", lambda d: d["packets"][0].update(executions=2)),
            ("bindings", lambda d: d["packets"][0].update(bindingsComplete=False)),
        ]:
            with self.subTest(name=name):
                doc = copy.deepcopy(self.doc)
                change(doc)
                self.assertTrue(self.result(doc)["errors"])

    def test_no_texture_copies(self):
        self.doc["packets"][0]["buffers"][0]["resource"]["dimension"] = 3
        self.assertIn("a texture was copied", self.result()["errors"])

    def test_missing_evidence_fails(self):
        (self.folder / "args.bin").unlink()
        self.assertIn("missing buffer file", self.result()["errors"])


if __name__ == "__main__":
    unittest.main()
