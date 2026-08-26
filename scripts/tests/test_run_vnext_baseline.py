from __future__ import annotations

import hashlib
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from scripts import run_vnext_baseline as baseline


class BaselineRunnerTests(unittest.TestCase):
    def test_dependency_lock_has_pinned_windows_inputs(self) -> None:
        lock = baseline.load_lock()["windows_x86_64"]
        bundle = lock["vcpkg_bundle"]
        qt = lock["qt"]
        self.assertRegex(bundle["openmw_deps_commit"], r"^[0-9a-f]{40}$")
        self.assertIn(bundle["openmw_deps_commit"], bundle["manifest_url"])
        self.assertRegex(bundle["manifest_sha256"], r"^[0-9a-f]{64}$")
        self.assertRegex(bundle["archive_sha512"], r"^[0-9a-f]{128}$")
        self.assertEqual(qt["version"], "6.6.3")
        self.assertRegex(qt["aqt_sha256"], r"^[0-9a-f]{64}$")

    def test_dependency_lock_has_pinned_macos_inputs(self) -> None:
        lock = baseline.load_lock()["macos"]
        self.assertRegex(lock["openmw_deps_commit"], r"^[0-9a-f]{40}$")
        self.assertEqual(lock["tag"], "2026-02-24")
        self.assertEqual(set(lock["vcpkg_bundles"]), {"arm64-osx-dynamic", "x64-osx-dynamic"})
        for triplet, bundle in lock["vcpkg_bundles"].items():
            self.assertIn(lock["openmw_deps_commit"], bundle["manifest_url"])
            self.assertIn(triplet, bundle["archive_name"])
            self.assertRegex(bundle["manifest_sha256"], r"^[0-9a-f]{64}$")
            self.assertRegex(bundle["archive_sha512"], r"^[0-9a-f]{128}$")

    def test_hash_file_streams_requested_algorithm(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "data"
            path.write_bytes(b"vnext baseline")
            self.assertEqual(baseline.hash_file(path, "sha256"), hashlib.sha256(b"vnext baseline").hexdigest())

    def test_make_environment_uses_ignored_workspace_dependencies_on_windows(self) -> None:
        with mock.patch.dict(baseline.os.environ, {}, clear=True):
            env = baseline.make_environment("Windows")
        self.assertEqual(pathlib.Path(env["VNEXT_VCPKG_ROOT"]), baseline.ROOT / "deps")
        self.assertEqual(pathlib.Path(env["VNEXT_QT_ROOT"]), baseline.ROOT / "deps" / "Qt" / "6.6.3" / "msvc2019_64")

    def test_make_environment_uses_supported_macos_architecture_defaults(self) -> None:
        with mock.patch.dict(baseline.os.environ, {}, clear=True), mock.patch.object(
            baseline.platform, "machine", return_value="arm64"
        ):
            arm = baseline.make_environment("Darwin")
        self.assertEqual(arm["VNEXT_VCPKG_TRIPLET"], "arm64-osx-dynamic")
        self.assertEqual(arm["VNEXT_QT_ROOT"], "/opt/homebrew/opt/qt")
        with mock.patch.dict(baseline.os.environ, {}, clear=True), mock.patch.object(
            baseline.platform, "machine", return_value="x86_64"
        ):
            intel = baseline.make_environment("Darwin")
        self.assertEqual(intel["VNEXT_VCPKG_TRIPLET"], "x64-osx-dynamic")
        self.assertEqual(intel["VNEXT_QT_ROOT"], "/usr/local/opt/qt")

    def test_macos_ci_provision_selects_xcode_16_and_installs_declared_formulas(self) -> None:
        env = {"GITHUB_ACTIONS": "true"}
        xcode = pathlib.Path("/Applications/Xcode_16.4.app")
        with (
            mock.patch.object(baseline.pathlib.Path, "glob", return_value=[xcode]),
            mock.patch.object(baseline, "find_tool", return_value="brew"),
            mock.patch.object(baseline, "run", side_effect=[None, "Xcode 16.4\nBuild version 16F6", None]) as run,
        ):
            baseline.provision_macos_prerequisites(env)
        self.assertEqual(run.call_args_list[0].args[0], ["sudo", "xcode-select", "-s", str(xcode / "Contents" / "Developer")])
        self.assertEqual(run.call_args_list[1].args[0], ["xcodebuild", "-version"])
        self.assertEqual(run.call_args_list[2].args[0], ["brew", "install", "cmake", "ninja", "qt"])

    def test_validate_windows_requires_visual_studio_2022_prompt(self) -> None:
        env = {"VSCMD_VER": "18.0", "VNEXT_VCPKG_ROOT": "missing", "VNEXT_QT_ROOT": "missing"}
        with self.assertRaisesRegex(baseline.BaselineError, "Visual Studio 2022"):
            baseline.validate_environment("Windows", env)

    def test_executable_names_are_platform_specific(self) -> None:
        self.assertEqual(baseline.executable_path("components-tests", "Windows").name, "components-tests.exe")
        self.assertEqual(baseline.executable_path("components-tests", "Linux").name, "components-tests")
        self.assertEqual(
            baseline.executable_path("components-tests", "Darwin"),
            baseline.BUILD_DIR / "OpenMW.app" / "Contents" / "MacOS" / "components-tests",
        )

    def test_full_build_ci_presets_are_bounded_to_implemented_platform_slices(self) -> None:
        self.assertEqual(
            baseline.CI_PRESETS,
            {
                "Windows": "vnext-baseline-windows-ci",
                "Linux": "vnext-baseline-linux-ci",
                "Darwin": "vnext-baseline-macos-ci",
            },
        )

    def test_legacy_exclusion_evidence_is_retained_with_baseline_json(self) -> None:
        self.assertEqual(
            baseline.LEGACY_EXCLUSION_EVIDENCE_PATH,
            baseline.BUILD_DIR / "vnext-legacy-exclusion.json",
        )

    def test_linux_ci_preset_enables_full_desktop_build_and_unbounded_target_list(self) -> None:
        presets = json.loads((baseline.ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        configure = {preset["name"]: preset for preset in presets["configurePresets"]}
        linux_ci = configure["vnext-baseline-linux-ci"]
        self.assertEqual(linux_ci["inherits"], "vnext-baseline-linux")
        self.assertEqual(
            linux_ci["cacheVariables"]["CMAKE_INSTALL_PREFIX"],
            "${sourceDir}/build/vnext-baseline-install",
        )
        self.assertTrue(all(linux_ci["cacheVariables"].values()))
        build = {preset["name"]: preset for preset in presets["buildPresets"]}["vnext-baseline-linux-ci"]
        self.assertNotIn("targets", build)

    def test_windows_ci_preset_enables_full_desktop_build_and_unbounded_target_list(self) -> None:
        presets = json.loads((baseline.ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        configure = {preset["name"]: preset for preset in presets["configurePresets"]}
        windows_ci = configure["vnext-baseline-windows-ci"]
        self.assertEqual(windows_ci["inherits"], "vnext-baseline-windows")
        self.assertEqual(
            windows_ci["cacheVariables"]["CMAKE_INSTALL_PREFIX"],
            "${sourceDir}/build/vnext-baseline-install",
        )
        self.assertTrue(all(windows_ci["cacheVariables"].values()))
        build = {preset["name"]: preset for preset in presets["buildPresets"]}["vnext-baseline-windows-ci"]
        self.assertNotIn("targets", build)

    def test_macos_ci_preset_enables_full_desktop_build_and_unbounded_target_list(self) -> None:
        presets = json.loads((baseline.ROOT / "CMakePresets.json").read_text(encoding="utf-8"))
        configure = {preset["name"]: preset for preset in presets["configurePresets"]}
        macos_ci = configure["vnext-baseline-macos-ci"]
        self.assertEqual(macos_ci["inherits"], "vnext-baseline-macos")
        self.assertEqual(
            macos_ci["cacheVariables"]["CMAKE_INSTALL_PREFIX"],
            "${sourceDir}/build/vnext-baseline-install",
        )
        self.assertTrue(all(macos_ci["cacheVariables"].values()))
        build = {preset["name"]: preset for preset in presets["buildPresets"]}["vnext-baseline-macos-ci"]
        self.assertNotIn("targets", build)

    def test_reads_compiler_identity_and_version_from_cmake_platform_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            cmake_files = pathlib.Path(directory) / "CMakeFiles" / "3.31.6"
            cmake_files.mkdir(parents=True)
            (cmake_files / "CMakeCXXCompiler.cmake").write_text(
                'set(CMAKE_CXX_COMPILER_ID "MSVC")\n'
                'set(CMAKE_CXX_COMPILER_VERSION "19.44.35228.0")\n',
                encoding="utf-8",
            )
            self.assertEqual(
                baseline.read_cmake_compiler_info(pathlib.Path(directory)),
                {
                    "CMAKE_CXX_COMPILER_ID": "MSVC",
                    "CMAKE_CXX_COMPILER_VERSION": "19.44.35228.0",
                },
            )

    def test_dependency_lock_is_valid_json_with_one_schema(self) -> None:
        data = json.loads(baseline.LOCK_PATH.read_text(encoding="utf-8"))
        self.assertEqual(data["schema_version"], 1)


if __name__ == "__main__":
    unittest.main()
