#!/usr/bin/env python3
"""Build and run the owner-approved GameNetworkingSockets selection proof."""

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
LOCK_PATH = ROOT / "scripts" / "vnext_gamenetworkingsockets_proof.json"
PROOF_DIR = ROOT / "docs" / "vnext" / "proofs" / "gamenetworkingsockets"
BUILD_DIR = ROOT / "build" / "vnext-gamenetworkingsockets-proof"
DOWNLOAD_DIR = BUILD_DIR / "downloads"
SOURCE_DIR = BUILD_DIR / "source"
OPENSSL_INSTALL_DIR = BUILD_DIR / "openssl-install"
PROTOBUF_BUILD_DIR = BUILD_DIR / "protobuf-build"
PROTOBUF_INSTALL_DIR = BUILD_DIR / "protobuf-install"
PROOF_BUILD_DIR = BUILD_DIR / "proof-build"
EVIDENCE_DIR = BUILD_DIR / "evidence"
EVIDENCE_PATH = EVIDENCE_DIR / "vnext-gamenetworkingsockets-proof.json"

EXPECTED_DEPENDENCIES = {
    "gamenetworkingsockets": ("1.6.0", "2cb93a06350bb065db53abdb0d87cf297e0bfd34"),
    "openssl": ("3.5.8", "f4dc4d58b48d346a8270183f89acf826d459b0ca"),
    "protobuf": ("6.33.4", "edaa823d8b36a8656d7b2b9241b7d0bfe50af878"),
    "abseil": ("20250512.1", "76bb24329e8bf5f39704eb10d21b9a80befa7c81"),
}
EXPECTED_BUILD_PROFILE = {
    "openssl": ["no-shared", "no-tests", "no-apps", "no-module", "no-legacy", "windows-no-asm"],
    "protobuf": [
        "static",
        "tests-off",
        "zlib-off",
        "pinned-local-abseil",
        "bundled-utf8-range",
        "windows-msvc-static-runtime",
    ],
    "gamenetworkingsockets": [
        "static-only",
        "openssl",
        "ice-off",
        "webrtc-off",
        "examples-off",
        "tests-off",
        "tools-off",
        "windows-msvc-static-runtime",
    ],
    "sanitizer": [
        "linux-clang-18-only",
        "proof-asan-ubsan",
        "gamenetworkingsockets-asan-ubsan-function-excluded",
        "protobuf-abseil-asan-ubsan",
        "openssl-unsanitized",
        "halt-on-error",
        "container-overflow-enabled",
        "no-runtime-suppressions",
    ],
}
SANITIZER_COMPILE_FLAGS = (
    "-g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize=alignment"
)
SANITIZER_LINK_FLAGS = "-fsanitize=address,undefined"
SANITIZER_ENVIRONMENT = {
    "ASAN_OPTIONS": "halt_on_error=1:detect_leaks=1:detect_container_overflow=1",
    "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1",
}
EXPECTED_BUDGETS = {
    "credential_bytes": 64,
    "resume_token_bytes": 64,
    "application_message_bytes": 65536,
    "reliable_queue_bytes": 262144,
    "captured_wire_bytes": 2097152,
    "callbacks_per_drain": 128,
    "authentication_failures_before_backoff": 3,
    "receive_buffer_bytes": 4096,
    "receive_buffer_messages": 4,
    "receive_max_message_bytes": 2048,
    "receive_max_segments_per_packet": 2,
    "concurrent_handshakes": 8,
    "flood_connections": 32,
}
EXPECTED_TESTS = [
    "authentication_ordering_encryption_capture_and_redaction",
    "resume_single_use_rotation_contention_and_generation",
    "bounded_latest_reliable_authentication_and_flood_queues",
    "reliable_and_unreliable_delivery_classes_under_faults",
    "actual_slow_reader_and_full_receive_buffer",
    "excessive_segments_and_maximum_message_fail_closed",
    "handshake_and_disconnect_flood_admission_bounds",
    "close_discards_unread_data_and_invalidates_handle",
    "stable_owned_telemetry_categories",
]
EXPECTED_VULNERABILITY_SOURCES = [
    "https://github.com/ValveSoftware/GameNetworkingSockets/security/advisories",
    "https://openssl-library.org/news/vulnerabilities/",
    "https://github.com/openssl/openssl/security/advisories",
    "https://github.com/protocolbuffers/protobuf/security/advisories",
    "https://github.com/abseil/abseil-cpp/security/advisories",
]
EXPECTED_GENERATED_POLICY = (
    "Generate GameNetworkingSockets protobuf C++ only at proof build time with the locked protoc; "
    "do not commit, reuse, or accept generated output from another version."
)


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
            "dependencies",
            "bundled_transitive_dependencies",
            "build_profile",
            "budgets",
            "supported_proof_platforms",
            "excluded_surfaces",
            "vulnerability_sources",
            "generated_policy",
            "update_policy",
        },
        "dependency lock",
    )
    if value["schema_version"] != 1:
        raise ProofError("dependency lock schema_version must be 1")

    dependencies = value["dependencies"]
    if not isinstance(dependencies, dict):
        raise ProofError("dependencies must be an object")
    require_exact_keys(dependencies, set(EXPECTED_DEPENDENCIES), "dependencies")
    for name, (expected_version, expected_commit) in EXPECTED_DEPENDENCIES.items():
        dependency = dependencies[name]
        if not isinstance(dependency, dict):
            raise ProofError(f"dependencies.{name} must be an object")
        common = {"version", "commit", "source_directory", "source_archive", "license"}
        discriminator = "release" if name == "protobuf" else "tag"
        require_exact_keys(dependency, common | {discriminator}, f"dependencies.{name}")
        if dependency["version"] != expected_version or dependency["commit"] != expected_commit:
            raise ProofError(f"dependencies.{name} does not retain the owner-approved identity")
        if not re.fullmatch(r"[0-9a-f]{40}", dependency["commit"]):
            raise ProofError(f"dependencies.{name}.commit must be a lowercase Git object ID")
        require_string(dependency[discriminator], f"dependencies.{name}.{discriminator}")
        require_string(dependency["source_directory"], f"dependencies.{name}.source_directory")

        archive = dependency["source_archive"]
        if not isinstance(archive, dict):
            raise ProofError(f"dependencies.{name}.source_archive must be an object")
        require_exact_keys(archive, {"url", "sha256"}, f"dependencies.{name}.source_archive")
        if not require_string(archive["url"], f"dependencies.{name}.source_archive.url").startswith("https://"):
            raise ProofError(f"dependencies.{name}.source_archive.url must use HTTPS")
        require_sha256(archive["sha256"], f"dependencies.{name}.source_archive.sha256")

        license_value = dependency["license"]
        if not isinstance(license_value, dict):
            raise ProofError(f"dependencies.{name}.license must be an object")
        require_exact_keys(license_value, {"spdx", "path", "sha256"}, f"dependencies.{name}.license")
        require_string(license_value["spdx"], f"dependencies.{name}.license.spdx")
        require_string(license_value["path"], f"dependencies.{name}.license.path")
        require_sha256(license_value["sha256"], f"dependencies.{name}.license.sha256")

    transitive = value["bundled_transitive_dependencies"]
    if not isinstance(transitive, dict):
        raise ProofError("bundled_transitive_dependencies must be an object")
    require_exact_keys(transitive, {"utf8_range"}, "bundled_transitive_dependencies")
    utf8 = transitive["utf8_range"]
    if not isinstance(utf8, dict):
        raise ProofError("bundled_transitive_dependencies.utf8_range must be an object")
    require_exact_keys(utf8, {"owner", "path", "license_spdx", "license_sha256"}, "utf8_range")
    if utf8["owner"] != "protobuf" or utf8["path"] != "third_party/utf8_range":
        raise ProofError("utf8_range must remain the Protobuf-bundled source")
    require_string(utf8["license_spdx"], "utf8_range.license_spdx")
    require_sha256(utf8["license_sha256"], "utf8_range.license_sha256")

    if value["build_profile"] != EXPECTED_BUILD_PROFILE:
        raise ProofError("build_profile differs from the approved restricted dependency surface")
    if value["budgets"] != EXPECTED_BUDGETS:
        raise ProofError("budgets differ from the proof source contract")
    require_string_list(value["supported_proof_platforms"], "supported_proof_platforms")
    require_string_list(value["excluded_surfaces"], "excluded_surfaces")
    if value["vulnerability_sources"] != EXPECTED_VULNERABILITY_SOURCES:
        raise ProofError("vulnerability_sources differ from the approved review set")
    if value["generated_policy"] != EXPECTED_GENERATED_POLICY:
        raise ProofError("generated_policy differs from the proof generation contract")
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
    request = urllib.request.Request(url, headers={"User-Agent": "TES3MP-vNext-GNS-proof"})
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
                    # Build-irrelevant repository links are intentionally not materialized on
                    # Windows. Their targets remain verified members of the same exact archive.
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
    if not (source_root / "CMakeLists.txt").is_file() and not (source_root / "Configure").is_file():
        raise ProofError(f"source archive did not contain an expected build entry point under {expected_root}")
    return source_root


