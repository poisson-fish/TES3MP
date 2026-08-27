#!/usr/bin/env python3
"""Run the owner-approved TES3MP sanitizer, race, and fuzz smoke profiles."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import pathlib
import platform
import re
import shutil
import subprocess
import sys
from typing import Any


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "components" / "tes3mp"
CORPUS_DIR = SOURCE_DIR / "tests" / "fuzz" / "corpus" / "spatial_round_trip"
PROTOCOL_FRAME_CORPUS_DIR = SOURCE_DIR / "tests" / "fuzz" / "corpus" / "protocol_frame"
PROFILES = {
    "asan-ubsan": {
        "preset": "tes3mp-safety-asan-ubsan",
        "build_dir": ROOT / "build" / "tes3mp-safety-asan-ubsan",
        "sanitizers": ["AddressSanitizer", "UndefinedBehaviorSanitizer"],
    },
    "tsan": {
        "preset": "tes3mp-safety-tsan",
        "build_dir": ROOT / "build" / "tes3mp-safety-tsan",
        "sanitizers": ["ThreadSanitizer"],
    },
}
CONTRACT_EXECUTABLES = (
    "tes3mp_protocol_tests",
    "tes3mp_protocol_frame_tests",
    "tes3mp_spatial_primitive_tests",
    "tes3mp_deterministic_facilities_tests",
    "tes3mp_deterministic_harness_tests",
    "tes3mp_fault_injection_tests",
    "tes3mp_observability_tests",
)
ASAN_UBSAN_ENVIRONMENT = {
    "ASAN_OPTIONS": "halt_on_error=1:detect_leaks=1:detect_container_overflow=1",
    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
}
TSAN_ENVIRONMENT = {"TSAN_OPTIONS": "halt_on_error=1"}
MAX_CORPUS_FILES = 64
MAX_CORPUS_FILE_BYTES = 256
MAX_SPATIAL_SNAPSHOT_BYTES = 109
MAX_PROTOCOL_FRAME_BYTES = 12 + 64 * 1024
EXPECTED_COMPILED_SOURCES = {
    "client_session/anchor.cpp",
    "protocol/anchor.cpp",
    "protocol/protocol_frame.cpp",
    "server_core/anchor.cpp",
    "server_core/deterministic_random.cpp",
    "server_core/fixed_tick_scheduler.cpp",
    "server_core/observability.cpp",
    "test_support/anchor.cpp",
    "test_support/deterministic_harness.cpp",
    "test_support/fault_injecting_link.cpp",
    "test_support/in_memory_link.cpp",
    "test_support/manual_clock.cpp",
    "test_support/recording_observability.cpp",
    "test_support/spatial_round_trip.cpp",
    "tests/deterministic_facilities_tests.cpp",
    "tests/deterministic_harness_tests.cpp",
    "tests/fault_injection_tests.cpp",
    "tests/observability_tests.cpp",
    "tests/protocol_frame_tests.cpp",
    "tests/spatial_primitive_tests.cpp",
    "tests/strong_value_tests.cpp",
    "transport/anchor.cpp",
}


class RuntimeSafetyError(RuntimeError):
    pass


def find_tool(name: str, environment_name: str) -> str:
    configured = os.environ.get(environment_name)
    if configured:
        path = pathlib.Path(configured)
        if path.is_file():
            return str(path)
        raise RuntimeSafetyError(f"{environment_name} does not name a file: {configured}")
    resolved = shutil.which(name)
    if not resolved:
        raise RuntimeSafetyError(f"Required tool '{name}' is not on PATH (or set {environment_name}).")
    return resolved


def validate_request(profile: str, fuzz_seconds: int, host: str | None = None) -> None:
    if profile not in PROFILES:
        raise RuntimeSafetyError(f"Unknown runtime-safety profile: {profile}")
    if fuzz_seconds < 0:
        raise RuntimeSafetyError("Fuzz duration cannot be negative")
    if (host or platform.system()) != "Linux":
        raise RuntimeSafetyError("TES3MP runtime-safety profiles are supported only on Linux")
    if profile != "asan-ubsan" and fuzz_seconds:
        raise RuntimeSafetyError("Fuzzing is available only in the ASan+UBSan profile")


def profile_environment(profile: str, base: dict[str, str] | None = None) -> dict[str, str]:
    environment = dict(base if base is not None else os.environ)
    for name in ("ASAN_OPTIONS", "LSAN_OPTIONS", "TSAN_OPTIONS", "UBSAN_OPTIONS"):
        environment.pop(name, None)
    environment["CC"] = find_tool("clang-18", "TES3MP_CLANG")
    environment["CXX"] = find_tool("clang++-18", "TES3MP_CLANGXX")
    if profile == "asan-ubsan":
        environment.update(ASAN_UBSAN_ENVIRONMENT)
    else:
        environment.update(TSAN_ENVIRONMENT)
    return environment


def configure_command(cmake: str, profile: str) -> list[str]:
    return [cmake, "--preset", PROFILES[profile]["preset"], "--fresh"]


def build_command(cmake: str, profile: str) -> list[str]:
    return [cmake, "--build", "--preset", PROFILES[profile]["preset"], "--parallel", "4"]


def executable_path(profile: str, name: str) -> pathlib.Path:
    return pathlib.Path(PROFILES[profile]["build_dir"]) / name


def verify_corpus() -> list[dict[str, Any]]:
    files = sorted(path for path in CORPUS_DIR.iterdir() if path.is_file())
    if not files or len(files) > MAX_CORPUS_FILES:
        raise RuntimeSafetyError(
            f"Spatial fuzz corpus must contain 1-{MAX_CORPUS_FILES} files, found {len(files)}"
        )
    records = []
    for path in files:
        size = path.stat().st_size
        if size > MAX_CORPUS_FILE_BYTES:
            raise RuntimeSafetyError(f"Fuzz seed exceeds {MAX_CORPUS_FILE_BYTES} bytes: {path}")
        records.append(
            {
                "path": str(path.relative_to(ROOT)).replace(os.sep, "/"),
                "bytes": size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
        )
    sizes = {record["bytes"] for record in records}
    if MAX_SPATIAL_SNAPSHOT_BYTES - 8 not in sizes or not any(
        size > MAX_SPATIAL_SNAPSHOT_BYTES for size in sizes
    ):
        raise RuntimeSafetyError("Spatial fuzz corpus must include exact interior-size and oversized seeds")
    return records


def verify_protocol_frame_corpus() -> list[dict[str, Any]]:
    files = sorted(path for path in PROTOCOL_FRAME_CORPUS_DIR.iterdir() if path.is_file())
    if not files or len(files) > MAX_CORPUS_FILES:
        raise RuntimeSafetyError(
            f"Protocol-frame fuzz corpus must contain 1-{MAX_CORPUS_FILES} files, found {len(files)}"
        )
    records = []
    for path in files:
        size = path.stat().st_size
        if size > MAX_CORPUS_FILE_BYTES:
            raise RuntimeSafetyError(
                f"Protocol-frame fuzz seed exceeds {MAX_CORPUS_FILE_BYTES} bytes: {path}"
            )
        records.append(
            {
                "path": str(path.relative_to(ROOT)).replace(os.sep, "/"),
                "bytes": size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            }
        )
    return records


def fuzz_command(
    profile: str,
    fuzz_seconds: int,
    artifact_dir: pathlib.Path,
    *,
    target: str = "tes3mp_spatial_round_trip_fuzz",
    corpus_dir: pathlib.Path = CORPUS_DIR,
    maximum_input_bytes: int = MAX_CORPUS_FILE_BYTES,
) -> list[str]:
    executable = executable_path(profile, target)
    return [
        str(executable),
        f"-max_total_time={fuzz_seconds}",
        "-timeout=5",
        "-rss_limit_mb=512",
        f"-max_len={maximum_input_bytes}",
        "-print_final_stats=1",
        f"-artifact_prefix={artifact_dir}{os.sep}",
        str(corpus_dir),
    ]


def run_command(
    command: list[str], *, environment: dict[str, str], cwd: pathlib.Path, log_path: pathlib.Path
) -> str:
    rendered = subprocess.list2cmdline(command)
    print(f"+ {rendered}", flush=True)
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        check=False,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    output = result.stdout or ""
    print(output, end="", flush=True)
    with log_path.open("a", encoding="utf-8") as stream:
        stream.write(f"+ {rendered}\n")
        stream.write(output)
        if output and not output.endswith("\n"):
            stream.write("\n")
    if result.returncode != 0:
        raise RuntimeSafetyError(f"Command failed with exit code {result.returncode}: {rendered}")
    return output


def read_compiler(build_dir: pathlib.Path) -> dict[str, str]:
    candidates = sorted((build_dir / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    for path in candidates:
        text = path.read_text(encoding="utf-8", errors="replace")
        compiler_id = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"]+)"\)', text)
        compiler_version = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)', text)
        compiler_path = re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)', text)
        if compiler_id and compiler_version and compiler_path:
            if compiler_id.group(1) != "Clang" or not compiler_version.group(1).startswith("18."):
                raise RuntimeSafetyError("Configured safety build did not resolve Clang 18")
            return {
                "path": compiler_path.group(1),
                "id": compiler_id.group(1),
                "version": compiler_version.group(1),
            }
    raise RuntimeSafetyError("CMake did not record a C++ compiler identity")


def verify_instrumented_compile_commands(profile: str, build_dir: pathlib.Path) -> list[str]:
    path = build_dir / "compile_commands.json"
    if not path.is_file():
        raise RuntimeSafetyError("Safety build did not export compile_commands.json")
    entries = json.loads(path.read_text(encoding="utf-8"))
    expected = set(EXPECTED_COMPILED_SOURCES)
    if profile == "asan-ubsan":
        expected.add("tests/fuzz/spatial_round_trip_fuzz.cpp")
        expected.add("tests/fuzz/protocol_frame_fuzz.cpp")
        required_flag = "-fsanitize=address,undefined"
    else:
        required_flag = "-fsanitize=thread"

    observed: dict[str, str] = {}
    source_root = SOURCE_DIR.resolve()
    for entry in entries:
        source = pathlib.Path(entry["file"])
        if not source.is_absolute():
            source = pathlib.Path(entry["directory"]) / source
        try:
            relative = source.resolve().relative_to(source_root).as_posix()
        except ValueError:
            continue
        command = entry.get("command") or " ".join(entry.get("arguments", []))
        observed[relative] = command

    missing = sorted(expected - observed.keys())
    if missing:
        raise RuntimeSafetyError(f"Safety build omitted expected owned sources: {missing}")
    uninstrumented = sorted(source for source in expected if required_flag not in observed[source])
    if uninstrumented:
        raise RuntimeSafetyError(f"Safety profile omitted instrumentation for owned sources: {uninstrumented}")
    return sorted(expected)


def write_json(path: pathlib.Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def execute(profile: str, fuzz_seconds: int) -> pathlib.Path:
    validate_request(profile, fuzz_seconds)
    cmake = find_tool("cmake", "TES3MP_CMAKE")
    ninja = find_tool("ninja", "TES3MP_NINJA")
    environment = profile_environment(profile)
    build_dir = pathlib.Path(PROFILES[profile]["build_dir"])
    evidence_dir = build_dir / "evidence"
    artifact_dir = build_dir / "fuzzer-artifacts"
    if evidence_dir.exists():
        shutil.rmtree(evidence_dir)
    if artifact_dir.exists():
        shutil.rmtree(artifact_dir)
    evidence_dir.mkdir(parents=True, exist_ok=True)
    artifact_dir.mkdir(parents=True, exist_ok=True)
    log_path = evidence_dir / "runtime-safety.log"
    log_path.write_text("", encoding="utf-8")

    corpus = verify_corpus()
    protocol_frame_corpus = verify_protocol_frame_corpus()
    try:
        run_command(configure_command(cmake, profile), environment=environment, cwd=SOURCE_DIR, log_path=log_path)
        run_command(build_command(cmake, profile), environment=environment, cwd=SOURCE_DIR, log_path=log_path)
        instrumented_sources = verify_instrumented_compile_commands(profile, build_dir)
        for name in CONTRACT_EXECUTABLES:
            if not executable_path(profile, name).is_file():
                raise RuntimeSafetyError(f"Expected contract executable is missing: {name}")
        if fuzz_seconds:
            run_command(
                fuzz_command(profile, fuzz_seconds, artifact_dir),
                environment=environment,
                cwd=SOURCE_DIR,
                log_path=log_path,
            )
            run_command(
                fuzz_command(
                    profile,
                    fuzz_seconds,
                    artifact_dir,
                    target="tes3mp_protocol_frame_fuzz",
                    corpus_dir=PROTOCOL_FRAME_CORPUS_DIR,
                    maximum_input_bytes=MAX_PROTOCOL_FRAME_BYTES + 1,
                ),
                environment=environment,
                cwd=SOURCE_DIR,
                log_path=log_path,
            )
    except RuntimeSafetyError as error:
        write_json(
            evidence_dir / "runtime-safety-failure.json",
            {
                "schema_version": 1,
                "source_commit": subprocess.check_output(
                    ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, encoding="utf-8"
                ).strip(),
                "host": {
                    "system": platform.system(),
                    "release": platform.release(),
                    "architecture": platform.machine(),
                },
                "profile": profile,
                "error": str(error),
                "log": str(log_path.relative_to(ROOT)).replace(os.sep, "/"),
            },
        )
        raise

    evidence_path = evidence_dir / "runtime-safety.json"
    write_json(
        evidence_path,
        {
            "schema_version": 1,
            "recorded_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
            "source_commit": subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True, encoding="utf-8"
            ).strip(),
            "host": {
                "system": platform.system(),
                "release": platform.release(),
                "architecture": platform.machine(),
            },
            "profile": profile,
            "sanitizers": PROFILES[profile]["sanitizers"],
            "toolchain": {
                "cmake": run_command(
                    [cmake, "--version"], environment=environment, cwd=SOURCE_DIR, log_path=log_path
                ).splitlines()[0],
                "ninja": run_command(
                    [ninja, "--version"], environment=environment, cwd=SOURCE_DIR, log_path=log_path
                ).strip(),
                "cxx": read_compiler(build_dir),
            },
            "contracts": list(CONTRACT_EXECUTABLES),
            "instrumented_sources": instrumented_sources,
            "fuzz": {
                "targets": (
                    ["tes3mp_spatial_round_trip_fuzz", "tes3mp_protocol_frame_fuzz"]
                    if profile == "asan-ubsan"
                    else []
                ),
                "seconds": fuzz_seconds,
                "corpora": {
                    "spatial_round_trip": corpus,
                    "protocol_frame": protocol_frame_corpus,
                },
                "maximum_input_bytes": {
                    "spatial_round_trip": MAX_CORPUS_FILE_BYTES,
                    "protocol_frame": MAX_PROTOCOL_FRAME_BYTES + 1,
                },
                "parser_max_bytes": {
                    "spatial_round_trip": MAX_SPATIAL_SNAPSHOT_BYTES,
                    "protocol_frame": MAX_PROTOCOL_FRAME_BYTES,
                },
            },
            "artifacts": {
                "log": str(log_path.relative_to(ROOT)).replace(os.sep, "/"),
                "fuzzer_reproducers": str(artifact_dir.relative_to(ROOT)).replace(os.sep, "/"),
            },
        },
    )
    print(f"TES3MP runtime-safety profile passed; evidence: {evidence_path}")
    return evidence_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", required=True, choices=sorted(PROFILES))
    parser.add_argument("--fuzz-seconds", type=int, default=0)
    arguments = parser.parse_args()
    try:
        execute(arguments.profile, arguments.fuzz_seconds)
    except RuntimeSafetyError as error:
        print(f"TES3MP runtime-safety profile failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
