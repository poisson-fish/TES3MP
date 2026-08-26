#!/usr/bin/env python3
"""Capture fail-closed Windows toolchain, package, and license evidence for vNext CI."""

from __future__ import annotations

import hashlib
import json
import os
import pathlib
import platform
import sys
import tarfile
from collections.abc import Iterable, Mapping


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "vnext-baseline"
BASELINE_EVIDENCE = BUILD_DIR / "vnext-baseline-evidence.json"
OUTPUT_DIR = BUILD_DIR / "windows-ci-evidence"
LOCK_PATH = ROOT / "scripts" / "vnext_baseline_dependencies.json"


class CaptureError(RuntimeError):
    pass


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_host(system: str, machine: str, environment: Mapping[str, str]) -> None:
    if system != "Windows" or machine.lower() not in {"amd64", "x86_64"}:
        raise CaptureError(f"Windows baseline evidence requires Windows x86-64, got {system} {machine}.")
    if not environment.get("VSCMD_VER", "").startswith("17."):
        raise CaptureError("Windows baseline evidence requires Visual Studio 2022 (VSCMD_VER 17.x).")
    if environment.get("VSCMD_ARG_TGT_ARCH", "").lower() not in {"x64", "amd64"}:
        raise CaptureError("Windows baseline evidence requires the x64 MSVC target environment.")
    if not environment.get("VCToolsVersion", "").startswith("14."):
        raise CaptureError("Windows baseline evidence requires the Visual Studio 2022 v143 toolset generation.")


def validate_baseline_evidence(evidence: dict) -> None:
    if evidence.get("preset") != "vnext-baseline-windows-ci":
        raise CaptureError("Baseline evidence did not use the full-build Windows CI CMake preset.")
    tools = evidence.get("tools")
    if not isinstance(tools, dict):
        raise CaptureError("Baseline evidence does not contain a tools object.")
    compiler_id = tools.get("CMAKE_CXX_COMPILER_ID")
    compiler_version = tools.get("CMAKE_CXX_COMPILER_VERSION")
    if compiler_id != "MSVC" or not isinstance(compiler_version, str) or not compiler_version.startswith("19."):
        raise CaptureError(f"Expected MSVC 19.x, got {compiler_id!r} {compiler_version!r}.")
    if tools.get("CMAKE_GENERATOR") != "Ninja":
        raise CaptureError(f"Expected Ninja generator, got {tools.get('CMAKE_GENERATOR')!r}.")
    installation = evidence.get("installation")
    if not isinstance(installation, dict) or not isinstance(installation.get("files"), int):
        raise CaptureError("Baseline evidence does not contain a completed installation inventory.")


def make_qt_license_record(qt_lock: object, windows_dependencies: object) -> dict[str, str]:
    if not isinstance(qt_lock, dict) or not isinstance(windows_dependencies, dict):
        raise CaptureError("Qt lock or resolved Windows dependency evidence is missing.")
    version = qt_lock.get("version")
    target = qt_lock.get("target")
    license_reference = qt_lock.get("license_reference")
    if not all(isinstance(value, str) and value for value in (version, target, license_reference)):
        raise CaptureError("Qt version, target, or license reference is missing from the dependency lock.")
    if windows_dependencies.get("qt_version") != version:
        raise CaptureError("Resolved Qt version does not match the dependency lock.")
    return {
        "version": version,
        "target": target,
        "reported_version": windows_dependencies["qt_version"],
        "license_reference": license_reference,
    }


def validate_vcpkg_evidence(
    package_lists: list[pathlib.Path],
    copyright_files: list[pathlib.Path],
    windows_dependencies: object,
) -> None:
    if not isinstance(windows_dependencies, dict):
        raise CaptureError("Resolved Windows dependency evidence is missing.")
    expected_lists = windows_dependencies.get("vcpkg_package_list_files")
    if not isinstance(expected_lists, list) or sorted(expected_lists) != sorted(path.name for path in package_lists):
        raise CaptureError("Archived vcpkg package lists do not match the baseline evidence.")
    expected_copyrights = windows_dependencies.get("vcpkg_copyright_files")
    if expected_copyrights != len(copyright_files):
        raise CaptureError("Archived vcpkg copyright count does not match the baseline evidence.")


def collect_files(patterns: Iterable[str], root: pathlib.Path) -> list[pathlib.Path]:
    return sorted({path for pattern in patterns for path in root.glob(pattern) if path.is_file()})


def archive_name(prefix: str, path: pathlib.Path, root: pathlib.Path) -> pathlib.PurePosixPath:
    return pathlib.PurePosixPath(prefix).joinpath(*path.relative_to(root).parts)


def create_dependency_archive(
    destination: pathlib.Path,
    entries: Iterable[tuple[pathlib.Path, pathlib.PurePosixPath]],
) -> int:
    materialized = sorted(entries, key=lambda entry: str(entry[1]))
    if not materialized:
        raise CaptureError("No Windows dependency evidence files were selected for archival.")
    with tarfile.open(destination, "w:gz") as archive:
        for path, archive_name in materialized:
            archive.add(path, arcname=archive_name, recursive=False)
    return len(materialized)


