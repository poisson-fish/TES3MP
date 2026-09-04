#!/usr/bin/env python3
"""Materialize the exact OpenXR-SDK input for the maintained PC VR target."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import tarfile
import tempfile
import time
import urllib.request
from pathlib import Path, PurePosixPath


ROOT = Path(__file__).resolve().parents[1]
PROVENANCE_PATH = ROOT / "docs" / "vnext" / "OPENMW_VR_PROVENANCE.json"
DEFAULT_CACHE = ROOT / "build" / "vnext-vr-dependencies"


class PreparationError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_openxr(path: Path) -> dict[str, str]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))["openxr"]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise PreparationError(f"cannot read OpenXR lock from {path}: {error}") from error
    required = {
        "repository", "fork_source_ref", "commit", "archive_url", "archive_sha256",
        "license", "license_path", "license_sha256", "acquisition_command", "cmake_path",
    }
    if not isinstance(value, dict) or set(value) != required:
        raise PreparationError("OpenXR lock fields differ from the reviewed schema")
    if value["license"] != "Apache-2.0" or len(value["commit"]) != 40:
        raise PreparationError("OpenXR identity or license differs from the reviewed selection")
    for key in ("archive_sha256", "license_sha256"):
        if len(value[key]) != 64 or any(character not in "0123456789abcdef" for character in value[key]):
            raise PreparationError(f"openxr.{key} is not a lowercase SHA-256 digest")
    return value


def download(lock: dict[str, str], archive: Path, attempts: int = 3) -> None:
    archive.parent.mkdir(parents=True, exist_ok=True)
    last_error: OSError | None = None
    for attempt in range(1, attempts + 1):
        partial = archive.with_suffix(archive.suffix + ".partial")
        partial.unlink(missing_ok=True)
        request = urllib.request.Request(lock["archive_url"], headers={"User-Agent": "TES3MP-vNext-VR"})
        try:
            with urllib.request.urlopen(request, timeout=180) as response, partial.open("wb") as output:
                shutil.copyfileobj(response, output)
            actual = sha256(partial)
            if actual != lock["archive_sha256"]:
                raise PreparationError(
                    f"OpenXR archive SHA-256 mismatch: expected {lock['archive_sha256']}, got {actual}"
                )
            os.replace(partial, archive)
            return
        except OSError as error:
            last_error = error
            partial.unlink(missing_ok=True)
            if attempt < attempts:
                time.sleep(attempt)
    raise PreparationError(f"cannot download exact OpenXR archive after {attempts} attempts: {last_error}")


def source_is_valid(source: Path, lock: dict[str, str]) -> bool:
    marker = source / ".tes3mp-openxr-lock.json"
    license_path = source / lock["license_path"]
    try:
        recorded = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return (
        recorded == {"commit": lock["commit"], "archive_sha256": lock["archive_sha256"]}
        and (source / "CMakeLists.txt").is_file()
        and license_path.is_file()
        and sha256(license_path) == lock["license_sha256"]
    )


def extract(archive_path: Path, source: Path, lock: dict[str, str]) -> None:
    expected_root = f"OpenXR-SDK-{lock['commit']}"
    staging = Path(tempfile.mkdtemp(prefix="openxr-", dir=source.parent))
    try:
        with tarfile.open(archive_path, "r:gz") as archive:
            for member in archive.getmembers():
                parsed = PurePosixPath(member.name)
                if parsed.is_absolute() or ".." in parsed.parts or not parsed.parts or parsed.parts[0] != expected_root:
                    raise PreparationError(f"unsafe OpenXR archive member: {member.name!r}")
                if not member.isfile():
                    continue
                target = staging.joinpath(*parsed.parts[1:])
                target.parent.mkdir(parents=True, exist_ok=True)
                content = archive.extractfile(member)
                if content is None:
                    raise PreparationError(f"cannot read OpenXR archive member: {member.name!r}")
                with content, target.open("wb") as output:
                    shutil.copyfileobj(content, output)
        license_path = staging / lock["license_path"]
        if not (staging / "CMakeLists.txt").is_file() or not license_path.is_file():
            raise PreparationError("OpenXR archive omits required build or license file")
        if sha256(license_path) != lock["license_sha256"]:
            raise PreparationError("OpenXR license hash differs from the reviewed lock")
        (staging / ".tes3mp-openxr-lock.json").write_text(
            json.dumps({"commit": lock["commit"], "archive_sha256": lock["archive_sha256"]}, indent=2) + "\n",
            encoding="utf-8",
        )
        if source.exists():
            shutil.rmtree(source)
        os.replace(staging, source)
    finally:
        if staging.exists():
            shutil.rmtree(staging)


def prepare(cache: Path, provenance: Path = PROVENANCE_PATH, offline: bool = False) -> Path:
    lock = load_openxr(provenance)
    archive = cache / "downloads" / f"OpenXR-SDK-{lock['commit']}.tar.gz"
    source = cache / "sources" / f"OpenXR-SDK-{lock['commit']}"
    source.parent.mkdir(parents=True, exist_ok=True)
    if source_is_valid(source, lock):
        return source.resolve()
    if not archive.is_file() or sha256(archive) != lock["archive_sha256"]:
        if offline:
            raise PreparationError("offline mode needs the exact verified OpenXR archive in the cache")
        download(lock, archive)
    extract(archive, source, lock)
    return source.resolve()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()
    try:
        source = prepare(args.cache.resolve(), offline=args.offline)
    except PreparationError as error:
        print(f"OpenXR preparation failed: {error}")
        return 1
    print(f"OpenXR source verified: {source}")
    print(f"CMake option: -DFETCHCONTENT_SOURCE_DIR_OPENXR={source}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
