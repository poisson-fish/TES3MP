#!/usr/bin/env python3
"""Verify the vNext tree and dependency inputs against its pinned OpenMW baseline."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Iterable


SCHEMA_VERSION = 1
DEFAULT_MANIFEST = "docs/vnext/BASELINE_PROVENANCE.json"


class VerificationError(RuntimeError):
    """Raised when the repository does not match the provenance manifest."""


def _run_git(repository: Path, *arguments: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )
    if check and result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit code {result.returncode}"
        raise VerificationError(f"git {' '.join(arguments)} failed: {detail}")
    return result


def _run_git_bytes(repository: Path, *arguments: str) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace").strip() or f"exit code {result.returncode}"
        raise VerificationError(f"git {' '.join(arguments)} failed: {detail}")
    return result.stdout


def _require_exact_keys(value: dict[str, Any], required: set[str], context: str) -> None:
    actual = set(value)
    missing = sorted(required - actual)
    extra = sorted(actual - required)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing {', '.join(missing)}")
        if extra:
            details.append(f"unknown {', '.join(extra)}")
        raise VerificationError(f"{context} has invalid fields: {'; '.join(details)}")


def _require_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise VerificationError(f"{context} must be a non-empty string")
    return value


def _require_string_list(value: Any, context: str) -> list[str]:
    if not isinstance(value, list) or not value:
        raise VerificationError(f"{context} must be a non-empty list")
    result = []
    for index, item in enumerate(value):
        result.append(_require_string(item, f"{context}[{index}]"))
    if len(result) != len(set(result)):
        raise VerificationError(f"{context} contains duplicate values")
    return result


def _require_repo_path(value: Any, context: str) -> str:
    path = _require_string(value, context)
    parsed = PurePosixPath(path)
    if parsed.is_absolute() or ".." in parsed.parts or "\\" in path or path.startswith("./"):
        raise VerificationError(f"{context} must be a normalized repository-relative POSIX path")
    return path


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as stream:
            value = json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        raise VerificationError(f"cannot read manifest {path}: {error}") from error
    if not isinstance(value, dict):
        raise VerificationError("manifest root must be a JSON object")
    _require_exact_keys(
        value,
        {"schema_version", "baseline", "cutover", "intentional_differences", "dependency_inputs"},
        "manifest",
    )
    if value["schema_version"] != SCHEMA_VERSION:
        raise VerificationError(
            f"unsupported schema_version {value['schema_version']!r}; expected {SCHEMA_VERSION}"
        )
    return value


def _validate_manifest(manifest: dict[str, Any]) -> tuple[dict[str, str], dict[str, str], dict[str, str], list[dict[str, Any]]]:
    baseline = manifest["baseline"]
    if not isinstance(baseline, dict):
        raise VerificationError("baseline must be an object")
    _require_exact_keys(baseline, {"repository", "tag", "commit", "tree"}, "baseline")
    baseline_values = {key: _require_string(value, f"baseline.{key}") for key, value in baseline.items()}

    cutover = manifest["cutover"]
    if not isinstance(cutover, dict):
        raise VerificationError("cutover must be an object")
    _require_exact_keys(
        cutover,
        {"commit", "tree", "first_parent", "second_parent", "preserved_prefix"},
        "cutover",
    )
    cutover_values = {key: _require_string(value, f"cutover.{key}") for key, value in cutover.items()}
    _require_repo_path(cutover_values["preserved_prefix"], "cutover.preserved_prefix")

    differences = manifest["intentional_differences"]
    if not isinstance(differences, list) or not differences:
        raise VerificationError("intentional_differences must be a non-empty list")
    expected: dict[str, str] = {}
    previous_path = ""
    for index, entry in enumerate(differences):
        context = f"intentional_differences[{index}]"
        if not isinstance(entry, dict):
            raise VerificationError(f"{context} must be an object")
        _require_exact_keys(entry, {"path", "status", "purpose"}, context)
        path = _require_repo_path(entry["path"], f"{context}.path")
        status = _require_string(entry["status"], f"{context}.status")
        _require_string(entry["purpose"], f"{context}.purpose")
        if status not in {"A", "M", "D", "T"}:
            raise VerificationError(f"{context}.status must be one of A, M, D, or T")
        if path in expected:
            raise VerificationError(f"duplicate intentional difference: {path}")
        if path <= previous_path:
            raise VerificationError("intentional_differences must be sorted by path")
        expected[path] = status
        previous_path = path

    inputs = manifest["dependency_inputs"]
    if not isinstance(inputs, list) or not inputs:
        raise VerificationError("dependency_inputs must be a non-empty list")
    seen_ids: set[str] = set()
    seen_files: set[str] = set()
    for input_index, entry in enumerate(inputs):
        context = f"dependency_inputs[{input_index}]"
        if not isinstance(entry, dict):
            raise VerificationError(f"{context} must be an object")
        _require_exact_keys(
            entry,
            {
                "id",
                "platforms",
                "files",
                "repositories",
                "resolution",
                "license_evidence",
                "phase1_follow_up",
            },
            context,
        )
        input_id = _require_string(entry["id"], f"{context}.id")
        if input_id in seen_ids:
            raise VerificationError(f"duplicate dependency input id: {input_id}")
        seen_ids.add(input_id)
        _require_string_list(entry["platforms"], f"{context}.platforms")
        _require_string_list(entry["repositories"], f"{context}.repositories")
        _require_string(entry["resolution"], f"{context}.resolution")
        _require_string(entry["license_evidence"], f"{context}.license_evidence")
        _require_string(entry["phase1_follow_up"], f"{context}.phase1_follow_up")
        files = entry["files"]
        if not isinstance(files, list) or not files:
            raise VerificationError(f"{context}.files must be a non-empty list")
        for file_index, file_entry in enumerate(files):
            file_context = f"{context}.files[{file_index}]"
            if not isinstance(file_entry, dict):
                raise VerificationError(f"{file_context} must be an object")
            _require_exact_keys(file_entry, {"path", "sha256"}, file_context)
            file_path = _require_repo_path(file_entry["path"], f"{file_context}.path")
            digest = _require_string(file_entry["sha256"], f"{file_context}.sha256")
            if len(digest) != 64 or any(character not in "0123456789abcdef" for character in digest):
                raise VerificationError(f"{file_context}.sha256 must be a lowercase SHA-256 digest")
            if file_path in seen_files:
                raise VerificationError(f"dependency declaration file is listed more than once: {file_path}")
            seen_files.add(file_path)

    return baseline_values, cutover_values, expected, inputs


def _verify_object(repository: Path, object_id: str, expected_type: str, context: str) -> None:
    actual_type = _run_git(repository, "cat-file", "-t", object_id).stdout.strip()
    if actual_type != expected_type:
        raise VerificationError(f"{context} {object_id} is {actual_type!r}, expected {expected_type!r}")


def _verify_ancestry(repository: Path, ancestor: str, descendant: str, context: str) -> None:
    result = _run_git(repository, "merge-base", "--is-ancestor", ancestor, descendant, check=False)
    if result.returncode != 0:
        raise VerificationError(f"{context}: {ancestor} is not an ancestor of {descendant}")


def _parse_name_status(lines: Iterable[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in lines:
        if not line:
            continue
        fields = line.split("\t")
        if len(fields) != 2:
            raise VerificationError(f"unexpected git diff --name-status output: {line!r}")
        status, path = fields
        if len(status) != 1:
            raise VerificationError(f"rename/copy or unknown diff status is not allowed: {line!r}")
        result[path] = status
    return result


def _verify_dependency_inputs(repository: Path, inputs: list[dict[str, Any]], treeish: str) -> None:
    for entry in inputs:
        for file_entry in entry["files"]:
            object_spec = f":{file_entry['path']}" if treeish == "INDEX" else f"{treeish}:{file_entry['path']}"
            try:
                contents = _run_git_bytes(repository, "cat-file", "blob", object_spec)
            except VerificationError as error:
                raise VerificationError(
                    f"dependency declaration file is missing from {treeish}: {file_entry['path']}"
                ) from error
            actual = hashlib.sha256(contents).hexdigest()
            if actual != file_entry["sha256"]:
                raise VerificationError(
                    f"dependency declaration hash mismatch for {file_entry['path']}: "
                    f"expected {file_entry['sha256']}, got {actual}"
                )


def verify_repository(
    repository: Path, manifest_path: Path, revision: str = "HEAD", use_index: bool = False
) -> list[str]:
    repository = Path(_run_git(repository, "rev-parse", "--show-toplevel").stdout.strip())
    manifest = load_manifest(manifest_path)
    baseline, cutover, expected, inputs = _validate_manifest(manifest)

    _verify_object(repository, baseline["commit"], "commit", "baseline commit")
    actual_baseline_tree = _run_git(repository, "show", "-s", "--format=%T", baseline["commit"]).stdout.strip()
    if actual_baseline_tree != baseline["tree"]:
        raise VerificationError(
            f"baseline tree mismatch: expected {baseline['tree']}, got {actual_baseline_tree}"
        )

    _verify_object(repository, cutover["commit"], "commit", "cutover commit")
    cutover_lines = _run_git(repository, "show", "-s", "--format=%T%n%P", cutover["commit"]).stdout.splitlines()
    actual_cutover_tree = cutover_lines[0]
    actual_parents = cutover_lines[1].split() if len(cutover_lines) > 1 else []
    expected_parents = [cutover["first_parent"], cutover["second_parent"]]
    if actual_cutover_tree != cutover["tree"]:
        raise VerificationError(
            f"cutover tree mismatch: expected {cutover['tree']}, got {actual_cutover_tree}"
        )
    if actual_parents != expected_parents:
        raise VerificationError(
            f"cutover parent mismatch: expected {' '.join(expected_parents)}, got {' '.join(actual_parents)}"
        )
    if cutover["second_parent"] != baseline["commit"]:
        raise VerificationError("cutover.second_parent must equal baseline.commit")

    comparison_revision = "HEAD" if use_index else revision
    resolved_revision = _run_git(repository, "rev-parse", "--verify", f"{comparison_revision}^{{commit}}").stdout.strip()
    _verify_ancestry(repository, baseline["commit"], resolved_revision, "baseline ancestry failure")
    _verify_ancestry(repository, cutover["commit"], resolved_revision, "cutover ancestry failure")

    if use_index:
        diff_result = _run_git(
            repository,
            "diff",
            "--cached",
            "--name-status",
            "--no-renames",
            baseline["commit"],
            "--",
        )
        reported_revision = "INDEX"
    else:
        diff_result = _run_git(
            repository,
            "diff",
            "--name-status",
            "--no-renames",
            baseline["commit"],
            resolved_revision,
            "--",
        )
        reported_revision = resolved_revision
    actual = _parse_name_status(diff_result.stdout.splitlines())
    if actual != expected:
        messages = []
        for path in sorted(set(actual) - set(expected)):
            messages.append(f"unrecorded {actual[path]} {path}")
        for path in sorted(set(expected) - set(actual)):
            messages.append(f"missing expected {expected[path]} {path}")
        for path in sorted(set(actual) & set(expected)):
            if actual[path] != expected[path]:
                messages.append(f"status mismatch {path}: expected {expected[path]}, got {actual[path]}")
        raise VerificationError("intentional difference mismatch: " + "; ".join(messages))

    _verify_dependency_inputs(repository, inputs, reported_revision)

    lines = [
        f"Baseline commit: {baseline['commit']}",
        f"Baseline tree:   {baseline['tree']}",
        f"Cutover commit:  {cutover['commit']}",
        f"Compared tree:   {reported_revision}",
        f"Intentional differences ({len(actual)}):",
    ]
    lines.extend(f"  {actual[path]} {path}" for path in sorted(actual))
    lines.append(f"Dependency declaration inputs verified ({sum(len(entry['files']) for entry in inputs)} files).")
    return lines


def _parse_arguments(arguments: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=Path.cwd(), help="path inside the Git repository")
    parser.add_argument("--manifest", type=Path, help=f"manifest path (default: {DEFAULT_MANIFEST})")
    parser.add_argument("--revision", default="HEAD", help="committed revision to compare with the baseline")
    parser.add_argument(
        "--index",
        action="store_true",
        help="compare the staged index with the baseline (revision is used only for ancestry through HEAD)",
    )
    return parser.parse_args(arguments)


def main(arguments: list[str] | None = None) -> int:
    options = _parse_arguments(sys.argv[1:] if arguments is None else arguments)
    try:
        repository = Path(
            _run_git(options.repository, "rev-parse", "--show-toplevel").stdout.strip()
        )
        manifest_path = options.manifest or repository / DEFAULT_MANIFEST
        for line in verify_repository(repository, manifest_path, options.revision, options.index):
            print(line)
    except VerificationError as error:
        print(f"baseline verification failed: {error}", file=sys.stderr)
        return 1
    print("Baseline provenance verification passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
