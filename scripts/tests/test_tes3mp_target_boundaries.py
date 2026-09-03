import ctypes
import functools
import json
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
OPENMW_MAIN_SOURCE = REPOSITORY_ROOT / "apps" / "openmw" / "main.cpp"


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
    def test_openmw_runtime_failure_is_visible_and_sanitized(self):
        source = OPENMW_MAIN_SOURCE.read_text(encoding="utf-8")
        status_body = source.split("class MultiplayerStatus final", 1)[1].split("};", 1)[0]
        self.assertIn('Log(Debug::Error) << message', status_body)
        self.assertIn('getWindowManager()->messageBox(message)', status_body)
        self.assertIn('"TES3MP connection status: "', status_body)
        self.assertIn('"TES3MP connection stopped: "', status_body)
        self.assertIn('prefix + describe(status)', status_body)
        for secret_source in ("tes3mp-password-file", "passwordFile", "AuthenticationMaterial"):
            self.assertNotIn(secret_source, status_body)

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

    def test_transport_factory_header_builds_without_selected_dependencies(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.16)
            project(tes3mp_transport_public_header LANGUAGES CXX)
            add_subdirectory("{ENGINE_INDEPENDENT_SOURCE.as_posix()}" tes3mp)
            add_executable(transport_header_consumer main.cpp)
            target_compile_features(transport_header_consumer PRIVATE cxx_std_20)
            target_link_libraries(transport_header_consumer PRIVATE TES3MP::Transport)
            """,
            files={
                "main.cpp": "#include <tes3mp/transport_gns.hpp>\nint main() { return 0; }\n"
            },
            build_target="transport_header_consumer",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_selected_adapter_fails_closed_without_verified_manifest(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.24)
            project(tes3mp_transport_missing_manifest LANGUAGES CXX)
            set(TES3MP_ENABLE_GNS_TRANSPORT ON CACHE BOOL "")
            set(TES3MP_TRANSPORT_DEPENDENCY_MANIFEST
                "${{CMAKE_CURRENT_SOURCE_DIR}}/absent.json" CACHE FILEPATH "")
            add_subdirectory("{ENGINE_INDEPENDENT_SOURCE.as_posix()}" tes3mp)
            """,
        )
        self.assertNotEqual(result.returncode, 0)
        output = result.stdout + result.stderr
        self.assertIn("TES3MP_ENABLE_GNS_TRANSPORT requires", output)
        self.assertIn("verified transport dependency", output)

    def test_selected_adapter_rejects_unapproved_manifest_profile(self):
        manifest = {
            "schema_version": 1,
            "production_profile": "plaintext-or-system-package-fallback",
        }
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.24)
            project(tes3mp_transport_wrong_profile LANGUAGES CXX)
            set(TES3MP_ENABLE_GNS_TRANSPORT ON CACHE BOOL "")
            set(TES3MP_TRANSPORT_DEPENDENCY_MANIFEST
                "${{CMAKE_CURRENT_SOURCE_DIR}}/manifest.json" CACHE FILEPATH "")
            add_subdirectory("{ENGINE_INDEPENDENT_SOURCE.as_posix()}" tes3mp)
            """,
            files={"manifest.json": json.dumps(manifest)},
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("manifest schema/profile is not approved", result.stdout + result.stderr)

    def test_selected_adapter_lock_hashes_match_repository_locks(self):
        import hashlib
        import re

        cmake = (ENGINE_INDEPENDENT_SOURCE / "CMakeLists.txt").read_text(encoding="utf-8")
        for variable, lock_name in (
            ("tes3mp_expected_gns_lock", "vnext_gamenetworkingsockets_proof.json"),
            ("tes3mp_expected_cares_lock", "vnext_cares_proof.json"),
        ):
            match = re.search(rf'set\({variable}\s+"([0-9a-f]{{64}})"\)', cmake)
            self.assertIsNotNone(match)
            lock = REPOSITORY_ROOT / "scripts" / lock_name
            self.assertEqual(match.group(1), hashlib.sha256(lock.read_bytes()).hexdigest())

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
        self.assertIn(
            'tes3mp_verify_target_includes(${target} FORBIDDEN "tes3mp/test_support")',
            cmake_text,
        )
        self.assertNotIn("vnext", cmake_text.lower())

    def test_selected_library_includes_are_private_to_adapter_sources(self):
        public_factory = (
            ENGINE_INDEPENDENT_SOURCE / "include" / "tes3mp" / "transport_gns.hpp"
        ).read_text(encoding="utf-8")
        abstraction = (
            ENGINE_INDEPENDENT_SOURCE / "include" / "tes3mp" / "transport.hpp"
        ).read_text(encoding="utf-8")
        adapter = (
            ENGINE_INDEPENDENT_SOURCE / "transport" / "transport_gns.cpp"
        ).read_text(encoding="utf-8")
        for selected_header in ("<ares.h>", "<steam/isteamnetworkingutils.h>"):
            self.assertNotIn(selected_header, public_factory)
            self.assertNotIn(selected_header, abstraction)
            self.assertIn(selected_header, adapter)
        self.assertEqual(adapter.count("k_ESteamNetworkingConfig_Unencrypted, 0"), 2)
        self.assertNotIn("k_ESteamNetworkingConfig_Unencrypted, 1", adapter)
        self.assertEqual(
            adapter.count(
                "SteamAPI_ISteamNetworkingUtils_SetGlobalCallback_SteamNetConnectionStatusChanged"
            ),
            2,
        )
        self.assertNotIn(
            "SteamNetworkingUtils()->SetGlobalCallback_SteamNetConnectionStatusChanged",
            adapter,
        )
        self.assertNotIn("sockets()->", adapter)
        for flat_socket_call in (
            "AcceptConnection",
            "CloseConnection",
            "CloseListenSocket",
            "ConfigureConnectionLanes",
            "ConnectByIPAddress",
            "CreateListenSocketIP",
            "GetListenSocketAddress",
            "ReceiveMessagesOnConnection",
            "RunCallbacks",
            "SendMessages",
            "SetConnectionUserData",
        ):
            self.assertIn(
                f"SteamAPI_ISteamNetworkingSockets_{flat_socket_call}", adapter
            )
        self.assertIn("SteamAPI_ISteamNetworkingUtils_AllocateMessage", adapter)
        self.assertNotIn("SteamNetworkingUtils()->AllocateMessage", adapter)
        self.assertIn("priorities{ 0, 0 }", adapter)
        self.assertIn("weights{ 1, 1 }", adapter)
        self.assertIn("k_nSteamNetworkingSend_Reliable", adapter)
        self.assertIn("k_nSteamNetworkingSend_UnreliableNoDelay", adapter)

        credential_header = (
            ENGINE_INDEPENDENT_SOURCE
            / "include"
            / "tes3mp"
            / "server_authentication.hpp"
        ).read_text(encoding="utf-8")
        credential_source = (
            ENGINE_INDEPENDENT_SOURCE / "server_core" / "credential_crypto.cpp"
        ).read_text(encoding="utf-8")
        cmake_text = (ENGINE_INDEPENDENT_SOURCE / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("<openssl/", credential_header)
        for selected_header in (
            "<openssl/crypto.h>",
            "<openssl/rand.h>",
            "<openssl/sha.h>",
        ):
            self.assertIn(selected_header, credential_source)
        self.assertIn(
            "target_link_libraries(tes3mp_server_core PRIVATE OpenSSL::Crypto)",
            cmake_text,
        )
        self.assertIn(
            "PRIVATE GameNetworkingSockets::static c-ares::cares_static OpenSSL::Crypto",
            cmake_text,
        )
        self.assertNotIn("<openssl/", abstraction)
        adapter_detail = (
            ENGINE_INDEPENDENT_SOURCE / "transport" / "transport_gns_detail.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("<openssl/evp.h>", adapter_detail)

    def test_selected_adapter_reuses_verified_abseil_package(self):
        cmake_text = (ENGINE_INDEPENDENT_SOURCE / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'set(absl_DIR "${TES3MP_PROTOBUF_ROOT}/lib/cmake/absl"', cmake_text
        )
        self.assertIn("find_package(absl CONFIG REQUIRED)", cmake_text)
        self.assertNotIn('add_subdirectory("${TES3MP_abseil_SOURCE_DIR}"', cmake_text)
        self.assertIn(
            'set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"', cmake_text
        )
        self.assertIn("set(SANITIZE_ADDRESS ON", cmake_text)
        self.assertIn("set(SANITIZE_UNDEFINED ON", cmake_text)
        self.assertIn(
            "target_compile_options(GameNetworkingSockets_s PRIVATE -fno-sanitize=function)",
            cmake_text,
        )

    def test_production_target_cannot_include_test_support(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.16)
            project(tes3mp_reverse_test_include LANGUAGES CXX)
            include("{BOUNDARY_MODULE.as_posix()}")
            add_library(server_core STATIC server_core.cpp)
            tes3mp_verify_target_includes(server_core FORBIDDEN tes3mp/test_support)
            """,
            files={
                "server_core.cpp": '#include <tes3mp/test_support/manual_clock.hpp>\n'
            },
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("forbidden include family", result.stdout + result.stderr)

    def test_runtime_safety_profiles_fail_closed_when_combined(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.16)
            project(tes3mp_invalid_safety_profile LANGUAGES CXX)
            set(TES3MP_ENABLE_ASAN_UBSAN ON CACHE BOOL "")
            set(TES3MP_ENABLE_TSAN ON CACHE BOOL "")
            add_subdirectory("{ENGINE_INDEPENDENT_SOURCE.as_posix()}" tes3mp)
            """,
        )
        self.assertNotEqual(result.returncode, 0)
        output = result.stdout + result.stderr
        self.assertIn("ASan+UBSan and ThreadSanitizer profiles", output)
        self.assertIn("separate build", output)

    def test_fuzzer_requires_asan_ubsan_profile(self):
        result = self._run_project(
            f"""
            cmake_minimum_required(VERSION 3.16)
            project(tes3mp_invalid_fuzzer_profile LANGUAGES CXX)
            set(TES3MP_BUILD_FUZZERS ON CACHE BOOL "")
            add_subdirectory("{ENGINE_INDEPENDENT_SOURCE.as_posix()}" tes3mp)
            """,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("require the ASan+UBSan profile", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
