from __future__ import annotations

import pathlib
import tempfile
import unittest

from scripts import capture_vnext_linux_ci as capture


class LinuxCiEvidenceTests(unittest.TestCase):
    def test_parses_and_validates_ubuntu_2404_x86_64(self) -> None:
        release = capture.parse_os_release('ID=ubuntu\nVERSION_ID="24.04"\nPRETTY_NAME="Ubuntu 24.04.3 LTS"\n')
        capture.validate_host(release, "x86_64")
        self.assertEqual(release["VERSION_ID"], "24.04")

    def test_rejects_wrong_distribution_or_architecture(self) -> None:
        with self.assertRaisesRegex(capture.CaptureError, "Ubuntu 24.04"):
            capture.validate_host({"ID": "ubuntu", "VERSION_ID": "26.04"}, "x86_64")
        with self.assertRaisesRegex(capture.CaptureError, "x86-64"):
            capture.validate_host({"ID": "ubuntu", "VERSION_ID": "24.04"}, "arm64")

    def test_validates_both_approved_compiler_gates(self) -> None:
        for gate, compiler_id, version in (("gcc-13", "GNU", "13.3.0"), ("clang-18", "Clang", "18.1.3")):
            with self.subTest(gate=gate):
                capture.validate_baseline_evidence(
                    {
                        "preset": "vnext-baseline-linux-ci",
                        "tools": {
                            "CMAKE_CXX_COMPILER_ID": compiler_id,
                            "CMAKE_CXX_COMPILER_VERSION": version,
                            "CMAKE_GENERATOR": "Ninja",
                        },
                    },
                    gate,
                )

    def test_rejects_compiler_or_generator_drift(self) -> None:
        evidence = {
            "preset": "vnext-baseline-linux-ci",
            "tools": {
                "CMAKE_CXX_COMPILER_ID": "GNU",
                "CMAKE_CXX_COMPILER_VERSION": "14.2.0",
                "CMAKE_GENERATOR": "Unix Makefiles",
            },
        }
        with self.assertRaisesRegex(capture.CaptureError, "Expected GNU 13"):
            capture.validate_baseline_evidence(evidence, "gcc-13")

    def test_redacts_credentials_in_apt_sources(self) -> None:
        self.assertEqual(
            capture.redact_source_credentials(
                "deb https://user:secret@example.invalid stable main\n"
                "deb https://token@example.invalid stable main\n"
            ),
            "deb https://<redacted>@example.invalid stable main\n"
            "deb https://<redacted>@example.invalid stable main\n",
        )

    def test_keeps_only_installed_dpkg_rows(self) -> None:
        self.assertEqual(
            capture.parse_installed_packages(
                "ii \tzlib1g\t1:1.3.dfsg-3.1ubuntu2.1\tamd64\n"
                "rc \told-package\t1.0\tamd64\n"
                "ii \tcmake\t3.28.3-1build7\tamd64\n"
            ),
            ["cmake\t3.28.3-1build7\tamd64", "zlib1g\t1:1.3.dfsg-3.1ubuntu2.1\tamd64"],
        )

    def test_creates_license_archive(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory) / "usr" / "share" / "doc"
            package = root / "example"
            package.mkdir(parents=True)
            (package / "copyright").write_text("Example license\n", encoding="utf-8")
            destination = pathlib.Path(directory) / "licenses.tar.gz"
            self.assertEqual(capture.create_license_archive(destination, root), 1)
            self.assertTrue(destination.is_file())

    def test_workflow_pins_runner_compilers_actions_and_repository_entry_points(self) -> None:
        workflow = (capture.ROOT / ".github" / "workflows" / "vnext-baseline-linux.yml").read_text(encoding="utf-8")
        for required in (
            "runs-on: ubuntu-24.04",
            "compiler: gcc-13",
            "compiler: clang-18",
            "actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd",
            "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02",
            "fetch-depth: 0",
            "python3 scripts/run_vnext_baseline.py all --ci",
            "python3 scripts/capture_vnext_linux_ci.py",
        ):
            self.assertIn(required, workflow)
        self.assertNotIn("ubuntu-latest", workflow)
        self.assertFalse((capture.ROOT / ".github" / "workflows" / "push.yml").exists())


if __name__ == "__main__":
    unittest.main()
