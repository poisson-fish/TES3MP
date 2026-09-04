import hashlib
import io
import json
import tarfile
import tempfile
import unittest
from pathlib import Path

from scripts import prepare_openmw_vr_dependencies as subject


class OpenXrPreparationTests(unittest.TestCase):
    def make_fixture(self, root: Path) -> tuple[Path, Path]:
        commit = "1" * 40
        archive = root / "fixture.tar.gz"
        license_bytes = b"test license\n"
        with tarfile.open(archive, "w:gz") as output:
            for name, contents in (("CMakeLists.txt", b"cmake_minimum_required(VERSION 3.5)\n"), ("LICENSE", license_bytes)):
                info = tarfile.TarInfo(f"OpenXR-SDK-{commit}/{name}")
                info.size = len(contents)
                output.addfile(info, io.BytesIO(contents))
        provenance = root / "provenance.json"
        provenance.write_text(json.dumps({"openxr": {
            "repository": "https://example.invalid/repo.git", "fork_source_ref": "tag",
            "commit": commit, "archive_url": archive.as_uri(),
            "archive_sha256": subject.sha256(archive), "license": "Apache-2.0",
            "license_path": "LICENSE", "license_sha256": hashlib.sha256(license_bytes).hexdigest(),
            "acquisition_command": "python scripts/prepare_openmw_vr_dependencies.py",
            "cmake_path": "extern/CMakeLists.txt",
        }}), encoding="utf-8")
        return provenance, archive

    def test_verified_cache_supports_offline_reuse(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            provenance, _ = self.make_fixture(root)
            cache = root / "cache"
            first = subject.prepare(cache, provenance)
            second = subject.prepare(cache, provenance, offline=True)
            self.assertEqual(first, second)
            self.assertTrue((first / "CMakeLists.txt").is_file())

    def test_offline_mode_fails_closed_without_archive(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            provenance, _ = self.make_fixture(root)
            with self.assertRaisesRegex(subject.PreparationError, "offline mode"):
                subject.prepare(root / "empty-cache", provenance, offline=True)


if __name__ == "__main__":
    unittest.main()
