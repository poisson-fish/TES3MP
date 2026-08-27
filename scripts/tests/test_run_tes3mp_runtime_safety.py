import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest import mock


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPOSITORY_ROOT / "scripts" / "run_tes3mp_runtime_safety.py"
WORKFLOW_PATH = REPOSITORY_ROOT / ".github" / "workflows" / "vnext-runtime-safety.yml"
PRESETS_PATH = REPOSITORY_ROOT / "components" / "tes3mp" / "CMakePresets.json"
CMAKE_MODULE_PATH = REPOSITORY_ROOT / "cmake" / "TES3MPRuntimeSafety.cmake"
COMPONENT_CMAKE_PATH = REPOSITORY_ROOT / "components" / "tes3mp" / "CMakeLists.txt"

SPEC = importlib.util.spec_from_file_location("run_tes3mp_runtime_safety", SCRIPT_PATH)
assert SPEC and SPEC.loader
safety = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(safety)


class RuntimeSafetyRunnerTests(unittest.TestCase):
    def test_profiles_are_separate_and_fuzzing_is_asan_only(self):
        safety.validate_request("asan-ubsan", 30, host="Linux")
        safety.validate_request("tsan", 0, host="Linux")
        with self.assertRaisesRegex(safety.RuntimeSafetyError, "only in the ASan"):
            safety.validate_request("tsan", 1, host="Linux")
        with self.assertRaisesRegex(safety.RuntimeSafetyError, "only on Linux"):
            safety.validate_request("asan-ubsan", 0, host="Windows")

    def test_profile_environment_is_strict_and_has_no_suppressions(self):
        with mock.patch.object(
            safety, "find_tool", side_effect=["/usr/bin/clang-18", "/usr/bin/clang++-18"]
        ):
            asan = safety.profile_environment("asan-ubsan", {})
        self.assertEqual(asan["CC"], "/usr/bin/clang-18")
        self.assertEqual(asan["CXX"], "/usr/bin/clang++-18")
        self.assertEqual(
            asan["ASAN_OPTIONS"],
            "halt_on_error=1:detect_leaks=1:detect_container_overflow=1",
        )
        self.assertEqual(asan["UBSAN_OPTIONS"], "halt_on_error=1:print_stacktrace=1")
        self.assertFalse(any("suppress" in key.lower() for key in asan))

        with mock.patch.object(
            safety, "find_tool", side_effect=["/usr/bin/clang-18", "/usr/bin/clang++-18"]
        ):
            tsan = safety.profile_environment(
                "tsan", {"ASAN_OPTIONS": "bad", "LSAN_OPTIONS": "suppressions=bad"}
            )
        self.assertEqual(tsan["TSAN_OPTIONS"], "halt_on_error=1")
        self.assertNotIn("ASAN_OPTIONS", tsan)
        self.assertNotIn("LSAN_OPTIONS", tsan)

    def test_commands_use_named_presets_and_bounded_fuzz_arguments(self):
        self.assertEqual(
            safety.configure_command("cmake", "asan-ubsan"),
            ["cmake", "--preset", "tes3mp-safety-asan-ubsan", "--fresh"],
        )
        self.assertEqual(
            safety.build_command("cmake", "tsan"),
            [
                "cmake",
                "--build",
                "--preset",
                "tes3mp-safety-tsan",
                "--parallel",
                "4",
            ],
        )
        command = safety.fuzz_command("asan-ubsan", 30, pathlib.Path("artifacts"))
        self.assertIn("-max_total_time=30", command)
        self.assertIn("-timeout=5", command)
        self.assertIn("-rss_limit_mb=512", command)
        self.assertIn("-max_len=256", command)
        frame_command = safety.fuzz_command(
            "asan-ubsan",
            30,
            pathlib.Path("artifacts"),
            target="tes3mp_protocol_frame_fuzz",
            corpus_dir=safety.PROTOCOL_FRAME_CORPUS_DIR,
            maximum_input_bytes=safety.MAX_PROTOCOL_FRAME_BYTES + 1,
        )
        self.assertIn("tes3mp_protocol_frame_fuzz", frame_command[0])
        self.assertIn("-max_len=65549", frame_command)
        handshake_command = safety.fuzz_command(
            "asan-ubsan",
            30,
            pathlib.Path("artifacts"),
            target="tes3mp_protocol_handshake_fuzz",
            corpus_dir=safety.PROTOCOL_HANDSHAKE_CORPUS_DIR,
            maximum_input_bytes=safety.TES3MP_SESSION_CONTROL_MAX_BYTES + 1,
        )
        self.assertIn("tes3mp_protocol_handshake_fuzz", handshake_command[0])
        self.assertIn("-max_len=4097", handshake_command)

    def test_seed_corpus_is_bounded_and_covers_length_boundaries(self):
        records = safety.verify_corpus()
        self.assertLessEqual(len(records), safety.MAX_CORPUS_FILES)
        self.assertTrue(all(record["bytes"] <= safety.MAX_CORPUS_FILE_BYTES for record in records))
        sizes = {record["bytes"] for record in records}
        self.assertIn(101, sizes)
        self.assertTrue(any(size > safety.MAX_SPATIAL_SNAPSHOT_BYTES for size in sizes))
        attributes = (REPOSITORY_ROOT / ".gitattributes").read_text(encoding="utf-8")
        self.assertIn("components/tes3mp/tests/fuzz/corpus/** binary", attributes)
        frame_records = safety.verify_protocol_frame_corpus()
        self.assertGreaterEqual(len(frame_records), 2)
        self.assertTrue(all(record["bytes"] <= safety.MAX_CORPUS_FILE_BYTES for record in frame_records))
        handshake_records = safety.verify_protocol_handshake_corpus()
        self.assertGreaterEqual(len(handshake_records), 2)
        self.assertTrue(
            all(record["bytes"] <= safety.MAX_CORPUS_FILE_BYTES for record in handshake_records)
        )

    def test_presets_keep_normal_build_clean_and_safety_profiles_exclusive(self):
        data = json.loads(PRESETS_PATH.read_text(encoding="utf-8"))
        configure = {preset["name"]: preset for preset in data["configurePresets"]}
        normal = configure["tes3mp-standalone"]
        self.assertNotIn("cacheVariables", normal)
        asan = configure["tes3mp-safety-asan-ubsan"]["cacheVariables"]
        self.assertEqual(asan["TES3MP_ENABLE_ASAN_UBSAN"], True)
        self.assertEqual(asan["TES3MP_ENABLE_TSAN"], False)
        self.assertEqual(asan["TES3MP_BUILD_FUZZERS"], True)
        tsan = configure["tes3mp-safety-tsan"]["cacheVariables"]
        self.assertEqual(tsan["TES3MP_ENABLE_ASAN_UBSAN"], False)
        self.assertEqual(tsan["TES3MP_ENABLE_TSAN"], True)
        self.assertEqual(tsan["TES3MP_BUILD_FUZZERS"], False)

    def test_cmake_policy_instruments_owned_targets_and_rejects_bad_profiles(self):
        module = CMAKE_MODULE_PATH.read_text(encoding="utf-8")
        component = COMPONENT_CMAKE_PATH.read_text(encoding="utf-8")
        self.assertIn("TES3MP_ENABLE_ASAN_UBSAN AND TES3MP_ENABLE_TSAN", module)
        self.assertIn("TES3MP_BUILD_FUZZERS AND NOT TES3MP_ENABLE_ASAN_UBSAN", module)
        self.assertIn('CMAKE_SYSTEM_NAME STREQUAL "Linux"', module)
        self.assertIn('CMAKE_CXX_COMPILER_VERSION MATCHES "^18\\\\."', module)
        self.assertNotIn("suppress", module.lower())
        for target in (
            "tes3mp_protocol_tests",
            "tes3mp_protocol_frame_tests",
            "tes3mp_protocol_handshake_tests",
            "tes3mp_spatial_primitive_tests",
            "tes3mp_deterministic_facilities_tests",
            "tes3mp_deterministic_harness_tests",
            "tes3mp_fault_injection_tests",
            "tes3mp_observability_tests",
        ):
            self.assertIn(f"tes3mp_enable_runtime_safety({target})", component)
        self.assertIn("tes3mp_enable_runtime_safety(${target})", component)
        self.assertIn("tes3mp_enable_libfuzzer(tes3mp_spatial_round_trip_fuzz)", component)
        self.assertIn("tes3mp_enable_libfuzzer(tes3mp_protocol_frame_fuzz)", component)
        self.assertIn("tes3mp_enable_libfuzzer(tes3mp_protocol_handshake_fuzz)", component)
        adapter = (REPOSITORY_ROOT / "apps" / "openmw" / "tes3mp" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        self.assertIn("tes3mp_enable_runtime_safety(openmw_tes3mp_adapter)", adapter)

    def test_compile_command_check_requires_every_owned_source_to_be_instrumented(self):
        with tempfile.TemporaryDirectory() as temporary:
            build_dir = pathlib.Path(temporary)
            entries = []
            for relative in sorted(safety.EXPECTED_COMPILED_SOURCES):
                entries.append(
                    {
                        "directory": str(REPOSITORY_ROOT),
                        "file": str(safety.SOURCE_DIR / relative),
                        "command": "clang++-18 -fsanitize=thread -c source.cpp",
                    }
                )
            (build_dir / "compile_commands.json").write_text(
                json.dumps(entries), encoding="utf-8"
            )
            observed = safety.verify_instrumented_compile_commands("tsan", build_dir)
            self.assertEqual(set(observed), safety.EXPECTED_COMPILED_SOURCES)

            entries[0]["command"] = "clang++-18 -c source.cpp"
            (build_dir / "compile_commands.json").write_text(
                json.dumps(entries), encoding="utf-8"
            )
            with self.assertRaisesRegex(safety.RuntimeSafetyError, "omitted instrumentation"):
                safety.verify_instrumented_compile_commands("tsan", build_dir)

    def test_workflow_is_pinned_bounded_phase_exit_only_and_retains_failures(self):
        workflow = WORKFLOW_PATH.read_text(encoding="utf-8")
        self.assertIn("runs-on: ubuntu-24.04", workflow)
        self.assertEqual(workflow.count("timeout-minutes: 20"), 2)
        self.assertIn("on:\n  workflow_dispatch:\n", workflow)
        for automatic_trigger in ("\n  push:", "\n  pull_request:", "\n  schedule:", "\n  release:"):
            self.assertNotIn(automatic_trigger, workflow)
        self.assertIn("--profile asan-ubsan --fuzz-seconds 30", workflow)
        self.assertIn("--profile tsan", workflow)
        self.assertEqual(workflow.count("if: always()"), 2)
        self.assertNotIn("@main", workflow)
        self.assertIn("actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd", workflow)
        self.assertIn("actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02", workflow)


if __name__ == "__main__":
    unittest.main()
