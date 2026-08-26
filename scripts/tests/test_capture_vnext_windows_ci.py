from __future__ import annotations

import pathlib
import tarfile
import tempfile
import unittest
from unittest import mock

from scripts import capture_vnext_windows_ci as capture


class WindowsCiEvidenceTests(unittest.TestCase):
    def test_validates_windows_2022_x64_v143_environment(self) -> None:
        capture.validate_host(
            "Windows",
            "AMD64",
            {
                "VSCMD_VER": "17.14.16",
                "VSCMD_ARG_TGT_ARCH": "x64",
                "VCToolsVersion": "14.44.35207",
            },
        )

    def test_rejects_wrong_host_architecture_or_toolchain(self) -> None:
        with self.assertRaisesRegex(capture.CaptureError, "Windows x86-64"):
            capture.validate_host(
                "Windows",
                "ARM64",
                {
                    "VSCMD_VER": "17.14",
                    "VSCMD_ARG_TGT_ARCH": "x64",
                    "VCToolsVersion": "14.44",
                },
            )
        with self.assertRaisesRegex(capture.CaptureError, "Visual Studio 2022"):
            capture.validate_host(
                "Windows",
                "AMD64",
                {
                    "VSCMD_VER": "18.0",
                    "VSCMD_ARG_TGT_ARCH": "x64",
                    "VCToolsVersion": "14.44",
                },
            )

    def test_validates_full_build_msvc_ninja_evidence(self) -> None:
        capture.validate_baseline_evidence(
            {
                "preset": "vnext-baseline-windows-ci",
                "tools": {
                    "CMAKE_CXX_COMPILER_ID": "MSVC",
                    "CMAKE_CXX_COMPILER_VERSION": "19.44.35228.0",
                    "CMAKE_GENERATOR": "Ninja",
                },
                "installation": {"files": 100},
            }
        )

    def test_rejects_compiler_generator_or_installation_drift(self) -> None:
        evidence = {
            "preset": "vnext-baseline-windows-ci",
            "tools": {
                "CMAKE_CXX_COMPILER_ID": "Clang",
                "CMAKE_CXX_COMPILER_VERSION": "19.1.0",
                "CMAKE_GENERATOR": "Visual Studio 17 2022",
            },
        }
        with self.assertRaisesRegex(capture.CaptureError, "Expected MSVC 19.x"):
            capture.validate_baseline_evidence(evidence)

    def test_qt_license_record_requires_the_locked_resolved_version(self) -> None:
        lock = {
            "version": "6.6.3",
            "target": "win64_msvc2019_64",
            "license_reference": "https://doc.qt.io/qt-6.6/licensing.html",
        }
        self.assertEqual(
            capture.make_qt_license_record(lock, {"qt_version": "6.6.3"}),
            {
                **lock,
                "reported_version": "6.6.3",
            },
        )
        with self.assertRaisesRegex(capture.CaptureError, "does not match"):
            capture.make_qt_license_record(lock, {"qt_version": "6.7.0"})

    def test_vcpkg_archive_must_match_the_baseline_inventory(self) -> None:
        package_lists = [
            pathlib.Path("fmt_11.2.0_x64-windows.list"),
            pathlib.Path("zlib_1.3.1_x64-windows.list"),
        ]
        copyrights = [pathlib.Path("fmt/copyright"), pathlib.Path("zlib/copyright")]
        evidence = {
            "vcpkg_package_list_files": [path.name for path in package_lists],
            "vcpkg_copyright_files": 2,
        }
        capture.validate_vcpkg_evidence(package_lists, copyrights, evidence)
        with self.assertRaisesRegex(capture.CaptureError, "package lists"):
            capture.validate_vcpkg_evidence(
                package_lists,
                copyrights,
                {**evidence, "vcpkg_package_list_files": []},
            )

    def test_collects_files_and_creates_dependency_archive(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            package_list = root / "installed" / "vcpkg" / "info" / "zlib.list"
            copyright_file = root / "installed" / "x64-windows" / "share" / "zlib" / "copyright"
            package_list.parent.mkdir(parents=True)
            copyright_file.parent.mkdir(parents=True)
            package_list.write_text("installed/x64-windows/include/zlib.h\n", encoding="utf-8")
            copyright_file.write_text("zlib license\n", encoding="utf-8")
            self.assertEqual(capture.collect_files(("installed/vcpkg/info/*.list",), root), [package_list])
            self.assertEqual(
                capture.archive_name("vcpkg/licenses", copyright_file, root / "installed"),
                pathlib.PurePosixPath("vcpkg/licenses/x64-windows/share/zlib/copyright"),
            )
            destination = root / "evidence.tar.gz"
            count = capture.create_dependency_archive(
                destination,
                (
                    (package_list, pathlib.PurePosixPath("vcpkg/package-lists/zlib.list")),
                    (copyright_file, pathlib.PurePosixPath("vcpkg/licenses/zlib/copyright")),
                ),
            )
            self.assertEqual(count, 2)
            with tarfile.open(destination, "r:gz") as archive:
                self.assertEqual(
                    archive.getnames(),
                    ["vcpkg/licenses/zlib/copyright", "vcpkg/package-lists/zlib.list"],
                )

    def test_capture_writes_a_self_consistent_evidence_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            deps = root / "deps"
            output = root / "output"
            baseline_path = root / "baseline.json"
            lock_path = root / "lock.json"
            manifest = deps / "downloads" / "openmw-vcpkg-manifest.txt"
            package_list = deps / "installed" / "vcpkg" / "info" / "zlib.list"
            copyright_file = deps / "installed" / "x64-windows" / "share" / "zlib" / "copyright"
            for path in (manifest, package_list, copyright_file):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(f"evidence for {path.name}\n", encoding="utf-8")
            baseline_path.write_text(
                """{
  "preset": "vnext-baseline-windows-ci",
  "source_commit": "0123456789abcdef",
  "tools": {
    "CMAKE_CXX_COMPILER_ID": "MSVC",
    "CMAKE_CXX_COMPILER_VERSION": "19.44.35228.0",
    "CMAKE_GENERATOR": "Ninja"
  },
  "installation": {"files": 10},
  "windows_dependencies": {
    "qt_version": "6.6.3",
    "vcpkg_package_list_files": ["zlib.list"],
    "vcpkg_copyright_files": 1
  }
}
""",
                encoding="utf-8",
            )
            lock_path.write_text(
                """{
  "windows_x86_64": {
    "qt": {
      "version": "6.6.3",
      "target": "win64_msvc2019_64",
      "license_reference": "https://doc.qt.io/qt-6.6/licensing.html"
    }
  }
}
""",
                encoding="utf-8",
            )
            environment = {
                "GITHUB_SHA": "0123456789abcdef",
                "VNEXT_VCPKG_ROOT": str(deps),
                "VSCMD_VER": "17.14.16",
                "VSCMD_ARG_TGT_ARCH": "x64",
                "VCToolsVersion": "14.44.35207",
            }
            with (
                mock.patch.object(capture, "BASELINE_EVIDENCE", baseline_path),
                mock.patch.object(capture, "LOCK_PATH", lock_path),
                mock.patch.object(capture, "ROOT", root),
                mock.patch.dict(capture.os.environ, environment, clear=True),
                mock.patch.object(capture.platform, "system", return_value="Windows"),
                mock.patch.object(capture.platform, "machine", return_value="AMD64"),
                mock.patch.object(capture.platform, "release", return_value="2022Server"),
                mock.patch.object(capture.platform, "version", return_value="10.0.20348"),
            ):
                evidence = capture.capture(output)
            self.assertEqual(evidence["source_commit"], "0123456789abcdef")
            self.assertEqual(evidence["artifacts"]["archive"]["files"], 5)
            self.assertTrue((output / "windows-ci-evidence.json").is_file())
            self.assertTrue((output / "windows-dependency-evidence.tar.gz").is_file())

    def test_workflow_pins_runner_actions_and_repository_entry_points(self) -> None:
        workflow = (capture.ROOT / ".github" / "workflows" / "vnext-baseline-windows.yml").read_text(
            encoding="utf-8"
        )
        for required in (
            "runs-on: windows-2022",
            "actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd",
            "ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756",
            "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02",
            "fetch-depth: 0",
            'vsversion: "2022"',
            "python scripts/run_vnext_baseline.py provision",
            "python scripts/run_vnext_baseline.py all --ci",
            "python scripts/capture_vnext_windows_ci.py",
        ):
            self.assertIn(required, workflow)
        self.assertNotIn("windows-latest", workflow)
        inherited_push_workflow = (capture.ROOT / ".github" / "workflows" / "push.yml").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("  Windows:", inherited_push_workflow)


if __name__ == "__main__":
    unittest.main()
