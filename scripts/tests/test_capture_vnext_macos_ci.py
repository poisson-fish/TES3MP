from __future__ import annotations

import json
import pathlib
import tarfile
import tempfile
import unittest
from unittest import mock

from scripts import capture_vnext_macos_ci as capture


class MacosCiEvidenceTests(unittest.TestCase):
    def test_validates_supported_host_architectures(self) -> None:
        self.assertEqual(capture.validate_host("Darwin", "arm64", "arm64"), "arm64-osx-dynamic")
        self.assertEqual(capture.validate_host("Darwin", "x86_64", "x86_64"), "x64-osx-dynamic")
        with self.assertRaisesRegex(capture.CaptureError, "Expected macOS"):
            capture.validate_host("Darwin", "arm64", "x86_64")
        with self.assertRaisesRegex(capture.CaptureError, "requires macOS"):
            capture.validate_host("Linux", "x86_64", "x86_64")

    def test_requires_xcode_16(self) -> None:
        self.assertEqual(capture.validate_xcode_version("Xcode 16.4\nBuild version 16F6"), "16.4")
        with self.assertRaisesRegex(capture.CaptureError, "Xcode 16"):
            capture.validate_xcode_version("Xcode 17.0\nBuild version 17A1")

    def test_validates_full_build_appleclang_ninja_evidence(self) -> None:
        evidence = {
            "preset": "vnext-baseline-macos-ci",
            "tools": {
                "CMAKE_CXX_COMPILER_ID": "AppleClang",
                "CMAKE_CXX_COMPILER_VERSION": "16.0.0.16000026",
                "CMAKE_GENERATOR": "Ninja",
            },
            "installation": {"files": 42},
            "macos_dependencies": {"vcpkg_triplet": "arm64-osx-dynamic"},
        }
        capture.validate_baseline_evidence(evidence, "arm64-osx-dynamic")
        evidence["tools"]["CMAKE_GENERATOR"] = "Unix Makefiles"
        with self.assertRaisesRegex(capture.CaptureError, "Ninja"):
            capture.validate_baseline_evidence(evidence, "arm64-osx-dynamic")

    def test_homebrew_inventory_requires_versions_and_licenses(self) -> None:
        raw = json.dumps(
            {
                "formulae": [
                    {"name": "cmake", "installed": [{"version": "4.1.0"}], "license": "BSD-3-Clause"},
                    {"name": "ninja", "installed": [{"version": "1.13.1"}], "license": "Apache-2.0"},
                    {"name": "qt", "installed": [{"version": "6.9.1"}], "license": "LGPL-3.0-only"},
                ]
            }
        )
        inventory = capture.homebrew_inventory(raw, ["cmake", "ninja", "qt"])
        self.assertEqual([entry["name"] for entry in inventory], ["cmake", "ninja", "qt"])
        with self.assertRaisesRegex(capture.CaptureError, "missing"):
            capture.homebrew_inventory(raw, ["cmake", "p7zip"])

    def test_capture_writes_a_self_consistent_evidence_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            build_dir = root / "build"
            output_dir = build_dir / "macos-ci-evidence"
            deps_root = root / "openmw-deps"
            qt_root = root / "qt"
            downloads = root / "downloads"
            package_dir = deps_root / "installed" / "vcpkg" / "info"
            license_dir = deps_root / "installed" / "arm64-osx-dynamic" / "share" / "zlib"
            package_dir.mkdir(parents=True)
            license_dir.mkdir(parents=True)
            qt_root.joinpath("bin").mkdir(parents=True)
            downloads.mkdir()
            package_dir.joinpath("zlib.list").write_text("installed file\n", encoding="utf-8")
            license_dir.joinpath("copyright").write_text("license\n", encoding="utf-8")
            qt_root.joinpath("bin", "qmake").write_text("fixture\n", encoding="utf-8")

            lock = json.loads(capture.LOCK_PATH.read_text(encoding="utf-8"))
            bundle = lock["macos"]["vcpkg_bundles"]["arm64-osx-dynamic"]
            manifest = downloads / "macos-arm64-osx-dynamic-manifest.txt"
            archive = downloads / bundle["archive_name"]
            manifest.write_text("manifest\n", encoding="utf-8")
            archive.write_text("archive\n", encoding="utf-8")
            baseline_path = build_dir / "vnext-baseline-evidence.json"
            build_dir.mkdir(exist_ok=True)
            baseline_path.write_text(
                json.dumps(
                    {
                        "source_commit": "a" * 40,
                        "preset": "vnext-baseline-macos-ci",
                        "tools": {
                            "CMAKE_CXX_COMPILER_ID": "AppleClang",
                            "CMAKE_CXX_COMPILER_VERSION": "16.0.0",
                            "CMAKE_GENERATOR": "Ninja",
                        },
                        "installation": {"files": 10},
                        "macos_dependencies": {
                            "vcpkg_triplet": "arm64-osx-dynamic",
                            "vcpkg_package_list_files": ["zlib.list"],
                            "vcpkg_copyright_files": 1,
                            "qt_version": "6.9.1",
                        },
                    }
                ),
                encoding="utf-8",
            )
            brew_json = json.dumps(
                {
                    "formulae": [
                        {"name": "cmake", "installed": [{"version": "4.1.0"}], "license": "BSD-3-Clause"},
                        {"name": "ninja", "installed": [{"version": "1.13.1"}], "license": "Apache-2.0"},
                        {"name": "qt", "installed": [{"version": "6.9.1"}], "license": "LGPL-3.0-only"},
                    ]
                }
            )

            def fake_run(command: list[str]) -> str:
                joined = " ".join(command)
                if joined == "xcodebuild -version":
                    return "Xcode 16.4\nBuild version 16F6\n"
                if "info --json=v2 --installed" in joined:
                    return brew_json
                if joined.endswith("qmake -query QT_VERSION"):
                    return "6.9.1\n"
                return f"fixture for {joined}\n"

            environment = {
                "GITHUB_SHA": "a" * 40,
                "VNEXT_VCPKG_ROOT": str(deps_root),
                "VNEXT_QT_ROOT": str(qt_root),
                "VNEXT_BREW": "brew",
            }
            with (
                mock.patch.object(capture, "BASELINE_EVIDENCE", baseline_path),
                mock.patch.object(capture, "DOWNLOAD_DIR", downloads),
                mock.patch.object(capture.platform, "system", return_value="Darwin"),
                mock.patch.object(capture.platform, "machine", return_value="arm64"),
                mock.patch.object(capture.platform, "release", return_value="24.0"),
                mock.patch.object(capture.platform, "version", return_value="fixture"),
                mock.patch.object(capture, "run", side_effect=fake_run),
                mock.patch.dict(capture.os.environ, environment, clear=True),
            ):
                evidence = capture.capture("arm64", output_dir)

            self.assertEqual(evidence["source_commit"], "a" * 40)
            self.assertEqual(evidence["artifacts"]["vcpkg_package_lists"], 1)
            self.assertEqual(evidence["artifacts"]["vcpkg_license_files"], 1)
            archive_path = output_dir / "macos-dependency-evidence.tar.gz"
            with tarfile.open(archive_path, "r:gz") as archive_file:
                names = archive_file.getnames()
            self.assertIn("vcpkg/package-lists/zlib.list", names)
            self.assertIn("vcpkg/licenses/arm64-osx-dynamic/share/zlib/copyright", names)

    def test_workflow_pins_platforms_cadence_actions_and_entry_points(self) -> None:
        workflow = (capture.ROOT / ".github" / "workflows" / "vnext-baseline-macos.yml").read_text(
            encoding="utf-8"
        )
        self.assertIn("runs-on: macos-15\n", workflow)
        self.assertIn("runs-on: macos-15-intel\n", workflow)
        self.assertIn("github.event_name == 'schedule'", workflow)
        self.assertIn("python3 scripts/run_vnext_baseline.py provision", workflow)
        self.assertIn("python3 scripts/run_vnext_baseline.py all --ci", workflow)
        self.assertIn("python3 scripts/capture_vnext_macos_ci.py", workflow)
        self.assertIn("actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd", workflow)
        self.assertIn("actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02", workflow)
        self.assertFalse((capture.ROOT / ".github" / "workflows" / "push.yml").exists())


if __name__ == "__main__":
    unittest.main()
