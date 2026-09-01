import pathlib
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
WORKFLOW_PATH = REPOSITORY_ROOT / ".github" / "workflows" / "vnext-transport-lifecycle.yml"


class TransportLifecycleWorkflowTests(unittest.TestCase):
    def test_manual_supported_platform_matrix_runs_every_phase6_contract(self):
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("on:\n  workflow_dispatch:\n", workflow)
        for automatic_trigger in ("\n  push:", "\n  pull_request:", "\n  schedule:", "\n  release:"):
            self.assertNotIn(automatic_trigger, workflow)
        for runner in ("ubuntu-24.04", "windows-2022", "macos-15", "macos-15-intel"):
            self.assertIn(f"runs-on: {runner}", workflow)
        for target in (
            "tes3mp_transport_lifecycle_tests",
            "tes3mp_transport_queue_tests",
            "tes3mp_server_authentication_tests",
            "tes3mp_transport_gns_schedule_tests",
            "tes3mp_transport_gns_tests",
        ):
            self.assertGreaterEqual(workflow.count(target), 8)
        self.assertIn('if [ "${{ matrix.compiler }}" != "clang-18-TSan" ]', workflow)
        self.assertNotIn("@main", workflow)


if __name__ == "__main__":
    unittest.main()
