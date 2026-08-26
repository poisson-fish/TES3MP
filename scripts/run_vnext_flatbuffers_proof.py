#!/usr/bin/env python3
"""Build and verify the owner-approved isolated FlatBuffers selection proof."""

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
LOCK_PATH = ROOT / "scripts" / "vnext_flatbuffers_proof.json"
PROOF_DIR = ROOT / "docs" / "vnext" / "proofs" / "flatbuffers"
SCHEMA_DIR = PROOF_DIR / "schema"
GENERATED_DIR = PROOF_DIR / "generated"
BUILD_DIR = ROOT / "build" / "vnext-flatbuffers-proof"
DOWNLOAD_DIR = BUILD_DIR / "downloads"
SOURCE_PARENT = BUILD_DIR / "source"
DEPENDENCY_BUILD_DIR = BUILD_DIR / "dependency-build"
GENERATED_CHECK_DIR = BUILD_DIR / "generated"
PROOF_BUILD_DIR = BUILD_DIR / "proof-build"
EVIDENCE_DIR = BUILD_DIR / "evidence"
EVIDENCE_PATH = EVIDENCE_DIR / "vnext-flatbuffers-proof.json"
FUZZ_CORPUS_DIR = BUILD_DIR / "fuzz-corpus"


class ProofError(RuntimeError):
    """Raised when selection-proof policy or execution fails."""


def require_exact_keys(value: dict[str, Any], keys: set[str], context: str) -> None:
    actual = set(value)
    if actual != keys:
        missing = sorted(keys - actual)
        extra = sorted(actual - keys)
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


def require_string_list(value: Any, context: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise ProofError(f"{context} must be a non-empty list")
    result = [require_string(item, f"{context}[{index}]") for index, item in enumerate(value)]
    if len(result) != len(set(result)):
        raise ProofError(f"{context} contains duplicate values")
    return result


def require_sha256(value: Any, context: str) -> str:
    digest = require_string(value, context)
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise ProofError(f"{context} must be a lowercase SHA-256 digest")
    return digest


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
            "generated_files",
            "generator_arguments",
            "supported_proof_platforms",
            "excluded_surfaces",
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
        {"name", "version", "tag", "commit", "source_archive", "license", "source_directory"},
        "dependency",
    )
    for key in ("name", "version", "tag", "source_directory"):
        require_string(dependency[key], f"dependency.{key}")
    commit = require_string(dependency["commit"], "dependency.commit")
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ProofError("dependency.commit must be a lowercase 40-character Git object ID")

    archive = dependency["source_archive"]
    if not isinstance(archive, dict):
        raise ProofError("dependency.source_archive must be an object")
    require_exact_keys(archive, {"url", "sha256"}, "dependency.source_archive")
    url = require_string(archive["url"], "dependency.source_archive.url")
    if not url.startswith("https://github.com/google/flatbuffers/"):
        raise ProofError("dependency.source_archive.url must use the official HTTPS repository")
    require_sha256(archive["sha256"], "dependency.source_archive.sha256")

    license_value = dependency["license"]
    if not isinstance(license_value, dict):
        raise ProofError("dependency.license must be an object")
    require_exact_keys(license_value, {"spdx", "path", "sha256"}, "dependency.license")
    for key in ("spdx", "path"):
        require_string(license_value[key], f"dependency.license.{key}")
    require_sha256(license_value["sha256"], "dependency.license.sha256")

    generated_files = require_string_list(value["generated_files"], "generated_files")
    for filename in generated_files:
        if PurePosixPath(filename).name != filename or not filename.endswith("_generated.h"):
            raise ProofError(f"generated_files contains invalid filename {filename!r}")
    arguments = require_string_list(value["generator_arguments"], "generator_arguments")
    if arguments != ["--cpp", "--scoped-enums"]:
        raise ProofError("generator_arguments must retain the approved restricted profile")
    require_string_list(value["supported_proof_platforms"], "supported_proof_platforms")
    require_string_list(value["excluded_surfaces"], "excluded_surfaces")
    require_string(value["update_policy"], "update_policy")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_tool(name: str, environment_name: str) -> str:
    override = os.environ.get(environment_name)
    if override:
        path = Path(override)
        if not path.is_file():
            raise ProofError(f"{environment_name} does not name a file: {path}")
        return str(path)
    found = shutil.which(name)
    if not found:
        raise ProofError(f"required tool {name!r} is not on PATH (or set {environment_name})")
    return found


