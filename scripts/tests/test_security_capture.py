import argparse
import importlib.util
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("security_capture", ROOT / "scripts" / "run_security_capture.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SecurityCaptureTests(unittest.TestCase):
    def args(self):
        return argparse.Namespace(openmw=pathlib.Path("openmw"), resources=pathlib.Path("resources"),
                                  data=[pathlib.Path("data")], fallback_archive=["Morrowind.bsa"],
                                  content=["Morrowind.esm"])

    def test_client_role_is_bounded_and_uses_password_file(self):
        with tempfile.TemporaryDirectory() as directory:
            pack = pathlib.Path(directory)
            pack.joinpath("openmw.cfg").write_text("tes3mp-content-manifest-id=" + "a" * 64 + "\n",
                                                   encoding="utf-8")
            command = MODULE.client_command(self.args(), pack, 25565, pathlib.Path("password"),
                                            "security-pick", pathlib.Path("evidence/security.ndjson"))
            joined = " ".join(map(str, command))
            self.assertIn("--tes3mp-automation-role=security-pick", joined)
            self.assertIn("--tes3mp-password-file=password", joined)
            self.assertNotIn("security-capture-secret", joined)
            with self.assertRaises(ValueError):
                MODULE.client_command(self.args(), pack, 1, pathlib.Path("password"), "other", pathlib.Path("x"))

    def test_validation_requires_unlock_wear_and_progress(self):
        completion = {"security_submitted": True, "security_unlocked": True, "resumes": 1,
                      "security_resumed_converged": True,
                      "security_initial_tool_condition": 10, "security_tool_condition": 9,
                      "security_initial_progress": 0.0, "security_progress": 0.02}
        phase = {"roles": [completion], "records": {"security-pick": [
                     {"event": "security_pick_submitted", "object_id": 1, "tool_stack_id": 3}]},
                 "queue_drain": {"final_reliable_messages": 0}}
        self.assertTrue(MODULE._validate(
            phase, "security-pick", "pick", "security_unlocked")["completion"]["security_unlocked"])
        phase["roles"][0]["security_tool_condition"] = 8
        with self.assertRaises(RuntimeError):
            MODULE._validate(phase, "security-pick", "pick", "security_unlocked")

    def test_validation_accepts_probe_disarm(self):
        completion = {"security_submitted": True, "security_disarmed": True, "resumes": 1,
                      "security_resumed_converged": True,
                      "security_initial_tool_condition": 10, "security_tool_condition": 9,
                      "security_initial_progress": 0.0, "security_progress": 0.03}
        phase = {"roles": [completion], "records": {"security-probe": [
                     {"event": "security_probe_submitted", "object_id": 1, "tool_stack_id": 2}]},
                 "queue_drain": {"final_reliable_messages": 0}}
        self.assertTrue(MODULE._validate(
            phase, "security-probe", "probe", "security_disarmed")["completion"]["security_disarmed"])


if __name__ == "__main__":
    unittest.main()
