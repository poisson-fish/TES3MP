import pathlib
import re
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKFLOW_PATH = REPOSITORY_ROOT / ".github" / "workflows" / "ci.yml"


class CiWorkflowTests(unittest.TestCase):
    def test_ci_is_consolidated_around_product_builds_and_sanitizers(self) -> None:
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        for trigger in ("\n  push:", "\n  pull_request:", "\n  workflow_dispatch:"):
            self.assertIn(trigger, workflow)
        for runner in ("ubuntu-24.04", "macos-15", "windows-2022"):
            self.assertIn(f"runs-on: {runner}", workflow)
        for preset in ("vnext-product-linux", "vnext-product-macos"):
            self.assertIn(f"cmake --preset {preset} --fresh", workflow)
            self.assertIn(f"cmake --build --preset {preset}", workflow)
        self.assertIn(".\\build_windows.ps1 -Target product -Clean", workflow)
        self.assertIn("scripts/provision_vnext_transport.py", workflow)
        self.assertIn("fetch-depth: 0", workflow)
        self.assertIn("--profile asan-ubsan --fuzz-seconds 30", workflow)
        self.assertNotIn("--profile tsan", workflow)
        self.assertIn("cancel-in-progress: true", workflow)
        self.assertNotRegex(workflow, r"uses:\s+[^\s@]+@v\d")

    def test_obsolete_vnext_workflows_are_removed(self) -> None:
        workflows = REPOSITORY_ROOT / ".github" / "workflows"
        self.assertEqual(list(workflows.glob("vnext-*.yml")), [])
        self.assertEqual(len(re.findall(r"^  (linux|macos|windows|asan-ubsan):$", WORKFLOW_PATH.read_text(), re.M)), 4)


if __name__ == "__main__":
    unittest.main()
