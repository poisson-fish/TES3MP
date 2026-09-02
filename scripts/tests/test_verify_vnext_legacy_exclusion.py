from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

from scripts import verify_vnext_legacy_exclusion as exclusion


class LegacyExclusionTests(unittest.TestCase):
    def make_fixture(self) -> tuple[tempfile.TemporaryDirectory[str], pathlib.Path, pathlib.Path]:
        temporary = tempfile.TemporaryDirectory()
        repository = pathlib.Path(temporary.name) / "repository"
        build = repository / "build" / "vnext-baseline"
        repository.mkdir()
        build.mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(repository)], check=True)
        subprocess.run(["git", "-C", str(repository), "config", "user.name", "Test"], check=True)
        subprocess.run(["git", "-C", str(repository), "config", "user.email", "test@example.invalid"], check=True)
        (repository / "CMakeLists.txt").write_text("add_executable(openmw main.cpp)\n", encoding="utf-8")
        (repository / "main.cpp").write_text("int main() {}\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(repository), "add", "."], check=True)
        subprocess.run(["git", "-C", str(repository), "commit", "-qm", "fixture"], check=True)
        (build / "compile_commands.json").write_text(
            json.dumps(
                [
                    {
                        "directory": str(build),
                        "command": f"c++ -c {repository / 'main.cpp'}",
                        "file": str(repository / "main.cpp"),
                    }
                ]
            ),
            encoding="utf-8",
        )
        (build / "build.ninja").write_text("build openmw: CXX_COMPILER main.cpp\n", encoding="utf-8")
        return temporary, repository, build

    def test_accepts_clean_source_and_configured_graph(self) -> None:
        temporary, repository, build = self.make_fixture()
        with temporary:
            evidence = exclusion.verify(repository, build)
        self.assertEqual(evidence["tracked_paths_checked"], 2)
        self.assertEqual(evidence["cmake_metadata_files_checked"], 1)
        self.assertEqual(evidence["compile_commands_checked"], 1)
        self.assertEqual(evidence["ninja_build_edges_checked"], 1)

    def test_rejects_archived_multiplayer_path_in_index(self) -> None:
        temporary, repository, build = self.make_fixture()
        with temporary:
            path = repository / "components" / "openmw-mp" / "NetworkMessages.hpp"
            path.parent.mkdir(parents=True)
            path.write_text("legacy\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(repository), "add", str(path)], check=True)
            with self.assertRaisesRegex(exclusion.ExclusionError, "legacy multiplayer paths remain tracked"):
                exclusion.verify_source_tree(repository, use_index=True)

    def test_rejects_legacy_dependency_in_cmake_metadata(self) -> None:
        temporary, repository, build = self.make_fixture()
        with temporary:
            (repository / "CMakeLists.txt").write_text("find_package(RakNet REQUIRED)\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(repository), "add", "CMakeLists.txt"], check=True)
            with self.assertRaisesRegex(exclusion.ExclusionError, "RakNet"):
                exclusion.verify_source_tree(repository, use_index=True)

    def test_rejects_legacy_source_in_compilation_database(self) -> None:
        temporary, repository, build = self.make_fixture()
        with temporary:
            legacy = repository / "apps" / "openmw-mp" / "main.cpp"
            legacy.parent.mkdir(parents=True)
            legacy.write_text("int main() {}\n", encoding="utf-8")
            (build / "compile_commands.json").write_text(
                json.dumps([{"directory": str(build), "command": f"c++ -c {legacy}", "file": str(legacy)}]),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(exclusion.ExclusionError, "legacy multiplayer source"):
                exclusion.verify_compile_commands(repository, build)

    def test_rejects_legacy_path_in_ninja_graph(self) -> None:
        temporary, repository, build = self.make_fixture()
        with temporary:
            (build / "build.ninja").write_text("build old: LINK /apps/openmw-mp/main.cpp.obj\n", encoding="utf-8")
            with self.assertRaisesRegex(exclusion.ExclusionError, "apps/openmw-mp"):
                exclusion.verify_ninja_graph(build)

    def test_allows_vnext_server_target_name(self) -> None:
        temporary, repository, build = self.make_fixture()
        with temporary:
            (build / "build.ninja").write_text("build tes3mp-server: LINK vnext-main.cpp.obj\n", encoding="utf-8")
            edges, digest = exclusion.verify_ninja_graph(build)
        self.assertEqual(edges, 1)
        self.assertRegex(digest, r"^[0-9a-f]{64}$")

    def test_rejects_empty_compilation_database(self) -> None:
        temporary, repository, build = self.make_fixture()
        with temporary:
            (build / "compile_commands.json").write_text("[]\n", encoding="utf-8")
            with self.assertRaisesRegex(exclusion.ExclusionError, "empty or invalid"):
                exclusion.verify_compile_commands(repository, build)

    def test_main_writes_machine_readable_evidence(self) -> None:
        temporary, repository, build = self.make_fixture()
        with temporary:
            evidence_path = build / "evidence.json"
            result = exclusion.main(
                ["--repository", str(repository), "--build-dir", str(build), "--evidence", str(evidence_path)]
            )
            evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
        self.assertEqual(result, 0)
        self.assertEqual(evidence["schema_version"], 1)
        self.assertRegex(evidence["ninja_graph_sha256"], r"^[0-9a-f]{64}$")


if __name__ == "__main__":
    unittest.main()
