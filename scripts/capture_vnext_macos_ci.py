#!/usr/bin/env python3
"""Capture fail-closed macOS toolchain, package, and license evidence for vNext CI."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys
import tarfile
from collections.abc import Iterable, Mapping


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "vnext-baseline"
BASELINE_EVIDENCE = BUILD_DIR / "vnext-baseline-evidence.json"
OUTPUT_DIR = BUILD_DIR / "macos-ci-evidence"
LOCK_PATH = ROOT / "scripts" / "vnext_baseline_dependencies.json"
DOWNLOAD_DIR = ROOT / "deps" / "downloads"
SUPPORTED_ARCHITECTURES = {
    "arm64": "arm64-osx-dynamic",
    "x86_64": "x64-osx-dynamic",
}


class CaptureError(RuntimeError):
    pass


def run(command: list[str]) -> str:
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise CaptureError(f"Command failed with exit code {result.returncode}: {' '.join(command)}\n{result.stdout}")
    return result.stdout


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_host(system: str, machine: str, expected_architecture: str) -> str:
    normalized = machine.lower()
    if normalized == "amd64":
        normalized = "x86_64"
    if system != "Darwin" or normalized not in SUPPORTED_ARCHITECTURES:
        raise CaptureError(f"macOS baseline evidence requires macOS arm64 or x86-64, got {system} {machine}.")
    if normalized != expected_architecture:
        raise CaptureError(f"Expected macOS {expected_architecture}, got {machine}.")
    return SUPPORTED_ARCHITECTURES[normalized]


def validate_xcode_version(contents: str) -> str:
    first_line = contents.splitlines()[0] if contents.splitlines() else ""
    if not first_line.startswith("Xcode 16."):
        raise CaptureError(f"macOS baseline evidence requires Xcode 16, got {first_line!r}.")
    return first_line.removeprefix("Xcode ")


def validate_baseline_evidence(evidence: dict, triplet: str) -> None:
    if evidence.get("preset") != "vnext-baseline-macos-ci":
        raise CaptureError("Baseline evidence did not use the full-build macOS CI CMake preset.")
    tools = evidence.get("tools")
    if not isinstance(tools, dict):
        raise CaptureError("Baseline evidence does not contain a tools object.")
    compiler_id = tools.get("CMAKE_CXX_COMPILER_ID")
    compiler_version = tools.get("CMAKE_CXX_COMPILER_VERSION")
    if compiler_id != "AppleClang" or not isinstance(compiler_version, str):
        raise CaptureError(f"Expected AppleClang, got {compiler_id!r} {compiler_version!r}.")
    if tools.get("CMAKE_GENERATOR") != "Ninja":
        raise CaptureError(f"Expected Ninja generator, got {tools.get('CMAKE_GENERATOR')!r}.")
    installation = evidence.get("installation")
    if not isinstance(installation, dict) or not isinstance(installation.get("files"), int):
        raise CaptureError("Baseline evidence does not contain a completed installation inventory.")
    dependencies = evidence.get("macos_dependencies")
    if not isinstance(dependencies, dict) or dependencies.get("vcpkg_triplet") != triplet:
        raise CaptureError("Baseline evidence does not contain the expected macOS dependency triplet.")


def collect_files(patterns: Iterable[str], root: pathlib.Path) -> list[pathlib.Path]:
    return sorted({path for pattern in patterns for path in root.glob(pattern) if path.is_file()})


def validate_vcpkg_evidence(
    package_lists: list[pathlib.Path],
    license_files: list[pathlib.Path],
    dependencies: Mapping[str, object],
) -> None:
    expected_lists = dependencies.get("vcpkg_package_list_files")
    if not isinstance(expected_lists, list) or sorted(expected_lists) != sorted(path.name for path in package_lists):
        raise CaptureError("Archived macOS vcpkg package lists do not match the baseline evidence.")
    expected_copyrights = dependencies.get("vcpkg_copyright_files")
    actual_copyrights = sum(path.name == "copyright" for path in license_files)
    if expected_copyrights != actual_copyrights:
        raise CaptureError("Archived macOS vcpkg copyright count does not match the baseline evidence.")
    if not package_lists or not license_files:
        raise CaptureError("The macOS vcpkg bundle contains no package-list or license evidence.")


def homebrew_inventory(contents: str, required_formulas: Iterable[str]) -> list[dict[str, object]]:
    parsed = json.loads(contents)
    formulae = parsed.get("formulae")
    if not isinstance(formulae, list):
        raise CaptureError("Homebrew did not return a formula inventory.")
    inventory = []
    for formula in formulae:
        if not isinstance(formula, dict) or not isinstance(formula.get("name"), str):
            continue
        installed = formula.get("installed")
        versions = [entry.get("version") for entry in installed if isinstance(entry, dict)] if isinstance(installed, list) else []
        inventory.append(
            {
                "name": formula["name"],
                "versions": [version for version in versions if isinstance(version, str)],
                "license": formula.get("license"),
            }
        )
    names = {entry["name"] for entry in inventory}
    missing = sorted(set(required_formulas) - names)
    if missing:
        raise CaptureError(f"Required Homebrew formulae are missing from the inventory: {', '.join(missing)}")
    for entry in inventory:
        if entry["name"] in required_formulas and (not entry["versions"] or not entry["license"]):
            raise CaptureError(f"Homebrew formula lacks resolved version or license evidence: {entry['name']}")
    return sorted(inventory, key=lambda entry: str(entry["name"]))


def archive_name(prefix: str, path: pathlib.Path, root: pathlib.Path) -> pathlib.PurePosixPath:
    return pathlib.PurePosixPath(prefix).joinpath(*path.relative_to(root).parts)


def create_dependency_archive(
    destination: pathlib.Path,
    entries: Iterable[tuple[pathlib.Path, pathlib.PurePosixPath]],
) -> int:
    materialized = sorted(entries, key=lambda entry: str(entry[1]))
    if not materialized:
        raise CaptureError("No macOS dependency evidence files were selected for archival.")
    with tarfile.open(destination, "w:gz") as archive:
        for path, name in materialized:
            archive.add(path, arcname=name, recursive=False)
    return len(materialized)


def capture(expected_architecture: str, output_dir: pathlib.Path = OUTPUT_DIR) -> dict:
    environment = os.environ
    triplet = validate_host(platform.system(), platform.machine(), expected_architecture)
    if not BASELINE_EVIDENCE.is_file():
        raise CaptureError(f"Missing baseline evidence: {BASELINE_EVIDENCE}")
    baseline = json.loads(BASELINE_EVIDENCE.read_text(encoding="utf-8"))
    validate_baseline_evidence(baseline, triplet)
    github_sha = environment.get("GITHUB_SHA")
    if github_sha and baseline.get("source_commit") != github_sha:
        raise CaptureError(
            f"Baseline evidence source {baseline.get('source_commit')!r} does not match GITHUB_SHA {github_sha!r}."
        )

    lock = json.loads(LOCK_PATH.read_text(encoding="utf-8"))["macos"]
    bundle = lock["vcpkg_bundles"][triplet]
    dependencies = baseline["macos_dependencies"]
    deps_root = pathlib.Path(environment["VNEXT_VCPKG_ROOT"])
    qt_root = pathlib.Path(environment["VNEXT_QT_ROOT"])
    package_lists = collect_files(("installed/vcpkg/info/*.list",), deps_root)
    license_files = collect_files(
        (f"installed/{triplet}/share/*/copyright", f"installed/{triplet}/licenses/**/*"),
        deps_root,
    )
    validate_vcpkg_evidence(package_lists, license_files, dependencies)
    manifest = DOWNLOAD_DIR / f"macos-{triplet}-manifest.txt"
    archive = DOWNLOAD_DIR / bundle["archive_name"]
    if not manifest.is_file() or not archive.is_file():
        raise CaptureError("Pinned macOS manifest or dependency archive is missing.")

    xcode_output = run(["xcodebuild", "-version"])
    xcode_version = validate_xcode_version(xcode_output)
    brew = environment.get("VNEXT_BREW", "brew")
    brew_config = run([brew, "config"])
    brew_raw = run([brew, "info", "--json=v2", "--installed"])
    formulas = homebrew_inventory(brew_raw, lock["homebrew"]["formulas"])
    qmake = qt_root / "bin" / "qmake"
    qt_version = run([str(qmake), "-query", "QT_VERSION"]).strip()
    if qt_version != dependencies.get("qt_version"):
        raise CaptureError("Resolved Homebrew Qt version does not match the baseline evidence.")

    output_dir.mkdir(parents=True, exist_ok=True)
    system_path = output_dir / "system.txt"
    system_path.write_text(run(["sw_vers"]) + run(["uname", "-a"]), encoding="utf-8")
    xcode_path = output_dir / "xcode.txt"
    xcode_path.write_text(xcode_output + run(["xcrun", "clang++", "--version"]), encoding="utf-8")
    brew_config_path = output_dir / "homebrew-config.txt"
    brew_config_path.write_text(brew_config, encoding="utf-8")
    brew_inventory_path = output_dir / "homebrew-formulas.json"
    brew_inventory_path.write_text(json.dumps(formulas, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    qt_license_path = output_dir / "qt-license-reference.json"
    qt_license_path.write_text(
        json.dumps(
            {
                "formula": lock["homebrew"]["qt_formula"],
                "reported_version": qt_version,
                "license_reference": lock["homebrew"]["qt_license_reference"],
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )

    archive_entries: list[tuple[pathlib.Path, pathlib.PurePosixPath]] = [
        (manifest, pathlib.PurePosixPath("openmw") / manifest.name),
        (LOCK_PATH, pathlib.PurePosixPath("vnext/dependency-lock.json")),
        (brew_inventory_path, pathlib.PurePosixPath("homebrew/formulas.json")),
        (qt_license_path, pathlib.PurePosixPath("qt/license-reference.json")),
    ]
    archive_entries.extend(
        (path, pathlib.PurePosixPath("vcpkg/package-lists") / path.name) for path in package_lists
    )
    archive_entries.extend(
        (path, archive_name("vcpkg/licenses", path, deps_root / "installed")) for path in license_files
    )
    dependencies_path = output_dir / "macos-dependency-evidence.tar.gz"
    archived_files = create_dependency_archive(dependencies_path, archive_entries)
    evidence = {
        "schema_version": 1,
        "source_commit": baseline.get("source_commit"),
        "architecture_gate": expected_architecture,
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
        "xcode": {"version": xcode_version},
        "tools": baseline["tools"],
        "dependencies": dependencies,
        "artifacts": {
            "system": {"path": system_path.name, "sha256": sha256(system_path)},
            "xcode": {"path": xcode_path.name, "sha256": sha256(xcode_path)},
            "homebrew_config": {"path": brew_config_path.name, "sha256": sha256(brew_config_path)},
            "homebrew_formulas": {
                "path": brew_inventory_path.name,
                "count": len(formulas),
                "sha256": sha256(brew_inventory_path),
            },
            "vcpkg_package_lists": len(package_lists),
            "vcpkg_license_files": len(license_files),
            "qt_license_reference": {"path": qt_license_path.name, "sha256": sha256(qt_license_path)},
            "archive": {
                "path": dependencies_path.name,
                "files": archived_files,
                "sha256": sha256(dependencies_path),
            },
        },
    }
    evidence_path = output_dir / "macos-ci-evidence.json"
    evidence_path.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return evidence


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--architecture", required=True, choices=sorted(SUPPORTED_ARCHITECTURES))
    options = parser.parse_args(arguments)
    try:
        evidence = capture(options.architecture)
    except (CaptureError, KeyError, OSError, json.JSONDecodeError) as error:
        print(f"macOS baseline evidence capture failed: {error}", file=sys.stderr)
        return 1
    print(
        "Captured macOS baseline evidence: "
        f"{evidence['artifacts']['vcpkg_package_lists']} vcpkg package lists, "
        f"{evidence['artifacts']['vcpkg_license_files']} vcpkg license files, and "
        f"{evidence['artifacts']['homebrew_formulas']['count']} Homebrew formula records."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
