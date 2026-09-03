#!/usr/bin/env python3
import json
import subprocess
import sys
from pathlib import Path


def verify(root: Path) -> None:
    path = root / "docs/vnext/OPENMW_PATCH_REGISTRY.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    if set(data) != {"schema_version", "baseline_commit", "patches"} or data["schema_version"] != 1:
        raise ValueError("invalid patch registry root")
    required = {"id", "slice", "owner", "purpose", "paths", "tests", "disposition", "removal_condition"}
    covered: set[str] = set()
    ids: set[str] = set()
    for patch in data["patches"]:
        if set(patch) != required or not all(patch[key] for key in required):
            raise ValueError("invalid patch entry")
        if patch["id"] in ids:
            raise ValueError(f"duplicate patch id: {patch['id']}")
        ids.add(patch["id"])
        for item in patch["paths"]:
            if item in covered or not (root / item).is_file():
                raise ValueError(f"invalid or duplicate registered path: {item}")
            covered.add(item)
        for item in patch["tests"]:
            if item.startswith("scripts/") and not (root / item).is_file():
                raise ValueError(f"missing registered test: {item}")
    result = subprocess.run(
        ["git", "-C", str(root), "diff", "--name-only", data["baseline_commit"], "--", "apps/openmw"],
        check=True, capture_output=True, text=True, encoding="utf-8")
    changed = {line for line in result.stdout.splitlines()
               if line and not line.startswith("apps/openmw/tes3mp/")}
    if changed != covered:
        raise ValueError(f"registry coverage mismatch: missing={sorted(changed-covered)}, stale={sorted(covered-changed)}")


if __name__ == "__main__":
    try:
        verify(Path(__file__).resolve().parents[1])
    except (OSError, ValueError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"OpenMW patch registry verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
    print("OpenMW patch registry verified")
