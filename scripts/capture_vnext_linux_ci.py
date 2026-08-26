#!/usr/bin/env python3
"""Capture fail-closed Ubuntu package and license evidence for vNext baseline CI."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
import tarfile
from typing import Iterable


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "vnext-baseline"
BASELINE_EVIDENCE = BUILD_DIR / "vnext-baseline-evidence.json"
OUTPUT_DIR = BUILD_DIR / "linux-ci-evidence"
SUPPORTED_COMPILERS = {
    "gcc-13": ("GNU", 13),
    "clang-18": ("Clang", 18),
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


def parse_os_release(contents: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in contents.splitlines():
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if value.startswith(('"', "'")) and value.endswith(value[0]):
            value = value[1:-1]
        result[key] = value
    return result


def validate_host(os_release: dict[str, str], machine: str) -> None:
    if os_release.get("ID") != "ubuntu" or os_release.get("VERSION_ID") != "24.04":
        raise CaptureError("Linux baseline evidence requires Ubuntu 24.04.")
    if machine.lower() not in {"x86_64", "amd64"}:
        raise CaptureError(f"Linux baseline evidence requires x86-64, got {machine!r}.")


def validate_baseline_evidence(evidence: dict, expected_compiler: str) -> None:
    if expected_compiler not in SUPPORTED_COMPILERS:
        raise CaptureError(f"Unsupported Linux compiler gate: {expected_compiler}")
    if evidence.get("preset") != "vnext-baseline-linux-ci":
        raise CaptureError("Baseline evidence did not use the full-build Linux CI CMake preset.")
    tools = evidence.get("tools")
    if not isinstance(tools, dict):
        raise CaptureError("Baseline evidence does not contain a tools object.")
    expected_id, expected_major = SUPPORTED_COMPILERS[expected_compiler]
    actual_id = tools.get("CMAKE_CXX_COMPILER_ID")
    version = tools.get("CMAKE_CXX_COMPILER_VERSION")
    if actual_id != expected_id or not isinstance(version, str):
        raise CaptureError(
            f"Expected {expected_id} {expected_major}, got compiler id {actual_id!r} and version {version!r}."
        )
    try:
        actual_major = int(version.split(".", 1)[0])
    except ValueError as error:
        raise CaptureError(f"Invalid compiler version in baseline evidence: {version!r}") from error
    if actual_major != expected_major:
        raise CaptureError(f"Expected {expected_id} {expected_major}, got {version}.")
    if tools.get("CMAKE_GENERATOR") != "Ninja":
        raise CaptureError(f"Expected Ninja generator, got {tools.get('CMAKE_GENERATOR')!r}.")


def redact_source_credentials(contents: str) -> str:
    return re.sub(r"(https?://)[^\s/@]+@", r"\1<redacted>@", contents)


def parse_installed_packages(contents: str) -> list[str]:
    return sorted(line.split("\t", 1)[1] for line in contents.splitlines() if line.startswith("ii \t"))


def capture_apt_sources(paths: Iterable[pathlib.Path]) -> str:
    sections = []
    for path in sorted(paths):
        if not path.is_file():
            continue
        sections.append(f"### {path}\n{redact_source_credentials(path.read_text(encoding='utf-8', errors='replace'))}")
    return "\n".join(sections)


def create_license_archive(destination: pathlib.Path, doc_root: pathlib.Path = pathlib.Path("/usr/share/doc")) -> int:
    copyright_files = sorted(path for path in doc_root.glob("*/copyright") if path.is_file())
    if not copyright_files:
        raise CaptureError(f"No package copyright files found under {doc_root}.")
    with tarfile.open(destination, "w:gz", dereference=True) as archive:
        for path in copyright_files:
            archive.add(path, arcname=pathlib.PurePosixPath("usr/share/doc") / path.parent.name / path.name)
    return len(copyright_files)


def capture(expected_compiler: str, output_dir: pathlib.Path = OUTPUT_DIR) -> dict:
    if platform.system() != "Linux":
        raise CaptureError(f"Linux evidence capture cannot run on {platform.system()}.")
    os_release_path = pathlib.Path("/etc/os-release")
    if not os_release_path.is_file():
        raise CaptureError("Missing /etc/os-release.")
    os_release = parse_os_release(os_release_path.read_text(encoding="utf-8"))
    validate_host(os_release, platform.machine())
    if not BASELINE_EVIDENCE.is_file():
        raise CaptureError(f"Missing baseline evidence: {BASELINE_EVIDENCE}")
    baseline = json.loads(BASELINE_EVIDENCE.read_text(encoding="utf-8"))
    validate_baseline_evidence(baseline, expected_compiler)
    github_sha = os.environ.get("GITHUB_SHA")
    if github_sha and baseline.get("source_commit") != github_sha:
        raise CaptureError(
            f"Baseline evidence source {baseline.get('source_commit')!r} does not match GITHUB_SHA {github_sha!r}."
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    packages_path = output_dir / "installed-packages.tsv"
    package_lines = parse_installed_packages(
        run(
            [
                "dpkg-query",
                "-W",
                "-f=${db:Status-Abbrev}\\t${binary:Package}\\t${Version}\\t${Architecture}\\n",
            ]
        )
    )
    packages_path.write_text("package\tversion\tarchitecture\n" + "\n".join(package_lines) + "\n", encoding="utf-8")

    apt_policy_path = output_dir / "apt-policy.txt"
    apt_policy_path.write_text(redact_source_credentials(run(["apt-cache", "policy"])), encoding="utf-8")
    source_paths = [pathlib.Path("/etc/apt/sources.list")]
    source_paths.extend(pathlib.Path("/etc/apt/sources.list.d").glob("*.list"))
    source_paths.extend(pathlib.Path("/etc/apt/sources.list.d").glob("*.sources"))
    apt_sources_path = output_dir / "apt-sources.txt"
    apt_sources_path.write_text(capture_apt_sources(source_paths), encoding="utf-8")

    licenses_path = output_dir / "package-copyrights.tar.gz"
    license_count = create_license_archive(licenses_path)
    evidence = {
        "schema_version": 1,
        "source_commit": baseline.get("source_commit"),
        "compiler_gate": expected_compiler,
        "host": {
            "distribution": os_release.get("PRETTY_NAME"),
            "id": os_release.get("ID"),
            "version_id": os_release.get("VERSION_ID"),
            "architecture": platform.machine(),
            "kernel": platform.release(),
        },
        "github_runner": {
            key: os.environ[key]
            for key in ("ImageOS", "ImageVersion", "RUNNER_ARCH", "RUNNER_ENVIRONMENT")
            if key in os.environ
        },
        "tools": baseline["tools"],
        "artifacts": {
            "installed_packages": {
                "path": packages_path.name,
                "count": len(package_lines),
                "sha256": sha256(packages_path),
            },
            "apt_policy": {"path": apt_policy_path.name, "sha256": sha256(apt_policy_path)},
            "apt_sources": {"path": apt_sources_path.name, "sha256": sha256(apt_sources_path)},
            "package_copyrights": {"path": licenses_path.name, "count": license_count, "sha256": sha256(licenses_path)},
        },
    }
    evidence_path = output_dir / "linux-ci-evidence.json"
    evidence_path.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return evidence


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True, choices=sorted(SUPPORTED_COMPILERS))
    options = parser.parse_args(arguments)
    try:
        evidence = capture(options.compiler)
    except (CaptureError, OSError, json.JSONDecodeError) as error:
        print(f"linux baseline evidence capture failed: {error}", file=sys.stderr)
        return 1
    print(
        "Captured Linux baseline evidence: "
        f"{evidence['artifacts']['installed_packages']['count']} packages, "
        f"{evidence['artifacts']['package_copyrights']['count']} copyright files."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
