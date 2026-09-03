import hashlib
import importlib.util
import io
import json
import pathlib
import tarfile
import tempfile
import unittest
from unittest import mock


MODULE_PATH = pathlib.Path(__file__).resolve().parents[1] / "run_vnext_cares_proof.py"
SPEC = importlib.util.spec_from_file_location("run_vnext_cares_proof", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
proof = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(proof)


class CaresProofTests(unittest.TestCase):
    def test_lock_retains_exact_identity_profile_and_budgets(self) -> None:
        lock = proof.load_lock()
        self.assertEqual(lock["dependency"]["version"], proof.EXPECTED_VERSION)
        self.assertEqual(lock["dependency"]["commit"], proof.EXPECTED_COMMIT)
        self.assertEqual(lock["build_profile"], proof.EXPECTED_BUILD_PROFILE)
        self.assertEqual(lock["budgets"], proof.EXPECTED_BUDGETS)
        self.assertEqual(lock["vulnerability_sources"], proof.EXPECTED_VULNERABILITY_SOURCES)
        self.assertEqual(lock["dependency"]["license"]["spdx"], "MIT")

    def test_required_dns_scenarios_are_retained(self) -> None:
        source = (proof.PROOF_DIR / "proof.cpp").read_text(encoding="utf-8")
        for scenario in proof.EXPECTED_TESTS:
            self.assertIn(scenario, source)
        for category in ("ARES_ENOTFOUND", "ARES_ENODATA", "ARES_ETIMEOUT", "ARES_ECANCELLED", "ARES_EDESTRUCTION"):
            self.assertIn(category, source)
        self.assertIn("ares_process_fds", source)
        self.assertNotIn("ARES_OPT_EVENT_THREAD", source)

    def test_windows_cares_build_disables_its_static_runtime_override(self) -> None:
        cmake = (proof.PROOF_DIR / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("set(CARES_MSVC_STATIC_RUNTIME OFF CACHE BOOL \"\" FORCE)", cmake)

    def test_lock_rejects_unknown_fields_and_profile_changes(self) -> None:
        lock = json.loads(proof.LOCK_PATH.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "lock.json"
            lock["dependency"]["floating"] = True
            path.write_text(json.dumps(lock), encoding="utf-8")
            with self.assertRaisesRegex(proof.ProofError, "unknown floating"):
                proof.load_lock(path)
            lock = json.loads(proof.LOCK_PATH.read_text(encoding="utf-8"))
            lock["build_profile"].remove("query-cache-off")
            path.write_text(json.dumps(lock), encoding="utf-8")
            with self.assertRaisesRegex(proof.ProofError, "restricted dependency surface"):
                proof.load_lock(path)

    def test_download_rejects_hash_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory) / "archive.tar.gz"
            with mock.patch.object(proof.urllib.request, "urlopen", return_value=io.BytesIO(b"wrong")):
                with self.assertRaisesRegex(proof.ProofError, "SHA-256 mismatch"):
                    proof.download_verified("https://example.invalid/archive", "0" * 64, destination)
            self.assertFalse(destination.exists())

    def test_safe_extraction_accepts_regular_files_and_internal_link(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            archive_path = root / "source.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                data = b"cmake_minimum_required(VERSION 3.24)\n"
                regular = tarfile.TarInfo("source-1/CMakeLists.txt")
                regular.size = len(data)
                archive.addfile(regular, io.BytesIO(data))
                link = tarfile.TarInfo("source-1/internal-link")
                link.type = tarfile.SYMTYPE
                link.linkname = "CMakeLists.txt"
                archive.addfile(link)
            source = proof.extract_regular_files(archive_path, root / "out", "source-1")
            self.assertEqual((source / "CMakeLists.txt").read_bytes(), data)
            self.assertFalse((source / "internal-link").exists())

    def test_safe_extraction_rejects_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            archive_path = root / "source.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                member = tarfile.TarInfo("source-1/../../escape")
                member.size = 1
                archive.addfile(member, io.BytesIO(b"x"))
            with self.assertRaises(proof.ProofError):
                proof.extract_regular_files(archive_path, root / "out", "source-1")

    def test_sha256_file_is_streamed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "data"
            path.write_bytes(b"resolver proof")
            self.assertEqual(proof.sha256_file(path), hashlib.sha256(b"resolver proof").hexdigest())

    def test_workflow_uses_supported_matrix_and_pinned_actions(self) -> None:
        workflow = (proof.ROOT / ".github" / "workflows" / "vnext-cares-proof.yml").read_text(encoding="utf-8")
        for value in ("ubuntu-24.04", "windows-2022", "macos-15", "macos-15-intel"):
            self.assertIn(value, workflow)
        for compiler in ("gcc-13", "clang-18", "MSVC 2022 v143", "Xcode 16"):
            self.assertIn(compiler, workflow)
        self.assertIn("on:\n  workflow_dispatch:\n", workflow)
        for automatic_trigger in ("\n  push:", "\n  pull_request:", "\n  schedule:", "\n  release:"):
            self.assertNotIn(automatic_trigger, workflow)
        self.assertIn("--sanitize", workflow)
        self.assertNotRegex(workflow, r"uses:\s+[^\s@]+@v\d")

    def test_hashed_inputs_have_platform_independent_line_endings(self) -> None:
        attributes = (proof.ROOT / ".gitattributes").read_text(encoding="utf-8").splitlines()
        expected = {
            "docs/vnext/proofs/cares/** text eol=lf",
            "scripts/vnext_cares_proof.json text eol=lf",
        }
        self.assertTrue(expected.issubset(attributes))
        self.assertNotIn(b"\r", proof.LOCK_PATH.read_bytes())


if __name__ == "__main__":
    unittest.main()