def verify_licenses(lock: dict[str, Any], sources: dict[str, Path]) -> dict[str, str]:
    result: dict[str, str] = {}
    for name, source in sources.items():
        license_value = lock["dependencies"][name]["license"]
        license_path = source / license_value["path"]
        if not license_path.is_file():
            raise ProofError(f"{name} license file is missing: {license_path}")
        actual = sha256_file(license_path)
        if actual != license_value["sha256"]:
            raise ProofError(f"{name} license differs from the dependency lock")
        result[name] = actual
    utf8_license = sources["protobuf"] / "third_party" / "utf8_range" / "LICENSE"
    actual_utf8 = sha256_file(utf8_license)
    if actual_utf8 != lock["bundled_transitive_dependencies"]["utf8_range"]["license_sha256"]:
        raise ProofError("bundled utf8_range license differs from the dependency lock")
    result["utf8_range"] = actual_utf8
    return result


def configure_build_environment() -> tuple[str, str, str, dict[str, str]]:
    cmake_candidates = [
        Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
        / "Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
    ]
    ninja_candidates = [candidate.parents[2] / "Ninja/ninja.exe" for candidate in cmake_candidates]
    perl_candidates = [Path("C:/Strawberry/perl/bin/perl.exe")]
    cmake = find_tool("cmake", "VNEXT_CMAKE", cmake_candidates)
    ninja = find_tool("ninja", "VNEXT_NINJA", ninja_candidates)
    perl = find_tool("perl", "VNEXT_PERL", perl_candidates)
    environment = os.environ.copy()
    if os.name == "nt":
        environment.update({"LANG": "C", "LC_ALL": "C", "LC_CTYPE": "C"})
        perl_os = run(
            [perl, "-MConfig", "-e", "print $Config{osname}"], environment=environment, capture=True
        )
        if perl_os != "MSWin32":
            raise ProofError("the Windows OpenSSL proof requires native Strawberry Perl (MSWin32)")
    environment["PATH"] = os.pathsep.join(
        [str(OPENSSL_INSTALL_DIR / "bin"), str(PROTOBUF_INSTALL_DIR / "bin"), environment.get("PATH", "")]
    )
    return cmake, ninja, perl, environment


