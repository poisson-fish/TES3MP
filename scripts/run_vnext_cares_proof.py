#!/usr/bin/env python3
"""Build and run the owner-authorized exact-pin c-ares selection proof."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
LOCK_PATH = ROOT / "scripts" / "vnext_cares_proof.json"
PROOF_DIR = ROOT / "docs" / "vnext" / "proofs" / "cares"
BUILD_DIR = ROOT / "build" / "vnext-cares-proof"
DOWNLOAD_DIR = BUILD_DIR / "downloads"
SOURCE_DIR = BUILD_DIR / "source"
PROOF_BUILD_DIR = BUILD_DIR / "proof-build"
EVIDENCE_DIR = BUILD_DIR / "evidence"
EVIDENCE_PATH = EVIDENCE_DIR / "vnext-cares-proof.json"

EXPECTED_VERSION = "1.34.8"
EXPECTED_COMMIT = "63a4c4c71b86e448bcc1c55287c35aa4aa0f4246"
EXPECTED_BUILD_PROFILE = [
    "static-only",
    "shared-off",
    "tools-off",
    "tests-off",
    "install-off",
    "caller-pumped-ares-process-fds",
    "event-thread-unused",
    "query-cache-off",
    "windows-msvc-static-runtime",
]
EXPECTED_BUDGETS = {
    "hostname_bytes": 253,
    "label_bytes": 63,
    "resolved_addresses": 8,
    "live_connection_candidates": 2,
    "resolution_preference_delay_ms": 50,
    "connection_attempt_stagger_ms": 250,
    "proof_query_timeout_ms": 80,
    "proof_query_tries": 1,
}
EXPECTED_TESTS = [
    "bounded_host_and_separate_port_contract",
    "numeric_address_bypasses_dns",
    "dual_stack_success_and_port_propagation",
    "ipv4_only_success",
    "ipv6_only_success",
    "nxdomain_and_no_data_are_distinct_failures",
    "timeout_is_bounded",
    "malformed_response_fails_closed",
    "cancellation_completes_owned_callback",
    "destruction_completes_owned_callback",
    "duplicate_addresses_are_deduplicated",
    "more_than_eight_answers_are_bounded",
    "caller_pumped_process_fds_profile",
]
EXPECTED_VULNERABILITY_SOURCES = [
    "https://github.com/c-ares/c-ares/security/advisories",
    "https://c-ares.org/changelog.html",
]
SANITIZER_COMPILE_FLAGS = "-g -fsanitize=address,undefined -fno-omit-frame-pointer"
SANITIZER_LINK_FLAGS = "-fsanitize=address,undefined"
SANITIZER_ENVIRONMENT = {
    "ASAN_OPTIONS": "halt_on_error=1:detect_leaks=1:detect_container_overflow=1",
    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
}


class ProofError(RuntimeError):
    """Raised when the dependency lock, build, or retained proof fails closed."""


def require_exact_keys(value: dict[str, Any], expected: set[str], context: str) -> None:
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        details = []
        if missing:
            details.append(f"missing {', '.join(missing)}")
        if extra:
            details.append(f"unknown {', '.join(extra)}")
        raise ProofError(f"{context} has invalid fields: {'; '.join(details)}")


def require_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ProofError(f"{context} must be a non-empty string")
    return value


def require_sha256(value: Any, context: str) -> str:
    digest = require_string(value, context)
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ProofError(f"{context} must be a lowercase SHA-256 digest")
    return digest


def require_string_list(value: Any, context: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise ProofError(f"{context} must be a non-empty list")
    result = [require_string(item, f"{context}[{index}]") for index, item in enumerate(value)]
    if len(result) != len(set(result)):
        raise ProofError(f"{context} contains duplicate values")
    return result


def load_lock(path: Path = LOCK_PATH) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ProofError(f"cannot read dependency lock {path}: {error}") from error
    if not isinstance(value, dict):
        raise ProofError("dependency lock root must be an object")
    require_exact_keys(
        value,
        {
            "schema_version",
            "dependency",
            "build_profile",
            "budgets",
            "supported_proof_platforms",
            "excluded_surfaces",
            "vulnerability_sources",
            "update_policy",
        },
        "dependency lock",
    )
    if value["schema_version"] != 1:
        raise ProofError("dependency lock schema_version must be 1")

    dependency = value["dependency"]
    if not isinstance(dependency, dict):
        raise ProofError("dependency must be an object")
    require_exact_keys(
        dependency,
        {"name", "version", "tag", "commit", "source_directory", "source_archive", "license"},
        "dependency",
    )
    if dependency["name"] != "c-ares" or dependency["version"] != EXPECTED_VERSION:
        raise ProofError("dependency does not retain the authorized c-ares identity")
    if dependency["tag"] != f"v{EXPECTED_VERSION}" or dependency["commit"] != EXPECTED_COMMIT:
        raise ProofError("dependency tag or commit differs from the authorized selection proof")
    if not re.fullmatch(r"[0-9a-f]{40}", dependency["commit"]):
        raise ProofError("dependency.commit must be a lowercase Git object ID")
    require_string(dependency["source_directory"], "dependency.source_directory")

    archive = dependency["source_archive"]
    if not isinstance(archive, dict):
        raise ProofError("dependency.source_archive must be an object")
    require_exact_keys(archive, {"url", "sha256"}, "dependency.source_archive")
    if not require_string(archive["url"], "dependency.source_archive.url").startswith("https://"):
        raise ProofError("dependency.source_archive.url must use HTTPS")
    require_sha256(archive["sha256"], "dependency.source_archive.sha256")

    license_value = dependency["license"]
    if not isinstance(license_value, dict):
        raise ProofError("dependency.license must be an object")
    require_exact_keys(license_value, {"spdx", "path", "sha256"}, "dependency.license")
    if license_value["spdx"] != "MIT":
        raise ProofError("dependency.license.spdx must remain MIT")
    require_string(license_value["path"], "dependency.license.path")
    require_sha256(license_value["sha256"], "dependency.license.sha256")

    if value["build_profile"] != EXPECTED_BUILD_PROFILE:
        raise ProofError("build_profile differs from the authorized restricted dependency surface")
    if value["budgets"] != EXPECTED_BUDGETS:
        raise ProofError("budgets differ from the proof source contract")
    require_string_list(value["supported_proof_platforms"], "supported_proof_platforms")
    require_string_list(value["excluded_surfaces"], "excluded_surfaces")
    if value["vulnerability_sources"] != EXPECTED_VULNERABILITY_SOURCES:
        raise ProofError("vulnerability_sources differ from the authorized review set")
    require_string(value["update_policy"], "update_policy")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_tool(name: str, environment_name: str, windows_candidates: Iterable[Path] = ()) -> str:
    override = os.environ.get(environment_name)
    if override:
        path = Path(override)
        if not path.is_file():
            raise ProofError(f"{environment_name} does not name a file: {path}")
        return str(path)
    found = shutil.which(name)
    if found:
        return found
    if os.name == "nt":
        for candidate in windows_candidates:
            if candidate.is_file():
                return str(candidate)
    raise ProofError(f"required tool {name!r} is not on PATH (or set {environment_name})")


def run(
    arguments: Iterable[str | Path],
    *,
    cwd: Path | None = None,
    environment: dict[str, str] | None = None,
    capture: bool = False,
) -> str:
    command = [str(argument) for argument in arguments]
    print("+", subprocess.list2cmdline(command), flush=True)
    result = subprocess.run(
        command,
        cwd=cwd,
        env=environment,
        check=False,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )
    if result.returncode != 0:
        detail = result.stdout.strip() if capture and result.stdout else f"exit code {result.returncode}"
        raise ProofError(f"command failed: {detail}")
    return result.stdout.strip() if capture and result.stdout else ""


def download_verified(url: str, expected_sha256: str, destination: Path) -> None:
    if destination.is_file() and sha256_file(destination) == expected_sha256:
        print(f"Using verified cached archive: {destination}")
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".partial")
    partial.unlink(missing_ok=True)
    request = urllib.request.Request(url, headers={"User-Agent": "TES3MP-vNext-c-ares-proof"})
    try:
        with urllib.request.urlopen(request, timeout=180) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
    except OSError as error:
        partial.unlink(missing_ok=True)
        raise ProofError(f"cannot download {url}: {error}") from error
    actual = sha256_file(partial)
    if actual != expected_sha256:
        partial.unlink(missing_ok=True)
        raise ProofError(f"archive SHA-256 mismatch: expected {expected_sha256}, got {actual}")
    os.replace(partial, destination)


def extract_regular_files(archive_path: Path, destination: Path, expected_root: str) -> Path:
    if not expected_root or PurePosixPath(expected_root).name != expected_root:
        raise ProofError(f"invalid expected archive root {expected_root!r}")
    source_root = destination / expected_root
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    try:
        with tarfile.open(archive_path, "r:gz") as archive:
            for member in archive.getmembers():
                parsed = PurePosixPath(member.name)
                if parsed.is_absolute() or ".." in parsed.parts or not parsed.parts:
                    raise ProofError(f"unsafe source archive member: {member.name!r}")
                if parsed.parts[0] != expected_root:
                    raise ProofError(f"source archive member is outside {expected_root!r}: {member.name!r}")
                if member.issym() or member.islnk():
                    link = PurePosixPath(member.linkname)
                    if link.is_absolute():
                        raise ProofError(f"source archive link is absolute: {member.name!r}")
                    combined = parsed.parent.joinpath(link) if member.issym() else link
                    normalized: list[str] = []
                    for part in combined.parts:
                        if part in ("", "."):
                            continue
                        if part == "..":
                            if not normalized:
                                raise ProofError(f"source archive link escapes its root: {member.name!r}")
                            normalized.pop()
                        else:
                            normalized.append(part)
                    if not normalized or normalized[0] != expected_root:
                        raise ProofError(f"source archive link escapes {expected_root!r}: {member.name!r}")
                    continue
                if not member.isfile():
                    continue
                target = destination.joinpath(*parsed.parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                source = archive.extractfile(member)
                if source is None:
                    raise ProofError(f"cannot read source archive member: {member.name!r}")
                with source, target.open("wb") as output:
                    shutil.copyfileobj(source, output)
    except (OSError, tarfile.TarError) as error:
        raise ProofError(f"cannot extract source archive {archive_path}: {error}") from error
    if not (source_root / "CMakeLists.txt").is_file():
        raise ProofError(f"source archive omitted CMakeLists.txt under {expected_root}")
    return source_root


def parse_toolchain(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    required = {
        "C_COMPILER",
        "C_COMPILER_ID",
        "C_COMPILER_VERSION",
        "CXX_COMPILER",
        "CXX_COMPILER_ID",
        "CXX_COMPILER_VERSION",
        "MSVC_RUNTIME",
    }
    if set(result) != required or any(not result[key] for key in required - {"MSVC_RUNTIME"}):
        raise ProofError("configured compiler evidence is missing or incomplete")
    return result


def configure_tools() -> tuple[str, str, dict[str, str]]:
    cmake_candidates = [
        Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
        / "Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    ]
    ninja_candidates = [candidate.parents[2] / "Ninja/ninja.exe" for candidate in cmake_candidates]
    cmake = find_tool("cmake", "VNEXT_CMAKE", cmake_candidates)
    ninja = find_tool("ninja", "VNEXT_NINJA", ninja_candidates)
    environment = os.environ.copy()
    if os.name == "nt":
        environment.update({"LANG": "C", "LC_ALL": "C", "LC_CTYPE": "C"})
    return cmake, ninja, environment


def build_and_test(
    cmake: str, ninja: str, environment: dict[str, str], source: Path, sanitize: bool
) -> str:
    if PROOF_BUILD_DIR.exists():
        shutil.rmtree(PROOF_BUILD_DIR)
    arguments = [
        cmake,
        "-S",
        PROOF_DIR,
        "-B",
        PROOF_BUILD_DIR,
        "-G",
        "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
        f"-DVNEXT_CARES_SOURCE_DIR={source.resolve()}",
    ]
    if os.name == "nt":
        arguments.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")
    if sanitize:
        if platform.system() != "Linux":
            raise ProofError("--sanitize is supported only by the Linux Clang proof job")
        arguments.extend(
            [
                f"-DCMAKE_C_FLAGS={SANITIZER_COMPILE_FLAGS}",
                f"-DCMAKE_CXX_FLAGS={SANITIZER_COMPILE_FLAGS}",
                f"-DCMAKE_EXE_LINKER_FLAGS={SANITIZER_LINK_FLAGS}",
            ]
        )
    run(arguments, environment=environment)
    run(
        [cmake, "--build", PROOF_BUILD_DIR, "--target", "vnext_cares_proof", "--parallel", "4"],
        environment=environment,
    )
    ctest_name = "ctest.exe" if os.name == "nt" else "ctest"
    adjacent_ctest = Path(cmake).with_name(ctest_name)
    ctest = str(adjacent_ctest) if adjacent_ctest.is_file() else find_tool("ctest", "VNEXT_CTEST")
    output = run(
        [ctest, "--test-dir", PROOF_BUILD_DIR, "--output-on-failure", "-V"],
        environment=environment,
        capture=True,
    )
    for scenario in EXPECTED_TESTS:
        if f"PASS {scenario}" not in output:
            raise ProofError(f"CTest output did not observe required scenario {scenario}")
    return output


def write_evidence(
    lock: dict[str, Any],
    archive: Path,
    license_sha256: str,
    cmake: str,
    ninja: str,
    test_output: str,
    sanitize: bool,
) -> None:
    toolchain = parse_toolchain(PROOF_BUILD_DIR / "vnext-toolchain.txt")
    source = (PROOF_DIR / "proof.cpp").read_text(encoding="utf-8")
    for scenario in EXPECTED_TESTS:
        if scenario not in source or f"PASS {scenario}" not in test_output:
            raise ProofError(f"retained proof evidence is missing scenario {scenario}")
    evidence = {
        "schema_version": 1,
        "generated_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "lock_sha256": sha256_file(LOCK_PATH),
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
            "python": platform.python_version(),
        },
        "toolchain": {
            "cmake": run([cmake, "--version"], capture=True).splitlines()[0],
            "ninja": run([ninja, "--version"], capture=True),
            "c_compiler": toolchain["C_COMPILER"],
            "c_compiler_id": toolchain["C_COMPILER_ID"],
            "c_compiler_version": toolchain["C_COMPILER_VERSION"],
            "cxx_compiler": toolchain["CXX_COMPILER"],
            "cxx_compiler_id": toolchain["CXX_COMPILER_ID"],
            "cxx_compiler_version": toolchain["CXX_COMPILER_VERSION"],
            "msvc_runtime": toolchain["MSVC_RUNTIME"],
            "sanitizers": "ASan+UBSan" if sanitize else "none",
        },
        "dependency": {
            "name": "c-ares",
            "version": lock["dependency"]["version"],
            "commit": lock["dependency"]["commit"],
            "archive_sha256": sha256_file(archive),
            "license_sha256": license_sha256,
        },
        "build_profile": lock["build_profile"],
        "budgets": lock["budgets"],
        "tests": EXPECTED_TESTS,
        "ctest": test_output,
        "production_dependency_claim": "none; owner acceptance remains a separate gate",
    }
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    EVIDENCE_PATH.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
    shutil.copy2(LOCK_PATH, EVIDENCE_DIR / LOCK_PATH.name)
    license_path = SOURCE_DIR / lock["dependency"]["source_directory"] / lock["dependency"]["license"]["path"]
    shutil.copy2(license_path, EVIDENCE_DIR / "c-ares-LICENSE.md")


def execute(sanitize: bool) -> None:
    lock = load_lock()
    dependency = lock["dependency"]
    archive = DOWNLOAD_DIR / f"c-ares-{dependency['version']}.tar.gz"
    download_verified(dependency["source_archive"]["url"], dependency["source_archive"]["sha256"], archive)
    source = extract_regular_files(archive, SOURCE_DIR, dependency["source_directory"])
    license_path = source / dependency["license"]["path"]
    if not license_path.is_file() or sha256_file(license_path) != dependency["license"]["sha256"]:
        raise ProofError("c-ares license differs from the dependency lock")
    cmake, ninja, environment = configure_tools()
    if sanitize:
        environment.update(SANITIZER_ENVIRONMENT)
    test_output = build_and_test(cmake, ninja, environment, source, sanitize)
    print(test_output)
    write_evidence(lock, archive, sha256_file(license_path), cmake, ninja, test_output, sanitize)
    print(f"Retained evidence: {EVIDENCE_PATH}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="enable the Linux Clang ASan/UBSan proof")
    parser.add_argument("--validate-only", action="store_true", help="validate the lock without downloads or builds")
    arguments = parser.parse_args()
    try:
        load_lock()
        if arguments.validate_only:
            print("c-ares proof lock validation passed")
        else:
            execute(arguments.sanitize)
    except ProofError as error:
        print(f"c-ares proof failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
