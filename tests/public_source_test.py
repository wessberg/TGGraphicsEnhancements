"""CPU checks that source export cannot accidentally include local development data."""
from pathlib import Path
import importlib.util
import json
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("public_source",ROOT/"tools/maintenance/public_source.py")
exporter=importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)

class PublicSourceTests(unittest.TestCase):
    def test_private_and_runtime_paths_rejected(self):
        for name in [".development/history/raw.md",".testruns/source.cpp","tgpt-images/raw.png",
                     "DLSS-SDK-310.9.16/include/sdk.h","Build.Local.props",".git/config",
                     "src/bin/plugin.dll","native/runtime.exe","docs/../private.md","/docs/absolute.md"]:
            with self.subTest(name=name):
                self.assertFalse(exporter.allowed(name))

    def test_source_and_product_images_allowed(self):
        for name in ["src/Plugin.cs","native/core.cpp","tests/test.py","docs/PT-STATE.md",
                     "docs/coverart.jpg","Build.Local.props.example","LICENSES/MIT.txt"]:
            self.assertTrue(exporter.allowed(name),name)

    def test_selection_uses_current_files_and_excludes_deleted_private_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/"docs").mkdir()
            (root/"docs/current.md").write_text("current",encoding="utf-8")
            (root/"docs/new.md").write_text("new",encoding="utf-8")
            listing=b"docs/current.md\0docs/new.md\0docs/deleted.md\0.testruns/raw.py\0"
            with mock.patch.object(exporter.subprocess,"check_output",return_value=listing):
                self.assertEqual([name for name,_ in exporter.source_files(root)],
                                 ["docs/current.md","docs/new.md"])

    def test_snapshot_manifest_matches_bytes_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            source=root/"source.cpp"; source.write_bytes(b"source\r\n")
            output=root/"artifacts/public-source/source.zip"
            with mock.patch.object(exporter,"ROOT",root):
                exporter.export(output,[("native/source.cpp",source)])
                with zipfile.ZipFile(output) as archive:
                    self.assertEqual(archive.read("native/source.cpp"),source.read_bytes())
                    manifest=json.loads(archive.read("SOURCE-MANIFEST.json"))
                    self.assertEqual(manifest["files"][0]["sha256"],exporter.hashlib.sha256(source.read_bytes()).hexdigest())
                    self.assertNotIn(".git",archive.namelist())
                with self.assertRaises(ValueError):
                    exporter.export(output,[("native/source.cpp",source)])
                with self.assertRaises(ValueError):
                    exporter.export(root/"outside.zip",[])

    def test_audit_flags_conversational_documentation(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"note.md"
            path.write_text("The owner says: copied conversation",encoding="utf-8")
            self.assertTrue(any("Conversational" in item for item in exporter.audit([("docs/note.md",path)])))

if __name__=="__main__":
    unittest.main()
