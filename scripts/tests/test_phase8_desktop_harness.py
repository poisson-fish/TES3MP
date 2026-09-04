import argparse
import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location(
    "run_phase8_desktop_demo", ROOT / "scripts" / "run_phase8_desktop_demo.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class Phase8DesktopHarnessTests(unittest.TestCase):
    def test_harness_retains_per_role_diagnostics(self):
        source = (ROOT / "scripts/run_phase8_desktop_demo.py").read_text(encoding="utf-8")
        self.assertIn('f"{role}.stdout.log"', source)
        self.assertIn('f"{role}.stderr.log"', source)
        self.assertIn('artifacts / "rss-samples.json"', source)

    def test_proof_phases_are_isolated_across_disconnect_grace(self):
        self.assertGreater(MODULE.PHASE_SETTLE_SECONDS, MODULE.DISCONNECT_GRACE_SECONDS)
        source = (ROOT / "scripts/run_phase8_desktop_demo.py").read_text(encoding="utf-8")
        self.assertEqual(source.count("time.sleep(PHASE_SETTLE_SECONDS)"), 2)

    def test_flow_peer_evidence_excludes_the_target_player(self):
        source = (ROOT / "apps/openmw/tes3mp/desktop_automation.cpp").read_text(encoding="utf-8")
        self.assertIn("std::ranges::any_of(observedPlayers", source)
        self.assertIn("snapshot.header().targetPlayerId()", source)
        self.assertIn("snapshot.header().targetEntityId()", source)

    def args(self):
        return argparse.Namespace(
            openmw=pathlib.Path("openmw"), resources=pathlib.Path("resources"),
            data=[pathlib.Path("data")], content=["Morrowind.esm"],
            fallback_archive=["Morrowind.bsa"],
            interior="Balmora", worldspace="Wilderness", avatar="player")

    def test_command_is_fixed_role_and_credential_file_only(self):
        command = MODULE.client_command(
            self.args(), 25560, pathlib.Path("secret-file"), "flow-one", pathlib.Path("evidence"))
        joined = " ".join(command)
        self.assertIn("--tes3mp-automation-role=flow-one", joined)
        self.assertIn("--tes3mp-password-file=secret-file", joined)
        self.assertIn("--fallback-archive=Morrowind.bsa", joined)
        self.assertIn("--user-data=flow-one-user", joined)
        self.assertNotIn("phase8-desktop-secret", joined)

    def test_unknown_role_is_rejected(self):
        with self.assertRaises(ValueError):
            MODULE.client_command(
                self.args(), 25560, pathlib.Path("secret-file"), "other", pathlib.Path("evidence"))

    def test_completion_is_bounded_and_typed(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence = pathlib.Path(directory) / "flow-one.ndjson"
            evidence.write_text(
                json.dumps({"event": "phase8_desktop_complete", "role": "flow-one", "success": True}) + "\n",
                encoding="utf-8")
            self.assertTrue(MODULE.read_completion(evidence, "flow-one")["success"])

    def test_testing_guard_owns_automation_surface(self):
        cmake = (ROOT / "apps" / "openmw" / "tes3mp" / "CMakeLists.txt").read_text(encoding="utf-8")
        options = (ROOT / "apps" / "openmw" / "options.cpp").read_text(encoding="utf-8")
        self.assertIn("if(BUILD_TESTING)", cmake)
        self.assertIn("TES3MP_OPENMW_DESKTOP_AUTOMATION", cmake)
        self.assertIn("#ifdef TES3MP_OPENMW_DESKTOP_AUTOMATION", options)


if __name__ == "__main__":
    unittest.main()