def build_openssl(perl: str, environment: dict[str, str], source: Path) -> None:
    if OPENSSL_INSTALL_DIR.exists():
        shutil.rmtree(OPENSSL_INSTALL_DIR)
    prefix = OPENSSL_INSTALL_DIR.resolve().as_posix()
    options = ["no-shared", "no-tests", "no-apps", "no-module", "no-legacy"]
    if os.name == "nt":
        make = find_tool("nmake", "VNEXT_MAKE")
        configuration = "VC-WIN64A"
        options.append("no-asm")
    else:
        make = find_tool("make", "VNEXT_MAKE")
        configuration = ""
    arguments: list[str | Path] = [perl, "Configure"]
    if configuration:
        arguments.append(configuration)
    arguments.extend(options)
    arguments.extend([f"--prefix={prefix}", f"--openssldir={prefix}/ssl"])
    run(arguments, cwd=source, environment=environment)
    if os.name == "nt":
        run([make], cwd=source, environment=environment)
        run([make, "install_sw"], cwd=source, environment=environment)
    else:
        jobs = str(max(2, min(4, os.cpu_count() or 2)))
        run([make, f"-j{jobs}"], cwd=source, environment=environment)
        run([make, "install_sw"], cwd=source, environment=environment)


def build_protobuf(
    cmake: str,
    ninja: str,
    environment: dict[str, str],
    source: Path,
    abseil: Path,
    sanitize: bool,
) -> None:
    for directory in (PROTOBUF_BUILD_DIR, PROTOBUF_INSTALL_DIR):
        if directory.exists():
            shutil.rmtree(directory)
    arguments = [
        cmake,
        "-S",
        source,
        "-B",
        PROTOBUF_BUILD_DIR,
        "-G",
        "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_INSTALL_PREFIX={PROTOBUF_INSTALL_DIR.resolve()}",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DCMAKE_CXX_STANDARD=17",
        "-DABSL_ENABLE_INSTALL=ON",
        "-DABSL_PROPAGATE_CXX_STD=ON",
        f"-DFETCHCONTENT_SOURCE_DIR_ABSL={abseil.resolve()}",
        "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
        "-Dprotobuf_BUILD_TESTS=OFF",
        "-Dprotobuf_BUILD_SHARED_LIBS=OFF",
        "-Dprotobuf_BUILD_PROTOC_BINARIES=ON",
        "-Dprotobuf_BUILD_LIBPROTOC=ON",
        "-Dprotobuf_BUILD_EXAMPLES=OFF",
        "-Dprotobuf_WITH_ZLIB=OFF",
    ]
    if os.name == "nt":
        arguments.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")
    if sanitize:
        arguments.extend(
            [
                f"-DCMAKE_CXX_FLAGS={SANITIZER_COMPILE_FLAGS}",
                f"-DCMAKE_EXE_LINKER_FLAGS={SANITIZER_LINK_FLAGS}",
                f"-DCMAKE_MODULE_LINKER_FLAGS={SANITIZER_LINK_FLAGS}",
                f"-DCMAKE_SHARED_LINKER_FLAGS={SANITIZER_LINK_FLAGS}",
            ]
        )
    run(arguments, environment=environment)
    run([cmake, "--build", PROTOBUF_BUILD_DIR, "--target", "install", "--parallel", "4"], environment=environment)


