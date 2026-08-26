from __future__ import annotations

import hashlib
import io
import json
import pathlib
import tarfile
import tempfile
import unittest
from unittest import mock

from scripts import run_vnext_gamenetworkingsockets_proof as proof


class GameNetworkingSocketsProofRunnerTests(unittest.TestCase):
    def test_dependency_lock_is_exact_and_restricted(self) -> None:
        lock = proof.load_lock()
        self.assertEqual(
            set(lock["dependencies"]), {"gamenetworkingsockets", "openssl", "protobuf", "abseil"}
        )
        for name, (version, commit) in proof.EXPECTED_DEPENDENCIES.items():
            self.assertEqual(lock["dependencies"][name]["version"], version)
            self.assertEqual(lock["dependencies"][name]["commit"], commit)
            self.assertRegex(lock["dependencies"][name]["source_archive"]["sha256"], r"^[0-9a-f]{64}$")
        self.assertIn("ice-off", lock["build_profile"]["gamenetworkingsockets"])
        self.assertIn("windows-no-asm", lock["build_profile"]["openssl"])
        self.assertIn("operator-managed certificates", lock["excluded_surfaces"])
        self.assertEqual(lock["vulnerability_sources"], proof.EXPECTED_VULNERABILITY_SOURCES)
        self.assertEqual(lock["generated_policy"], proof.EXPECTED_GENERATED_POLICY)
        self.assertEqual(lock["budgets"]["receive_buffer_bytes"], 4096)
        self.assertEqual(lock["budgets"]["receive_buffer_messages"], 4)
        self.assertEqual(lock["budgets"]["receive_max_message_bytes"], 2048)
        self.assertEqual(lock["budgets"]["receive_max_segments_per_packet"], 2)
        self.assertEqual(lock["budgets"]["concurrent_handshakes"], 8)
        self.assertEqual(lock["budgets"]["flood_connections"], 32)

    def test_required_hostile_resource_and_close_scenarios_are_retained(self) -> None:
        required = {
            "actual_slow_reader_and_full_receive_buffer",
            "excessive_segments_and_maximum_message_fail_closed",
            "handshake_and_disconnect_flood_admission_bounds",
            "close_discards_unread_data_and_invalidates_handle",
        }
        self.assertTrue(required.issubset(proof.EXPECTED_TESTS))
        source = (proof.PROOF_DIR / "proof.cpp").read_text(encoding="utf-8")
        for scenario in required:
            self.assertIn(scenario, source)

    def test_lock_rejects_unknown_fields(self) -> None:
        lock = json.loads(proof.LOCK_PATH.read_text(encoding="utf-8"))
        lock["dependencies"]["openssl"]["floating"] = True
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "lock.json"
            path.write_text(json.dumps(lock), encoding="utf-8")
            with self.assertRaisesRegex(proof.ProofError, "unknown floating"):
                proof.load_lock(path)

    def test_lock_rejects_changed_build_profile_and_budget(self) -> None:
        lock = json.loads(proof.LOCK_PATH.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "lock.json"
            lock["build_profile"]["gamenetworkingsockets"].remove("ice-off")
            path.write_text(json.dumps(lock), encoding="utf-8")
            with self.assertRaisesRegex(proof.ProofError, "restricted dependency surface"):
                proof.load_lock(path)
            lock = json.loads(proof.LOCK_PATH.read_text(encoding="utf-8"))
            lock["budgets"]["credential_bytes"] += 1
            path.write_text(json.dumps(lock), encoding="utf-8")
            with self.assertRaisesRegex(proof.ProofError, "proof source contract"):
                proof.load_lock(path)

    def test_download_rejects_hash_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory) / "archive.tar.gz"
            with mock.patch.object(proof.urllib.request, "urlopen", return_value=io.BytesIO(b"wrong")):
                with self.assertRaisesRegex(proof.ProofError, "SHA-256 mismatch"):
                    proof.download_verified("https://example.invalid/archive", "0" * 64, destination)
            self.assertFalse(destination.exists())

    def test_safe_extraction_accepts_internal_link_and_regular_files(self) -> None:
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

    def test_safe_extraction_rejects_traversal_and_escaping_link(self) -> None:
        for member_name, link_name in (("source-1/../../escape", None), ("source-1/link", "../../escape")):
            with self.subTest(member_name=member_name), tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                archive_path = root / "source.tar.gz"
                with tarfile.open(archive_path, "w:gz") as archive:
                    if link_name is None:
                        member = tarfile.TarInfo(member_name)
                        member.size = 1
                        archive.addfile(member, io.BytesIO(b"x"))
                    else:
                        member = tarfile.TarInfo(member_name)
                        member.type = tarfile.SYMTYPE
                        member.linkname = link_name
                        archive.addfile(member)
                with self.assertRaises(proof.ProofError):
                    proof.extract_regular_files(archive_path, root / "out", "source-1")

    def test_sha256_file_is_streamed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "data"
            path.write_bytes(b"transport proof")
            self.assertEqual(proof.sha256_file(path), hashlib.sha256(b"transport proof").hexdigest())

    def test_workflow_uses_approved_matrix_and_pinned_actions(self) -> None:
        workflow = (
            proof.ROOT / ".github" / "workflows" / "vnext-gamenetworkingsockets-proof.yml"
        ).read_text(encoding="utf-8")
        for value in ("ubuntu-24.04", "windows-2022", "macos-15", "macos-15-intel"):
            self.assertIn(value, workflow)
        for compiler in ("gcc-13", "clang-18", "MSVC 2022 v143", "Xcode 16"):
            self.assertIn(compiler, workflow)
        self.assertIn("--sanitize", workflow)
        self.assertIn("strawberryperl", workflow)
        self.assertNotRegex(workflow, r"uses:\s+[^\s@]+@v\d")

    def test_hashed_inputs_have_platform_independent_line_endings(self) -> None:
        attributes = (proof.ROOT / ".gitattributes").read_text(encoding="utf-8").splitlines()
        expected = {
            "docs/vnext/proofs/gamenetworkingsockets/** text eol=lf",
            "scripts/vnext_gamenetworkingsockets_proof.json text eol=lf",
        }
        self.assertTrue(expected.issubset(attributes))
        self.assertNotIn(b"\r", proof.LOCK_PATH.read_bytes())


if __name__ == "__main__":
    unittest.main()