def run(arguments: Iterable[str], *, cwd: Path | None = None, capture: bool = False) -> str:
    command = [str(argument) for argument in arguments]
    print("+", subprocess.list2cmdline(command), flush=True)
    result = subprocess.run(
        command,
        cwd=cwd,
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
    request = urllib.request.Request(url, headers={"User-Agent": "TES3MP-vNext-FlatBuffers-proof"})
    try:
        with urllib.request.urlopen(request, timeout=120) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
    except OSError as error:
        raise ProofError(f"cannot download {url}: {error}") from error
    actual = sha256_file(partial)
    if actual != expected_sha256:
        partial.unlink(missing_ok=True)
        raise ProofError(f"archive SHA-256 mismatch: expected {expected_sha256}, got {actual}")
    os.replace(partial, destination)


def extract_regular_files(archive_path: Path, destination: Path, expected_root: str) -> Path:
    source_root = destination / expected_root
    marker = source_root / "CMakeLists.txt"
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
    if not marker.is_file():
        raise ProofError(f"source archive did not contain {expected_root}/CMakeLists.txt")
    return source_root


BLOCK_PATTERN = re.compile(r"\b(table|enum|union)\s+(\w+)(?:\s*:\s*\w+)?\s*\{([^}]*)\}", re.DOTALL)
FIELD_PATTERN = re.compile(r"^\s*(\w+)\s*:\s*([^;]+);\s*$")
VALUE_PATTERN = re.compile(r"^\s*(\w+)\s*=\s*(\d+)\s*,?\s*$")
ID_PATTERN = re.compile(r"\(\s*id\s*:\s*(\d+)\s*\)")


def parse_schema(text: str, context: str) -> dict[str, dict[str, tuple[str, int]]]:
    without_comments = re.sub(r"//.*", "", text)
    forbidden = ("required", "nested_flatbuffer", "flexbuffer", "native_type", "cpp_type", "offset64", "vector64")
    for token in forbidden:
        if re.search(rf"\b{re.escape(token)}\b", without_comments):
            raise ProofError(f"{context} uses forbidden schema feature {token!r}")
    result: dict[str, dict[str, tuple[str, int]]] = {}
    union_names = {match.group(2) for match in BLOCK_PATTERN.finditer(without_comments) if match.group(1) == "union"}
    for match in BLOCK_PATTERN.finditer(without_comments):
        kind, name, body = match.groups()
        key = f"{kind}:{name}"
        if key in result:
            raise ProofError(f"{context} declares {key} more than once")
        entries: dict[str, tuple[str, int]] = {}
        used_ids: set[int] = set()
        implicit_union_ids: set[int] = set()
        for line in body.splitlines():
            line = line.strip()
            if not line:
                continue
            if kind == "table":
                field = FIELD_PATTERN.fullmatch(line)
                if not field:
                    raise ProofError(f"{context} has an unsupported table declaration: {line!r}")
                field_name, declaration = field.groups()
                id_match = ID_PATTERN.search(declaration)
                if not id_match:
                    raise ProofError(f"{context} field {name}.{field_name} has no explicit id")
                field_id = int(id_match.group(1))
                normalized = ID_PATTERN.sub("", declaration)
                normalized = re.sub(r"\s+", "", normalized)
                field_type = normalized.split("=", 1)[0]
                if field_id in used_ids or field_id in implicit_union_ids:
                    raise ProofError(f"{context} reuses field id {field_id} in table {name}")
                used_ids.add(field_id)
                if field_type in union_names:
                    if field_id == 0 or field_id - 1 in used_ids or field_id - 1 in implicit_union_ids:
                        raise ProofError(f"{context} union field {name}.{field_name} has an invalid discriminator id")
                    implicit_union_ids.add(field_id - 1)
                entries[field_name] = (normalized, field_id)
            else:
                value = VALUE_PATTERN.fullmatch(line)
                if not value:
                    raise ProofError(f"{context} {kind} {name} member lacks an explicit value: {line!r}")
                member_name, raw_value = value.groups()
                member_value = int(raw_value)
                if member_value in used_ids:
                    raise ProofError(f"{context} reuses value {member_value} in {kind} {name}")
                used_ids.add(member_value)
                entries[member_name] = (raw_value, member_value)
        if kind == "table":
            all_ids = used_ids | implicit_union_ids
            if all_ids != set(range(len(all_ids))):
                raise ProofError(f"{context} table {name} field ids are not consecutive from zero")
        result[key] = entries
    if not result:
        raise ProofError(f"{context} contains no schema declarations")
    return result


def verify_schema_policy(v1_text: str, v2_text: str) -> None:
    old = parse_schema(v1_text, "v1.fbs")
    new = parse_schema(v2_text, "v2.fbs")
    for block_name, old_entries in old.items():
        if block_name not in new:
            raise ProofError(f"v2.fbs removed {block_name}")
        for entry_name, signature in old_entries.items():
            if new[block_name].get(entry_name) != signature:
                raise ProofError(f"v2.fbs changed or removed {block_name}.{entry_name}")


def generate_and_compare(flatc: Path, lock: dict[str, Any], update_generated: bool) -> None:
    verify_schema_policy(
        (SCHEMA_DIR / "v1.fbs").read_text(encoding="utf-8"),
        (SCHEMA_DIR / "v2.fbs").read_text(encoding="utf-8"),
    )
    if GENERATED_CHECK_DIR.exists():
        shutil.rmtree(GENERATED_CHECK_DIR)
    GENERATED_CHECK_DIR.mkdir(parents=True)
    run(
        [
            str(flatc),
            *lock["generator_arguments"],
            "-o",
            str(GENERATED_CHECK_DIR),
            str(SCHEMA_DIR / "v1.fbs"),
            str(SCHEMA_DIR / "v2.fbs"),
        ]
    )
    actual_names = sorted(path.name for path in GENERATED_CHECK_DIR.iterdir() if path.is_file())
    expected_names = sorted(lock["generated_files"])
    if actual_names != expected_names:
        raise ProofError(f"flatc generated {actual_names}, expected {expected_names}")
    if update_generated:
        GENERATED_DIR.mkdir(parents=True, exist_ok=True)
        for name in expected_names:
            shutil.copyfile(GENERATED_CHECK_DIR / name, GENERATED_DIR / name)
    for name in expected_names:
        expected = GENERATED_DIR / name
        if not expected.is_file():
            raise ProofError(f"committed generated file is missing: {expected}")
        if expected.read_bytes() != (GENERATED_CHECK_DIR / name).read_bytes():
            raise ProofError(f"committed generated file differs from pinned flatc output: {name}")


def locate_flatc(build_dir: Path) -> Path:
    names = ("flatc.exe", "flatc") if os.name == "nt" else ("flatc",)
    for name in names:
        candidates = sorted(build_dir.rglob(name))
        for candidate in candidates:
            if candidate.is_file():
                return candidate
    raise ProofError(f"cannot find built flatc under {build_dir}")


def locate_executable(build_dir: Path, stem: str) -> Path:
    names = (f"{stem}.exe", stem) if os.name == "nt" else (stem,)
    for name in names:
        for candidate in sorted(build_dir.rglob(name)):
            if candidate.is_file():
                return candidate
    raise ProofError(f"cannot find {stem} under {build_dir}")


def compiler_evidence(build_dir: Path) -> dict[str, str]:
    for path in sorted((build_dir / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake")):
        text = path.read_text(encoding="utf-8", errors="replace")
        result: dict[str, str] = {}
        for key in ("CMAKE_CXX_COMPILER_ID", "CMAKE_CXX_COMPILER_VERSION", "CMAKE_CXX_COMPILER_ARCHITECTURE_ID"):
            match = re.search(rf'set\({key} "([^"]*)"\)', text)
            if match:
                result[key.removeprefix("CMAKE_CXX_").lower()] = match.group(1)
        if result:
            return result
    raise ProofError(f"cannot read compiler evidence from {build_dir}")


def write_evidence(
    lock: dict[str, Any], cmake: str, ninja: str, flatc: Path, source_root: Path, fuzz_seconds: int
) -> None:
    dependency = lock["dependency"]
    license_source = source_root / dependency["license"]["path"]
    actual_license_hash = sha256_file(license_source)
    if actual_license_hash != dependency["license"]["sha256"]:
        raise ProofError(
            f"license SHA-256 mismatch: expected {dependency['license']['sha256']}, got {actual_license_hash}"
        )
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(license_source, EVIDENCE_DIR / "FlatBuffers-LICENSE")
    evidence = {
        "schema_version": 1,
        "captured_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "host": {"system": platform.system(), "machine": platform.machine(), "release": platform.release()},
        "tools": {
            "cmake": run([cmake, "--version"], capture=True).splitlines()[0],
            "ninja": run([ninja, "--version"], capture=True),
            "flatc": run([str(flatc), "--version"], capture=True),
            "compiler": compiler_evidence(PROOF_BUILD_DIR),
        },
        "dependency": dependency,
        "dependency_lock_sha256": sha256_file(LOCK_PATH),
        "schemas": {
            path.name: sha256_file(path) for path in sorted(SCHEMA_DIR.glob("*.fbs"))
        },
        "generated": {
            name: sha256_file(GENERATED_DIR / name) for name in lock["generated_files"]
        },
        "tests": ["vnext-flatbuffers-proof"] + ([f"vnext-flatbuffers-fuzz:{fuzz_seconds}s"] if fuzz_seconds else []),
        "fuzz_corpus": {
            path.name: sha256_file(path) for path in sorted(FUZZ_CORPUS_DIR.iterdir()) if path.is_file()
        },
        "android_arm64_assessment": "docs/vnext/proofs/flatbuffers/ANDROID_ARM64.md",
    }
    EVIDENCE_PATH.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Wrote evidence: {EVIDENCE_PATH}")


def execute(update_generated: bool = False, fuzz_seconds: int = 0) -> None:
    lock = load_lock()
    cmake = find_tool("cmake", "VNEXT_CMAKE")
    ninja = find_tool("ninja", "VNEXT_NINJA")
    dependency = lock["dependency"]
    archive = DOWNLOAD_DIR / f"flatbuffers-{dependency['version']}.tar.gz"
    download_verified(
        dependency["source_archive"]["url"], dependency["source_archive"]["sha256"], archive
    )
    source_root = extract_regular_files(archive, SOURCE_PARENT, dependency["source_directory"])

    run(
        [
            cmake,
            "-S",
            str(source_root),
            "-B",
            str(DEPENDENCY_BUILD_DIR),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            "-DCMAKE_CXX_STANDARD=20",
            "-DFLATBUFFERS_BUILD_TESTS=OFF",
            "-DFLATBUFFERS_BUILD_FLATLIB=OFF",
            "-DFLATBUFFERS_BUILD_FLATC=ON",
            "-DFLATBUFFERS_INSTALL=OFF",
        ]
    )
    run([cmake, "--build", str(DEPENDENCY_BUILD_DIR), "--target", "flatc"])
    flatc = locate_flatc(DEPENDENCY_BUILD_DIR)
    expected_version = f"flatc version {dependency['version']}"
    actual_version = run([str(flatc), "--version"], capture=True)
    if actual_version != expected_version:
        raise ProofError(f"unexpected flatc version: expected {expected_version!r}, got {actual_version!r}")
    generate_and_compare(flatc, lock, update_generated)

    run(
        [
            cmake,
            "-S",
            str(PROOF_DIR),
            "-B",
            str(PROOF_BUILD_DIR),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
            f"-DFLATBUFFERS_SOURCE_DIR={source_root}",
            f"-DVNEXT_FLATBUFFERS_BUILD_FUZZER={'ON' if fuzz_seconds else 'OFF'}",
        ]
    )
    run([cmake, "--build", str(PROOF_BUILD_DIR)])
    run([cmake, "--build", str(PROOF_BUILD_DIR), "--target", "test"])
    proof_executable = locate_executable(PROOF_BUILD_DIR, "vnext-flatbuffers-proof")
    if FUZZ_CORPUS_DIR.exists():
        shutil.rmtree(FUZZ_CORPUS_DIR)
    run([str(proof_executable), "--write-corpus", str(FUZZ_CORPUS_DIR)])
    if fuzz_seconds:
        fuzz_executable = locate_executable(PROOF_BUILD_DIR, "vnext-flatbuffers-fuzz")
        run(
            [
                str(fuzz_executable),
                f"-max_total_time={fuzz_seconds}",
                "-print_final_stats=1",
                str(FUZZ_CORPUS_DIR),
            ]
        )
    write_evidence(lock, cmake, ninja, flatc, source_root, fuzz_seconds)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--update-generated",
        action="store_true",
        help="replace committed generated proof headers with exact pinned flatc output",
    )
    parser.add_argument(
        "--fuzz-seconds",
        type=int,
        default=0,
        help="build the Clang ASan/UBSan/libFuzzer target and run it for this many seconds",
    )
    arguments = parser.parse_args()
    if arguments.fuzz_seconds < 0:
        parser.error("--fuzz-seconds cannot be negative")
    try:
        execute(update_generated=arguments.update_generated, fuzz_seconds=arguments.fuzz_seconds)
    except ProofError as error:
        print(f"FlatBuffers proof failed: {error}", file=sys.stderr)
        return 1
    print("FlatBuffers selection proof passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
