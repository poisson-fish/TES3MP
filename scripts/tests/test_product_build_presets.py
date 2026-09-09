from __future__ import annotations

import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ProductBuildPresetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.presets = json.loads((ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        cls.configure = {preset["name"]: preset for preset in cls.presets["configurePresets"]}
        cls.build = {preset["name"]: preset for preset in cls.presets["buildPresets"]}

    def test_product_configuration_excludes_upstream_tools_and_test_suites(self) -> None:
        cache = self.configure["vnext-product-common"]["cacheVariables"]
        self.assertTrue(cache["BUILD_OPENMW"])
        self.assertTrue(cache["TES3MP_ENABLE_GNS_TRANSPORT"])
        for name in (
            "BUILD_TESTING",
            "BUILD_LAUNCHER",
            "BUILD_WIZARD",
            "BUILD_MWINIIMPORTER",
            "BUILD_OPENCS",
            "BUILD_ESSIMPORTER",
            "BUILD_BSATOOL",
            "BUILD_ESMTOOL",
            "BUILD_NIFTEST",
            "BUILD_NAVMESHTOOL",
            "BUILD_BULLETOBJECTTOOL",
            "BUILD_COMPONENTS_TESTS",
            "BUILD_OPENMW_TESTS",
            "BUILD_OPENCS_TESTS",
            "BUILD_BENCHMARKS",
        ):
            self.assertFalse(cache[name], name)

    def test_product_builds_only_the_shipping_client_and_server(self) -> None:
        expected = ["openmw", "tes3mp_server"]
        for platform in ("windows", "linux", "macos"):
            preset = self.build[f"vnext-product-{platform}"]
            self.assertEqual(preset["targets"], expected)

    def test_non_shipping_windows_targets_are_explicit_opt_ins(self) -> None:
        self.assertEqual(self.build["vnext-product-client-windows"]["targets"], ["openmw"])
        self.assertEqual(self.build["vnext-product-server-windows"]["targets"], ["tes3mp_server"])
        self.assertEqual(
            self.build["vnext-product-headless-windows"]["targets"],
            ["tes3mp_headless_client"],
        )
        self.assertEqual(
            self.build["vnext-product-checks-windows"]["targets"],
            [
                "openmw",
                "tes3mp_server",
                "openmw_tes3mp_adapter_tests_run",
                "tes3mp_server_app_tests_run",
            ],
        )


if __name__ == "__main__":
    unittest.main()
