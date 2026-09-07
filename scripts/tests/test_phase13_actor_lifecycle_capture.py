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
    "run_phase13_actor_lifecycle_capture", ROOT / "scripts" / "run_phase13_actor_lifecycle_capture.py")
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class Phase13ActorLifecycleCaptureTests(unittest.TestCase):
    def args(self, root):
        return argparse.Namespace(
            openmw=pathlib.Path("openmw"), artifacts=root, interior="Balmora, Guild of Mages",
            worldspace="Wilderness", avatar="player", actor_prototype="ajira",
            resources=pathlib.Path("resources"), data=[pathlib.Path("data")],
            fallback_archive=["Morrowind.bsa"], content=["Morrowind.esm"])

    def test_command_maps_actor_and_keeps_credentials_in_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            command = MODULE.client_command(
                self.args(root), 25570, pathlib.Path("secret-file"), "actor-auth", "auth", "continuity")
            joined = " ".join(map(str, command))
            self.assertIn("--tes3mp-content-actor-prototype-map=1=ajira", joined)
            self.assertIn("--tes3mp-password-file=secret-file", joined)
            self.assertIn("continuity-identity", joined)
            self.assertNotIn("phase13-actor-capture-secret", joined)

    def test_server_config_binds_collision_and_actor_content(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            config = MODULE.write_server_config(root, 25570, root / "password").read_text(encoding="utf-8")
            self.assertIn("actor_content_file=", config)
            self.assertIn("wander 0 0 0 1000000 0 0", (root / "actor-content").read_text(encoding="utf-8"))

    def test_evidence_is_bounded_and_checks_common_canonical_samples(self):
        completion = {"event": "phase8_desktop_complete", "role": "flow-one", "success": True}
        samples = [
            {"event": "phase13_actor_sample", "revision": revision, "actor_id": 1,
             "entity_id": 9001, "prototype_id": 1, "tick": revision, "x": revision, "y": 0, "z": 0}
            for revision in range(1, 6)
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "evidence.ndjson"
            path.write_text("\n".join(json.dumps(row) for row in [*samples, completion]) + "\n", encoding="utf-8")
            result, parsed = MODULE.read_evidence(path, "flow-one")
            self.assertTrue(result["success"])
            self.assertEqual(MODULE.common_samples(parsed, parsed), 5)
            parsed[-1]["x"] = -1
            with self.assertRaisesRegex(RuntimeError, "diverged"):
                MODULE.common_samples(samples, parsed)


if __name__ == "__main__":
    unittest.main()
