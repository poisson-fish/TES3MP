from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

import sys


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from verify_vnext_baseline import VerificationError, verify_repository  # noqa: E402


class BaselineVerifierTests(unittest.TestCase):
    def setUp(self) -> None:
        self._temporary_directory = tempfile.TemporaryDirectory()
        self.repository = Path(self._temporary_directory.name)
        self._git("init", "--quiet")
        self._git("config", "user.name", "Baseline Test")
        self._git("config", "user.email", "baseline-test@example.invalid")

        (self.repository / "dependency.txt").write_text("dependency declaration\n", encoding="utf-8")
        (self.repository / "upstream.txt").write_text("upstream\n", encoding="utf-8")
        self._git("add", ".")
        self._git("commit", "--quiet", "-m", "baseline")
        self.baseline = self._git("rev-parse", "HEAD")
        self.baseline_tree = self._git("rev-parse", "HEAD^{tree}")

        pre_cutover = self._git(
            "commit-tree", self.baseline_tree, "-p", self.baseline, "-m", "pre-cutover"
        )
        self.cutover = self._git(
            "commit-tree",
            self.baseline_tree,
            "-p",
            pre_cutover,
            "-p",
            self.baseline,
            "-m",
            "cutover",
        )
        self._git("checkout", "--quiet", "-B", "work", self.cutover)
        (self.repository / "allowed.txt").write_text("intentional\n", encoding="utf-8")
        self._git("add", "allowed.txt")
        self._git("commit", "--quiet", "-m", "intentional change")

        self.manifest_path = self.repository.parent / f"{self.repository.name}-manifest.json"
        self.manifest = self._new_manifest(pre_cutover)
        self._write_manifest()

    def tearDown(self) -> None:
        self.manifest_path.unlink(missing_ok=True)
        self._temporary_directory.cleanup()

    def _git(self, *arguments: str) -> str:
        result = subprocess.run(
            ["git", "-C", str(self.repository), *arguments],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
        )
        return result.stdout.strip()

    def _git_bytes(self, *arguments: str) -> bytes:
        return subprocess.check_output(["git", "-C", str(self.repository), *arguments])

    def _new_manifest(self, pre_cutover: str) -> dict[str, object]:
        dependency_hash = hashlib.sha256(
            self._git_bytes("cat-file", "blob", f"{self.baseline}:dependency.txt")
        ).hexdigest()
        return {
            "schema_version": 1,
            "baseline": {
                "repository": "https://example.invalid/upstream.git",
                "tag": "baseline-tag",
                "commit": self.baseline,
                "tree": self.baseline_tree,
            },
            "cutover": {
                "commit": self.cutover,
                "tree": self.baseline_tree,
                "first_parent": pre_cutover,
                "second_parent": self.baseline,
                "preserved_prefix": "docs/vnext",
            },
            "intentional_differences": [
                {"path": "allowed.txt", "status": "A", "purpose": "test fixture"}
            ],
            "dependency_inputs": [
                {
                    "id": "fixture",
                    "platforms": ["test"],
                    "files": [{"path": "dependency.txt", "sha256": dependency_hash}],
                    "repositories": ["https://example.invalid/packages"],
                    "resolution": "fixed fixture",
                    "license_evidence": "fixture metadata",
                    "phase1_follow_up": "none",
                }
            ],
        }

    def _write_manifest(self) -> None:
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def test_accepts_exact_recorded_difference_and_dependency_hash(self) -> None:
        report = verify_repository(self.repository, self.manifest_path)
        self.assertIn("Intentional differences (1):", report)
        self.assertIn("  A allowed.txt", report)

    def test_rejects_unrecorded_difference(self) -> None:
        (self.repository / "unrecorded.txt").write_text("unexpected\n", encoding="utf-8")
        self._git("add", "unrecorded.txt")
        self._git("commit", "--quiet", "-m", "unrecorded")
        with self.assertRaisesRegex(VerificationError, "unrecorded A unrecorded.txt"):
            verify_repository(self.repository, self.manifest_path)

    def test_rejects_missing_recorded_difference(self) -> None:
        self.manifest["intentional_differences"].append(  # type: ignore[union-attr]
            {"path": "missing.txt", "status": "A", "purpose": "must exist"}
        )
        self._write_manifest()
        with self.assertRaisesRegex(VerificationError, "missing expected A missing.txt"):
            verify_repository(self.repository, self.manifest_path)

    def test_rejects_dependency_declaration_drift(self) -> None:
        (self.repository / "dependency.txt").write_text("changed declaration\n", encoding="utf-8")
        self._git("add", "dependency.txt")
        self._git("commit", "--quiet", "-m", "dependency drift")
        self.manifest["intentional_differences"] = [  # type: ignore[index]
            {"path": "allowed.txt", "status": "A", "purpose": "test fixture"},
            {"path": "dependency.txt", "status": "M", "purpose": "test dependency change"},
        ]
        self._write_manifest()
        with self.assertRaisesRegex(VerificationError, "dependency declaration hash mismatch"):
            verify_repository(self.repository, self.manifest_path)

    def test_rejects_wrong_cutover_parent_order(self) -> None:
        cutover = self.manifest["cutover"]
        assert isinstance(cutover, dict)
        cutover["first_parent"], cutover["second_parent"] = (
            cutover["second_parent"],
            cutover["first_parent"],
        )
        self._write_manifest()
        with self.assertRaisesRegex(VerificationError, "cutover parent mismatch"):
            verify_repository(self.repository, self.manifest_path)

    def test_rejects_unsorted_or_duplicate_manifest_paths(self) -> None:
        self.manifest["intentional_differences"] = [  # type: ignore[index]
            {"path": "z.txt", "status": "A", "purpose": "later"},
            {"path": "a.txt", "status": "A", "purpose": "earlier"},
        ]
        self._write_manifest()
        with self.assertRaisesRegex(VerificationError, "must be sorted by path"):
            verify_repository(self.repository, self.manifest_path)


if __name__ == "__main__":
    unittest.main()
