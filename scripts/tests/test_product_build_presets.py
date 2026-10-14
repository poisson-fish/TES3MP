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
        self.assertFalse(cache["TES3MP_ENABLE_DESKTOP_AUTOMATION"])
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

    def test_non_shipping_targets_are_explicit_opt_ins_across_platforms(self) -> None:
        for platform in ("windows", "linux", "macos"):
            self.assertEqual(self.build[f"vnext-product-client-{platform}"]["targets"], ["openmw"])
            self.assertEqual(self.build[f"vnext-product-server-{platform}"]["targets"], ["tes3mp_server"])
            self.assertEqual(
                self.build[f"vnext-product-headless-{platform}"]["targets"],
                ["tes3mp_headless_client"],
            )
            self.assertEqual(
                self.build[f"vnext-product-checks-{platform}"]["targets"],
                [
                    "openmw",
                    "tes3mp_server",
                    "openmw_tes3mp_adapter_tests_run",
                    "tes3mp_server_app_tests_run",
                ],
            )

    def test_desktop_evidence_has_an_instrumented_persistent_tree(self) -> None:
        for platform in ("windows", "linux", "macos"):
            configure = self.configure[f"vnext-desktop-evidence-{platform}"]
            self.assertEqual(configure["inherits"], f"vnext-product-{platform}")
            self.assertEqual(configure["binaryDir"], "${sourceDir}/build/vnext-desktop-evidence")
            self.assertTrue(configure["cacheVariables"]["TES3MP_ENABLE_DESKTOP_AUTOMATION"])
            self.assertEqual(
                self.build[f"vnext-desktop-evidence-{platform}"]["targets"],
                ["tes3mp_desktop_evidence"],
            )

    def test_focused_presets_do_not_name_the_openmw_product(self) -> None:
        for platform in ("windows", "linux", "macos"):
            expected = {
                f"vnext-protocol-contracts-{platform}": ["tes3mp_protocol_contracts_run"],
                f"vnext-server-logic-{platform}": ["tes3mp_server_logic_tests_run", "tes3mp_server_app_tests_run"],
                f"vnext-adapter-tests-{platform}": ["openmw_tes3mp_adapter_tests_run"],
            }
            for name, targets in expected.items():
                self.assertEqual(self.build[name]["targets"], targets)
                self.assertNotIn("openmw", targets)

    def test_vscode_visible_presets_provide_display_names_and_platform_defaults(self) -> None:
        for preset in self.presets["configurePresets"]:
            if not preset.get("hidden", False):
                self.assertTrue(preset.get("displayName"), preset["name"])
        for preset in self.presets["buildPresets"]:
            self.assertTrue(preset.get("displayName"), preset["name"])
        root_cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertTrue(
            'include("${CMAKE_CURRENT_LIST_DIR}/cmake/TES3MPPlatformDefaults.cmake")' in root_cmake,
            "CMakeLists.txt must include cmake/TES3MPPlatformDefaults.cmake",
        )
        self.assertTrue((ROOT / "cmake/TES3MPPlatformDefaults.cmake").is_file())

    def test_desktop_automation_is_not_coupled_to_global_testing(self) -> None:
        for relative in ("apps/openmw/CMakeLists.txt", "apps/openmw/tes3mp/CMakeLists.txt"):
            text = (ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn("BUILD_TESTING", text)
            self.assertIn("TES3MP_ENABLE_DESKTOP_AUTOMATION", text)

    def test_standalone_presets_expose_focused_contract_surfaces(self) -> None:
        presets = json.loads((ROOT / "components/tes3mp/CMakePresets.json").read_text(encoding="utf-8"))
        build = {preset["name"]: preset for preset in presets["buildPresets"]}
        self.assertEqual(build["tes3mp-protocol-contracts"]["targets"], ["tes3mp_protocol_contracts_run"])
        self.assertEqual(build["tes3mp-adapter-tests"]["targets"], ["openmw_tes3mp_adapter_tests_run"])
        self.assertEqual(
            build["tes3mp-server-logic"]["targets"],
            ["tes3mp_server_logic_tests_run", "tes3mp_server_app_tests_run"],
        )

    def test_windows_and_posix_wrappers_are_quiet_by_default_and_keep_full_logs(self) -> None:
        ps1 = (ROOT / "build_windows.ps1").read_text(encoding="utf-8")
        self.assertTrue("[switch]$VerboseOutput" in ps1, "missing -VerboseOutput switch")
        self.assertTrue('"build\\logs"' in ps1, "missing build\\logs directory")
        self.assertTrue("*>> $buildLog" in ps1, "missing log redirection")
        self.assertTrue("Get-Content -LiteralPath $buildLog -Tail $TailLines" in ps1, "missing log tail")

        sh = (ROOT / "build.sh").read_text(encoding="utf-8")
        self.assertTrue("--verbose" in sh, "missing --verbose option")
        self.assertTrue('LOG_DIR="${REPO_ROOT}/build/logs"' in sh, "missing build/logs directory")
        self.assertTrue('tail -n "${TAIL_LINES}"' in sh, "missing log tail")

    def test_desktop_capture_runners_share_the_common_harness(self) -> None:
        harness = (ROOT / "scripts/desktop_evidence_harness.py").read_text(encoding="utf-8")
        for symbol in ("build_client_command", "run_phase", "bake_pack", "write_summary"):
            self.assertIn(f"def {symbol}", harness)
        for name in ("run_weather_reconnect_capture.py", "run_wait_rest_capture.py"):
            runner = (ROOT / "scripts" / name).read_text(encoding="utf-8")
            self.assertIn("import desktop_evidence_harness as harness", runner)
            self.assertNotIn("def _runtime_config", runner)
            self.assertNotIn("subprocess.Popen", runner)


if __name__ == "__main__":
    unittest.main()
