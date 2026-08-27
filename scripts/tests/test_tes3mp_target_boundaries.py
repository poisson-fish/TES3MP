import ctypes
import functools
import os
import pathlib
import shutil
import subprocess
import tempfile
import textwrap
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
BOUNDARY_MODULE = REPOSITORY_ROOT / "cmake" / "TES3MPVerifyTargetBoundaries.cmake"
ENGINE_INDEPENDENT_SOURCE = REPOSITORY_ROOT / "components" / "tes3mp"
OPENMW_ADAPTER_SOURCE = REPOSITORY_ROOT / "apps" / "openmw" / "tes3mp"


def _find_tool(name, windows_candidates):
    discovered = shutil.which(name)
    if discovered:
        return discovered
    if os.name == "nt":
        for candidate in windows_candidates:
            if candidate.is_file():
                return str(candidate)
    return None


VISUAL_STUDIO_CMAKE_ROOTS = [
    pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    / "Microsoft Visual Studio"
    / "2022"
    / edition
    / "Common7"
    / "IDE"
    / "CommonExtensions"
    / "Microsoft"
    / "CMake"
    for edition in ("BuildTools", "Community", "Professional", "Enterprise")
]
CMAKE = _find_tool(
    "cmake",
    [root / "CMake" / "bin" / "cmake.exe" for root in VISUAL_STUDIO_CMAKE_ROOTS]
    + [pathlib.Path(r"C:\Program Files\CMake\bin\cmake.exe")],
)
NINJA = _find_tool(
    "ninja",
    [root / "Ninja" / "ninja.exe" for root in VISUAL_STUDIO_CMAKE_ROOTS],
)


@functools.lru_cache(maxsize=1)
def _build_environment():
    environment = os.environ.copy()
    if os.name == "nt" and not environment.get("VSCMD_VER", "").startswith("17."):
        program_files_x86 = pathlib.Path(
            environment.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
        )
        candidates = [
            program_files_x86
            / "Microsoft Visual Studio"
            / "2022"
            / edition
            / "Common7"
            / "Tools"
            / "VsDevCmd.bat"
            for edition in ("BuildTools", "Community", "Professional", "Enterprise")
        ]
        developer_command = next((path for path in candidates if path.is_file()), None)
        if developer_command is not None:
            buffer = ctypes.create_unicode_buffer(32768)
            length = ctypes.windll.kernel32.GetShortPathNameW(
                str(developer_command), buffer, len(buffer)
            )
            short_path = buffer.value if 0 < length < len(buffer) else str(developer_command)
            command = f"call {short_path} -arch=x64 -host_arch=x64 >nul && set"
            result = subprocess.run(
                [
                    "cmd.exe",
                    "/d",
                    "/s",
                    "/c",
                    command,
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            environment.update(
                line.split("=", 1)
                for line in result.stdout.splitlines()
                if "=" in line
            )

    tool_directories = [
        str(pathlib.Path(tool).parent) for tool in (CMAKE, NINJA) if tool is not None
    ]
    if tool_directories:
        environment["PATH"] = os.pathsep.join(tool_directories) + os.pathsep + environment.get(
            "PATH", ""
        )
    return environment


class TES3MPTargetBoundaryTests(unittest.TestCase):
    def _run_project(self, cmake_lists, files=None, build_target=None):
        if CMAKE is None or NINJA is None:
            self.skipTest("CMake and Ninja are required for target-boundary build tests")
        with tempfile.TemporaryDirectory() as temp_dir:
            source_dir = pathlib.Path(temp_dir) / "source"
            build_dir = pathlib.Path(temp_dir) / "build"
            source_dir.mkdir()
            (source_dir / "CMakeLists.txt").write_text(
                textwrap.dedent(cmake_lists), encoding="utf-8"
            )
            for relative_path, contents in (files or {}).items():
                path = source_dir / relative_path
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(contents, encoding="utf-8")

            configure = subprocess.run(
                [
                    CMAKE,
                    "-S",
                    str(source_dir),
                    "-B",
                    str(build_dir),
                    "-G",
                    "Ninja",
                ],
                check=False,
                capture_output=True,
                text=True,
                env=_build_environment(),
            )
            if build_target is None or configure.returncode != 0:
                return configure

            return subprocess.run(
                [CMAKE, "--build", str(build_dir), "--target", build_target],
                check=False,
                capture_output=True,
                text=True,
                env=_build_environment(),
            )

    def test_engine_independent_graph_configures_and_builds_without_openmw(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.16)
            project(tes3mp_independent_graph LANGUAGES CXX)
            add_subdirectory("{ENGINE_INDEPENDENT_SOURCE.as_posix()}" tes3mp)
            add_executable(alias_consumer main.cpp)
            target_link_libraries(alias_consumer PRIVATE TES3MP::TestSupport)
            """,
            files={"main.cpp": "int main() { return 0; }\n"},
            build_target="alias_consumer",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_protocol_value_contracts_build_and_run_without_openmw(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.16)
            project(tes3mp_strong_value_contract LANGUAGES CXX)
            add_subdirectory("{ENGINE_INDEPENDENT_SOURCE.as_posix()}" tes3mp)
            """,
            build_target="tes3mp_protocol_tests_run",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_adapter_graph_configures_with_only_approved_leaf_dependencies(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.16)
            project(tes3mp_adapter_graph LANGUAGES CXX)
            add_library(openmw-lib STATIC openmw.cpp)
            add_subdirectory("{ENGINE_INDEPENDENT_SOURCE.as_posix()}" tes3mp)
            add_subdirectory("{OPENMW_ADAPTER_SOURCE.as_posix()}" adapter)
            add_executable(adapter_consumer main.cpp)
            target_link_libraries(adapter_consumer PRIVATE TES3MP::OpenMWAdapter)
            """,
            files={
                "openmw.cpp": "void openmwTargetAnchor() {}\n",
                "main.cpp": "int main() { return 0; }\n",
            },
            build_target="adapter_consumer",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_undeclared_direct_dependency_fails_configuration(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.16)
            project(tes3mp_forbidden_link LANGUAGES CXX)
            include("{BOUNDARY_MODULE.as_posix()}")
            add_library(protocol STATIC protocol.cpp)
            add_library(openmw-lib STATIC openmw.cpp)
            target_link_libraries(protocol PRIVATE openmw-lib)
            tes3mp_verify_target_dependencies(protocol)
            """,
            files={
                "protocol.cpp": "void protocolAnchor() {}\n",
                "openmw.cpp": "void openmwAnchor() {}\n",
            },
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("forbidden direct dependency 'openmw-lib'", result.stdout + result.stderr)

    def test_forbidden_include_family_fails_configuration(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.16)
            project(tes3mp_forbidden_include LANGUAGES CXX)
            include("{BOUNDARY_MODULE.as_posix()}")
            add_library(protocol STATIC protocol.cpp)
            tes3mp_verify_target_includes(protocol FORBIDDEN apps/openmw)
            """,
            files={"protocol.cpp": '#include "apps/openmw/engine.hpp"\n'},
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("forbidden include family", result.stdout + result.stderr)

    def test_committed_graph_has_no_reverse_test_support_dependency(self):
        cmake_text = (ENGINE_INDEPENDENT_SOURCE / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        production_prefix = cmake_text.split(
            "tes3mp_add_engine_independent_library(tes3mp_test_support", 1
        )[0]
        self.assertNotIn("tes3mp_test_support", production_prefix)
        self.assertNotIn("vnext", cmake_text.lower())


if __name__ == "__main__":
    unittest.main()
