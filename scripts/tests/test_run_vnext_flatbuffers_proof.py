from __future__ import annotations

import hashlib
import io
import json
import pathlib
import tarfile
import tempfile
import unittest
from unittest import mock

from scripts import run_vnext_flatbuffers_proof as proof


class FlatBuffersProofRunnerTests(unittest.TestCase):
    def test_dependency_lock_is_exact_and_restricted(self) -> None:
        lock = proof.load_lock()
        dependency = lock["dependency"]
        self.assertEqual(dependency["version"], "25.12.19")
        self.assertEqual(dependency["tag"], "v25.12.19")
        self.assertRegex(dependency["commit"], r"^[0-9a-f]{40}$")
        self.assertRegex(dependency["source_archive"]["sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(dependency["license"]["spdx"], "Apache-2.0")
        self.assertEqual(lock["generator_arguments"], ["--cpp", "--scoped-enums"])
        self.assertEqual(set(lock["seed_corpus"]), set(proof.SEED_CORPUS_FILES))
        for digest in lock["seed_corpus"].values():
            self.assertRegex(digest, r"^[0-9a-f]{64}$")
        self.assertIn("reflection", lock["excluded_surfaces"])
        self.assertIn("nested FlatBuffers", lock["excluded_surfaces"])

    def test_lock_rejects_unapproved_generator_arguments(self) -> None:
        lock = json.loads(proof.LOCK_PATH.read_text(encoding="utf-8"))
        lock["generator_arguments"].append("--gen-object-api")
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "lock.json"
            path.write_text(json.dumps(lock), encoding="utf-8")
            with self.assertRaisesRegex(proof.ProofError, "restricted profile"):
                proof.load_lock(path)

    def test_download_rejects_hash_mismatch_without_replacing_cache(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = pathlib.Path(directory) / "archive.tar.gz"
            response = io.BytesIO(b"hostile archive")
            with mock.patch.object(proof.urllib.request, "urlopen", return_value=response):
                with self.assertRaisesRegex(proof.ProofError, "SHA-256 mismatch"):
                    proof.download_verified("https://example.invalid/archive", "0" * 64, destination)
            self.assertFalse(destination.exists())
            self.assertFalse(destination.with_suffix(".gz.partial").exists())

    def test_safe_extraction_ignores_links_and_extracts_regular_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            archive_path = root / "source.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                data = b"cmake_minimum_required(VERSION 3.16)\n"
                regular = tarfile.TarInfo("flatbuffers-1/CMakeLists.txt")
                regular.size = len(data)
                archive.addfile(regular, io.BytesIO(data))
                link = tarfile.TarInfo("flatbuffers-1/ignored-link")
                link.type = tarfile.SYMTYPE
                link.linkname = "../../outside"
                archive.addfile(link)
            source = proof.extract_regular_files(archive_path, root / "out", "flatbuffers-1")
            self.assertEqual((source / "CMakeLists.txt").read_bytes(), data)
            self.assertFalse((source / "ignored-link").exists())
            (source / "CMakeLists.txt").write_bytes(b"tampered")
            source = proof.extract_regular_files(archive_path, root / "out", "flatbuffers-1")
            self.assertEqual((source / "CMakeLists.txt").read_bytes(), data)

    def test_safe_extraction_rejects_parent_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            archive_path = root / "source.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                data = b"escape"
                member = tarfile.TarInfo("flatbuffers-1/../../escape")
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
            with self.assertRaisesRegex(proof.ProofError, "unsafe source archive member"):
                proof.extract_regular_files(archive_path, root / "out", "flatbuffers-1")

    def test_approved_schema_pair_passes_policy(self) -> None:
        proof.verify_schema_policy(
            (proof.SCHEMA_DIR / "v1.fbs").read_text(encoding="utf-8"),
            (proof.SCHEMA_DIR / "v2.fbs").read_text(encoding="utf-8"),
        )

    def test_schema_policy_rejects_missing_id(self) -> None:
        schema = "table T { value:uint; }"
        with self.assertRaisesRegex(proof.ProofError, "no explicit id"):
            proof.parse_schema(schema, "bad.fbs")

    def test_schema_policy_rejects_reused_id(self) -> None:
        schema = """table T {
          first:uint (id: 0);
          second:uint (id: 0);
        }"""
        with self.assertRaisesRegex(proof.ProofError, "reuses field id"):
            proof.parse_schema(schema, "bad.fbs")

    def test_schema_policy_rejects_forbidden_required_field(self) -> None:
        schema = "table T { value:string (required, id: 0); }"
        with self.assertRaisesRegex(proof.ProofError, "forbidden schema feature"):
            proof.parse_schema(schema, "bad.fbs")

    def test_schema_policy_rejects_changed_existing_field(self) -> None:
        old = "table T { value:uint (id: 0); }"
        new = "table T { value:ulong (id: 0); }"
        with self.assertRaisesRegex(proof.ProofError, "changed or removed"):
            proof.verify_schema_policy(old, new)

    def test_schema_policy_rejects_nonconsecutive_ids(self) -> None:
        schema = "table T { value:uint (id: 1); }"
        with self.assertRaisesRegex(proof.ProofError, "not consecutive"):
            proof.parse_schema(schema, "bad.fbs")

    def test_sha256_file_is_streamed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "data"
            path.write_bytes(b"flatbuffers proof")
            self.assertEqual(proof.sha256_file(path), hashlib.sha256(b"flatbuffers proof").hexdigest())

    def test_seed_corpus_rejects_unpinned_or_platform_specific_output(self) -> None:
        lock = proof.load_lock()
        with tempfile.TemporaryDirectory() as directory:
            corpus = pathlib.Path(directory)
            for filename in proof.SEED_CORPUS_FILES:
                (corpus / filename).write_bytes(b"seed")
            with mock.patch.object(proof, "FUZZ_CORPUS_DIR", corpus), mock.patch.object(
                proof, "sha256_file", side_effect=lambda path: lock["seed_corpus"][path.name]
            ):
                self.assertEqual(proof.verify_seed_corpus(lock), lock["seed_corpus"])
                (corpus / "fuzzer-generated").write_bytes(b"mutation")
                with self.assertRaisesRegex(proof.ProofError, "generated seed corpus"):
                    proof.verify_seed_corpus(lock)

    def test_workflow_uses_approved_matrix_and_pinned_actions(self) -> None:
        workflow = (proof.ROOT / ".github" / "workflows" / "vnext-flatbuffers-proof.yml").read_text(
            encoding="utf-8"
        )
        for value in ("ubuntu-24.04", "windows-2022", "macos-15", "macos-15-intel"):
            self.assertIn(value, workflow)
        for compiler in ("gcc-13", "clang-18", "MSVC 2022 v143", "Xcode 16"):
            self.assertIn(compiler, workflow)
        self.assertIn("on:\n  workflow_dispatch:\n", workflow)
        for automatic_trigger in ("\n  push:", "\n  pull_request:", "\n  schedule:", "\n  release:"):
            self.assertNotIn(automatic_trigger, workflow)
        self.assertNotIn("github.event_name", workflow)
        self.assertNotRegex(workflow, r"uses:\s+[^\s@]+@v\d")
        self.assertIn("scripts/run_vnext_flatbuffers_proof.py", workflow)

    def test_hashed_proof_inputs_have_platform_independent_line_endings(self) -> None:
        attributes = (proof.ROOT / ".gitattributes").read_text(encoding="utf-8").splitlines()
        expected_rules = {
            "docs/vnext/proofs/flatbuffers/generated/*.h text eol=lf",
            "docs/vnext/proofs/flatbuffers/schema/*.fbs text eol=lf",
            "scripts/vnext_flatbuffers_proof.json text eol=lf",
        }
        self.assertTrue(expected_rules.issubset(attributes))
        hashed_inputs = [proof.LOCK_PATH, *proof.SCHEMA_DIR.glob("*.fbs")]
        hashed_inputs.extend(proof.GENERATED_DIR / name for name in proof.load_lock()["generated_files"])
        for path in hashed_inputs:
            contents = path.read_bytes()
            self.assertNotIn(b"\r", contents)


if __name__ == "__main__":
    unittest.main()