def capture(output_dir: pathlib.Path = OUTPUT_DIR) -> dict:
    environment = os.environ
    validate_host(platform.system(), platform.machine(), environment)
    if not BASELINE_EVIDENCE.is_file():
        raise CaptureError(f"Missing baseline evidence: {BASELINE_EVIDENCE}")
    baseline = json.loads(BASELINE_EVIDENCE.read_text(encoding="utf-8"))
    validate_baseline_evidence(baseline)
    github_sha = environment.get("GITHUB_SHA")
    if github_sha and baseline.get("source_commit") != github_sha:
        raise CaptureError(
            f"Baseline evidence source {baseline.get('source_commit')!r} does not match GITHUB_SHA {github_sha!r}."
        )

    deps_root = pathlib.Path(environment.get("VNEXT_VCPKG_ROOT", ROOT / "deps"))
    manifest = deps_root / "downloads" / "openmw-vcpkg-manifest.txt"
    if not manifest.is_file():
        raise CaptureError(f"Missing pinned OpenMW dependency manifest: {manifest}")
    if not LOCK_PATH.is_file():
        raise CaptureError(f"Missing dependency lock: {LOCK_PATH}")

    package_lists = collect_files(("installed/vcpkg/info/*.list",), deps_root)
    vcpkg_copyrights = collect_files(("installed/x64-windows/share/*/copyright",), deps_root)
    vcpkg_other_licenses = collect_files(("installed/x64-windows/licenses/**/*",), deps_root)
    vcpkg_licenses = sorted(set(vcpkg_copyrights + vcpkg_other_licenses))
    if not package_lists:
        raise CaptureError("The vcpkg bundle contains no package-list evidence.")
    if not vcpkg_licenses:
        raise CaptureError("The vcpkg bundle contains no retained license evidence.")

    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))
    qt_lock = lock.get("windows_x86_64", {}).get("qt")
    windows_dependencies = baseline.get("windows_dependencies")
    validate_vcpkg_evidence(package_lists, vcpkg_copyrights, windows_dependencies)
    qt_license_record = make_qt_license_record(qt_lock, windows_dependencies)

    output_dir.mkdir(parents=True, exist_ok=True)
    qt_license_path = output_dir / "qt-license-reference.json"
    qt_license_path.write_text(
        json.dumps(
            qt_license_record,
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )

    archive_entries: list[tuple[pathlib.Path, pathlib.PurePosixPath]] = [
        (manifest, pathlib.PurePosixPath("openmw/openmw-vcpkg-manifest.txt")),
        (LOCK_PATH, pathlib.PurePosixPath("vnext/dependency-lock.json")),
        (qt_license_path, pathlib.PurePosixPath("qt/license-reference.json")),
    ]
    archive_entries.extend(
        (path, pathlib.PurePosixPath("vcpkg/package-lists") / path.name) for path in package_lists
    )
    archive_entries.extend(
        (path, archive_name("vcpkg/licenses", path, deps_root / "installed"))
        for path in vcpkg_licenses
    )
    archive_path = output_dir / "windows-dependency-evidence.tar.gz"
    archived_files = create_dependency_archive(archive_path, archive_entries)
    evidence = {
        "schema_version": 1,
        "source_commit": baseline.get("source_commit"),
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "version": platform.version(),
            "architecture": platform.machine(),
        },
        "github_runner": {
            key: environment[key]
            for key in ("ImageOS", "ImageVersion", "RUNNER_ARCH", "RUNNER_ENVIRONMENT")
            if key in environment
        },
        "visual_studio": {
            key: environment[key]
            for key in (
                "VSCMD_VER",
                "VSCMD_ARG_HOST_ARCH",
                "VSCMD_ARG_TGT_ARCH",
                "VCToolsVersion",
                "WindowsSDKVersion",
            )
            if key in environment
        },
        "tools": baseline["tools"],
        "dependencies": windows_dependencies,
        "artifacts": {
            "dependency_lock": {"path": str(LOCK_PATH.relative_to(ROOT)), "sha256": sha256(LOCK_PATH)},
            "openmw_manifest": {"path": manifest.name, "sha256": sha256(manifest)},
            "package_lists": len(package_lists),
            "vcpkg_license_files": len(vcpkg_licenses),
            "qt_license_reference": {
                "path": qt_license_path.name,
                "sha256": sha256(qt_license_path),
            },
            "archive": {
                "path": archive_path.name,
                "files": archived_files,
                "sha256": sha256(archive_path),
            },
        },
    }
    evidence_path = output_dir / "windows-ci-evidence.json"
    evidence_path.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return evidence


def main() -> int:
    try:
        evidence = capture()
    except (CaptureError, OSError, json.JSONDecodeError) as error:
        print(f"windows baseline evidence capture failed: {error}", file=sys.stderr)
        return 1
    print(
        "Captured Windows baseline evidence: "
        f"{evidence['artifacts']['package_lists']} package lists, "
        f"{evidence['artifacts']['vcpkg_license_files']} vcpkg license files, "
        "and a pinned Qt license reference."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
