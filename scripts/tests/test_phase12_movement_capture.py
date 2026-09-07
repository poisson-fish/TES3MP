import argparse
import importlib.util
import pathlib
import socket
import sys
import tempfile
import time
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location(
    "run_phase12_movement_capture", ROOT / "scripts" / "run_phase12_movement_capture.py")
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class Phase12MovementCaptureTests(unittest.TestCase):
    def test_profiles_are_fixed_and_bounded(self):
        direct = MODULE.ImpairmentSchedule("direct")
        self.assertIsInstance(direct.delivery("client_to_server", 1.0), float)
        jitter = MODULE.ImpairmentSchedule("jitter")
        self.assertEqual(jitter.delivery("client_to_server", 1.0), 1.045)
        self.assertEqual(jitter.delivery("client_to_server", 1.0), 1.005)
        loss = MODULE.ImpairmentSchedule("loss")
        self.assertIsNone([loss.delivery("server_to_client", 1.0) for _ in range(20)][-1])
        stall = MODULE.ImpairmentSchedule("stall")
        values = [stall.delivery("client_to_server", 1.0) for _ in range(40)]
        self.assertEqual(values[-1], 1.25)
        with self.assertRaises(ValueError):
            MODULE.ImpairmentSchedule("unknown")

    def test_relay_round_trips_and_stops(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        server.settimeout(2)
        relay_port = MODULE.free_port()
        relay = MODULE.UdpImpairmentRelay(relay_port, server.getsockname()[1], "direct")
        relay.start()
        client = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client.settimeout(2)
        client.sendto(b"request", ("127.0.0.1", relay_port))
        payload, peer = server.recvfrom(1024)
        self.assertEqual(payload, b"request")
        server.sendto(b"response", peer)
        self.assertEqual(client.recvfrom(1024)[0], b"response")
        stats = relay.stop()
        self.assertEqual(stats.received, 2)
        self.assertEqual(stats.delivered, 2)
        client.close()
        server.close()

    def test_metric_parser_rejects_overflow_and_missing_fields(self):
        rows = "\n".join(
            f"TES3MP movement evidence: metric={name} samples=1 min=1 max=2 total=2"
            for name in MODULE.REQUIRED_METRICS)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "client.log"
            path.write_text(rows, encoding="utf-8")
            self.assertEqual(set(MODULE.parse_metrics(path)), MODULE.REQUIRED_METRICS)
            path.write_text(rows + "\nTES3MP movement evidence dropped observations: 1", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "overflowed"):
                MODULE.parse_metrics(path)

    def test_commands_keep_credentials_in_files(self):
        args = argparse.Namespace(
            artifacts=pathlib.Path("evidence"), profile="direct", interior="Balmora",
            worldspace="Wilderness", avatar="player", resources=pathlib.Path("resources"),
            data=[pathlib.Path("data")], fallback_archive=["Morrowind.bsa"], content=["Morrowind.esm"])
        command = MODULE.client_command(
            args, pathlib.Path("openmw"), 25570, pathlib.Path("secret-file"), "capture-one", "capture-one")
        joined = " ".join(map(str, command))
        self.assertIn("--tes3mp-automation-role=capture-one", joined)
        self.assertIn("--tes3mp-password-file=secret-file", joined)
        self.assertNotIn("phase12-capture-secret", joined)

    def test_runtime_directories_are_explicit(self):
        source = (ROOT / "scripts" / "run_phase12_movement_capture.py").read_text(encoding="utf-8")
        self.assertIn('parser.add_argument("--runtime-dir"', source)
        self.assertIn('environment["PATH"] =', source)


if __name__ == "__main__":
    unittest.main()
