import json
import subprocess
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class OpenMWVRTargetTests(unittest.TestCase):
    def test_repository_target_passes(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/verify_openmw_vr_target.py")],
            cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_provenance_uses_exact_immutable_inputs(self):
        data = json.loads((ROOT / "docs/vnext/OPENMW_VR_PROVENANCE.json").read_text(encoding="utf-8"))
        self.assertEqual(data["upstream"]["ref"], "openmw-vr-0.51-rc1")
        self.assertEqual(data["upstream"]["commit"], "56a8e01390507375c9c2f2593e1c09e0df88c505")
        self.assertEqual(data["openxr"]["commit"], "1ca7bec6b531185530c9b4f1e7a50e1fd55e7641")
        self.assertEqual(data["openxr"]["license"], "Apache-2.0")
        self.assertEqual(len(data["openxr"]["archive_sha256"]), 64)
        self.assertEqual(len(data["openxr"]["license_sha256"]), 64)
        self.assertEqual(
            data["openxr"]["acquisition_command"],
            "python scripts/prepare_openmw_vr_dependencies.py")

    def test_registry_records_all_initial_conflicts(self):
        data = json.loads((ROOT / "docs/vnext/OPENMW_VR_PATCH_REGISTRY.json").read_text(encoding="utf-8"))
        self.assertEqual(len(data["observed_p8_overlap_paths"]), 10)
        paths = set(data["merge_resolutions"][0]["paths"])
        self.assertEqual(paths, {
            ".github/workflows/push.yml", "README.md", "apps/openmw/CMakeLists.txt",
            "apps/openmw/main.cpp", "apps/openmw/mwrender/renderingmanager.cpp",
            "apps/openmw/mwrender/vismask.hpp",
        })

    def test_registry_records_slice92_merge_parents(self):
        data = json.loads((ROOT / "docs/vnext/OPENMW_VR_PATCH_REGISTRY.json").read_text(encoding="utf-8"))
        resolution = next(value for value in data["merge_resolutions"] if value["id"] == "P9-002")
        self.assertEqual(set(resolution["paths"]), {"apps/openmw/CMakeLists.txt", "apps/openmw/main.cpp"})
        self.assertIn("2b866511bba6b72e2ae85f28216a8a94982fcae7", resolution["purpose"])
        self.assertIn("54999379a2f7ee7aa5c10beee3ca8a741a70bf80", resolution["purpose"])

    def test_vr_update_rehearsal_does_not_move_the_maintained_target_on_failure(self):
        branch = "refs/heads/vnext-vr"
        before = subprocess.check_output(
            ["git", "rev-parse", branch], cwd=ROOT, text=True).strip()
        data = json.loads((ROOT / "docs/vnext/OPENMW_VR_PROVENANCE.json").read_text(encoding="utf-8"))
        rehearsal = subprocess.run(
            ["git", "merge-tree", "--write-tree", *data["integration"]["parents"]],
            cwd=ROOT, capture_output=True, text=True)
        after = subprocess.check_output(
            ["git", "rev-parse", branch], cwd=ROOT, text=True).strip()
        self.assertNotEqual(rehearsal.returncode, 0)
        self.assertEqual(before, after)


if __name__ == "__main__":
    unittest.main()