def build_and_test_proof(
    cmake: str,
    ninja: str,
    environment: dict[str, str],
    gns_source: Path,
    sanitize: bool,
) -> str:
    if PROOF_BUILD_DIR.exists():
        shutil.rmtree(PROOF_BUILD_DIR)
    prefix_path = f"{OPENSSL_INSTALL_DIR.resolve()};{PROTOBUF_INSTALL_DIR.resolve()}"
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
        f"-DCMAKE_PREFIX_PATH={prefix_path}",
        f"-DOPENSSL_ROOT_DIR={OPENSSL_INSTALL_DIR.resolve()}",
        f"-DProtobuf_DIR={PROTOBUF_INSTALL_DIR.resolve() / 'lib/cmake/protobuf'}",
        f"-DVNEXT_GNS_SOURCE_DIR={gns_source.resolve()}",
    ]
    if os.name == "nt":
        arguments.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")
    if sanitize:
        if platform.system() != "Linux":
            raise ProofError("--sanitize is supported only by the Linux Clang proof job")
        arguments.extend(["-DSANITIZE_ADDRESS=ON", "-DSANITIZE_UNDEFINED=ON"])
    run(arguments, environment=environment)
    run([cmake, "--build", PROOF_BUILD_DIR, "--target", "vnext_gamenetworkingsockets_proof", "--parallel", "4"],
        environment=environment)
    ctest_name = "ctest.exe" if os.name == "nt" else "ctest"
    adjacent_ctest = Path(cmake).with_name(ctest_name)
    ctest = str(adjacent_ctest) if adjacent_ctest.is_file() else find_tool("ctest", "VNEXT_CTEST")
    output = run(
        [ctest, "--test-dir", PROOF_BUILD_DIR, "--output-on-failure", "-V"],
        environment=environment,
        capture=True,
    )
    for test in EXPECTED_TESTS:
        if f"PASS {test}" not in output:
            raise ProofError(f"CTest output did not observe required scenario {test}")
    return output


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


