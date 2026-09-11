import argparse
import importlib.util
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
SPEC = importlib.util.spec_from_file_location("weather_capture", ROOT / "scripts" / "run_weather_reconnect_capture.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class WeatherReconnectCaptureTests(unittest.TestCase):
    def args(self):
        return argparse.Namespace(openmw=pathlib.Path("openmw"), resources=pathlib.Path("resources"),
                                  data=[pathlib.Path("data")], fallback_archive=["Morrowind.bsa"],
                                  content=["Morrowind.esm"])

    def test_client_uses_baked_options_and_bounded_role(self):
        with tempfile.TemporaryDirectory() as directory:
            pack = pathlib.Path(directory)
            pack.joinpath("openmw.cfg").write_text(
                "tes3mp-content-manifest-id=" + "a" * 64 + "\n"
                "tes3mp-content-weather-region-map=1=Bitter Coast Region\n"
                "tes3mp-content-weather-region-map=2=Ashlands Region\n", encoding="utf-8")
            command = MODULE.client_command(self.args(), pack, 25565, pathlib.Path("password"),
                                            "weather-one", pathlib.Path("evidence/weather-one.ndjson"))
            joined = " ".join(map(str, command))
            self.assertIn("--tes3mp-content-weather-region-map=2=Ashlands Region", joined)
            self.assertIn("--tes3mp-automation-role=weather-one", joined)
            self.assertNotIn("weather-capture-secret", joined)
            with self.assertRaises(ValueError):
                MODULE.client_command(self.args(), pack, 1, pathlib.Path("password"), "other", pathlib.Path("x"))

    def test_pair_requires_identical_two_region_transition(self):
        regions = [{"region": 1, "current": 1, "target": 2, "revision": 2,
                    "transition_start": 10, "transition_end": 20, "next_selection": 30},
                   {"region": 2, "current": 1, "target": 2, "revision": 2,
                    "transition_start": 10, "transition_end": 20, "next_selection": 30}]
        phase = {"records": {"weather-one": [{"event": "weather_sample", "regions": regions}],
                             "weather-two": [{"event": "weather_sample", "regions": regions}]}}
        self.assertEqual(len(MODULE._validate_pair(phase)["identical_transition"]), 2)
        phase["records"]["weather-two"][0]["regions"] = [dict(regions[0], revision=3), dict(regions[1])]
        with self.assertRaises(RuntimeError):
            MODULE._validate_pair(phase)


if __name__ == "__main__":
    unittest.main()
