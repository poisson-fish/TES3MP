#!/usr/bin/env python3
"""Prove that the configured vNext build excludes archived TES3MP multiplayer code."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
from typing import Iterable


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "vnext-baseline"
EVIDENCE_PATH = BUILD_DIR / "vnext-legacy-exclusion.json"

LEGACY_PATH_PREFIXES = (
    "apps/browser",
    "apps/master",
    "apps/openmw-mp",
    "components/openmw-mp",
    "files/tes3mp",
)
LEGACY_EXACT_PATHS = {
    "cmake/FindRakNet.cmake",
    "tes3mp-changelog.md",
    "tes3mp-credits.md",
}
LEGACY_BUILD_PATTERN = re.compile(
    r"(?i)(?:tes3mp-(?:server|browser)|raknet|crabnet|corescripts|packetprocessor|"
    r"(?:^|/)apps/openmw-mp(?:/|$)|(?:^|/)components/openmw-mp(?:/|$))"
)


class ExclusionError(RuntimeError):
    """Raised when legacy multiplayer input is reachable from the active build."""


def run_git(repository: pathlib.Path, *arguments: str, binary: bool = False) -> str | bytes:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=not binary,
        encoding=None if binary else "utf-8",
    )
    if result.returncode != 0:
        stderr = result.stderr if isinstance(result.stderr, str) else result.stderr.decode("utf-8", errors="replace")
        raise ExclusionError(f"git {' '.join(arguments)} failed: {stderr.strip()}")
    return result.stdout


def normalized_path(value: str | pathlib.Path) -> str:
    return str(value).replace("\\", "/")


def is_legacy_path(path: str) -> bool:
    normalized = normalized_path(path).strip("/")
    return normalized in LEGACY_EXACT_PATHS or any(
        normalized == prefix or normalized.startswith(prefix + "/") for prefix in LEGACY_PATH_PREFIXES
    )


def list_tree_paths(repository: pathlib.Path, use_index: bool) -> list[str]:
    if use_index:
        raw = run_git(repository, "ls-files", "--cached", "-z", binary=True)
    else:
        raw = run_git(repository, "ls-tree", "-r", "--name-only", "-z", "HEAD", binary=True)
    assert isinstance(raw, bytes)
    return [entry.decode("utf-8", errors="surrogateescape") for entry in raw.split(b"\0") if entry]


def read_tree_file(repository: pathlib.Path, path: str, use_index: bool) -> bytes:
    specifier = f":{path}" if use_index else f"HEAD:{path}"
    contents = run_git(repository, "cat-file", "blob", specifier, binary=True)
    assert isinstance(contents, bytes)
    return contents


def build_metadata_paths(paths: Iterable[str]) -> list[str]:
    return sorted(
        path
        for path in paths
        if pathlib.PurePosixPath(path).name == "CMakeLists.txt"
        or path.endswith(".cmake")
        or path == "CMakePresets.json"
    )


def find_legacy_token(contents: str) -> str | None:
    normalized = normalized_path(contents)
    match = LEGACY_BUILD_PATTERN.search(normalized)
    return match.group(0) if match else None


def verify_source_tree(repository: pathlib.Path, use_index: bool = False) -> tuple[int, int]:
    paths = list_tree_paths(repository, use_index)
    legacy_paths = sorted(path for path in paths if is_legacy_path(path))
    if legacy_paths:
        raise ExclusionError("legacy multiplayer paths remain tracked: " + ", ".join(legacy_paths))

    metadata = build_metadata_paths(paths)
    if not metadata:
        raise ExclusionError("no tracked CMake build metadata was found")
    for path in metadata:
        contents = read_tree_file(repository, path, use_index).decode("utf-8", errors="replace")
        token = find_legacy_token(contents)
        if token:
            raise ExclusionError(f"legacy multiplayer token {token!r} is present in build metadata {path}")
    return len(paths), len(metadata)


def relative_source_path(path: pathlib.Path, repository: pathlib.Path, build_dir: pathlib.Path) -> str | None:
    resolved = path.resolve()
    for base in (repository.resolve(), build_dir.resolve()):
        try:
            return normalized_path(resolved.relative_to(base))
        except ValueError:
            continue
    return None


def verify_compile_commands(repository: pathlib.Path, build_dir: pathlib.Path) -> int:
    path = build_dir / "compile_commands.json"
    try:
        commands = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ExclusionError(f"cannot read configured compilation database {path}: {error}") from error
    if not isinstance(commands, list) or not commands:
        raise ExclusionError(f"configured compilation database is empty or invalid: {path}")

    for index, entry in enumerate(commands):
        if not isinstance(entry, dict) or not isinstance(entry.get("file"), str):
            raise ExclusionError(f"compile_commands.json entry {index} has no source file")
        source = pathlib.Path(entry["file"])
        if not source.is_absolute():
            directory = pathlib.Path(entry.get("directory", build_dir))
            source = directory / source
        relative = relative_source_path(source, repository, build_dir)
        if relative is not None and is_legacy_path(relative):
            raise ExclusionError(f"legacy multiplayer source is present in the compilation database: {relative}")
        command_text = entry.get("command") or " ".join(entry.get("arguments", []))
        if not isinstance(command_text, str):
            raise ExclusionError(f"compile_commands.json entry {index} has invalid command data")
        token = find_legacy_token(command_text)
        if token:
            raise ExclusionError(f"legacy multiplayer token {token!r} is present in compilation command {index}")
    return len(commands)


def verify_ninja_graph(build_dir: pathlib.Path) -> tuple[int, str]:
    path = build_dir / "build.ninja"
    try:
        contents = path.read_bytes()
    except OSError as error:
        raise ExclusionError(f"cannot read configured Ninja graph {path}: {error}") from error
    if not contents:
        raise ExclusionError(f"configured Ninja graph is empty: {path}")
    text = contents.decode("utf-8", errors="replace")
    token = find_legacy_token(text)
    if token:
        raise ExclusionError(f"legacy multiplayer token {token!r} is present in configured Ninja graph")
    return text.count("\nbuild ") + int(text.startswith("build ")), hashlib.sha256(contents).hexdigest()


def verify(
    repository: pathlib.Path = ROOT,
    build_dir: pathlib.Path = BUILD_DIR,
    *,
    use_index: bool = False,
) -> dict[str, object]:
    repository = pathlib.Path(str(run_git(repository, "rev-parse", "--show-toplevel")).strip())
    tracked_count, metadata_count = verify_source_tree(repository, use_index)
    compile_count = verify_compile_commands(repository, build_dir)
    ninja_edge_count, ninja_sha256 = verify_ninja_graph(build_dir)
    revision = "INDEX" if use_index else str(run_git(repository, "rev-parse", "HEAD")).strip()
    return {
        "schema_version": 1,
        "source_revision": revision,
        "tracked_paths_checked": tracked_count,
        "cmake_metadata_files_checked": metadata_count,
        "compile_commands_checked": compile_count,
        "ninja_build_edges_checked": ninja_edge_count,
        "ninja_graph_sha256": ninja_sha256,
        "excluded_legacy_path_prefixes": list(LEGACY_PATH_PREFIXES),
        "excluded_legacy_exact_paths": sorted(LEGACY_EXACT_PATHS),
        "excluded_build_tokens": [
            "tes3mp-server",
            "tes3mp-browser",
            "RakNet",
            "CrabNet",
            "CoreScripts",
            "PacketProcessor",
            "apps/openmw-mp",
            "components/openmw-mp",
        ],
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=pathlib.Path, default=ROOT)
    parser.add_argument("--build-dir", type=pathlib.Path, default=BUILD_DIR)
    parser.add_argument("--evidence", type=pathlib.Path, default=EVIDENCE_PATH)
    parser.add_argument("--index", action="store_true", help="inspect the staged tree instead of HEAD")
    args = parser.parse_args(argv)
    evidence = verify(args.repository, args.build_dir, use_index=args.index)
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(
        "Legacy multiplayer exclusion verified: "
        f"{evidence['tracked_paths_checked']} tracked paths, "
        f"{evidence['cmake_metadata_files_checked']} CMake files, "
        f"{evidence['compile_commands_checked']} compile commands, and "
        f"{evidence['ninja_build_edges_checked']} Ninja build edges checked."
    )
    print(f"Wrote evidence: {args.evidence}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ExclusionError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(2)
