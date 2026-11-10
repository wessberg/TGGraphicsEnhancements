"""CPU checks for active SDKs and informational cache-version matching."""
from pathlib import Path
import importlib.util
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("audit_sdks",ROOT/"tools/maintenance/audit_sdks.py")
sdk=importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)

class SdkAuditTests(unittest.TestCase):
    def test_cache_reference_at_line_end_is_informational(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/"DLSS-SDK-310.9.1").mkdir()
            (root/"DLSS-SDK-310.9.16").mkdir()
            (root/"build/old").mkdir(parents=True)
            (root/"build/old/CMakeCache.txt").write_text("DLSS_ROOT:PATH=C:/old/DLSS-SDK-310.9.1\nOTHER:BOOL=ON\n",encoding="utf-8")
            entry=sdk.inventory(root)["alternatives"][0]
            self.assertEqual(entry["cache_references"],["build/old/CMakeCache.txt"])
            self.assertNotIn("retain",entry["disposition"])

    def test_new_version_does_not_count_as_old_version_reference(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/"DLSS-SDK-310.9.1").mkdir()
            (root/"build/new").mkdir(parents=True)
            (root/"build/new/CMakeCache.txt").write_text("DLSS_ROOT:PATH=C:/new/DLSS-SDK-310.9.16\n",encoding="utf-8")
            entry=sdk.inventory(root)["alternatives"][0]
            self.assertEqual(entry["cache_references"],[])

    def test_pt_sdks_remain_active_dependencies(self):
        folders={entry[0] for entry in sdk.CURRENT.values()}
        for folder in ["RTXDI-Library-3.1","NRD-SDK-4.17.3","NVAPI-R615","DXC-1.9.2607","Agility-SDK-1.619.6"]:
            self.assertIn(folder,folders)

if __name__=="__main__":
    unittest.main()