def write_evidence(
    lock: dict[str, Any],
    archives: dict[str, Path],
    licenses: dict[str, str],
    cmake: str,
    ninja: str,
    perl: str,
    environment: dict[str, str],
    test_output: str,
    sanitize: bool,
) -> None:
    toolchain = parse_toolchain(PROOF_BUILD_DIR / "vnext-toolchain.txt")
    for test in EXPECTED_TESTS:
        if test not in (PROOF_DIR / "proof.cpp").read_text(encoding="utf-8"):
            raise ProofError(f"proof source no longer contains required scenario {test}")
        if f"PASS {test}" not in test_output:
            raise ProofError(f"retained CTest output is missing required scenario {test}")
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
            "perl": run([perl, "-e", "printf qq{%vd}, $^V"], environment=environment, capture=True),
            "c_compiler": toolchain["C_COMPILER"],
            "c_compiler_id": toolchain["C_COMPILER_ID"],
            "c_compiler_version": toolchain["C_COMPILER_VERSION"],
            "cxx_compiler": toolchain["CXX_COMPILER"],
            "cxx_compiler_id": toolchain["CXX_COMPILER_ID"],
            "cxx_compiler_version": toolchain["CXX_COMPILER_VERSION"],
            "msvc_runtime": toolchain["MSVC_RUNTIME"],
            "sanitizers": "ASan+UBSan" if sanitize else "none",
        },
        "dependencies": {
            name: {
                "version": lock["dependencies"][name]["version"],
                "commit": lock["dependencies"][name]["commit"],
                "archive_sha256": sha256_file(path),
                "license_sha256": licenses[name],
            }
            for name, path in archives.items()
        },
        "bundled_transitive_dependencies": {
            "utf8_range": {"license_sha256": licenses["utf8_range"]}
        },
        "build_profile": lock["build_profile"],
        "budgets": lock["budgets"],
        "tests": EXPECTED_TESTS,
        "ctest": test_output,
        "endpoint_identity_claim": "none; automatic basic encryption only",
    }
    serialized = json.dumps(evidence, indent=2, sort_keys=True) + "\n"
    secret_markers = ("pw-canary-", "resume-canary-")
    if any(marker in serialized for marker in secret_markers):
        raise ProofError("secret canary appeared in retained evidence")
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    EVIDENCE_PATH.write_text(serialized, encoding="utf-8", newline="\n")
    shutil.copy2(LOCK_PATH, EVIDENCE_DIR / LOCK_PATH.name)
    for name, source in {
        dependency: SOURCE_DIR / lock["dependencies"][dependency]["source_directory"]
        for dependency in EXPECTED_DEPENDENCIES
    }.items():
        license_path = source / lock["dependencies"][name]["license"]["path"]
        shutil.copy2(license_path, EVIDENCE_DIR / f"{name}-LICENSE")
    utf8_license = SOURCE_DIR / lock["dependencies"]["protobuf"]["source_directory"] / "third_party/utf8_range/LICENSE"
    shutil.copy2(utf8_license, EVIDENCE_DIR / "utf8_range-LICENSE")


def execute(sanitize: bool) -> None:
    lock = load_lock()
    archives: dict[str, Path] = {}
    sources: dict[str, Path] = {}
    for name, dependency in lock["dependencies"].items():
        archive = DOWNLOAD_DIR / f"{name}-{dependency['version']}.tar.gz"
        download_verified(dependency["source_archive"]["url"], dependency["source_archive"]["sha256"], archive)
        archives[name] = archive
    if SOURCE_DIR.exists():
        shutil.rmtree(SOURCE_DIR)
    SOURCE_DIR.mkdir(parents=True)
    for name, dependency in lock["dependencies"].items():
        temporary = BUILD_DIR / f"extract-{name}"
        source = extract_regular_files(archives[name], temporary, dependency["source_directory"])
        destination = SOURCE_DIR / dependency["source_directory"]
        shutil.move(str(source), destination)
        shutil.rmtree(temporary)
        sources[name] = destination

    licenses = verify_licenses(lock, sources)
    cmake, ninja, perl, environment = configure_build_environment()
    if sanitize:
        environment.update(SANITIZER_ENVIRONMENT)
    build_openssl(perl, environment, sources["openssl"])
    build_protobuf(cmake, ninja, environment, sources["protobuf"], sources["abseil"], sanitize)
    test_output = build_and_test_proof(cmake, ninja, environment, sources["gamenetworkingsockets"], sanitize)
    print(test_output)
    write_evidence(lock, archives, licenses, cmake, ninja, perl, environment, test_output, sanitize)
    print(f"Retained evidence: {EVIDENCE_PATH}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true", help="enable the Linux Clang ASan/UBSan proof")
    parser.add_argument("--validate-only", action="store_true", help="validate the lock without downloads or builds")
    arguments = parser.parse_args()
    try:
        load_lock()
        if arguments.validate_only:
            print("GameNetworkingSockets proof lock validation passed")
        else:
            execute(arguments.sanitize)
    except ProofError as error:
        print(f"GameNetworkingSockets proof failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
