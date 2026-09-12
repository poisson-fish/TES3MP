import argparse
import importlib.util
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("wait_rest_capture", ROOT / "scripts" / "run_wait_rest_capture.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class WaitRestCaptureTests(unittest.TestCase):
    def args(self):
        return argparse.Namespace(openmw=pathlib.Path("openmw"), resources=pathlib.Path("resources"),
                                  data=[pathlib.Path("data")], fallback_archive=["Morrowind.bsa"],
                                  content=["Morrowind.esm"])

    def test_client_uses_baked_options_and_bounded_role(self):
        with tempfile.TemporaryDirectory() as directory:
            pack = pathlib.Path(directory)
            pack.joinpath("openmw.cfg").write_text(
                "tes3mp-content-manifest-id=" + "a" * 64 + "\n", encoding="utf-8")
            command = MODULE.client_command(self.args(), pack, 25565, pathlib.Path("password"),
                                            "wait-one", pathlib.Path("evidence/wait-one.ndjson"))
            joined = " ".join(map(str, command))
            self.assertIn("--tes3mp-automation-role=wait-one", joined)
            self.assertNotIn("wait-rest-capture-secret", joined)
            with self.assertRaises(ValueError):
                MODULE.client_command(self.args(), pack, 1, pathlib.Path("password"), "other", pathlib.Path("x"))

    def test_pair_requires_one_identical_duplicate_free_rollover(self):
        rollover = {"event": "wait_rest_time_sample", "revision": 9, "tick": 7,
                    "day": 1, "month": 0, "year": 428, "milliseconds_since_midnight": 3_600_200}
        completion = {"world_time_duplicate_presentations": 0}
        phase = {"records": {"wait-one": [rollover], "wait-two": [dict(rollover)]},
                 "roles": [completion, dict(completion)]}
        self.assertEqual(MODULE._validate_pair(phase)["identical_rollover"][3], 428)
        phase["records"]["wait-two"][0]["revision"] = 10
        with self.assertRaises(RuntimeError):
            MODULE._validate_pair(phase)

    def test_reconnect_and_slow_peer_require_duplicate_free_completion(self):
        before = {"event": "wait_rest_time_sample", "revision": 2, "day": 30, "month": 11,
                  "year": 427, "milliseconds_since_midnight": 82_800_000}
        after = dict(before, revision=3, day=1, month=0, year=428, milliseconds_since_midnight=3_600_000)
        reconnect = {"records": {"wait-reconnect": [before, after]}, "roles": [
            {}, {"resumes": 1, "world_time_resumed_converged": True,
                 "world_time_duplicate_presentations": 0}]}
        self.assertEqual(MODULE._validate_reconnect(reconnect)["after"]["year"], 428)
        slow = {"records": {"wait-slow": [
                    {"event": "wait_slow_peer_stall_started"}, {"event": "wait_slow_peer_recovered"}]},
                "roles": [{}, {"slow_peer_recovered": True, "world_time_duplicate_presentations": 0}],
                "memory": {}, "queue_drain": {}}
        self.assertTrue(MODULE._validate_slow(slow)["completion"]["slow_peer_recovered"])


if __name__ == "__main__":
    unittest.main()
